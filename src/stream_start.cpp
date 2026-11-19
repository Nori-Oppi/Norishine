/**
 * @file src/stream_start.cpp
 * @brief Stream start timeline (roadmap N5).
 */
#include "stream_start.h"

#include "logging.h"

#include <format>
#include <mutex>
#include <optional>

using namespace std::literals;

namespace stream_start {
  namespace {
    std::mutex lock;
    std::optional<std::chrono::steady_clock::time_point> started;
    timeline_t current;
    std::optional<timeline_t> finished;

    double since_start(std::chrono::steady_clock::time_point now) {
      return std::chrono::duration<double, std::milli>(now - *started).count();
    }
  }  // namespace

  void begin(std::string_view kind) {
    std::lock_guard lg {lock};
    started = std::chrono::steady_clock::now();
    current = timeline_t {};
    current.kind = kind;
  }

  void mark(std::string_view step) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard lg {lock};
    if (!started) {
      return;
    }
    current.steps.emplace_back(std::string {step}, since_start(now));
  }

  void first_frame_sent() {
    auto now = std::chrono::steady_clock::now();
    std::string line;
    {
      std::lock_guard lg {lock};
      if (!started) {
        return;
      }
      current.total_ms = since_start(now);
      line = std::format("Stream start ({}):", current.kind);
      double previous = 0;
      for (const auto &[step, ms] : current.steps) {
        line += std::format(" {} {:.0f} (+{:.0f}),", step, ms, ms - previous);
        previous = ms;
      }
      line += std::format(" first frame sent {:.0f} (+{:.0f}) ms", current.total_ms, current.total_ms - previous);
      finished = std::move(current);
      current = timeline_t {};
      started.reset();
    }
    BOOST_LOG(info) << line;
  }

  bool last(timeline_t &out) {
    std::lock_guard lg {lock};
    if (!finished) {
      return false;
    }
    out = *finished;
    return true;
  }
}  // namespace stream_start
