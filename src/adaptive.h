/**
 * @file src/adaptive.h
 * @brief Declarations for the per-stream controller that keeps bitrate and error correction
 *        at the best level the network, the host and the client can sustain.
 *
 * One controller runs for each stream. Once a second it reads what the host measured
 * (recovery requests, frame rate, capture-to-send latency) and, from clients that send it
 * (Norilight), what the client measured (frames lost, round-trip time, decode time). It
 * lowers the bitrate quickly when the network is congested, raises error correction when
 * packets are lost without congestion, and probes back up slowly towards the bitrate the
 * stream was started with, which is never exceeded. See docs/AUTOTUNE.md.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

// lib includes
#include <nlohmann/json.hpp>

namespace adaptive {
  using clock = std::chrono::steady_clock;

  /**
   * @brief Host settings for the controller (adaptive_bitrate, adaptive_min_bitrate,
   *        fec_percentage, adaptive_max_fec).
   */
  struct settings_t {
    bool enabled = true;
    int min_kbps = 3000;  ///< Never go below this (or below the start bitrate, if lower).
    int base_fec = 20;  ///< Error correction to return to when the network is clean.
    int max_fec = 50;  ///< Highest error correction the controller may use.
  };

  /**
   * @brief What a client measured over its last report interval. Every figure is optional.
   */
  struct client_report_t {
    std::optional<double> loss_percent;  ///< Video frames lost by the network (after FEC), percent of frames.
    std::optional<double> rtt_ms;  ///< Round-trip time to the host.
    std::optional<double> rtt_variance_ms;
    std::optional<double> decode_ms;  ///< Average time to decode a frame.
    std::optional<double> fps;  ///< Frames received per second.
    std::optional<double> display_hz;  ///< Refresh rate of the display the stream is shown on.
  };

  /**
   * @brief Parse a client report from JSON. Unknown fields are ignored; wrong types and
   *        values out of range are reported.
   * @return An error message, or empty on success.
   */
  std::string parse_report(const nlohmann::json &body, client_report_t &out);

  /**
   * @brief What the host measured, read once per tick.
   */
  struct host_sample_t {
    std::uint64_t recovery_requests = 0;  ///< Total key frame requests and reference invalidations so far.
    std::optional<double> fps;  ///< Frames sent per second, recently.
    std::optional<double> latency_ms;  ///< Average capture-to-network time, recently.
  };

  /**
   * @brief Why the controller last changed something.
   */
  enum class reason_e {
    none,
    congestion,  ///< Loss together with rising delay, or heavy loss: bitrate down.
    delay,  ///< Round-trip time rising (queues filling): bitrate down a little.
    loss,  ///< Loss without rising delay: more error correction, then bitrate down.
    probe,  ///< Clean for a while: bitrate up.
    fec_relax,  ///< Clean for a while: error correction back towards the setting.
    settings,  ///< The host's settings changed during the stream (N9).
    client,  ///< The client asked for a bitrate or error correction during the stream (N9).
  };

  std::string_view to_string(reason_e reason);

  /**
   * @brief The controller's state, as the management API shows it.
   */
  struct state_t {
    bool enabled = false;
    bool live_bitrate = false;  ///< The encoder can change bitrate during the stream.
    int bitrate_kbps = 0;  ///< Bitrate the encoder is set to now.
    int max_kbps = 0;  ///< The stream's start bitrate: never exceeded.
    int min_kbps = 0;
    int fec_percent = 0;
    reason_e last_reason = reason_e::none;
    std::optional<double> last_change_s;  ///< Seconds into the stream of the last change.
    std::uint32_t decreases = 0;
    std::uint32_t increases = 0;
    bool client_reports = false;  ///< A client report arrived recently.
    bool host_limited = false;  ///< The host can't encode frames as fast as asked.
    bool client_limited = false;  ///< The client can't decode frames as fast as they arrive.
    std::optional<double> base_rtt_ms;  ///< Lowest recent round-trip time.
  };

  nlohmann::json to_json(const state_t &state);

  /**
   * @brief What to do after a tick.
   */
  struct decision_t {
    std::optional<int> bitrate_kbps;  ///< Set when the encoder should change bitrate now.
    int fec_percent = 0;  ///< Error correction to use from now on.
  };

  /**
   * @brief The controller for one stream. Thread-safe: ticks come from the control thread
   *        and client reports from the HTTPS thread.
   */
  class controller_t {
  public:
    /**
     * @param settings Host settings.
     * @param start_kbps The bitrate the stream was started with; the controller never exceeds it.
     * @param target_fps The frame rate the stream was started with.
     * @param live_bitrate Whether the encoder can change bitrate during the stream.
     * @param start When the stream was set up; the API reports change times from it.
     */
    controller_t(const settings_t &settings, int start_kbps, double target_fps, bool live_bitrate, clock::time_point start = clock::now());

    /// A report from the client.
    void report(const client_report_t &report, clock::time_point now = clock::now());

    /// Run once a second or so. Earlier calls return the current state unchanged.
    decision_t tick(const host_sample_t &sample, clock::time_point now = clock::now());

    state_t state(clock::time_point now = clock::now()) const;

    /// Error correction to use now.
    int fec_percent() const;

    /**
     * @brief The host's settings changed during the stream (N9): new error correction and
     *        bounds apply at once. `enabled` stays as the stream started.
     * @param cap_kbps When above 0, the stream's bitrate never goes above it from now on
     *        (max_bitrate lowered).
     * @return The bitrate to change to, if it had to drop, and the error correction now.
     */
    decision_t update_settings(const settings_t &settings, int cap_kbps = 0, clock::time_point now = clock::now());

    /**
     * @brief The client asked for a new bitrate during the stream (N9): it becomes the
     *        stream's bitrate and its new ceiling, as if the stream had started with it.
     * @return The bitrate to change to (empty when it is the current one).
     */
    decision_t set_client_bitrate(int kbps, clock::time_point now = clock::now());

    /**
     * @brief The client asked for an error correction during the stream (N9): it becomes
     *        the level the controller returns to, and is used at once.
     */
    decision_t set_client_fec(int percent, clock::time_point now = clock::now());

    /// Seconds between ticks.
    static constexpr auto tick_interval = std::chrono::seconds(1);
    /// Reports older than this are ignored.
    static constexpr auto report_max_age = std::chrono::seconds(3);

  private:
    struct tick_t {
      bool loss = false;
      bool heavy_loss = false;
      bool delay = false;  ///< RTT high for two ticks in a row.
      bool delay_now = false;  ///< RTT high in this tick.
    };

    tick_t assess(const host_sample_t &sample, clock::time_point now);
    void change_bitrate(int kbps, reason_e reason, clock::time_point now, decision_t &decision);
    bool fresh_report(clock::time_point now) const;

    mutable std::mutex mutex;
    settings_t settings;
    int max_kbps;
    int min_kbps;
    const double target_fps;
    const bool live_bitrate;
    const clock::time_point start;

    int bitrate_kbps;
    int fec;
    std::optional<clock::time_point> first_tick;  ///< Ticks start once the stream runs.
    std::optional<clock::time_point> last_tick;
    std::optional<std::uint64_t> last_recovery_total;
    std::deque<std::uint64_t> recent_recovery;  ///< Recovery requests per tick, newest last.
    std::optional<clock::time_point> last_change;
    std::optional<clock::time_point> last_decrease;
    std::optional<clock::time_point> last_fec_change;
    std::optional<clock::time_point> clean_since;
    std::optional<int> knee_kbps;  ///< Bitrate at which trouble last started.
    int delay_ticks = 0;
    std::deque<clock::time_point> loss_history;  ///< Ticks with loss, recent ones.

    std::optional<client_report_t> last_report;
    std::optional<clock::time_point> last_report_at;
    std::deque<std::pair<clock::time_point, double>> rtt_history;  ///< For the lowest recent RTT.

    reason_e last_reason = reason_e::none;
    std::uint32_t decreases = 0;
    std::uint32_t increases = 0;
    bool host_limited = false;
    bool client_limited = false;
  };
}  // namespace adaptive
