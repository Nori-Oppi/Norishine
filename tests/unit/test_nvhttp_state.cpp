/**
 * @file tests/unit/test_nvhttp_state.cpp
 * @brief Test that the host's unique ID is saved before any client pairs.
 */
#include "../tests_common.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>
#include <src/config.h>
#include <src/httpcommon.h>
#include <src/nvhttp.h>

namespace nvhttp {
  void load_state();
}  // namespace nvhttp

namespace {
  namespace fs = std::filesystem;

  /**
   * @brief Points the state file at a temporary path for one test.
   */
  class HostStateFileTest: public testing::Test {
  protected:
    void SetUp() override {
      saved_path = config::nvhttp.file_state;
      saved_fresh_state = config::sunshine.flags[config::flag::FRESH_STATE];
      saved_unique_id = http::unique_id;
      path = fs::temp_directory_path() / ("norishine-state-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".json");
      fs::remove(path);
      config::nvhttp.file_state = path.string();
      config::sunshine.flags[config::flag::FRESH_STATE] = false;
      http::unique_id.clear();
    }

    void TearDown() override {
      fs::remove(path);
      config::nvhttp.file_state = saved_path;
      config::sunshine.flags[config::flag::FRESH_STATE] = saved_fresh_state;
      http::unique_id = saved_unique_id;
    }

    void write(const nlohmann::json &json) const {
      std::ofstream(path) << json.dump();
    }

    nlohmann::json read() const {
      nlohmann::json json;
      std::ifstream(path) >> json;
      return json;
    }

    fs::path path;
    std::string saved_path;
    std::string saved_unique_id;
    bool saved_fresh_state = false;
  };
}  // namespace

TEST_F(HostStateFileTest, MissingFileGetsAStableUniqueId) {
  nvhttp::load_state();
  const auto first_id = http::unique_id;
  ASSERT_FALSE(first_id.empty());
  EXPECT_EQ(read()["root"]["uniqueid"], first_id);

  nvhttp::load_state();
  EXPECT_EQ(http::unique_id, first_id);
}

TEST_F(HostStateFileTest, CredentialsOnlyFileGetsAStableUniqueIdAndKeepsCredentials) {
  write({{"username", "admin"}, {"salt", "abc"}});

  nvhttp::load_state();
  const auto first_id = http::unique_id;
  ASSERT_FALSE(first_id.empty());

  const auto saved = read();
  EXPECT_EQ(saved["root"]["uniqueid"], first_id);
  EXPECT_EQ(saved["username"], "admin");
  EXPECT_EQ(saved["salt"], "abc");

  nvhttp::load_state();
  EXPECT_EQ(http::unique_id, first_id);
}

TEST_F(HostStateFileTest, PairedClientsWithoutUniqueIdAreKept) {
  write({{"root", {{"named_devices", nlohmann::json::array({{{"name", "Laptop"}, {"cert", "not a real certificate"}, {"uuid", "client-uuid"}, {"perm", 0x03000000}}})}}}});

  nvhttp::load_state();
  ASSERT_FALSE(http::unique_id.empty());

  const auto saved = read();
  EXPECT_EQ(saved["root"]["uniqueid"], http::unique_id);
  ASSERT_EQ(saved["root"]["named_devices"].size(), 1);
  EXPECT_EQ(saved["root"]["named_devices"][0]["uuid"], "client-uuid");
}
