/**
 * @file src/stream.h
 * @brief Declarations for the streaming protocols.
 */
#pragma once

// standard includes
#include <utility>

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
    };

    info_t describe(session_t &session);

    /**
     * @brief Pass a client's own measurements to its stream's controller.
     * @return False when the stream isn't adapted (adaptive_bitrate is off).
     */
    bool report(session_t &session, const adaptive::client_report_t &report);
  }  // namespace session
}  // namespace stream
