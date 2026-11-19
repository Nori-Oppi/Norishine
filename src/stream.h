/**
 * @file src/stream.h
 * @brief Declarations for the streaming protocols.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// lib includes
#include <boost/asio.hpp>

// local includes
#include "adaptive.h"
#include "audio.h"
#include "crypto.h"
#include "stream_metrics.h"
#include "video.h"

namespace stream {
  constexpr auto VIDEO_STREAM_PORT = 9;
  constexpr auto CONTROL_PORT = 10;
  constexpr auto AUDIO_STREAM_PORT = 11;

  struct session_t;

  struct config_t {
    audio::config_t audio;
    video::config_t monitor;

    int packetsize;
    int minRequiredFecPackets;
    int mlFeatureFlags;
    int controlProtocolType;
    int audioQosType;
    int videoQosType;

    uint32_t encryptionFlagsEnabled;

    std::optional<int> gcmap;
  };

  namespace session {
    enum class state_e : int {
      STOPPED,  ///< The session is stopped
      STOPPING,  ///< The session is stopping
      STARTING,  ///< The session is starting
      RUNNING,  ///< The session is running
    };

    std::shared_ptr<session_t> alloc(config_t &config, rtsp_stream::launch_session_t &launch_session);
    std::string uuid(const session_t& session);
    bool uuid_match(const session_t& session, const std::string_view& uuid);
    bool update_device_info(session_t& session, const std::string& name, const crypto::PERM& newPerm);
    int start(session_t &session, const std::string &addr_string);
    void stop(session_t &session);
    void graceful_stop(session_t& session);
    void join(session_t &session);
    state_e state(session_t &session);
    inline bool send(session_t& session, const std::string_view &payload);

    /**
     * @brief What the management pages show about a running stream.
     */
    struct info_t {
      std::string uuid;  ///< The paired client's UUID.
      std::string name;  ///< The paired client's name.
      std::string address;  ///< The client's IP address.
      int width = 0;
      int height = 0;
      int fps = 0;
      int bitrate_kbps = 0;
      int codec = 0;  ///< 0 H.264, 1 HEVC, 2 AV1
      bool hdr = false;
      state_e state = state_e::STOPPED;
      stream_metrics::snapshot_t metrics;  ///< Measured frame rate, bitrate, latency and recovery.
      std::optional<adaptive::state_t> adaptive;  ///< Bitrate and error correction now, when adapting.
      config::tuning_e tuning = config::tuning_e::host;  ///< The client's tuning goal for the encoder.
    };

    info_t describe(session_t &session);

    /**
     * @brief Pass a client's own measurements to its stream's controller.
     * @return False when the stream isn't adapted (adaptive_bitrate is off).
     */
    bool report(session_t &session, const adaptive::client_report_t &report);

    /**
     * @brief What a client asks to change in its own running stream (N9). Every field is
     *        optional.
     */
    struct live_change_t {
      std::optional<int> bitrate_kbps;  ///< The bitrate asked for, as at launch: before error correction, audio and overhead.
      std::optional<int> fec_percent;  ///< Error correction to return to and use now.
      std::optional<config::tuning_e> tuning;  ///< Tuning goal: the encoder preset.
      std::optional<int> width;  ///< Picture size (with height).
      std::optional<int> height;
      std::optional<int> fps;
      std::optional<int> codec;  ///< 0 H.264, 1 HEVC, 2 AV1
    };

    /**
     * @brief Parse a live change from JSON (POST /api/v1/session/settings).
     * @return An error message, or empty on success.
     */
    std::string parse_change(const nlohmann::json &body, live_change_t &out);

    /**
     * @brief Apply a client's change to its running stream (N9). Bitrate and the encoder
     *        preset rebuild the encoder behind one key frame; error correction changes at once.
     *        Picture size, frame rate and codec also rebuild the client's decoder: the control
     *        stream tells it the new format (message 0x3100) before the first frame in it.
     * @param status Set to the HTTP status to answer.
     * @return An error message, or empty on success.
     */
    std::string change(session_t &session, const live_change_t &change, int &status);
  }  // namespace session

  /**
   * @brief The video encoder's bitrate for a bitrate a client asks for: what is left after
   *        error correction, audio and packet overhead, as at launch.
   */
  int encoder_kbps(std::int64_t requested_kbps, int fec_percent, bool high_quality_audio, int audio_channels);

  /**
   * @brief Settings changed without a restart (config::apply_live, N9): running streams
   *        take them over. Encoder settings rebuild each stream's encoder behind a key
   *        frame; error correction, the adaptation bounds and max_bitrate change at once.
   */
  void settings_changed(const std::vector<std::string> &keys);

  /**
   * @brief The host is about to restart (N10): an update, or a restart for its settings.
   *        Clients that ask about their own stream (GET /api/v1/session, the reports)
   *        hear why in `host_restarting`, so Norilight waits and starts the app again.
   * @param reason "update" or "restart".
   * @return How long to wait before restarting so streaming clients hear it (Norilight
   *         reports once a second); zero without streams.
   */
  std::chrono::milliseconds announce_restart(std::string_view reason);

  /**
   * @brief Why the host is about to restart, or empty.
   */
  std::string restart_reason();
}  // namespace stream
