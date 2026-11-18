/**
 * @file tests/unit/test_stream_metrics.cpp
 * @brief Test src/stream_metrics.* and src/host_stats.*: per-stream figures and host load math.
 */
#include "../tests_common.h"

// local includes
#include <src/host_stats.h>
#include <src/stream_metrics.h>

using namespace std::chrono_literals;
using stream_metrics::recorder_t;

namespace {
  // Sends `fps` frames a second for `seconds`, each `bytes` long, with the given latency.
  void stream_for(recorder_t &r, stream_metrics::clock::time_point &now, int seconds, int fps, std::size_t bytes, std::chrono::microseconds latency) {
    auto step = std::chrono::duration_cast<stream_metrics::clock::duration>(1s) / fps;
    for (int i = 0; i < seconds * fps; ++i) {
      r.frame_sent(bytes, latency, 1ms, false, now);
      now += step;
    }
  }
}  // namespace

TEST(StreamMetricsTests, NothingRecentBeforeAFullSecond) {
  auto t0 = stream_metrics::clock::now();
  recorder_t r {t0};
  auto s = r.snapshot(t0 + 500ms);
  EXPECT_EQ(s.window_s, 0);
  EXPECT_FALSE(s.fps);
  EXPECT_FALSE(s.bitrate_kbps);
  EXPECT_FALSE(s.latency_ms);

  r.frame_sent(1000, 2ms, 1ms, true, t0 + 600ms);
  s = r.snapshot(t0 + 900ms);
  EXPECT_EQ(s.window_s, 0);
  EXPECT_FALSE(s.fps);
  EXPECT_EQ(s.frames, 1u);
  EXPECT_EQ(s.key_frames, 1u);

  auto j = stream_metrics::to_json(s);
  EXPECT_TRUE(j["fps"].is_null());
  EXPECT_TRUE(j["latency_ms"].is_null());
  EXPECT_EQ(j["frames"], 1);
}

TEST(StreamMetricsTests, FrameRateAndBitrateOverTheWindow) {
  auto t0 = stream_metrics::clock::now();
  recorder_t r {t0};
  // Setup time before the first frame doesn't count.
  auto now = t0 + 3s;
  stream_for(r, now, 10, 60, 25'000, 4ms);  // 60 fps at 12 Mbps

  auto s = r.snapshot(now);
  EXPECT_EQ(s.window_s, stream_metrics::window_seconds);
  ASSERT_TRUE(s.fps);
  EXPECT_NEAR(*s.fps, 60, 0.5);
  ASSERT_TRUE(s.bitrate_kbps);
  EXPECT_NEAR(*s.bitrate_kbps, 12'000, 100);
  ASSERT_TRUE(s.latency_ms);
  EXPECT_NEAR(s.latency_ms->avg, 4, 0.01);
  ASSERT_TRUE(s.send_ms);
  EXPECT_NEAR(s.send_ms->max, 1, 0.01);
  EXPECT_NEAR(s.duration_s, 10, 0.1);
  EXPECT_EQ(s.frames, 600u);
  EXPECT_EQ(s.bytes, 600u * 25'000);
}

TEST(StreamMetricsTests, WindowFollowsRecentFrames) {
  auto t0 = stream_metrics::clock::now();
  recorder_t r {t0};
  auto now = t0;
  stream_for(r, now, 10, 120, 1000, 2ms);
  stream_for(r, now, 10, 30, 1000, 10ms);

  auto s = r.snapshot(now);
  ASSERT_TRUE(s.fps);
  EXPECT_NEAR(*s.fps, 30, 0.5);
  ASSERT_TRUE(s.latency_ms);
  EXPECT_NEAR(s.latency_ms->min, 10, 0.01);
  EXPECT_EQ(s.frames, 1500u);

  // A stalled stream drops to zero rather than keeping old figures.
  s = r.snapshot(now + 20s);
  ASSERT_TRUE(s.fps);
  EXPECT_EQ(*s.fps, 0);
  EXPECT_FALSE(s.latency_ms);
}

TEST(StreamMetricsTests, LatencyRangeSkipsDuplicateFrames) {
  auto t0 = stream_metrics::clock::now();
  recorder_t r {t0};
  r.frame_sent(100, 3ms, 1ms, false, t0);
  r.frame_sent(100, 9ms, 2ms, false, t0 + 100ms);
  r.frame_sent(100, std::nullopt, 5ms, false, t0 + 200ms);
  r.frame_sent(100, 6ms, 1ms, false, t0 + 300ms);

  auto s = r.snapshot(t0 + 1s);
  ASSERT_TRUE(s.latency_ms);
  EXPECT_NEAR(s.latency_ms->min, 3, 0.01);
  EXPECT_NEAR(s.latency_ms->avg, 6, 0.01);
  EXPECT_NEAR(s.latency_ms->max, 9, 0.01);
  ASSERT_TRUE(s.send_ms);
  EXPECT_NEAR(s.send_ms->avg, 2.25, 0.01);
  EXPECT_NEAR(s.send_ms->max, 5, 0.01);
  EXPECT_EQ(s.duplicate_frames, 1u);
}

TEST(StreamMetricsTests, RecoveryRequestsAndLoss) {
  auto t0 = stream_metrics::clock::now();
  recorder_t r {t0};
  r.frame_sent(100, 1ms, 1ms, true, t0);
  r.key_frame_requested(t0 + 1500ms);
  r.ref_frames_invalidated(10, 14, t0 + 1600ms);
  r.ref_frames_invalidated(20, 19, t0 + 1700ms);  // malformed range: counted, no frames
  r.client_loss_report(3);
  r.client_loss_report(-1);

  auto s = r.snapshot(t0 + 2100ms);
  EXPECT_EQ(s.key_frame_requests, 1u);
  EXPECT_EQ(s.ref_invalidations, 2u);
  EXPECT_EQ(s.frames_invalidated, 5u);
  EXPECT_EQ(s.client_lost_frames, 3u);
  EXPECT_EQ(s.recent_recovery_requests, 3u);

  // Out of the window later on; totals stay.
  s = r.snapshot(t0 + 30s);
  EXPECT_EQ(s.recent_recovery_requests, 0u);
  EXPECT_EQ(s.key_frame_requests, 1u);
}

TEST(HostStatsTests, CpuPercent) {
  EXPECT_NEAR(*host_stats::cpu_percent({100, 1000}, {175, 1100}), 25, 0.001);
  EXPECT_NEAR(*host_stats::cpu_percent({0, 0}, {0, 100}), 100, 0.001);
  EXPECT_FALSE(host_stats::cpu_percent({0, 100}, {0, 100}));  // no time passed
  EXPECT_FALSE(host_stats::cpu_percent({50, 100}, {40, 200}));  // counters went back
}

TEST(HostStatsTests, GpuLoadIsTheBusiestEngine) {
  auto [gpu, encoder] = host_stats::gpu_load({
    {"pid_100_luid_0x00000000_0x0000C2A4_phys_0_eng_0_engtype_3D", 30},
    {"pid_200_luid_0x00000000_0x0000C2A4_phys_0_eng_0_engtype_3D", 25},  // same engine: adds up
    {"pid_100_luid_0x00000000_0x0000C2A4_phys_0_eng_3_engtype_Copy", 70},
    {"pid_300_luid_0x00000000_0x0000C2A4_phys_0_eng_5_engtype_VideoEncode", 40},
    {"pid_300_luid_0x00000000_0x0000D111_phys_0_eng_0_engtype_3D", 10},  // another GPU
    {"no luid here", 99},
  });
  ASSERT_TRUE(gpu);
  EXPECT_NEAR(*gpu, 70, 0.001);
  ASSERT_TRUE(encoder);
  EXPECT_NEAR(*encoder, 40, 0.001);
}

TEST(HostStatsTests, GpuLoadAmdCodecEngineAndCaps) {
  auto [gpu, encoder] = host_stats::gpu_load({
    {"pid_1_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 80},
    {"pid_2_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 60},
    {"pid_1_luid_0x0_0x1_phys_0_eng_7_engtype_VideoCodec", 12.5},
  });
  ASSERT_TRUE(gpu);
  EXPECT_NEAR(*gpu, 100, 0.001);
  ASSERT_TRUE(encoder);
  EXPECT_NEAR(*encoder, 12.5, 0.001);

  auto none = host_stats::gpu_load({});
  EXPECT_FALSE(none.first);
  EXPECT_FALSE(none.second);
}

TEST(HostStatsTests, JsonHasEveryFieldEvenWhenUnknown) {
  auto j = host_stats::to_json({});
  for (const auto *key : {"cpu_percent", "process_cpu_percent", "memory_total_mb", "memory_used_mb", "process_memory_mb", "gpu_percent", "encoder_percent"}) {
    ASSERT_TRUE(j.contains(key)) << key;
    EXPECT_TRUE(j[key].is_null()) << key;
  }
}

#ifdef _WIN32
TEST(HostStatsTests, WindowsReportsCpuAndMemory) {
  auto s = host_stats::current();
  ASSERT_TRUE(s.cpu_percent);
  EXPECT_GE(*s.cpu_percent, 0);
  EXPECT_LE(*s.cpu_percent, 100);
  ASSERT_TRUE(s.memory_total_mb);
  ASSERT_TRUE(s.memory_used_mb);
  EXPECT_GT(*s.memory_total_mb, 0u);
  EXPECT_LE(*s.memory_used_mb, *s.memory_total_mb);
  ASSERT_TRUE(s.process_memory_mb);
}
#endif
