/**
 * @file src/stream_start.h
 * @brief Times each step from a client's launch or resume request to the first video
 *        frame sent, and logs them as one line (roadmap N5).
 */
#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stream_start {
  /**
   * @brief One finished stream start: each step's time in milliseconds since the request.
   */
  struct timeline_t {
    std::string kind;  ///< "launch" or "resume"
    std::vector<std::pair<std::string, double>> steps;
    double total_ms = 0;
  };

  /**
   * @brief Starts a new timeline: a launch or resume request just arrived.
   */
  void begin(std::string_view kind);

  /**
   * @brief Records a step of the current stream start. Ignored when no start is under way.
   */
  void mark(std::string_view step);

  /**
   * @brief The first video frame was sent: logs the timeline and ends it.
   */
  void first_frame_sent();

  /**
   * @brief The last finished timeline, if any.
   */
  bool last(timeline_t &out);
}  // namespace stream_start
