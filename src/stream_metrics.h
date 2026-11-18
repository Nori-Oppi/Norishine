/**
 * @file src/stream_metrics.h
 * @brief Declarations for per-stream metrics shown by the management API.
 */
#pragma once

// standard includes
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

// lib includes
#include <nlohmann/json.hpp>

namespace stream_metrics {
  using clock = std::chrono::steady_clock;

  /// Seconds the recent figures (frame rate, bitrate, latency) are averaged over.
  inline constexpr int window_seconds = 5;

  /**
   * @brief Minimum, average and maximum of a value over the recent window.
   */
  struct range_t {
    double min = 0;
    double avg = 0;
    double max = 0;
  };

  /**
   * @brief What a stream looked like recently, and in total since it started.
   *
   * Recent figures are over the last `window` complete seconds; they are empty until the
   * stream has run for a full second.
   */
  struct snapshot_t {
    double duration_s = 0;  ///< Time since the stream's first frame (or since it was created).
    int window_s = 0;  ///< Complete seconds the recent figures cover (0 = none yet).
    std::optional<double> fps;  ///< Frames sent per second.
    std::optional<double> bitrate_kbps;  ///< Video sent, in kilobits per second (before FEC and headers).
    std::optional<range_t> latency_ms;  ///< Capture to handed to the network, for captured frames.
    std::optional<range_t> send_ms;  ///< Time spent sending each frame (FEC, encryption, pacing).
    std::uint32_t recent_recovery_requests = 0;  ///< Key frame requests and reference invalidations in the window.

    std::uint64_t frames = 0;  ///< Frames sent in total.
    std::uint64_t duplicate_frames = 0;  ///< Frames sent again because nothing new was captured.
    std::uint64_t bytes = 0;  ///< Video bytes sent in total.
    std::uint64_t key_frames = 0;  ///< Key (IDR) frames sent.
    std::uint64_t key_frame_requests = 0;  ///< Key frames the client asked for.
    std::uint64_t ref_invalidations = 0;  ///< Times the client reported lost reference frames.
    std::uint64_t frames_invalidated = 0;  ///< Frames covered by those reports.
    std::uint64_t client_lost_frames = 0;  ///< Frames lost, from the client's loss reports (older clients only).
  };

  /**
   * @brief Collects one stream's metrics. Thread-safe: the video, control and HTTP
   * threads all use it.
   */
  class recorder_t {
  public:
    explicit recorder_t(clock::time_point now = clock::now());

    /**
     * @brief A frame was sent.
     * @param bytes Encoded size of the frame.
     * @param latency Capture to handed to the network; empty for duplicate frames.
     * @param send_time Time spent sending it.
     * @param key_frame Whether it was a key frame.
     */
    void frame_sent(std::size_t bytes, std::optional<clock::duration> latency, clock::duration send_time, bool key_frame, clock::time_point now = clock::now());

    /// The client asked for a key frame.
    void key_frame_requested(clock::time_point now = clock::now());

    /// The client reported lost reference frames `first`..`last`.
    void ref_frames_invalidated(std::int64_t first, std::int64_t last, clock::time_point now = clock::now());

    /// An older client's periodic loss report.
    void client_loss_report(std::int32_t lost_frames);

    snapshot_t snapshot(clock::time_point now = clock::now()) const;

  private:
    struct bucket_t {
      std::int64_t second = -1;
      std::uint32_t frames = 0;
      std::uint64_t bytes = 0;
      std::uint32_t latency_count = 0;
      double latency_sum = 0;
      double latency_min = 0;
      double latency_max = 0;
      double send_sum = 0;
      double send_max = 0;
      double send_min = 0;
      std::uint32_t recovery_requests = 0;
    };

    bucket_t &bucket_for(clock::time_point now);
    std::int64_t second_of(clock::time_point now) const;

    mutable std::mutex mutex;
    clock::time_point start;
    std::optional<clock::time_point> first_frame;
    std::array<bucket_t, window_seconds + 2> buckets {};
    snapshot_t totals;
  };

  /// The snapshot as the management API returns it (null for figures not known yet).
  nlohmann::json to_json(const snapshot_t &snapshot);
}  // namespace stream_metrics
