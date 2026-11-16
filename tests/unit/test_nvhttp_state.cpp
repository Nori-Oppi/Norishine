/**
 * @file tests/unit/test_nvhttp_state.cpp
 * @brief Test the state layout (src/state_store.*): migration from the single state file
 *        of Apollo and older versions, and that the host's unique ID is saved before any
 *        client pairs.
 */
#include "../tests_common.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>
#include <src/config.h>
#include <src/httpcommon.h>
#include <src/nvhttp.h>
#include <src/state_store.h>

namespace nvhttp {
  void load_state();
}  // namespace nvhttp

namespace {
  namespace fs = std::filesystem;
  using nlohmann::json;

  /**
   * @brief Points the state at a temporary folder for one test.
   */
  class HostStateFileTest: public testing::Test {
  protected:
    void SetUp() override {
      saved_state = config::nvhttp.file_state;
      saved_credentials = config::sunshine.credentials_file;
      saved_fresh_state = config::sunshine.flags[config::flag::FRESH_STATE];
      saved_unique_id = http::unique_id;
      saved_paths = state_store::paths();
      dir = fs::temp_directory_path() / ("norishine-state-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                                         ::testing::UnitTest::GetInstance()->current_test_info()->name());
      fs::remove_all(dir);
      fs::create_directories(dir);
      legacy = dir / "sunshine_state.json";
      config::nvhttp.file_state = legacy.string();
      config::sunshine.credentials_file = legacy.string();
      config::sunshine.flags[config::flag::FRESH_STATE] = false;
      http::unique_id.clear();
    }

    void TearDown() override {
      fs::remove_all(dir);
      config::nvhttp.file_state = saved_state;
      config::sunshine.credentials_file = saved_credentials;
      config::sunshine.flags[config::flag::FRESH_STATE] = saved_fresh_state;
      http::unique_id = saved_unique_id;
      state_store::set_paths(saved_paths);
    }

    static void write(const fs::path &file, const json &content) {
      std::ofstream(file) << content.dump();
    }

    static json read(const fs::path &file) {
      json content;
      std::ifstream(file) >> content;
      return content;
    }

    fs::path state(const char *name) const {
      return dir / "state" / name;
    }

    fs::path dir;
    fs::path legacy;
    std::string saved_state;
    std::string saved_credentials;
    std::string saved_unique_id;
    state_store::paths_t saved_paths;
    bool saved_fresh_state = false;
  };

  const json apollo_device {{"name", "Laptop"}, {"cert", "not a real certificate"}, {"uuid", "client-uuid"}, {"perm", 0x03000000}};
}  // namespace

TEST_F(HostStateFileTest, NewHostGetsAStableUniqueId) {
  state_store::init();
  nvhttp::load_state();
  const auto first_id = http::unique_id;
  ASSERT_FALSE(first_id.empty());
  EXPECT_EQ(read(state("host.json"))["uniqueid"], first_id);
  EXPECT_EQ(read(state("host.json"))["version"], state_store::current_version);
  EXPECT_FALSE(fs::exists(legacy));

  state_store::init();
  nvhttp::load_state();
  EXPECT_EQ(http::unique_id, first_id);
}

TEST_F(HostStateFileTest, ApolloStateFileIsSplitAndKept) {
  write(legacy, {
    {"username", "admin"},
    {"password", "0123abcd"},
    {"salt", "abc"},
    {"something_else", 42},
    {"root", {{"uniqueid", "11111111-2222-3333-4444-555555555555"}, {"named_devices", json::array({apollo_device})}}},
  });

  state_store::init();
  EXPECT_EQ(config::sunshine.credentials_file, state("credentials.json").string());
  nvhttp::load_state();

  EXPECT_EQ(http::unique_id, "11111111-2222-3333-4444-555555555555") << "clients must still recognise the host";
  auto pairing = read(state("pairing.json"));
  ASSERT_EQ(pairing["named_devices"].size(), 1u);
  EXPECT_EQ(pairing["named_devices"][0]["uuid"], "client-uuid");
  EXPECT_EQ(pairing["named_devices"][0]["perm"], 0x03000000);
  auto credentials = read(state("credentials.json"));
  EXPECT_EQ(credentials["username"], "admin");
  EXPECT_EQ(credentials["password"], "0123abcd");
  EXPECT_EQ(credentials["salt"], "abc");

  // The original stays as a backup, with everything in it.
  EXPECT_FALSE(fs::exists(legacy));
  auto backup = read(dir / "sunshine_state.json.v0.bak");
  EXPECT_EQ(backup["something_else"], 42);
  EXPECT_EQ(backup["root"]["uniqueid"], "11111111-2222-3333-4444-555555555555");

  // Running again changes nothing.
  auto result = state_store::migrate(state_store::paths());
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.from_version, state_store::current_version);
  EXPECT_TRUE(result.backup.empty());
}

TEST_F(HostStateFileTest, CredentialsOnlyFileGetsAStableUniqueIdAndKeepsCredentials) {
  write(legacy, {{"username", "admin"}, {"password", "x"}, {"salt", "abc"}});

  state_store::init();
  nvhttp::load_state();
  const auto first_id = http::unique_id;
  ASSERT_FALSE(first_id.empty());
  EXPECT_EQ(read(state("host.json"))["uniqueid"], first_id);
  EXPECT_EQ(read(state("credentials.json"))["username"], "admin");

  nvhttp::load_state();
  EXPECT_EQ(http::unique_id, first_id);
}

TEST_F(HostStateFileTest, PairedClientsWithoutUniqueIdAreKept) {
  write(legacy, {{"root", {{"named_devices", json::array({apollo_device})}}}});

  state_store::init();
  nvhttp::load_state();
  ASSERT_FALSE(http::unique_id.empty());
  EXPECT_EQ(read(state("host.json"))["uniqueid"], http::unique_id);
  auto pairing = read(state("pairing.json"));
  ASSERT_EQ(pairing["named_devices"].size(), 1u);
  EXPECT_EQ(pairing["named_devices"][0]["uuid"], "client-uuid");
  EXPECT_FALSE(fs::exists(state("credentials.json"))) << "no account to move";
}

TEST_F(HostStateFileTest, UnreadableStateIsNeverOverwritten) {
  std::ofstream(legacy) << "{ this is not json";

  state_store::init();
  EXPECT_FALSE(state_store::writable());
  EXPECT_EQ(config::sunshine.credentials_file, legacy.string()) << "the account is still looked for where it was";
  nvhttp::load_state();
  EXPECT_FALSE(http::unique_id.empty());
  const auto id = http::unique_id;
  nvhttp::load_state();
  EXPECT_EQ(http::unique_id, id) << "one ID for the whole run";

  EXPECT_TRUE(fs::exists(legacy));
  EXPECT_FALSE(fs::exists(state("host.json")));
  std::ifstream in(legacy);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "{ this is not json");
}

TEST_F(HostStateFileTest, SeparateCredentialsFileStaysWhereItIs) {
  const auto credentials = dir / "my_credentials.json";
  write(credentials, {{"username", "admin"}, {"password", "x"}, {"salt", "abc"}});
  write(legacy, {{"root", {{"uniqueid", "21111111-2222-3333-4444-555555555555"}}}});
  config::sunshine.credentials_file = credentials.string();

  state_store::init();
  EXPECT_EQ(config::sunshine.credentials_file, credentials.string());
  EXPECT_FALSE(fs::exists(state("credentials.json")));
  EXPECT_EQ(read(credentials)["username"], "admin");
  EXPECT_EQ(read(state("host.json"))["uniqueid"], "21111111-2222-3333-4444-555555555555");
}

TEST_F(HostStateFileTest, InterruptedMigrationIsCompleted) {
  // host.json is written last; if the old file is still there, only the rename is left.
  fs::create_directories(dir / "state");
  write(state("host.json"), {{"version", 1}, {"uniqueid", "31111111-2222-3333-4444-555555555555"}});
  write(legacy, {{"root", {{"uniqueid", "41111111-2222-3333-4444-555555555555"}}}});

  state_store::init();
  EXPECT_FALSE(fs::exists(legacy));
  EXPECT_TRUE(fs::exists(dir / "sunshine_state.json.v0.bak"));
  nvhttp::load_state();
  EXPECT_EQ(http::unique_id, "31111111-2222-3333-4444-555555555555");
}

TEST_F(HostStateFileTest, StateFromANewerVersionIsUsedAsItIs) {
  fs::create_directories(dir / "state");
  write(state("host.json"), {{"version", 99}, {"uniqueid", "51111111-2222-3333-4444-555555555555"}, {"new_thing", true}});

  state_store::init();
  EXPECT_TRUE(state_store::writable());
  nvhttp::load_state();
  EXPECT_EQ(http::unique_id, "51111111-2222-3333-4444-555555555555");
  EXPECT_TRUE(read(state("host.json"))["new_thing"]) << "unknown entries are kept";
}

TEST_F(HostStateFileTest, OtherStateFileNamesGetTheirOwnFolder) {
  auto p = state_store::paths_for(dir / "dev_state.json", dir / "dev_state.json");
  EXPECT_EQ(p.dir, dir / "dev_state_state");
  EXPECT_TRUE(p.credentials_in_legacy);
  EXPECT_EQ(p.credentials, dir / "dev_state_state" / "credentials.json");
}
