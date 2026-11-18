/**
 * @file src/recommend.h
 * @brief Recommendations for the host's encoder settings from what earlier streams measured
 *        (roadmap M9.4, docs/AUTOTUNE.md "Host settings").
 *
 * Each stream is summed up once a second while it runs (recorder_t). When it ends, the
 * summary goes into a short history per encoder, together with the settings the host
 * ran it with. derive() turns the recent streams that ran with the current settings into
 * recommendations: a faster preset when the encoder was the bottleneck, a higher-quality
 * one when there was plenty of room and clients asked for quality, more error correction
 * when frames were lost often. store_t keeps the history, applies recommendations through
 * the config file and keeps the previous values so they can be restored.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

namespace recommend {
  using clock = std::chrono::steady_clock;

  /// Settings a recommendation can change; streams record the values they ran with.
  inline constexpr std::string_view tracked_keys[] = {"nvenc_preset", "amd_quality", "qsv_preset", "sw_preset", "fec_percentage"};

  /// Streams shorter than this (after the first seconds) aren't recorded.
  inline constexpr double min_stream_seconds = 30;
  /// Streams kept per encoder.
  inline constexpr std::size_t history_size = 10;
  /// Recent streams derive() looks at, and how many it needs at least.
  inline constexpr std::size_t recent_streams = 5;
  inline constexpr std::size_t min_streams = 2;

  /**
   * @brief One second of a running stream.
   */
  struct tick_t {
    std::optional<double> fps;  ///< Frames sent per second, recently.
    std::optional<double> latency_ms;  ///< Average capture-to-network time, recently.
    std::uint64_t recovery_total = 0;  ///< Key frame requests and reference invalidations so far.
    std::optional<double> encoder_percent;  ///< Host video encoder load, if known.
    std::optional<int> fec_percent;  ///< Error correction the stream uses now, if adapted.
  };

  /**
   * @brief What a finished stream measured.
   */
  struct stream_t {
    std::int64_t ended = 0;  ///< Unix time.
    std::string tuning;  ///< The client's goal: "", "performance", "balanced" or "quality".
    double seconds = 0;  ///< Seconds measured.
    double target_fps = 60;
    double host_limited_share = 0;  ///< Share of seconds the host couldn't encode as fast as asked.
    std::optional<double> latency_ms;  ///< Average capture-to-network time.
    std::optional<double> encoder_percent;  ///< Average encoder load, where the host measures it.
    double recovery_per_minute = 0;  ///< Frames the client reported lost (after FEC), per minute.
    std::uint32_t fec_raises = 0;  ///< Times the adaptive controller raised error correction (after the first seconds).
    nlohmann::json settings = nlohmann::json::object();  ///< tracked_keys values the host ran with.
  };

  nlohmann::json to_json(const stream_t &stream);
  std::optional<stream_t> stream_from_json(const nlohmann::json &json);

  /**
   * @brief Sums up one stream. tick() may be called more often than once a second.
   */
  class recorder_t {
  public:
    explicit recorder_t(double target_fps, clock::time_point start = clock::now());

    void tick(const tick_t &tick, clock::time_point now = clock::now());

    /**
     * @brief The stream's summary, or nothing when it was too short to say anything.
     */
    std::optional<stream_t> finish(std::string tuning, nlohmann::json settings, std::int64_t ended) const;

    /// The first seconds of a stream (encoder start, first key frame) aren't counted.
    static constexpr auto settle_time = std::chrono::seconds(5);

  private:
    mutable std::mutex mutex;
    const double target_fps;
    const clock::time_point start;
    std::optional<clock::time_point> last_tick;
    std::optional<std::uint64_t> first_recovery;
    std::uint64_t last_recovery = 0;
    std::uint32_t seconds = 0;
    std::uint32_t limited = 0;
    std::uint32_t fec_raises = 0;
    std::optional<int> last_fec;
    double latency_sum = 0;
    std::uint32_t latency_count = 0;
    double encoder_sum = 0;
    std::uint32_t encoder_count = 0;
  };

  /// Seconds the host counts as encoder-limited: the same test as the adaptive controller.
  bool host_limited(const tick_t &tick, double target_fps);

  /**
   * @brief One recommended change.
   */
  struct recommendation_t {
    std::string key;
    nlohmann::json current;
    nlohmann::json recommended;
    std::string reason;  ///< "encoder_limited", "headroom" or "loss".
    int streams = 0;  ///< Streams it is based on.
  };

  /// The preset setting of an encoder ("nvenc" -> "nvenc_preset"); empty if it has none.
  std::string_view preset_key(std::string_view encoder);

  /// One step faster or slower (higher quality) on a preset scale; nothing at the end of it.
  std::optional<nlohmann::json> faster(std::string_view key, const nlohmann::json &value);
  std::optional<nlohmann::json> higher_quality(std::string_view key, const nlohmann::json &value);

  /**
   * @brief Recommendations for one encoder.
   * @param encoder The encoder the host uses ("nvenc", ...).
   * @param streams Streams on that encoder, oldest first.
   * @param current Current values of tracked_keys (from the config file, defaults filled in).
   */
  std::vector<recommendation_t> derive(std::string_view encoder, const std::deque<stream_t> &streams, const nlohmann::json &current);

  /// tracked_keys from read_config_file()'s "values", with schema defaults for missing ones.
  nlohmann::json tracked_values(const nlohmann::json &file_values);

  /**
   * @brief Answer of apply() and rollback(): HTTP status and JSON body.
   */
  struct reply_t {
    int code = 200;
    nlohmann::json body;
  };

  /**
   * @brief Stream history, recommendations and the values kept for undo, in one state file.
   */
  class store_t {
  public:
    /// @param file State file; empty keeps everything in memory only.
    explicit store_t(std::filesystem::path file = {});

    /// A finished stream on this encoder.
    void record(const std::string &encoder, const stream_t &stream);

    /// GET /api/v1/recommendations.
    nlohmann::json describe(const std::string &encoder, const std::string &config_file);

    /// POST /api/v1/recommendations/apply: {"keys": [...]} (optional; all when missing).
    reply_t apply(const nlohmann::json &body, const std::string &encoder, const std::string &config_file);

    /// POST /api/v1/recommendations/rollback: restore the values from before the last apply.
    reply_t rollback(const std::string &config_file);

    std::deque<stream_t> history(const std::string &encoder);

  private:
    void load();
    void save();

    std::mutex mutex;
    std::filesystem::path file;
    std::map<std::string, std::deque<stream_t>, std::less<>> streams;
    nlohmann::json applied;  ///< null, or {"at", "values": {key: value}, "previous": {key: value or null}}.
  };

  /**
   * @brief Read the state file and note the settings this run of the host uses.
   *        Call once at startup, after state_store::init().
   */
  void init(const std::string &config_file);

  /// The tracked settings this run of the host uses (read at startup; they need a restart).
  nlohmann::json running_settings();

  /// The host's store.
  store_t &store();

  /// The encoder recommendations are for: the active one, else the last one streamed with.
  std::string encoder_for(std::string_view active);
}  // namespace recommend
