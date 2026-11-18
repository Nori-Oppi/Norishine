/**
 * @file tests/unit/test_tuning.cpp
 * @brief Test the per-stream encoder choices for a client's tuning goal (config::tuned_*).
 */
#include "../tests_common.h"

// local includes
#include <src/config.h>

using config::tuning_e;

namespace {
  std::string option(const std::vector<std::pair<std::string, std::string>> &options, const std::string &name) {
    for (auto &[n, v] : options) {
      if (n == name) {
        return v;
      }
    }
    return {};
  }

  struct client_tuning_guard {
    bool saved = config::video.client_tuning;

    ~client_tuning_guard() {
      config::video.client_tuning = saved;
    }
  };
}  // namespace

TEST(TuningTests, GoalsFromTheLaunchRequest) {
  client_tuning_guard guard;
  config::video.client_tuning = true;
  EXPECT_EQ(config::tuning_from_view("performance"), tuning_e::performance);
  EXPECT_EQ(config::tuning_from_view("balanced"), tuning_e::balanced);
  EXPECT_EQ(config::tuning_from_view("quality"), tuning_e::quality);
  EXPECT_EQ(config::tuning_from_view(""), tuning_e::host);
  EXPECT_EQ(config::tuning_from_view("turbo"), tuning_e::host);
  EXPECT_EQ(config::to_string(tuning_e::quality), "quality");

  config::video.client_tuning = false;
  EXPECT_EQ(config::tuning_from_view("quality"), tuning_e::host);
}

TEST(TuningTests, BalancedKeepsTheHostsSettings) {
  for (auto encoder : {"amdvce", "quicksync", "software", "nvenc", "vaapi"}) {
    EXPECT_TRUE(config::tuned_options(encoder, 1, tuning_e::balanced).empty()) << encoder;
    EXPECT_TRUE(config::tuned_options(encoder, 1, tuning_e::host).empty()) << encoder;
  }
}

TEST(TuningTests, PerformanceAndQualityPresets) {
  // AMD: speed and quality presets, per codec.
  auto amd_fast = config::tuned_options("amdvce", 1, tuning_e::performance);
  auto amd_good = config::tuned_options("amdvce", 1, tuning_e::quality);
  ASSERT_FALSE(option(amd_fast, "quality").empty());
  ASSERT_FALSE(option(amd_good, "quality").empty());
  EXPECT_NE(option(amd_fast, "quality"), option(amd_good, "quality"));
  EXPECT_FALSE(option(config::tuned_options("amdvce", 2, tuning_e::quality), "quality").empty());

  // Quick Sync: 7 is veryfast, 3 slow.
  EXPECT_EQ(option(config::tuned_options("quicksync", 0, tuning_e::performance), "preset"), "7");
  EXPECT_EQ(option(config::tuned_options("quicksync", 0, tuning_e::quality), "preset"), "3");

  // Software: x264/x265 presets by name, SVT-AV1 by number.
  EXPECT_EQ(option(config::tuned_options("software", 0, tuning_e::performance), "preset"), "ultrafast");
  EXPECT_EQ(option(config::tuned_options("software", 1, tuning_e::quality), "preset"), "veryfast");
  EXPECT_EQ(option(config::tuned_options("software", 2, tuning_e::performance), "preset"), "12");
  EXPECT_EQ(option(config::tuned_options("software", 2, tuning_e::quality), "preset"), "10");

  // Encoders without presets are left alone.
  EXPECT_TRUE(config::tuned_options("videotoolbox", 1, tuning_e::quality).empty());
}

TEST(TuningTests, Nvenc) {
  nvenc::nvenc_config host;
  host.quality_preset = 3;
  host.two_pass = nvenc::nvenc_two_pass::full_resolution;

  auto fast = config::tuned_nvenc(host, tuning_e::performance);
  EXPECT_EQ(fast.quality_preset, 1);
  EXPECT_EQ(fast.two_pass, nvenc::nvenc_two_pass::disabled);

  auto good = config::tuned_nvenc(host, tuning_e::quality);
  EXPECT_EQ(good.quality_preset, 4);
  EXPECT_EQ(good.two_pass, nvenc::nvenc_two_pass::full_resolution);

  host.quality_preset = 6;
  host.two_pass = nvenc::nvenc_two_pass::disabled;
  good = config::tuned_nvenc(host, tuning_e::quality);
  EXPECT_EQ(good.quality_preset, 6) << "never lower than the host's";
  EXPECT_EQ(good.two_pass, nvenc::nvenc_two_pass::quarter_resolution);

  auto same = config::tuned_nvenc(host, tuning_e::balanced);
  EXPECT_EQ(same.quality_preset, 6);
  EXPECT_EQ(same.two_pass, nvenc::nvenc_two_pass::disabled);
}
