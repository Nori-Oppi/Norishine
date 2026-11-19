/**
 * @file src/adaptive.cpp
 * @brief Definitions for the per-stream bitrate and error correction controller.
 */
// standard includes
#include <algorithm>
#include <cmath>
#include <tuple>

// local includes
#include "adaptive.h"

using namespace std::chrono_literals;

namespace adaptive {
  namespace {
    // No decisions while the stream settles (first key frames, client buffers).
    constexpr auto startup_grace = 3s;
    // Loss right after a bitrate change may come from before it.
    constexpr auto change_grace = 2s;
    constexpr auto min_decrease_gap = 2s;
    // Raise the bitrate only after this long without trouble and since the last change.
    constexpr auto probe_clean = 10s;
    // Close to the bitrate where trouble last started, probe in small, slow steps.
    constexpr auto near_knee_clean = 30s;
    constexpr double near_knee_step = 1.04;
    constexpr double probe_step = 1.15;
    // Decreases closer together than this belong to the same run of trouble.
    constexpr auto episode_gap = 10s;
    // Forget the knee after this long: the network may have improved.
    constexpr auto knee_memory = 120s;
    constexpr double congestion_factor = 0.7;
    constexpr double backoff_factor = 0.85;
    constexpr int fec_step_up = 10;
    constexpr int fec_step_down = 5;
    constexpr auto fec_up_gap = 2s;
    constexpr auto fec_relax_clean = 20s;
    constexpr auto fec_relax_gap = 10s;
    // Lowest RTT over this window is the base the current RTT is compared to.
    constexpr auto rtt_window = 60s;
    constexpr double loss_percent = 1.0;
    constexpr double heavy_loss_percent = 5.0;
    constexpr int recovery_window_ticks = 3;
    // Loss this often within this long is more than error correction can fix.
    constexpr auto loss_memory = 10s;

    int round_kbps(double kbps) {
      return static_cast<int>(std::lround(kbps / 100.0)) * 100;
    }

    double seconds(clock::duration d) {
      return std::chrono::duration<double>(d).count();
    }

    std::string read_number(const nlohmann::json &body, const char *key, double max, std::optional<double> &out) {
      auto it = body.find(key);
      if (it == body.end() || it->is_null()) {
        return {};
      }
      if (!it->is_number()) {
        return std::string {key} + " must be a number";
      }
      auto value = it->get<double>();
      if (!std::isfinite(value) || value < 0 || value > max) {
        return std::string {key} + " must be between 0 and " + std::to_string(static_cast<int>(max));
      }
      out = value;
      return {};
    }
  }  // namespace

  std::string parse_report(const nlohmann::json &body, client_report_t &out) {
    if (!body.is_object()) {
      return "the report must be a JSON object";
    }
    client_report_t report;
    for (auto [key, max, field] : {
           std::tuple {"loss_percent", 100.0, &report.loss_percent},
           std::tuple {"rtt_ms", 10000.0, &report.rtt_ms},
           std::tuple {"rtt_variance_ms", 10000.0, &report.rtt_variance_ms},
           std::tuple {"decode_ms", 10000.0, &report.decode_ms},
           std::tuple {"fps", 1000.0, &report.fps},
           std::tuple {"display_hz", 1000.0, &report.display_hz},
         }) {
      if (auto error = read_number(body, key, max, *field); !error.empty()) {
        return error;
      }
    }
    out = report;
    return {};
  }

  std::string_view to_string(reason_e reason) {
    switch (reason) {
      case reason_e::congestion:
        return "congestion";
      case reason_e::delay:
        return "delay";
      case reason_e::loss:
        return "loss";
      case reason_e::probe:
        return "probe";
      case reason_e::fec_relax:
        return "fec_relax";
      case reason_e::settings:
        return "settings";
      case reason_e::client:
        return "client";
      case reason_e::none:
        break;
    }
    return "none";
  }

  nlohmann::json to_json(const state_t &state) {
    auto optional = [](const std::optional<double> &value) {
      return value ? nlohmann::json(std::round(*value * 10) / 10) : nlohmann::json(nullptr);
    };
    return {
      {"enabled", state.enabled},
      {"live_bitrate", state.live_bitrate},
      {"bitrate_kbps", state.bitrate_kbps},
      {"max_kbps", state.max_kbps},
      {"min_kbps", state.min_kbps},
      {"fec_percent", state.fec_percent},
      {"last_reason", to_string(state.last_reason)},
      {"last_change_s", optional(state.last_change_s)},
      {"decreases", state.decreases},
      {"increases", state.increases},
      {"client_reports", state.client_reports},
      {"host_limited", state.host_limited},
      {"client_limited", state.client_limited},
      {"base_rtt_ms", optional(state.base_rtt_ms)},
    };
  }

  controller_t::controller_t(const settings_t &settings, int start_kbps, double target_fps, bool live_bitrate, clock::time_point start):
      settings {settings},
      max_kbps {std::max(start_kbps, 1)},
      min_kbps {std::clamp(settings.min_kbps, 1, std::max(start_kbps, 1))},
      target_fps {target_fps > 0 ? target_fps : 60},
      live_bitrate {live_bitrate},
      start {start},
      bitrate_kbps {std::max(start_kbps, 1)},
      fec {settings.base_fec} {
  }

  void controller_t::report(const client_report_t &report, clock::time_point now) {
    std::lock_guard lg {mutex};
    last_report = report;
    last_report_at = now;
    if (report.rtt_ms && *report.rtt_ms > 0) {
      rtt_history.emplace_back(now, *report.rtt_ms);
    }
    while (!rtt_history.empty() && now - rtt_history.front().first > rtt_window) {
      rtt_history.pop_front();
    }
  }

  bool controller_t::fresh_report(clock::time_point now) const {
    return last_report && last_report_at && now - *last_report_at <= report_max_age;
  }

  int controller_t::fec_percent() const {
    std::lock_guard lg {mutex};
    return fec;
  }

  controller_t::tick_t controller_t::assess(const host_sample_t &sample, clock::time_point now) {
    tick_t t;

    // Recovery requests: the client lost a frame that error correction couldn't repair.
    std::uint64_t delta = 0;
    if (last_recovery_total && sample.recovery_requests >= *last_recovery_total) {
      delta = sample.recovery_requests - *last_recovery_total;
    }
    last_recovery_total = sample.recovery_requests;
    recent_recovery.push_back(delta);
    while (recent_recovery.size() > recovery_window_ticks) {
      recent_recovery.pop_front();
    }
    std::uint64_t recent = 0;
    for (auto n : recent_recovery) {
      recent += n;
    }
    t.loss = delta >= 2 || recent >= 3;
    t.heavy_loss = delta >= 6;

    bool fresh = fresh_report(now);
    // Frames a client misses while it sets up its decoder aren't network loss.
    bool settled_report = fresh && first_tick && *last_report_at >= *first_tick + startup_grace;
    if (settled_report && last_report->loss_percent) {
      t.loss = t.loss || *last_report->loss_percent >= loss_percent;
      t.heavy_loss = t.heavy_loss || *last_report->loss_percent >= heavy_loss_percent;
    }

    // Rising round-trip time means queues are filling somewhere on the path.
    bool delayed_now = false;
    if (fresh && last_report->rtt_ms && !rtt_history.empty()) {
      auto base = std::min_element(rtt_history.begin(), rtt_history.end(), [](auto &a, auto &b) {
                    return a.second < b.second;
                  })->second;
      delayed_now = *last_report->rtt_ms > base + std::max(10.0, base * 0.5);
    }
    delay_ticks = delayed_now ? delay_ticks + 1 : 0;
    t.delay = delay_ticks >= 2;
    t.delay_now = delayed_now;

    // Bottlenecks a lower bitrate wouldn't fix; while they last, don't raise it either.
    auto frame_ms = 1000.0 / target_fps;
    host_limited = sample.fps && sample.latency_ms && *sample.fps < target_fps * 0.85 && *sample.latency_ms > frame_ms * 0.8;
    client_limited = fresh && last_report->decode_ms && *last_report->decode_ms > frame_ms * 0.9;

    return t;
  }

  decision_t controller_t::update_settings(const settings_t &new_settings, int cap_kbps, clock::time_point now) {
    std::lock_guard lg {mutex};
    decision_t decision;
    const bool at_base = fec == settings.base_fec;
    settings.base_fec = new_settings.base_fec;
    settings.max_fec = std::max(new_settings.max_fec, new_settings.base_fec);
    settings.min_kbps = new_settings.min_kbps;
    if (cap_kbps > 0 && cap_kbps < max_kbps) {
      max_kbps = cap_kbps;
    }
    min_kbps = std::clamp(settings.min_kbps, 1, max_kbps);

    // Error correction the controller hasn't raised follows the setting; a raised one stays
    // within the new bounds.
    auto new_fec = at_base ? settings.base_fec : std::clamp(fec, settings.base_fec, settings.max_fec);
    if (new_fec != fec) {
      fec = new_fec;
      last_fec_change = now;
      last_reason = reason_e::settings;
      last_change = now;
    }
    if (bitrate_kbps > max_kbps) {
      bitrate_kbps = max_kbps;
      last_change = now;
      last_reason = reason_e::settings;
      recent_recovery.clear();
      decision.bitrate_kbps = bitrate_kbps;
    }
    decision.fec_percent = fec;
    return decision;
  }

  decision_t controller_t::set_client_bitrate(int kbps, clock::time_point now) {
    std::lock_guard lg {mutex};
    decision_t decision;
    kbps = std::max(kbps, 1);
    max_kbps = kbps;
    min_kbps = std::clamp(settings.min_kbps, 1, max_kbps);
    // Trouble at the old bitrate says little about the new one.
    knee_kbps.reset();
    if (kbps != bitrate_kbps) {
      bitrate_kbps = kbps;
      last_change = now;
      last_reason = reason_e::client;
      recent_recovery.clear();
      clean_since = now;
      decision.bitrate_kbps = kbps;
    }
    decision.fec_percent = fec;
    return decision;
  }

  decision_t controller_t::set_client_fec(int percent, clock::time_point now) {
    std::lock_guard lg {mutex};
    decision_t decision;
    settings.base_fec = std::max(percent, 1);
    settings.max_fec = std::max(settings.max_fec, settings.base_fec);
    fec = settings.base_fec;
    last_fec_change = now;
    last_change = now;
    last_reason = reason_e::client;
    decision.fec_percent = fec;
    return decision;
  }

  void controller_t::set_target_fps(double fps) {
    std::lock_guard lg {mutex};
    if (fps > 0) {
      target_fps = fps;
    }
  }

  void controller_t::change_bitrate(int kbps, reason_e reason, clock::time_point now, decision_t &decision) {
    kbps = std::clamp(kbps, min_kbps, max_kbps);
    if (kbps == bitrate_kbps) {
      return;
    }
    if (kbps < bitrate_kbps) {
      // The knee is where this run of trouble started, not each step down within it.
      if (!knee_kbps || !last_decrease || now - *last_decrease > episode_gap) {
        knee_kbps = bitrate_kbps;
      }
      ++decreases;
      last_decrease = now;
    } else {
      ++increases;
    }
    bitrate_kbps = kbps;
    last_change = now;
    last_reason = reason;
    // Requests from before the change say nothing about the new bitrate.
    recent_recovery.clear();
    decision.bitrate_kbps = kbps;
  }

  decision_t controller_t::tick(const host_sample_t &sample, clock::time_point now) {
    std::lock_guard lg {mutex};
    decision_t decision;

    if (!settings.enabled) {
      decision.fec_percent = fec;
      return decision;
    }
    if (last_tick && now - *last_tick < tick_interval - 50ms) {
      decision.fec_percent = fec;
      return decision;
    }
    last_tick = now;
    if (!first_tick) {
      first_tick = now;
    }

    auto t = assess(sample, now);
    decision.fec_percent = fec;

    if (now - *first_tick < startup_grace) {
      // Clients ask for key frames while their decoder starts; those requests mustn't
      // count as loss once the grace ends.
      recent_recovery.clear();
      clean_since = now;
      return decision;
    }
    if (last_change && now - *last_change < change_grace) {
      // Ignore what may still come from before the change, but keep the RTT base fresh.
      recent_recovery.clear();
      return decision;
    }

    while (!loss_history.empty() && now - loss_history.front() > loss_memory) {
      loss_history.pop_front();
    }

    if (t.loss || t.delay) {
      clean_since.reset();
      if (t.loss) {
        loss_history.push_back(now);
      }
      bool can_decrease = live_bitrate && (!last_decrease || now - *last_decrease >= min_decrease_gap);
      bool can_raise_fec = fec < settings.max_fec && (!last_fec_change || now - *last_fec_change >= fec_up_gap);
      auto raise_fec = [&]() {
        fec = std::min(settings.max_fec, fec + fec_step_up);
        last_fec_change = now;
        last_change = now;
        recent_recovery.clear();
        last_reason = reason_e::loss;
      };

      if (!live_bitrate) {
        // Error correction is all this encoder can change during a stream.
        if (t.loss && can_raise_fec) {
          raise_fec();
        }
      } else if (t.heavy_loss || (t.loss && (t.delay || t.delay_now))) {
        if (can_decrease) {
          change_bitrate(round_kbps(bitrate_kbps * congestion_factor), reason_e::congestion, now, decision);
        }
      } else if (t.delay) {
        if (can_decrease) {
          change_bitrate(round_kbps(bitrate_kbps * backoff_factor), reason_e::delay, now, decision);
        }
      } else {
        // Loss without congestion signs: likely radio or a busy link. Repair it with more
        // error correction first; if that doesn't help, send less.
        if (can_raise_fec && loss_history.size() <= 2) {
          raise_fec();
        } else if (can_decrease) {
          change_bitrate(round_kbps(bitrate_kbps * backoff_factor), reason_e::loss, now, decision);
        }
      }
      decision.fec_percent = fec;
      return decision;
    }

    if (!clean_since) {
      clean_since = now;
    }
    auto clean_for = now - *clean_since;

    if (knee_kbps && last_decrease && now - *last_decrease > knee_memory) {
      knee_kbps.reset();
    }

    // Probe upwards towards the start bitrate.
    if (live_bitrate && bitrate_kbps < max_kbps && !host_limited && !client_limited) {
      bool near_knee = knee_kbps && bitrate_kbps * probe_step >= *knee_kbps * 0.95;
      auto needed = near_knee ? near_knee_clean : probe_clean;
      if (clean_for >= needed && (!last_change || now - *last_change >= needed)) {
        auto step = near_knee ? near_knee_step : probe_step;
        auto next = std::max(round_kbps(bitrate_kbps * step), bitrate_kbps + 100);
        change_bitrate(next, reason_e::probe, now, decision);
      }
    }

    // Let error correction drift back to the host's setting.
    if (fec > settings.base_fec && clean_for >= fec_relax_clean && (!last_fec_change || now - *last_fec_change >= fec_relax_gap)) {
      fec = std::max(settings.base_fec, fec - fec_step_down);
      last_fec_change = now;
      if (!decision.bitrate_kbps) {
        last_reason = reason_e::fec_relax;
      }
    }

    decision.fec_percent = fec;
    return decision;
  }

  state_t controller_t::state(clock::time_point now) const {
    std::lock_guard lg {mutex};
    state_t s;
    s.enabled = settings.enabled;
    s.live_bitrate = live_bitrate;
    s.bitrate_kbps = bitrate_kbps;
    s.max_kbps = max_kbps;
    s.min_kbps = min_kbps;
    s.fec_percent = fec;
    s.last_reason = last_reason;
    if (last_change) {
      s.last_change_s = seconds(*last_change - start);
    }
    s.decreases = decreases;
    s.increases = increases;
    s.client_reports = fresh_report(now);
    s.host_limited = host_limited;
    s.client_limited = client_limited;
    if (!rtt_history.empty()) {
      s.base_rtt_ms = std::min_element(rtt_history.begin(), rtt_history.end(), [](auto &a, auto &b) {
                        return a.second < b.second;
                      })->second;
    }
    return s;
  }
}  // namespace adaptive
