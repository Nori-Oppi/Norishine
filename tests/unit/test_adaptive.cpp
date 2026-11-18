/**
 * @file tests/unit/test_adaptive.cpp
 * @brief Test src/adaptive.*: the per-stream bitrate and error correction controller.
 */
#include "../tests_common.h"

// local includes
#include <src/adaptive.h>

using namespace std::chrono_literals;
using adaptive::client_report_t;
using adaptive::controller_t;
using adaptive::host_sample_t;
using adaptive::reason_e;
using adaptive::settings_t;

namespace {
  /// Drives a controller second by second, like the control thread does.
  struct sim_t {
    explicit sim_t(int start_kbps = 20000, settings_t settings = {}, bool live = true):
        t {adaptive::clock::now()},
        c {settings, start_kbps, 60, live, t} {
    }

    /// One second passes; `recovery` new recovery requests arrived in it.
    adaptive::decision_t second(int recovery = 0, std::optional<client_report_t> report = std::nullopt) {
      t += 1s;
      total += recovery;
      if (report) {
        c.report(*report, t);
      }
      host_sample_t sample;
      sample.recovery_requests = total;
      sample.fps = 60;
      sample.latency_ms = 5;
      auto d = c.tick(sample, t);
      if (d.bitrate_kbps) {
        changes.push_back(*d.bitrate_kbps);
      }
      return d;
    }

    void clean(int seconds, std::optional<client_report_t> report = std::nullopt) {
      for (int i = 0; i < seconds; ++i) {
        second(0, report);
      }
    }

    adaptive::state_t state() const {
      return c.state(t);
    }

    adaptive::clock::time_point t;
    controller_t c;
    std::uint64_t total = 0;
    std::vector<int> changes;
  };

  client_report_t rtt(double ms, std::optional<double> loss = std::nullopt) {
    client_report_t r;
    r.rtt_ms = ms;
    r.loss_percent = loss;
    return r;
  }
}  // namespace

TEST(AdaptiveTests, StaysAtTheStartBitrateOnACleanNetwork) {
  sim_t s;
  s.clean(120);
  EXPECT_TRUE(s.changes.empty());
  auto st = s.state();
  EXPECT_EQ(st.bitrate_kbps, 20000);
  EXPECT_EQ(st.max_kbps, 20000);
  EXPECT_EQ(st.fec_percent, 20);
  EXPECT_EQ(st.last_reason, reason_e::none);
}

TEST(AdaptiveTests, NothingHappensWhileTheStreamSettles) {
  sim_t s;
  s.second(10);
  s.second(10);
  EXPECT_TRUE(s.changes.empty());
}

TEST(AdaptiveTests, KeyFrameRequestsWhileTheClientStartsAreNotLoss) {
  // Clients ask for a few key frames while their decoder starts (Norilight sends four).
  sim_t s;
  s.second(0);
  s.second(4);
  s.clean(40);
  EXPECT_TRUE(s.changes.empty());
  EXPECT_EQ(s.state().fec_percent, 20);
  EXPECT_EQ(s.state().last_reason, reason_e::none);
}

TEST(AdaptiveTests, ClientLossWhileTheStreamSettlesIsNotLoss) {
  sim_t s;
  s.second(0, rtt(5, 3));
  s.second(0, rtt(5, 3));
  s.second(0);
  s.second(0);  // the report from the grace period is still fresh here
  EXPECT_EQ(s.state().fec_percent, 20);
  s.second(0, rtt(5, 3));  // after the grace period, loss counts
  EXPECT_EQ(s.state().fec_percent, 30);
  EXPECT_EQ(s.state().last_reason, reason_e::loss);
}

TEST(AdaptiveTests, HeavyLossCutsTheBitrate) {
  sim_t s;
  s.clean(5);
  auto d = s.second(8);
  ASSERT_TRUE(d.bitrate_kbps);
  EXPECT_EQ(*d.bitrate_kbps, 14000);
  EXPECT_EQ(s.state().last_reason, reason_e::congestion);
  EXPECT_EQ(s.state().decreases, 1u);
}

TEST(AdaptiveTests, LossFromBeforeAChangeIsIgnored) {
  sim_t s;
  s.clean(5);
  s.second(8);
  s.second(8);  // within the grace period after the change
  EXPECT_EQ(s.changes.size(), 1u);
  s.second(8);
  EXPECT_EQ(s.changes.size(), 2u);
  EXPECT_EQ(s.changes.back(), 9800);
}

TEST(AdaptiveTests, LightLossRaisesErrorCorrectionFirst) {
  sim_t s;
  s.clean(5);
  auto d = s.second(2);
  EXPECT_FALSE(d.bitrate_kbps);
  EXPECT_EQ(d.fec_percent, 30);
  EXPECT_EQ(s.state().last_reason, reason_e::loss);

  // Loss goes on: more error correction once more, then less bitrate.
  s.second(0);
  s.second(0);
  d = s.second(2);
  EXPECT_EQ(d.fec_percent, 40);
  EXPECT_TRUE(s.changes.empty());
  s.second(0);
  s.second(0);
  d = s.second(2);
  ASSERT_TRUE(d.bitrate_kbps);
  EXPECT_EQ(*d.bitrate_kbps, 17000);
}

TEST(AdaptiveTests, ErrorCorrectionNeverPassesTheSetting) {
  settings_t settings;
  settings.base_fec = 20;
  settings.max_fec = 25;
  sim_t s {20000, settings};
  s.clean(5);
  EXPECT_EQ(s.second(2).fec_percent, 25);
}

TEST(AdaptiveTests, ErrorCorrectionRelaxesWhenClean) {
  sim_t s;
  s.clean(5);
  s.second(2);
  EXPECT_EQ(s.c.fec_percent(), 30);
  s.clean(15);
  EXPECT_EQ(s.c.fec_percent(), 30);
  s.clean(10);
  EXPECT_EQ(s.c.fec_percent(), 25);
  s.clean(10);
  EXPECT_EQ(s.c.fec_percent(), 20);
  s.clean(60);
  EXPECT_EQ(s.c.fec_percent(), 20);
}

TEST(AdaptiveTests, RisingRoundTripTimeBacksOff) {
  sim_t s;
  s.clean(10, rtt(5));
  // One slow reading is noise.
  s.second(0, rtt(40));
  EXPECT_TRUE(s.changes.empty());
  s.second(0, rtt(5));
  // Two in a row mean queues are filling.
  s.second(0, rtt(40));
  auto d = s.second(0, rtt(40));
  ASSERT_TRUE(d.bitrate_kbps);
  EXPECT_EQ(*d.bitrate_kbps, 17000);
  EXPECT_EQ(s.state().last_reason, reason_e::delay);
  ASSERT_TRUE(s.state().base_rtt_ms);
  EXPECT_EQ(*s.state().base_rtt_ms, 5);
}

TEST(AdaptiveTests, LossWithRisingDelayIsCongestion) {
  sim_t s;
  s.clean(10, rtt(5));
  auto d = s.second(0, rtt(40, 2));
  ASSERT_TRUE(d.bitrate_kbps);
  EXPECT_EQ(*d.bitrate_kbps, 14000);
  EXPECT_EQ(s.state().last_reason, reason_e::congestion);
}

TEST(AdaptiveTests, ClientReportedHeavyLossCutsTheBitrate) {
  sim_t s;
  s.clean(5);
  auto d = s.second(0, rtt(5, 10));
  ASSERT_TRUE(d.bitrate_kbps);
  EXPECT_EQ(*d.bitrate_kbps, 14000);
}

TEST(AdaptiveTests, StaleReportsAreIgnored) {
  sim_t s;
  s.clean(5);
  s.c.report(rtt(5, 10), s.t - 10s);
  s.clean(5);
  EXPECT_TRUE(s.changes.empty());
  EXPECT_FALSE(s.state().client_reports);
}

TEST(AdaptiveTests, ProbesBackUpToTheStartBitrateButNotPast) {
  sim_t s;
  s.clean(5);
  s.second(8);  // 20000 -> 14000
  s.clean(300);
  ASSERT_FALSE(s.changes.empty());
  EXPECT_EQ(s.changes.back(), 20000);
  EXPECT_EQ(s.state().bitrate_kbps, 20000);
  for (auto kbps : s.changes) {
    EXPECT_LE(kbps, 20000);
  }
  EXPECT_GE(s.state().increases, 2u);
}

TEST(AdaptiveTests, ProbesSlowlyNearWhereTroubleStarted) {
  sim_t s;
  s.clean(5);
  s.second(8);  // knee at 20000, now 14000
  s.clean(12);
  ASSERT_EQ(s.changes.size(), 2u);
  EXPECT_EQ(s.changes[1], 16100);  // normal steps, far from the knee
  s.clean(10);
  ASSERT_EQ(s.changes.size(), 3u);
  EXPECT_EQ(s.changes[2], 18500);
  // 18500 * 1.15 passes the knee: wait 30 seconds and step less.
  s.clean(29);
  EXPECT_EQ(s.changes.size(), 3u);
  s.clean(1);
  ASSERT_EQ(s.changes.size(), 4u);
  EXPECT_EQ(s.changes[3], 19200);
}

TEST(AdaptiveTests, ProbesQuicklyAfterARunOfCuts) {
  sim_t s;
  s.clean(5);
  s.second(8);  // 20000 -> 14000
  s.second(0);
  s.second(8);  // -> 9800
  s.second(0);
  s.second(8);  // -> 6900; trouble started at 20000, not here
  ASSERT_EQ(s.changes.size(), 3u);
  EXPECT_EQ(s.changes[2], 6900);
  s.clean(12);
  ASSERT_EQ(s.changes.size(), 4u);
  EXPECT_EQ(s.changes[3], 7900);  // a normal step, though 7900 passes 6900
  s.clean(10);
  ASSERT_EQ(s.changes.size(), 5u);
  EXPECT_EQ(s.changes[4], 9100);
}

TEST(AdaptiveTests, NeverBelowTheLowestBitrate) {
  settings_t settings;
  settings.min_kbps = 10000;
  sim_t s {20000, settings};
  s.clean(5);
  for (int i = 0; i < 20; ++i) {
    s.second(10);
  }
  EXPECT_EQ(s.state().bitrate_kbps, 10000);
  for (auto kbps : s.changes) {
    EXPECT_GE(kbps, 10000);
  }
}

TEST(AdaptiveTests, LowestBitrateIsCappedByTheStartBitrate) {
  settings_t settings;
  settings.min_kbps = 50000;
  sim_t s {20000, settings};
  EXPECT_EQ(s.state().min_kbps, 20000);
  s.clean(5);
  s.second(10);
  EXPECT_TRUE(s.changes.empty());
}

TEST(AdaptiveTests, WithoutLiveBitrateOnlyErrorCorrectionChanges) {
  sim_t s {20000, {}, false};
  s.clean(5);
  for (int i = 0; i < 20; ++i) {
    s.second(10);
  }
  EXPECT_TRUE(s.changes.empty());
  EXPECT_GT(s.c.fec_percent(), 20);
  EXPECT_FALSE(s.state().live_bitrate);
}

TEST(AdaptiveTests, OffMeansNoChanges) {
  settings_t settings;
  settings.enabled = false;
  sim_t s {20000, settings};
  s.clean(5);
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(s.second(10).fec_percent, 20);
  }
  EXPECT_TRUE(s.changes.empty());
}

TEST(AdaptiveTests, NoProbingWhileTheClientCantDecodeFastEnough) {
  sim_t s;
  s.clean(5);
  s.second(8);  // 14000
  client_report_t slow;
  slow.decode_ms = 25;  // 60 fps leaves 16.7 ms a frame
  s.clean(60, slow);
  EXPECT_EQ(s.changes.size(), 1u);
  EXPECT_TRUE(s.state().client_limited);
}

TEST(AdaptiveTests, ReportsParse) {
  client_report_t r;
  EXPECT_EQ(adaptive::parse_report({{"loss_percent", 1.5}, {"rtt_ms", 3}, {"decode_ms", nullptr}, {"other", "ignored"}}, r), "");
  EXPECT_EQ(r.loss_percent, 1.5);
  EXPECT_EQ(r.rtt_ms, 3);
  EXPECT_FALSE(r.decode_ms);

  EXPECT_NE(adaptive::parse_report({{"loss_percent", 101}}, r), "");
  EXPECT_NE(adaptive::parse_report({{"rtt_ms", -1}}, r), "");
  EXPECT_NE(adaptive::parse_report({{"fps", "60"}}, r), "");
  EXPECT_NE(adaptive::parse_report(nlohmann::json::array(), r), "");
}

TEST(AdaptiveTests, StateAsJson) {
  sim_t s;
  s.clean(5);
  s.second(8);
  auto j = adaptive::to_json(s.state());
  EXPECT_EQ(j["bitrate_kbps"], 14000);
  EXPECT_EQ(j["max_kbps"], 20000);
  EXPECT_EQ(j["last_reason"], "congestion");
  EXPECT_TRUE(j["last_change_s"].is_number());
  EXPECT_TRUE(j["base_rtt_ms"].is_null());
}
