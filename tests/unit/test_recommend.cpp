/**
 * @file tests/unit/test_recommend.cpp
 * @brief Test src/recommend.*: encoder setting recommendations, apply and rollback.
 */
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <fstream>
#include <sstream>

// local includes
#include <src/recommend.h>

using namespace std::chrono_literals;
using recommend::recorder_t;
using recommend::stream_t;
using recommend::tick_t;

namespace {
  stream_t stream(nlohmann::json settings, std::string tuning = "") {
    stream_t s;
    s.ended = 1000;
    s.tuning = std::move(tuning);
    s.seconds = 120;
    s.target_fps = 60;
    s.latency_ms = 4;
    s.encoder_percent = 40;
    s.settings = std::move(settings);
    return s;
  }

  stream_t limited(nlohmann::json settings, std::string tuning = "") {
    auto s = stream(std::move(settings), std::move(tuning));
    s.host_limited_share = 0.3;
    s.latency_ms = 15;
    return s;
  }

  stream_t roomy(nlohmann::json settings) {
    return stream(std::move(settings), "quality");
  }

  stream_t lossy(nlohmann::json settings) {
    auto s = stream(std::move(settings));
    s.recovery_per_minute = 8;
    return s;
  }

  const nlohmann::json nv4 = {{"nvenc_preset", 4}, {"fec_percentage", 20}};

  /// A config file in a temporary folder, removed at the end of the test.
  struct temp_config_t {
    temp_config_t() {
      dir = std::filesystem::temp_directory_path() / ("recommend_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      std::filesystem::create_directories(dir);
      file = (dir / "sunshine.conf").string();
    }

    ~temp_config_t() {
      std::error_code ec;
      std::filesystem::remove_all(dir, ec);
    }

    void write(const std::string &text) const {
      std::ofstream(file, std::ios::binary) << text;
    }

    std::string read() const {
      std::ifstream in(file, std::ios::binary);
      std::stringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }

    std::filesystem::path dir;
    std::string file;
  };
}  // namespace

TEST(RecommendTests, PresetScales) {
  EXPECT_EQ(recommend::preset_key("nvenc"), "nvenc_preset");
  EXPECT_EQ(recommend::preset_key("amdvce"), "amd_quality");
  EXPECT_EQ(recommend::preset_key("quicksync"), "qsv_preset");
  EXPECT_EQ(recommend::preset_key("software"), "sw_preset");
  EXPECT_EQ(recommend::preset_key("vaapi"), "");

  EXPECT_EQ(*recommend::faster("nvenc_preset", 4), 3);
  EXPECT_FALSE(recommend::faster("nvenc_preset", 1));
  EXPECT_EQ(*recommend::higher_quality("nvenc_preset", 4), 5);
  EXPECT_FALSE(recommend::higher_quality("nvenc_preset", 5));  // P5 is as far as headroom goes
  EXPECT_EQ(*recommend::faster("amd_quality", "balanced"), "speed");
  EXPECT_EQ(*recommend::higher_quality("amd_quality", "balanced"), "quality");
  EXPECT_FALSE(recommend::higher_quality("amd_quality", "quality"));
  EXPECT_EQ(*recommend::faster("qsv_preset", "medium"), "fast");
  EXPECT_EQ(*recommend::faster("sw_preset", "superfast"), "ultrafast");
  EXPECT_FALSE(recommend::faster("sw_preset", "nonsense"));
  EXPECT_FALSE(recommend::faster("fec_percentage", 20));
}

TEST(RecommendTests, RecorderSumsUpAStream) {
  auto t = recommend::clock::now();
  recorder_t r {60, t};
  std::uint64_t recovery = 0;
  for (int i = 1; i <= 65; ++i) {
    tick_t tick;
    tick.fps = i % 4 == 0 ? 40 : 60;  // every fourth second the encoder falls behind
    tick.latency_ms = i % 4 == 0 ? 15 : 5;
    tick.encoder_percent = 50;
    // Raised at the start (not counted: before the first counted second), then twice more.
    tick.fec_percent = i < 4 ? 20 : i > 50 ? 40 : i > 35 ? 30 : 25;
    recovery += i % 10 == 0 ? 1 : 0;
    tick.recovery_total = recovery;
    r.tick(tick, t + std::chrono::seconds(i));
    // Ticks less than a second apart don't count twice.
    r.tick(tick, t + std::chrono::seconds(i) + 100ms);
  }
  auto s = r.finish("balanced", nv4, 1234);
  ASSERT_TRUE(s);
  EXPECT_EQ(s->seconds, 61);  // the first 4 seconds settle
  EXPECT_EQ(s->tuning, "balanced");
  EXPECT_EQ(s->ended, 1234);
  EXPECT_NEAR(s->host_limited_share, 15.0 / 61, 0.01);
  EXPECT_NEAR(*s->encoder_percent, 50, 0.01);
  EXPECT_EQ(s->fec_raises, 2);
  EXPECT_NEAR(s->recovery_per_minute, 6.0 * 60 / 61, 0.01);
  EXPECT_EQ(s->settings, nv4);
}

TEST(RecommendTests, ShortStreamsAreNotRecorded) {
  auto t = recommend::clock::now();
  recorder_t r {60, t};
  for (int i = 1; i <= 20; ++i) {
    tick_t tick;
    tick.fps = 60;
    tick.latency_ms = 5;
    r.tick(tick, t + std::chrono::seconds(i));
  }
  EXPECT_FALSE(r.finish("", nv4, 0));
}

TEST(RecommendTests, StreamRoundTripsThroughJson) {
  auto s = limited(nv4, "quality");
  s.encoder_percent.reset();
  auto back = recommend::stream_from_json(recommend::to_json(s));
  ASSERT_TRUE(back);
  EXPECT_EQ(back->tuning, "quality");
  EXPECT_NEAR(back->host_limited_share, 0.3, 0.001);
  EXPECT_FALSE(back->encoder_percent);
  EXPECT_EQ(back->settings, nv4);
  EXPECT_FALSE(recommend::stream_from_json("nonsense"));
}

TEST(RecommendTests, EncoderBottleneckRecommendsAFasterPreset) {
  std::deque<stream_t> streams {limited(nv4), stream(nv4), limited(nv4, "balanced")};
  auto recs = recommend::derive("nvenc", streams, nv4);
  ASSERT_EQ(recs.size(), 1);
  EXPECT_EQ(recs[0].key, "nvenc_preset");
  EXPECT_EQ(recs[0].current, 4);
  EXPECT_EQ(recs[0].recommended, 3);
  EXPECT_EQ(recs[0].reason, "encoder_limited");
  EXPECT_EQ(recs[0].streams, 3);
}

TEST(RecommendTests, OneStreamIsNotEnough) {
  std::deque<stream_t> streams {limited(nv4)};
  EXPECT_TRUE(recommend::derive("nvenc", streams, nv4).empty());
}

TEST(RecommendTests, AFullyBusyEncoderCountsAsTheBottleneck) {
  auto busy = stream(nv4);
  busy.encoder_percent = 97;
  std::deque<stream_t> streams {busy, busy};
  auto recs = recommend::derive("nvenc", streams, nv4);
  ASSERT_EQ(recs.size(), 1);
  EXPECT_EQ(recs[0].reason, "encoder_limited");
}

TEST(RecommendTests, PerformanceStreamsDontCountForTheBottleneck) {
  // They ran with the fastest preset anyway, not the host's.
  std::deque<stream_t> streams {limited(nv4, "performance"), limited(nv4, "performance"), limited(nv4, "performance")};
  EXPECT_TRUE(recommend::derive("nvenc", streams, nv4).empty());
}

TEST(RecommendTests, OnlyStreamsWithTheCurrentValueCount) {
  nlohmann::json nv5 = {{"nvenc_preset", 5}, {"fec_percentage", 20}};
  // Limited at P5, then the preset went to P4: P4 has no streams yet.
  std::deque<stream_t> streams {limited(nv5), limited(nv5), stream(nv4)};
  EXPECT_TRUE(recommend::derive("nvenc", streams, nv4).empty());
  // At P5 they would still count.
  EXPECT_EQ(recommend::derive("nvenc", streams, nv5).size(), 1);
}

TEST(RecommendTests, HeadroomAndQualityRecommendAHigherQualityPreset) {
  std::deque<stream_t> streams {roomy(nv4), roomy(nv4), stream(nv4, "")};
  auto recs = recommend::derive("nvenc", streams, nv4);
  ASSERT_EQ(recs.size(), 1);
  EXPECT_EQ(recs[0].recommended, 5);
  EXPECT_EQ(recs[0].reason, "headroom");

  // Without the encoder load, there's no telling how much room there is.
  auto unknown = roomy(nv4);
  unknown.encoder_percent.reset();
  streams = {unknown, unknown, unknown};
  EXPECT_TRUE(recommend::derive("nvenc", streams, nv4).empty());

  // Mostly Balanced clients: they didn't ask for more quality.
  streams = {roomy(nv4), stream(nv4), stream(nv4)};
  EXPECT_TRUE(recommend::derive("nvenc", streams, nv4).empty());

  // High latency is no headroom.
  auto slow = roomy(nv4);
  slow.latency_ms = 12;
  streams = {roomy(nv4), roomy(nv4), slow};
  EXPECT_TRUE(recommend::derive("nvenc", streams, nv4).empty());
}

TEST(RecommendTests, FrequentLossRecommendsMoreErrorCorrection) {
  std::deque<stream_t> streams {lossy(nv4), stream(nv4)};
  auto recs = recommend::derive("nvenc", streams, nv4);
  ASSERT_EQ(recs.size(), 1);
  EXPECT_EQ(recs[0].key, "fec_percentage");
  EXPECT_EQ(recs[0].recommended, 30);
  EXPECT_EQ(recs[0].reason, "loss");

  auto raised = stream(nv4);
  raised.fec_raises = 3;  // in 2 minutes
  streams = {raised, raised};
  EXPECT_EQ(recommend::derive("nvenc", streams, nv4).size(), 1);

  // One raise is one bad moment, not frequent loss; nor is a few raises in a long stream.
  raised.fec_raises = 1;
  streams = {raised, raised};
  EXPECT_TRUE(recommend::derive("nvenc", streams, nv4).empty());
  raised.fec_raises = 3;
  raised.seconds = 1800;
  streams = {raised, raised};
  EXPECT_TRUE(recommend::derive("nvenc", streams, nv4).empty());

  // Not past 50 %.
  nlohmann::json high = {{"nvenc_preset", 4}, {"fec_percentage", 50}};
  streams = {lossy(high), lossy(high)};
  EXPECT_TRUE(recommend::derive("nvenc", streams, high).empty());
}

TEST(RecommendTests, SeveralAtOnce) {
  auto both = limited(nv4);
  both.recovery_per_minute = 10;
  std::deque<stream_t> streams {both, both};
  auto recs = recommend::derive("nvenc", streams, nv4);
  ASSERT_EQ(recs.size(), 2);
  EXPECT_EQ(recs[0].key, "nvenc_preset");
  EXPECT_EQ(recs[1].key, "fec_percentage");
}

TEST(RecommendTests, OtherEncodersUseTheirOwnPreset) {
  nlohmann::json amd = {{"amd_quality", "balanced"}, {"fec_percentage", 20}};
  std::deque<stream_t> streams {limited(amd), limited(amd)};
  auto recs = recommend::derive("amdvce", streams, amd);
  ASSERT_EQ(recs.size(), 1);
  EXPECT_EQ(recs[0].recommended, "speed");
  // VA-API has no preset setting; only error correction can be recommended.
  EXPECT_TRUE(recommend::derive("vaapi", streams, amd).empty());
}

TEST(RecommendTests, TrackedValuesFillInDefaults) {
  auto values = recommend::tracked_values({{"nvenc_preset", 3}});
  EXPECT_EQ(values["nvenc_preset"], 3);
  EXPECT_EQ(values["fec_percentage"], 20);
  EXPECT_EQ(values["amd_quality"], "balanced");
  EXPECT_EQ(values["sw_preset"], "superfast");
}

TEST(RecommendTests, ApplyAndRollback) {
  temp_config_t config;
  config.write("nvenc_preset = 4\nsunshine_name = test\n");
  recommend::store_t store {config.dir / "recommendations.json"};

  auto described = store.describe("nvenc", config.file);
  EXPECT_TRUE(described["recommendations"].empty());
  EXPECT_TRUE(described["applied"].is_null());
  EXPECT_EQ(store.rollback(config.file).code, 409);
  EXPECT_EQ(store.apply(nlohmann::json::object(), "nvenc", config.file).code, 409);

  auto both = limited(nv4);
  both.recovery_per_minute = 10;
  store.record("nvenc", both);
  store.record("nvenc", both);

  described = store.describe("nvenc", config.file);
  ASSERT_EQ(described["recommendations"].size(), 2);
  EXPECT_EQ(described["streams"], 2);
  EXPECT_EQ(described["recommendations"][0]["restart_required"], true);

  // Bad bodies, unknown keys.
  EXPECT_EQ(store.apply({{"keys", "nvenc_preset"}}, "nvenc", config.file).code, 400);
  EXPECT_EQ(store.apply({{"keys", {"sw_preset"}}}, "nvenc", config.file).code, 409);

  // Apply one, then the other.
  auto reply = store.apply({{"keys", {"nvenc_preset"}}}, "nvenc", config.file);
  EXPECT_EQ(reply.code, 200) << reply.body.dump();
  EXPECT_EQ(reply.body["restart_required"], true);
  EXPECT_NE(config.read().find("nvenc_preset = 3"), std::string::npos);
  reply = store.apply(nlohmann::json(nullptr), "nvenc", config.file);
  EXPECT_EQ(reply.code, 200) << reply.body.dump();
  EXPECT_NE(config.read().find("fec_percentage = 30"), std::string::npos);

  // The streams ran with the old values, so nothing is recommended now.
  described = store.describe("nvenc", config.file);
  EXPECT_TRUE(described["recommendations"].empty());
  ASSERT_TRUE(described["applied"].is_object());
  EXPECT_EQ(described["applied"]["settings"].size(), 2);

  // Kept across a restart of the host.
  recommend::store_t reloaded {config.dir / "recommendations.json"};
  EXPECT_EQ(reloaded.history("nvenc").size(), 2);

  reply = reloaded.rollback(config.file);
  EXPECT_EQ(reply.code, 200) << reply.body.dump();
  EXPECT_TRUE(reply.body["skipped"].empty());
  auto text = config.read();
  EXPECT_NE(text.find("nvenc_preset = 4"), std::string::npos);
  // fec_percentage wasn't in the file before, so it's removed again.
  EXPECT_EQ(text.find("fec_percentage"), std::string::npos);
  EXPECT_NE(text.find("sunshine_name = test"), std::string::npos);
  EXPECT_TRUE(reloaded.describe("nvenc", config.file)["applied"].is_null());
  EXPECT_EQ(reloaded.describe("nvenc", config.file)["recommendations"].size(), 2);
}

TEST(RecommendTests, RollbackLeavesSettingsChangedSince) {
  temp_config_t config;
  config.write("nvenc_preset = 4\n");
  recommend::store_t store {config.dir / "recommendations.json"};
  store.record("nvenc", limited(nv4));
  store.record("nvenc", limited(nv4));
  ASSERT_EQ(store.apply({}, "nvenc", config.file).code, 200);

  // Someone set it by hand afterwards.
  config.write("nvenc_preset = 6\n");
  auto reply = store.rollback(config.file);
  EXPECT_EQ(reply.code, 200);
  EXPECT_EQ(reply.body["skipped"], nlohmann::json::array({"nvenc_preset"}));
  EXPECT_NE(config.read().find("nvenc_preset = 6"), std::string::npos);
}

TEST(RecommendTests, HistoryIsShort) {
  recommend::store_t store;
  for (int i = 0; i < 15; ++i) {
    store.record("nvenc", stream(nv4));
  }
  EXPECT_EQ(store.history("nvenc").size(), recommend::history_size);
  store.record("", stream(nv4));
  EXPECT_TRUE(store.history("").empty());
}
