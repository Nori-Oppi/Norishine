/**
 * @file tests/unit/test_stream.cpp
 * @brief Test src/stream.*
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// local includes
#include <src/network.h>
#include <src/rtsp.h>
#include <src/stream.h>
#include <src/video.h>

namespace stream {
  std::vector<uint8_t> concat_and_insert(uint64_t insert_size, uint64_t slice_size, const std::string_view &data1, const std::string_view &data2);
  std::optional<std::pair<std::uint16_t, std::string_view>> parse_control_packet(const ENetPacket &packet);
  bool test_receive_ping_endpoints(bool session_id_pings);
}  // namespace stream

// Sunshine c1012508: audio and video receives used to share one sender endpoint.
TEST(StreamPingTests, KeepsLegacyAudioAndVideoSenderEndpointsSeparate) {
  for (int attempt = 0; attempt < 20; ++attempt) {
    ASSERT_TRUE(stream::test_receive_ping_endpoints(false)) << "Attempt " << attempt;
  }
}

TEST(StreamPingTests, KeepsSessionIdAudioAndVideoSenderEndpointsSeparate) {
  for (int attempt = 0; attempt < 20; ++attempt) {
    ASSERT_TRUE(stream::test_receive_ping_endpoints(true)) << "Attempt " << attempt;
  }
}

TEST(ConcatAndInsertTests, ConcatNoInsertionTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(0, 2, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {'a', 'b', 'c', 'd', 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatLargeStrideTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(1, sizeof(b1) + sizeof(b2) + 1, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {0, 'a', 'b', 'c', 'd', 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatSmallStrideTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(1, 1, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {0, 'a', 0, 'b', 0, 'c', 0, 'd', 0, 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ControlPacketTests, RejectsZeroLengthPacket) {
  net::packet_t packet {enet_packet_create(nullptr, 0, 0)};

  ASSERT_NE(packet, nullptr);
  EXPECT_EQ(packet->data, nullptr);
  EXPECT_EQ(stream::parse_control_packet(*packet), std::nullopt);
}

TEST(ControlPacketTests, RejectsOneBytePacket) {
  const std::uint8_t data {0x06};
  net::packet_t packet {enet_packet_create(&data, sizeof(data), 0)};

  ASSERT_NE(packet, nullptr);
  EXPECT_EQ(stream::parse_control_packet(*packet), std::nullopt);
}

TEST(ControlPacketTests, AcceptsTypeWithoutPayload) {
  const std::array<std::uint8_t, 2> data {0x06, 0x02};
  net::packet_t packet {enet_packet_create(data.data(), data.size(), 0)};

  ASSERT_NE(packet, nullptr);
  auto message = stream::parse_control_packet(*packet);
  ASSERT_TRUE(message);
  EXPECT_EQ(message->first, 0x0206);
  EXPECT_TRUE(message->second.empty());
}

TEST(ControlPacketTests, AcceptsTypeAndPayload) {
  const std::array<std::uint8_t, 5> data {0x06, 0x02, 'a', 'b', 'c'};
  net::packet_t packet {enet_packet_create(data.data(), data.size(), 0)};

  ASSERT_NE(packet, nullptr);
  auto message = stream::parse_control_packet(*packet);
  ASSERT_TRUE(message);
  EXPECT_EQ(message->first, 0x0206);
  EXPECT_EQ(message->second, "abc");
}

TEST(LiveChangeTests, ParsesWhatAClientAsksFor) {
  stream::session::live_change_t change;
  EXPECT_EQ(stream::session::parse_change({{"bitrate_kbps", 30000}, {"fec_percent", 25}, {"tuning", "quality"}}, change), "");
  EXPECT_EQ(change.bitrate_kbps, 30000);
  EXPECT_EQ(change.fec_percent, 25);
  EXPECT_EQ(change.tuning, config::tuning_e::quality);

  auto error = [](const nlohmann::json &body) {
    stream::session::live_change_t c;
    return stream::session::parse_change(body, c);
  };
  EXPECT_NE(error(nlohmann::json::object()), "");
  EXPECT_NE(error(nlohmann::json::array()), "");
  EXPECT_NE(error({{"bitrate_kbps", 100}}), "");
  EXPECT_NE(error({{"bitrate_kbps", "20000"}}), "");
  EXPECT_NE(error({{"fec_percent", 0}}), "");
  EXPECT_NE(error({{"tuning", "fast"}}), "");
  EXPECT_EQ(error({{"fec_percent", 255}, {"bitrate_kbps", nullptr}}), "");
}

TEST(LiveChangeTests, EncoderBitrateLeavesRoomForErrorCorrectionAndAudio) {
  // 20 Mbps, 20 % error correction, stereo at normal quality: as at launch
  EXPECT_EQ(stream::encoder_kbps(20000, 20, false, 2), 15308);
  // Error correction above 80 % isn't taken off
  EXPECT_EQ(stream::encoder_kbps(20000, 90, false, 2), 19308);
  // Small bitrates lose at most 20 % for audio and 10 % for overhead
  EXPECT_EQ(stream::encoder_kbps(1000, 0, true, 8), 720);
}

TEST(LiveChangeTests, ParsesPictureSizeFrameRateAndCodec) {
  stream::session::live_change_t change;
  EXPECT_EQ(stream::session::parse_change({{"width", 1280}, {"height", 720}, {"fps", 120}, {"codec", "av1"}}, change), "");
  EXPECT_EQ(change.width, 1280);
  EXPECT_EQ(change.height, 720);
  EXPECT_EQ(change.fps, 120);
  EXPECT_EQ(change.codec, 2);

  auto codec = [](const char *name) {
    stream::session::live_change_t c;
    stream::session::parse_change({{"codec", name}}, c);
    return c.codec.value_or(-1);
  };
  EXPECT_EQ(codec("H.264"), 0);
  EXPECT_EQ(codec("hevc"), 1);
  EXPECT_EQ(codec("AV1"), 2);
  EXPECT_EQ(codec("vp9"), -1);

  auto error = [](const nlohmann::json &body) {
    stream::session::live_change_t c;
    return stream::session::parse_change(body, c);
  };
  EXPECT_NE(error({{"width", 1280}}), "");  // without height
  EXPECT_NE(error({{"width", 1281}, {"height", 720}}), "");  // odd
  EXPECT_NE(error({{"width", 100}, {"height", 100}}), "");
  EXPECT_NE(error({{"fps", 5}}), "");
  EXPECT_NE(error({{"codec", 1}}), "");
}

// N9: with several streams on one display, the display captures at the highest rate any
// of them asked for, so one stream's change doesn't slow the others down.
TEST(LiveChangeTests, SharedDisplayCapturesAtTheHighestRate) {
  EXPECT_EQ(video::shared_capture_fps({60}), 60);
  EXPECT_EQ(video::shared_capture_fps({30, 120, 60}), 120);
  EXPECT_EQ(video::shared_capture_fps({120, 30}), 120);  // the second stream lowered its rate
}

TEST(LiveChangeTests, RefusesFormatsTheEncoderCantTake) {
  auto hevc = video::active_hevc_mode;
  auto av1 = video::active_av1_mode;
  video::active_hevc_mode = 2;  // HEVC 8-bit only
  video::active_av1_mode = 1;  // no AV1

  EXPECT_EQ(video::live_format_error(0, 1920, 1080, false), "");
  EXPECT_EQ(video::live_format_error(1, 3840, 2160, false), "");
  EXPECT_NE(video::live_format_error(2, 1920, 1080, false), "");  // AV1 off
  EXPECT_EQ(video::live_format_error(1, 1920, 1080, true), "");  // HEVC without 10-bit: SDR, as at launch
  EXPECT_NE(video::live_format_error(0, 1920, 1080, true), "");  // H.264 HDR
  EXPECT_NE(video::live_format_error(0, 7680, 4320, false), "");  // H.264 above 4096
  EXPECT_EQ(video::live_format_error(1, 7680, 4320, false), "");

  video::active_hevc_mode = hevc;
  video::active_av1_mode = av1;
}

// N10: an announced restart that never happened (a failed update) stops being reported,
// so a client whose stream ends later doesn't wait for a restart that won't come.
TEST(RestartAnnouncementTests, ExpiresWhenTheRestartDoesntHappen) {
  auto start = std::chrono::steady_clock::now();
  stream::announce_restart("update", start);
  EXPECT_EQ(stream::restart_reason(start + std::chrono::seconds(30)), "update");
  EXPECT_EQ(stream::restart_reason(start + stream::restart_announcement_lifetime + std::chrono::seconds(1)), "");
  EXPECT_EQ(stream::restart_reason(start + std::chrono::seconds(30)), "");  // cleared for good

  stream::announce_restart("restart", start);
  EXPECT_EQ(stream::restart_reason(start), "restart");
  stream::announce_restart("restart", start - stream::restart_announcement_lifetime * 2);
  EXPECT_EQ(stream::restart_reason(start), "");
}
