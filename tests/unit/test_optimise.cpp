/**
 * @file tests/unit/test_optimise.cpp
 * @brief Test src/optimise.*: which preset the encoder test picks (N6).
 */
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <fstream>

// local includes
#include <src/config_schema.h>
#include <src/optimise.h>
#include <src/recommend.h>

using optimise::result_t;

namespace {
  result_t step(nlohmann::json value, double p95_ms, int frames = 120) {
    result_t r;
    r.value = std::move(value);
    r.frames = frames;
    r.avg_ms = p95_ms * 0.8;
    r.p95_ms = p95_ms;
    return r;
  }

  constexpr double frame_ms = 1000.0 / 60;  // 6.7 ms keeps up (40 %)
}  // namespace

TEST(Optimise, PicksHighestQualityThatKeepsUp) {
  std::vector<result_t> results {step("speed", 2.0), step("balanced", 3.0), step("quality", 5.5)};
  auto pick = optimise::pick(results, 2, frame_ms);
  ASSERT_TRUE(pick);
  EXPECT_EQ(results[*pick].value, "quality");
}

TEST(Optimise, SkipsStepsThatFallBehind) {
  std::vector<result_t> results {step("speed", 2.0), step("balanced", 4.0), step("quality", 9.0)};
  auto pick = optimise::pick(results, 2, frame_ms);
  ASSERT_TRUE(pick);
  EXPECT_EQ(results[*pick].value, "balanced");
}

TEST(Optimise, NeverAboveTheQualityCeiling) {
  std::vector<result_t> results {step(1, 1.0), step(2, 1.0), step(3, 1.0), step(4, 1.0), step(5, 1.0)};
  auto pick = optimise::pick(results, 3, frame_ms);
  ASSERT_TRUE(pick);
  EXPECT_EQ(results[*pick].value, 4);
}

TEST(Optimise, FastestWhenNothingKeepsUp) {
  std::vector<result_t> results {step("ultrafast", 9.0), step("superfast", 12.0)};
  auto pick = optimise::pick(results, 1, frame_ms);
  ASSERT_TRUE(pick);
  EXPECT_EQ(results[*pick].value, "ultrafast");
}

TEST(Optimise, IgnoresStepsThatCouldNotBeMeasured) {
  auto broken = step("speed", 1.0, 0);
  broken.error = "The display showed no new frames";
  std::vector<result_t> results {broken, step("balanced", 3.0), step("quality", 1.0, 0)};
  auto pick = optimise::pick(results, 2, frame_ms);
  ASSERT_TRUE(pick);
  EXPECT_EQ(results[*pick].value, "balanced");
}

TEST(Optimise, NothingWhenNothingWasMeasured) {
  std::vector<result_t> results {step("speed", 0, 0), step("balanced", 0, 0)};
  EXPECT_FALSE(optimise::pick(results, 1, frame_ms));
}

TEST(Optimise, PresetScalesMatchTheRecommendations) {
  auto amd = recommend::preset_scale("amd_quality");
  ASSERT_TRUE(amd);
  EXPECT_EQ(amd->steps.front(), "speed");
  EXPECT_EQ(amd->quality_ceiling, 2u);
  EXPECT_FALSE(recommend::preset_scale("fec_percentage"));
}

TEST(Optimise, ApplyValuesKeepsTheOldValueForRollback) {
  auto dir = std::filesystem::temp_directory_path() / "norishine-optimise-test";
  std::filesystem::create_directories(dir);
  auto conf = (dir / "sunshine.conf").string();
  {
    std::ofstream f {conf};
    f << "amd_quality = balanced\n";
  }
  recommend::store_t store;
  auto reply = store.apply_values({{"amd_quality", "quality"}}, conf, "Optimise");
  EXPECT_EQ(reply.code, 200);
  EXPECT_EQ(config::schema::read_config_file(conf)["values"]["amd_quality"], "quality");
  auto undo = store.rollback(conf);
  EXPECT_EQ(undo.code, 200);
  EXPECT_EQ(config::schema::read_config_file(conf)["values"]["amd_quality"], "balanced");
  std::filesystem::remove_all(dir);
}
