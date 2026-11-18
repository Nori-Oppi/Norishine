/**
 * @file src/recommend.cpp
 * @brief Encoder setting recommendations from what earlier streams measured.
 */
// standard includes
#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>

// local includes
#include "config_schema.h"
#include "logging.h"
#include "recommend.h"
#include "state_store.h"

using namespace std::literals;

namespace recommend {
  namespace {
    // A stream counts as encoder-limited when this share of its seconds was, or its encoder
    // was nearly always busy.
    constexpr double limited_share = 0.10;
    constexpr double busy_encoder_percent = 90;
    // Plenty of room: hardly ever limited, encoder under this load, latency under this share
    // of a frame interval.
    constexpr double roomy_limited_share = 0.02;
    constexpr double roomy_encoder_percent = 60;
    constexpr double roomy_latency_frames = 0.5;
    // Frequent loss: this many lost frames a minute, or error correction raised at least this
    // often (and at least twice).
    constexpr double lossy_recovery_per_minute = 3;
    constexpr double lossy_fec_raises_per_minute = 0.5;
    constexpr std::uint32_t lossy_fec_raises = 2;
    constexpr int fec_step = 10;
    constexpr int fec_ceiling = 50;

    struct scale_t {
      std::string_view key;
      std::vector<nlohmann::json> steps;  ///< Fastest first.
      std::size_t quality_ceiling;  ///< Highest step headroom may recommend.
    };

    const std::vector<scale_t> &scales() {
      static const std::vector<scale_t> list {
        {"nvenc_preset", {1, 2, 3, 4, 5, 6, 7}, 4},
        {"amd_quality", {"speed", "balanced", "quality"}, 2},
        {"qsv_preset", {"veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"}, 4},
        {"sw_preset", {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"}, 4},
      };
      return list;
    }

    const scale_t *scale_for(std::string_view key) {
      for (const auto &s : scales()) {
        if (s.key == key) {
          return &s;
        }
      }
      return nullptr;
    }

    std::optional<std::size_t> step_of(const scale_t &scale, const nlohmann::json &value) {
      for (std::size_t i = 0; i < scale.steps.size(); ++i) {
        if (scale.steps[i] == value) {
          return i;
        }
      }
      return std::nullopt;
    }

    bool limited(const stream_t &s) {
      return s.host_limited_share >= limited_share || (s.encoder_percent && *s.encoder_percent >= busy_encoder_percent);
    }

    bool roomy(const stream_t &s) {
      auto frame_ms = 1000.0 / (s.target_fps > 0 ? s.target_fps : 60);
      return s.host_limited_share < roomy_limited_share &&
             s.encoder_percent && *s.encoder_percent < roomy_encoder_percent &&
             s.latency_ms && *s.latency_ms < frame_ms * roomy_latency_frames;
    }

    bool lossy(const stream_t &s) {
      auto raises_per_minute = s.seconds > 0 ? s.fec_raises * 60 / s.seconds : 0;
      return s.recovery_per_minute >= lossy_recovery_per_minute ||
             (s.fec_raises >= lossy_fec_raises && raises_per_minute >= lossy_fec_raises_per_minute);
    }

    nlohmann::json optional(const std::optional<double> &v) {
      return v ? nlohmann::json(std::round(*v * 10) / 10) : nlohmann::json(nullptr);
    }

    std::optional<double> optional_double(const nlohmann::json &j, std::string_view key) {
      auto it = j.find(key);
      if (it == j.end() || !it->is_number()) {
        return std::nullopt;
      }
      return it->get<double>();
    }

    std::int64_t unix_now() {
      return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    bool restart_required(std::string_view key) {
      const auto *setting = config::schema::find(key);
      return !setting || setting->restart_required;
    }
  }  // namespace

  nlohmann::json to_json(const stream_t &s) {
    return {
      {"ended", s.ended},
      {"tuning", s.tuning},
      {"seconds", std::round(s.seconds)},
      {"target_fps", s.target_fps},
      {"host_limited_share", std::round(s.host_limited_share * 1000) / 1000},
      {"latency_ms", optional(s.latency_ms)},
      {"encoder_percent", optional(s.encoder_percent)},
      {"recovery_per_minute", std::round(s.recovery_per_minute * 100) / 100},
      {"fec_raises", s.fec_raises},
      {"settings", s.settings},
    };
  }

  std::optional<stream_t> stream_from_json(const nlohmann::json &j) {
    if (!j.is_object() || !j.contains("seconds") || !j["seconds"].is_number()) {
      return std::nullopt;
    }
    stream_t s;
    s.ended = j.value("ended", std::int64_t {0});
    s.tuning = j.value("tuning", std::string {});
    s.seconds = j.value("seconds", 0.0);
    s.target_fps = j.value("target_fps", 60.0);
    s.host_limited_share = j.value("host_limited_share", 0.0);
    s.latency_ms = optional_double(j, "latency_ms");
    s.encoder_percent = optional_double(j, "encoder_percent");
    s.recovery_per_minute = j.value("recovery_per_minute", 0.0);
    s.fec_raises = j.value("fec_raises", std::uint32_t {0});
    if (auto it = j.find("settings"); it != j.end() && it->is_object()) {
      s.settings = *it;
    }
    return s;
  }

  bool host_limited(const tick_t &tick, double target_fps) {
    // As adaptive::controller_t: fewer frames than asked while each takes most of a frame interval.
    auto frame_ms = 1000.0 / target_fps;
    return tick.fps && tick.latency_ms && *tick.fps < target_fps * 0.85 && *tick.latency_ms > frame_ms * 0.8;
  }

  recorder_t::recorder_t(double target_fps, clock::time_point start):
      target_fps {target_fps > 0 ? target_fps : 60},
      start {start} {
  }

  void recorder_t::tick(const tick_t &tick, clock::time_point now) {
    std::lock_guard lock {mutex};
    if (now - start < settle_time || !tick.fps) {
      return;
    }
    if (last_tick && now - *last_tick < std::chrono::seconds(1)) {
      return;
    }
    last_tick = now;
    ++seconds;
    if (!first_recovery) {
      first_recovery = tick.recovery_total;
    }
    last_recovery = std::max(last_recovery, tick.recovery_total);
    if (host_limited(tick, target_fps)) {
      ++limited;
    }
    // A raise is a change from one counted second to the next, so error correction the
    // stream started with (or raised in its first seconds) doesn't count.
    if (tick.fec_percent) {
      if (last_fec && *tick.fec_percent > *last_fec) {
        ++fec_raises;
      }
      last_fec = tick.fec_percent;
    }
    if (tick.latency_ms) {
      latency_sum += *tick.latency_ms;
      ++latency_count;
    }
    if (tick.encoder_percent) {
      encoder_sum += *tick.encoder_percent;
      ++encoder_count;
    }
  }

  std::optional<stream_t> recorder_t::finish(std::string tuning, nlohmann::json settings, std::int64_t ended) const {
    std::lock_guard lock {mutex};
    if (seconds < min_stream_seconds) {
      return std::nullopt;
    }
    stream_t s;
    s.ended = ended;
    s.tuning = std::move(tuning);
    s.seconds = seconds;
    s.target_fps = target_fps;
    s.host_limited_share = static_cast<double>(limited) / seconds;
    if (latency_count) {
      s.latency_ms = latency_sum / latency_count;
    }
    // Encoder load from only a few seconds says little.
    if (encoder_count * 2 >= seconds) {
      s.encoder_percent = encoder_sum / encoder_count;
    }
    auto lost = first_recovery ? last_recovery - *first_recovery : 0;
    s.recovery_per_minute = static_cast<double>(lost) * 60 / seconds;
    s.fec_raises = fec_raises;
    s.settings = std::move(settings);
    return s;
  }

  std::string_view preset_key(std::string_view encoder) {
    if (encoder == "nvenc"sv) {
      return "nvenc_preset"sv;
    }
    if (encoder == "amdvce"sv) {
      return "amd_quality"sv;
    }
    if (encoder == "quicksync"sv) {
      return "qsv_preset"sv;
    }
    if (encoder == "software"sv) {
      return "sw_preset"sv;
    }
    return {};
  }

  std::optional<nlohmann::json> faster(std::string_view key, const nlohmann::json &value) {
    const auto *scale = scale_for(key);
    if (!scale) {
      return std::nullopt;
    }
    auto step = step_of(*scale, value);
    if (!step || *step == 0) {
      return std::nullopt;
    }
    return scale->steps[*step - 1];
  }

  std::optional<nlohmann::json> higher_quality(std::string_view key, const nlohmann::json &value) {
    const auto *scale = scale_for(key);
    if (!scale) {
      return std::nullopt;
    }
    auto step = step_of(*scale, value);
    if (!step || *step >= scale->quality_ceiling) {
      return std::nullopt;
    }
    return scale->steps[*step + 1];
  }

  std::vector<recommendation_t> derive(std::string_view encoder, const std::deque<stream_t> &streams, const nlohmann::json &current) {
    std::vector<recommendation_t> out;

    // The newest streams that ran with this setting's current value: older ones measured
    // something else.
    auto recent = [&](std::string_view key) {
      std::vector<const stream_t *> list;
      auto it = current.find(key);
      if (it == current.end()) {
        return list;
      }
      for (auto s = streams.rbegin(); s != streams.rend() && list.size() < recent_streams; ++s) {
        auto ran = s->settings.find(key);
        if (ran != s->settings.end() && *ran == *it) {
          list.push_back(&*s);
        }
      }
      return list;
    };

    if (auto key = preset_key(encoder); !key.empty() && current.contains(key)) {
      const auto &value = current[std::string {key}];
      auto list = recent(key);
      // The host's preset is what Balanced streams (and clients without a goal) use;
      // Performance and Quality streams replace it.
      std::vector<const stream_t *> own;
      std::copy_if(list.begin(), list.end(), std::back_inserter(own), [](const stream_t *s) {
        return s->tuning.empty() || s->tuning == "balanced"sv;
      });
      auto limited_count = std::count_if(own.begin(), own.end(), [](const stream_t *s) {
        return limited(*s);
      });
      auto quality_count = std::count_if(list.begin(), list.end(), [](const stream_t *s) {
        return s->tuning == "quality"sv;
      });
      bool all_roomy = std::all_of(list.begin(), list.end(), [](const stream_t *s) {
        return roomy(*s);
      });

      if (own.size() >= min_streams && static_cast<std::size_t>(limited_count) * 2 >= own.size()) {
        if (auto next = faster(key, value)) {
          out.push_back({std::string {key}, value, *next, "encoder_limited", static_cast<int>(own.size())});
        }
      } else if (list.size() >= min_streams && all_roomy && static_cast<std::size_t>(quality_count) * 2 > list.size()) {
        if (auto next = higher_quality(key, value)) {
          out.push_back({std::string {key}, value, *next, "headroom", static_cast<int>(list.size())});
        }
      }
    }

    if (current.contains("fec_percentage") && current["fec_percentage"].is_number_integer()) {
      auto value = current["fec_percentage"].get<int>();
      auto list = recent("fec_percentage");
      auto lossy_count = std::count_if(list.begin(), list.end(), [](const stream_t *s) {
        return lossy(*s);
      });
      if (list.size() >= min_streams && static_cast<std::size_t>(lossy_count) * 2 >= list.size() && value < fec_ceiling) {
        out.push_back({"fec_percentage", value, std::min(value + fec_step, fec_ceiling), "loss", static_cast<int>(list.size())});
      }
    }
    return out;
  }

  nlohmann::json tracked_values(const nlohmann::json &file_values) {
    nlohmann::json out = nlohmann::json::object();
    for (auto key : tracked_keys) {
      std::string k {key};
      if (file_values.is_object() && file_values.contains(k)) {
        out[k] = file_values[k];
      } else if (const auto *setting = config::schema::find(key)) {
        out[k] = setting->default_value;
      }
    }
    return out;
  }

  store_t::store_t(std::filesystem::path file):
      file {std::move(file)} {
    load();
  }

  void store_t::load() {
    applied = nullptr;
    if (file.empty()) {
      return;
    }
    auto content = state_store::read(file);
    if (!content || !content->is_object()) {
      return;
    }
    if (auto it = content->find("streams"); it != content->end() && it->is_object()) {
      for (const auto &[encoder, list] : it->items()) {
        if (!list.is_array()) {
          continue;
        }
        auto &dest = streams[encoder];
        for (const auto &entry : list) {
          if (auto s = stream_from_json(entry)) {
            dest.push_back(std::move(*s));
          }
        }
        while (dest.size() > history_size) {
          dest.pop_front();
        }
      }
    }
    if (auto it = content->find("applied"); it != content->end() && it->is_object() && (*it)["values"].is_object()) {
      applied = *it;
    }
  }

  void store_t::save() {
    if (file.empty()) {
      return;
    }
    nlohmann::json list = nlohmann::json::object();
    for (const auto &[encoder, entries] : streams) {
      auto &arr = list[encoder] = nlohmann::json::array();
      for (const auto &s : entries) {
        arr.push_back(to_json(s));
      }
    }
    if (!state_store::write(file, {{"version", 1}, {"streams", list}, {"applied", applied}})) {
      BOOST_LOG(warning) << "Recommendations: couldn't write "sv << file.string();
    }
  }

  void store_t::record(const std::string &encoder, const stream_t &stream) {
    if (encoder.empty()) {
      return;
    }
    std::lock_guard lock {mutex};
    auto &list = streams[encoder];
    list.push_back(stream);
    while (list.size() > history_size) {
      list.pop_front();
    }
    save();
  }

  std::deque<stream_t> store_t::history(const std::string &encoder) {
    std::lock_guard lock {mutex};
    auto it = streams.find(encoder);
    return it == streams.end() ? std::deque<stream_t> {} : it->second;
  }

  nlohmann::json store_t::describe(const std::string &encoder, const std::string &config_file) {
    auto file_values = config::schema::read_config_file(config_file)["values"];
    auto current = tracked_values(file_values);

    std::lock_guard lock {mutex};
    auto it = streams.find(encoder);
    static const std::deque<stream_t> none;
    const auto &list = it == streams.end() ? none : it->second;

    nlohmann::json recs = nlohmann::json::array();
    for (const auto &r : derive(encoder, list, current)) {
      recs.push_back({
        {"key", r.key},
        {"current", r.current},
        {"recommended", r.recommended},
        {"reason", r.reason},
        {"streams", r.streams},
        {"restart_required", restart_required(r.key)},
      });
    }

    nlohmann::json undo = nullptr;
    if (applied.is_object()) {
      nlohmann::json settings = nlohmann::json::array();
      for (const auto &[key, value] : applied["values"].items()) {
        auto previous = applied["previous"].value(key, nlohmann::json(nullptr));
        if (previous.is_null()) {
          const auto *setting = config::schema::find(key);
          previous = setting ? setting->default_value : nlohmann::json(nullptr);
        }
        settings.push_back({
          {"key", key},
          {"previous", previous},
          {"value", value},
          {"current", current.value(key, nlohmann::json(nullptr))},
          {"restart_required", restart_required(key)},
        });
      }
      undo = {{"at", applied.value("at", std::int64_t {0})}, {"settings", settings}};
    }

    return {
      {"status", true},
      {"encoder", encoder},
      {"streams", list.size()},
      {"recommendations", recs},
      {"applied", undo},
    };
  }

  reply_t store_t::apply(const nlohmann::json &body, const std::string &encoder, const std::string &config_file) {
    std::optional<std::vector<std::string>> keys;
    if (body.is_object() && body.contains("keys")) {
      const auto &list = body["keys"];
      if (!list.is_array() || !std::all_of(list.begin(), list.end(), [](const auto &k) {
            return k.is_string();
          })) {
        return {400, {{"status", false}, {"error", "\"keys\" must be a list of setting names"}}};
      }
      keys = list.get<std::vector<std::string>>();
    } else if (!body.is_null() && !body.is_object()) {
      return {400, {{"status", false}, {"error", "The body must be a JSON object"}}};
    }

    auto file_values = config::schema::read_config_file(config_file)["values"];
    auto current = tracked_values(file_values);

    std::lock_guard lock {mutex};
    auto it = streams.find(encoder);
    auto recs = it == streams.end() ? std::vector<recommendation_t> {} : derive(encoder, it->second, current);

    nlohmann::json set = nlohmann::json::object();
    if (keys) {
      for (const auto &key : *keys) {
        auto rec = std::find_if(recs.begin(), recs.end(), [&](const auto &r) {
          return r.key == key;
        });
        if (rec == recs.end()) {
          return {409, {{"status", false}, {"error", "No recommendation for " + key}}};
        }
        set[key] = rec->recommended;
      }
    } else {
      for (const auto &r : recs) {
        set[r.key] = r.recommended;
      }
    }
    if (set.empty()) {
      return {409, {{"status", false}, {"error", "Nothing to apply"}}};
    }

    auto change = config::schema::validate_change({{"set", set}});
    if (!change.errors.empty()) {
      return {400, {{"status", false}, {"errors", change.errors}}};
    }
    auto result = config::schema::apply_change(change, config_file);
    if (!result.written) {
      return {500, {{"status", false}, {"error", "Couldn't write the config file"}}};
    }

    // Keep the values from before the first apply, so one undo goes all the way back.
    if (!applied.is_object()) {
      applied = {{"values", nlohmann::json::object()}, {"previous", nlohmann::json::object()}};
    }
    for (const auto &[key, value] : set.items()) {
      if (!applied["previous"].contains(key)) {
        applied["previous"][key] = file_values.is_object() && file_values.contains(key) ? file_values[key] : nlohmann::json(nullptr);
      }
      applied["values"][key] = value;
    }
    applied["at"] = unix_now();
    save();

    BOOST_LOG(info) << "Recommendations: applied "sv << set.dump();
    return {200, {{"status", true}, {"changed", result.changed}, {"restart_required", result.restart_required}}};
  }

  reply_t store_t::rollback(const std::string &config_file) {
    std::lock_guard lock {mutex};
    if (!applied.is_object()) {
      return {409, {{"status", false}, {"error", "Nothing to undo"}}};
    }

    auto file_values = config::schema::read_config_file(config_file)["values"];
    auto current = tracked_values(file_values);
    nlohmann::json set = nlohmann::json::object();
    nlohmann::json reset = nlohmann::json::array();
    nlohmann::json skipped = nlohmann::json::array();
    for (const auto &[key, value] : applied["values"].items()) {
      // Changed again since: leave it as it is now.
      if (current.value(key, nlohmann::json(nullptr)) != value) {
        skipped.push_back(key);
        continue;
      }
      auto previous = applied["previous"].value(key, nlohmann::json(nullptr));
      if (previous.is_null()) {
        reset.push_back(key);
      } else {
        set[key] = previous;
      }
    }

    config::schema::apply_result_t result;
    if (!set.empty() || !reset.empty()) {
      auto change = config::schema::validate_change({{"set", set}, {"reset", reset}});
      if (!change.errors.empty()) {
        return {400, {{"status", false}, {"errors", change.errors}}};
      }
      result = config::schema::apply_change(change, config_file);
      if (!result.written) {
        return {500, {{"status", false}, {"error", "Couldn't write the config file"}}};
      }
    }
    applied = nullptr;
    save();

    BOOST_LOG(info) << "Recommendations: restored "sv << set.dump() << ", reset "sv << reset.dump();
    return {200, {{"status", true}, {"changed", result.changed}, {"restart_required", result.restart_required}, {"skipped", skipped}}};
  }

  namespace {
    std::mutex global_mutex;
    nlohmann::json running = nlohmann::json::object();
    std::unique_ptr<store_t> global_store;
  }  // namespace

  void init(const std::string &config_file) {
    auto values = tracked_values(config::schema::read_config_file(config_file)["values"]);
    std::filesystem::path file;
    if (state_store::writable() && !state_store::paths().dir.empty()) {
      file = state_store::paths().dir / "recommendations.json";
    }
    std::lock_guard lock {global_mutex};
    running = std::move(values);
    global_store = std::make_unique<store_t>(file);
  }

  nlohmann::json running_settings() {
    std::lock_guard lock {global_mutex};
    return running;
  }

  store_t &store() {
    std::lock_guard lock {global_mutex};
    if (!global_store) {
      global_store = std::make_unique<store_t>();
    }
    return *global_store;
  }

  std::string encoder_for(std::string_view active) {
    if (!active.empty()) {
      return std::string {active};
    }
    // Before the host has checked its encoders: the one the last stream used.
    auto &s = store();
    std::string best;
    std::int64_t newest = -1;
    for (auto key : {"nvenc"s, "amdvce"s, "quicksync"s, "software"s, "vaapi"s, "videotoolbox"s}) {
      auto list = s.history(key);
      if (!list.empty() && list.back().ended > newest) {
        newest = list.back().ended;
        best = key;
      }
    }
    return best;
  }
}  // namespace recommend
