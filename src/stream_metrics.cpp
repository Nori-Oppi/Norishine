/**
 * @file src/stream_metrics.cpp
 * @brief Definitions for per-stream metrics shown by the management API.
 */
// standard includes
#include <algorithm>
#include <cmath>

// local includes
#include "stream_metrics.h"

namespace stream_metrics {
  namespace {
    double to_ms(clock::duration d) {
      return std::chrono::duration<double, std::milli>(d).count();
    }

    // Divides by a whole number, so 1.9 stays 1.9 in the JSON (not 1.9000000000000001).
    double round_to(double value, int decimals) {
      double factor = std::pow(10.0, decimals);
      return std::round(value * factor) / factor;
    }
  }  // namespace

  recorder_t::recorder_t(clock::time_point now):
      start {now} {
  }

  std::int64_t recorder_t::second_of(clock::time_point now) const {
    auto origin = first_frame.value_or(start);
    if (now < origin) {
      return 0;
    }
    return std::chrono::duration_cast<std::chrono::seconds>(now - origin).count();
  }

  recorder_t::bucket_t &recorder_t::bucket_for(clock::time_point now) {
    auto second = second_of(now);
    auto &bucket = buckets[second % buckets.size()];
    if (bucket.second != second) {
      bucket = {};
      bucket.second = second;
    }
    return bucket;
  }

  void recorder_t::frame_sent(std::size_t bytes, std::optional<clock::duration> latency, clock::duration send_time, bool key_frame, clock::time_point now) {
    std::lock_guard lg {mutex};
    if (!first_frame) {
      // Recent figures count from the first frame, so setup time doesn't lower the frame rate.
      first_frame = now;
      for (auto &bucket : buckets) {
        bucket = {};
      }
    }

    auto &bucket = bucket_for(now);
    bucket.frames += 1;
    bucket.bytes += bytes;

    auto send = to_ms(send_time);
    bucket.send_min = bucket.frames == 1 ? send : std::min(bucket.send_min, send);
    bucket.send_max = std::max(bucket.send_max, send);
    bucket.send_sum += send;

    if (latency) {
      auto ms = to_ms(*latency);
      bucket.latency_min = bucket.latency_count == 0 ? ms : std::min(bucket.latency_min, ms);
      bucket.latency_max = std::max(bucket.latency_max, ms);
      bucket.latency_sum += ms;
      bucket.latency_count += 1;
    } else {
      totals.duplicate_frames += 1;
    }

    totals.frames += 1;
    totals.bytes += bytes;
    if (key_frame) {
      totals.key_frames += 1;
    }
  }

  void recorder_t::key_frame_requested(clock::time_point now) {
    std::lock_guard lg {mutex};
    totals.key_frame_requests += 1;
    bucket_for(now).recovery_requests += 1;
  }

  void recorder_t::ref_frames_invalidated(std::int64_t first, std::int64_t last, clock::time_point now) {
    std::lock_guard lg {mutex};
    totals.ref_invalidations += 1;
    if (last >= first) {
      totals.frames_invalidated += static_cast<std::uint64_t>(last - first + 1);
    }
    bucket_for(now).recovery_requests += 1;
  }

  void recorder_t::client_loss_report(std::int32_t lost_frames) {
    std::lock_guard lg {mutex};
    if (lost_frames > 0) {
      totals.client_lost_frames += static_cast<std::uint64_t>(lost_frames);
    }
  }

  snapshot_t recorder_t::snapshot(clock::time_point now) const {
    std::lock_guard lg {mutex};
    snapshot_t result = totals;
    result.duration_s = now > first_frame.value_or(start) ? std::chrono::duration<double>(now - first_frame.value_or(start)).count() : 0;

    // Complete seconds only: the current second is still filling.
    auto current = second_of(now);
    auto window = static_cast<int>(std::min<std::int64_t>(window_seconds, current));
    if (!first_frame || window <= 0) {
      return result;
    }
    result.window_s = window;

    std::uint64_t frames = 0;
    std::uint64_t bytes = 0;
    std::uint32_t latency_count = 0;
    double latency_sum = 0;
    double send_sum = 0;
    std::optional<range_t> latency;
    std::optional<range_t> send;
    for (const auto &bucket : buckets) {
      if (bucket.second < current - window || bucket.second >= current) {
        continue;
      }
      result.recent_recovery_requests += bucket.recovery_requests;
      if (bucket.frames > 0) {
        if (!send) {
          send = range_t {bucket.send_min, 0, bucket.send_max};
        } else {
          send->min = std::min(send->min, bucket.send_min);
          send->max = std::max(send->max, bucket.send_max);
        }
      }
      if (bucket.latency_count > 0) {
        if (!latency) {
          latency = range_t {bucket.latency_min, 0, bucket.latency_max};
        } else {
          latency->min = std::min(latency->min, bucket.latency_min);
          latency->max = std::max(latency->max, bucket.latency_max);
        }
      }
      frames += bucket.frames;
      bytes += bucket.bytes;
      latency_count += bucket.latency_count;
      latency_sum += bucket.latency_sum;
      send_sum += bucket.send_sum;
    }

    result.fps = static_cast<double>(frames) / window;
    result.bitrate_kbps = static_cast<double>(bytes) * 8 / 1000 / window;
    if (latency) {
      latency->avg = latency_sum / latency_count;
      result.latency_ms = latency;
    }
    if (send) {
      send->avg = send_sum / frames;
      result.send_ms = send;
    }
    return result;
  }

  nlohmann::json to_json(const snapshot_t &s) {
    auto range = [](const std::optional<range_t> &r) -> nlohmann::json {
      if (!r) {
        return nullptr;
      }
      return {{"min", round_to(r->min, 2)}, {"avg", round_to(r->avg, 2)}, {"max", round_to(r->max, 2)}};
    };
    auto number = [](const std::optional<double> &v, int decimals) -> nlohmann::json {
      return v ? nlohmann::json(round_to(*v, decimals)) : nlohmann::json(nullptr);
    };

    return {
      {"duration_s", round_to(s.duration_s, 1)},
      {"window_s", s.window_s},
      {"fps", number(s.fps, 1)},
      {"bitrate_kbps", number(s.bitrate_kbps, 0)},
      {"latency_ms", range(s.latency_ms)},
      {"send_ms", range(s.send_ms)},
      {"recent_recovery_requests", s.recent_recovery_requests},
      {"frames", s.frames},
      {"duplicate_frames", s.duplicate_frames},
      {"bytes", s.bytes},
      {"key_frames", s.key_frames},
      {"key_frame_requests", s.key_frame_requests},
      {"ref_invalidations", s.ref_invalidations},
      {"frames_invalidated", s.frames_invalidated},
      {"client_lost_frames", s.client_lost_frames},
    };
  }
}  // namespace stream_metrics
