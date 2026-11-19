/**
 * @file src/optimise.cpp
 * @brief Encoder test for the host's own preset (roadmap N6).
 */
#include "optimise.h"

#include "config.h"
#include "config_schema.h"
#include "display_device.h"
#include "logging.h"
#include "platform/common.h"
#include "process.h"
#include "recommend.h"
#include "rtsp.h"
#include "utility.h"
#include "video.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

using namespace std::literals;

namespace optimise {
  namespace {
    constexpr int frames_per_step = 120;  ///< Two seconds at 60 fps
    constexpr double test_fps = 60;

    std::mutex lock;
    std::atomic<bool> busy {false};
    nlohmann::json state = {{"state", "idle"}};

    void set_state(const nlohmann::json &changes) {
      std::lock_guard lg {lock};
      for (const auto &[k, v] : changes.items()) {
        state[k] = v;
      }
    }

    std::string as_text(const nlohmann::json &value) {
      return value.is_string() ? value.get<std::string>() : value.dump();
    }

    double percentile(std::vector<double> values, double q) {
      if (values.empty()) {
        return 0;
      }
      std::sort(values.begin(), values.end());
      auto index = static_cast<std::size_t>(std::lround(q * (values.size() - 1)));
      return values[std::min(index, values.size() - 1)];
    }

    nlohmann::json to_json(const result_t &r) {
      return {
        {"value", r.value},
        {"frames", r.frames},
        {"avg_ms", std::round(r.avg_ms * 100) / 100},
        {"p95_ms", std::round(r.p95_ms * 100) / 100},
        {"error", r.error.empty() ? nlohmann::json(nullptr) : nlohmann::json(r.error)},
      };
    }

    void run(std::string key, recommend::preset_scale_t scale, int codec, nlohmann::json current) {
      auto finish = util::fail_guard([] {
        busy = false;
      });

      // The moving test picture on the stream's display, so every step encodes the same work
      std::optional<boost::process::v1::child> pattern;
      if (auto app = proc::proc.optimise_test_app()) {
        auto cmd = app->cmd;
        auto output = display_device::map_output_name(config::video.output_name);
        if (!output.empty()) {
          cmd += " --display \"" + output + "\"";
        }
        boost::filesystem::path dir = app->working_dir;
        std::error_code ec;
        auto env = proc::proc.get_env();
        auto child = platf::run_command(false, true, cmd, dir, env, nullptr, ec, nullptr);
        if (!ec) {
          pattern = std::move(child);
          std::this_thread::sleep_for(1500ms);
        } else {
          BOOST_LOG(warning) << "Optimise: couldn't show the test picture: "sv << ec.message();
        }
      }

      // Each step is set in the running config while it's measured, then the config is put back
      auto saved = config::video;
      std::vector<result_t> results;
      int width = 0, height = 0;
      auto steps = std::min(scale.steps.size(), scale.quality_ceiling + 1);
      for (std::size_t i = 0; i < steps; ++i) {
        result_t r;
        r.value = scale.steps[i];
        if (!config::set_encoder_preset(key, as_text(r.value))) {
          r.error = "Unknown preset";
        } else {
          auto timing = video::time_encoder(codec, frames_per_step, 3s);
          width = std::max(width, timing.width);
          height = std::max(height, timing.height);
          r.frames = static_cast<int>(timing.frame_ms.size());
          r.error = timing.error;
          if (r.frames > 0) {
            double sum = 0;
            for (auto ms : timing.frame_ms) {
              sum += ms;
            }
            r.avg_ms = sum / r.frames;
            r.p95_ms = percentile(timing.frame_ms, 0.95);
          }
        }
        BOOST_LOG(info) << "Optimise: "sv << key << " = "sv << as_text(r.value) << ": "sv << r.frames << " frames, "sv
                        << r.avg_ms << " ms average, "sv << r.p95_ms << " ms 95th percentile"sv
                        << (r.error.empty() ? ""s : ", "s + r.error);
        results.push_back(r);

        nlohmann::json list = nlohmann::json::array();
        for (const auto &x : results) {
          list.push_back(to_json(x));
        }
        set_state({{"progress", static_cast<int>(100 * (i + 1) / steps)}, {"results", list}, {"width", width}, {"height", height}});
      }
      config::video = saved;

      if (pattern) {
        std::error_code ec;
        pattern->terminate(ec);
      }

      auto best = pick(results, scale.quality_ceiling, 1000.0 / test_fps);
      if (!best) {
        set_state({{"state", "failed"}, {"error", results.empty() || results.front().error.empty() ? "Nothing could be measured"s : results.front().error}});
        BOOST_LOG(warning) << "Optimise: nothing could be measured"sv;
        return;
      }
      auto picked = results[*best].value;
      set_state({{"state", "done"}, {"progress", 100}, {"pick", picked}, {"current", current}});
      BOOST_LOG(info) << "Optimise: "sv << key << " = "sv << as_text(picked) << " keeps up"sv
                      << (picked == current ? " (already set)"sv : ""sv);
    }
  }  // namespace

  std::optional<std::size_t> pick(const std::vector<result_t> &results, std::size_t quality_ceiling, double frame_ms) {
    std::optional<std::size_t> best;
    std::optional<std::size_t> fastest;
    for (std::size_t i = 0; i < results.size(); ++i) {
      const auto &r = results[i];
      if (r.frames == 0 || !r.error.empty()) {
        continue;
      }
      if (!fastest) {
        fastest = i;
      }
      if (i <= quality_ceiling && r.p95_ms <= frame_share * frame_ms) {
        best = i;
      }
    }
    return best ? best : fastest;
  }

  int start(std::string &error) {
    if (rtsp_stream::session_count() > 0) {
      error = "A stream is running.";
      return 409;
    }
    bool expected = false;
    if (!busy.compare_exchange_strong(expected, true)) {
      error = "The test is already running.";
      return 409;
    }
    auto release = util::fail_guard([] {
      busy = false;
    });

    if (video::probe_encoders()) {
      error = "No working encoder.";
      return 503;
    }
    auto encoder = std::string {video::active_encoder_name()};
    auto key = std::string {recommend::preset_key(encoder)};
    auto scale = recommend::preset_scale(key);
    if (key.empty() || !scale) {
      error = "This encoder has no preset to choose.";
      return 409;
    }
    auto current = recommend::tracked_values(config::schema::read_config_file(config::sunshine.config_file)["values"]).value(key, nlohmann::json(nullptr));
    int codec = video::active_hevc_mode > 1 ? 1 : 0;

    {
      std::lock_guard lg {lock};
      state = {
        {"state", "running"},
        {"progress", 0},
        {"encoder", encoder},
        {"codec", codec == 1 ? "HEVC" : "H.264"},
        {"fps", static_cast<int>(test_fps)},
        {"setting", key},
        {"current", current},
        {"results", nlohmann::json::array()},
        {"pick", nullptr},
        {"error", nullptr},
      };
    }
    BOOST_LOG(info) << "Optimise: testing "sv << key << " on "sv << encoder;
    release.disable();
    std::thread {run, key, *scale, codec, current}.detach();
    return 202;
  }

  bool running() {
    return busy;
  }

  nlohmann::json describe() {
    std::lock_guard lg {lock};
    auto out = state;
    out["status"] = true;
    return out;
  }

  std::pair<int, nlohmann::json> apply(const std::string &config_file) {
    nlohmann::json set;
    {
      std::lock_guard lg {lock};
      if (state.value("state", "") != "done" || state.value("pick", nlohmann::json(nullptr)).is_null()) {
        return {409, {{"status", false}, {"error", "Nothing to apply"}}};
      }
      set = {{state["setting"].get<std::string>(), state["pick"]}};
    }
    auto reply = recommend::store().apply_values(set, config_file, "Optimise");
    return {reply.code, reply.body};
  }
}  // namespace optimise
