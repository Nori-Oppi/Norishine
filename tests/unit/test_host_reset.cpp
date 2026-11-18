/**
 * @file tests/unit/test_host_reset.cpp
 * @brief Test src/host_reset.*: what a reset moves, the backup it makes, and undoing it by
 *        moving the backup folder back.
 */
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

// local includes
#include <src/host_reset.h>

namespace fs = std::filesystem;

namespace {
  class HostResetTest: public ::testing::Test {
  protected:
    fs::path root;
    fs::path config;  ///< Stands for the install's config folder.

    void SetUp() override {
      std::random_device rd;
      root = fs::temp_directory_path() / ("norishine-reset-test-" + std::to_string(rd()));
      config = root / "config";
      fs::create_directories(config / "state");
      fs::create_directories(config / "credentials");
      fs::create_directories(config / "covers");
      write(config / "sunshine.conf", "sunshine_name = Test\n");
      write(config / "apps.json", R"({"apps": []})");
      write(config / "state" / "host.json", R"({"version": 1, "uniqueid": "abc"})");
      write(config / "state" / "pairing.json", R"({"version": 1, "named_devices": []})");
      write(config / "state" / "credentials.json", R"({"version": 1, "username": "me"})");
      write(config / "credentials" / "cakey.pem", "key");
      write(config / "credentials" / "cacert.pem", "cert");
      write(config / "covers" / "game.png", "png");
      write(config / "sunshine.conf.bak", "old");
      write(config / "sunshine.log", "log");  // not part of a reset
    }

    void TearDown() override {
      std::error_code ec;
      fs::remove_all(root, ec);
    }

    static void write(const fs::path &file, const std::string &text) {
      std::ofstream out {file, std::ios::binary};
      out << text;
    }

    static std::string read(const fs::path &file) {
      std::ifstream in {file, std::ios::binary};
      return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    std::vector<fs::path> targets() const {
      return host_reset::targets_for(
        config / "sunshine.conf",
        config / "apps.json",
        config / "sunshine_state.json",
        config / "state",
        config / "state" / "credentials.json",
        config / "credentials" / "cakey.pem",
        config / "credentials" / "cacert.pem",
        config / "covers"
      );
    }
  };
}  // namespace

TEST_F(HostResetTest, ListsEachThingOnce) {
  auto list = targets();
  // credentials.json is inside the state folder, so it moves with it; the copies next to
  // the settings, apps and old state file (.bak, .before-import, .v0.bak) are listed too.
  EXPECT_EQ(list.size(), 16u);
  EXPECT_EQ(std::count(list.begin(), list.end(), config / "state" / "credentials.json"), 0);

  auto twice = host_reset::targets_for(config / "a.conf", config / "a.conf", {}, {}, {}, {}, {}, {});
  EXPECT_EQ(twice.size(), 4u);  // a.conf and its three copies, once each
}

TEST_F(HostResetTest, StampIsDateAndTime) {
  auto s = host_reset::stamp(std::chrono::system_clock::now());
  ASSERT_EQ(s.size(), 15u);
  EXPECT_EQ(s[8], '-');
}

TEST_F(HostResetTest, MovesEverythingToADatedBackup) {
  auto result = host_reset::move_to_backup(targets(), config, "20261118-174200");
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.backup, config / "Norishine-backup-20261118-174200");

  // Gone from where the host looks, so it starts as on first run...
  for (auto p : {"sunshine.conf", "apps.json", "state", "covers", "credentials/cakey.pem", "credentials/cacert.pem"}) {
    EXPECT_FALSE(fs::exists(config / p)) << p;
  }
  // ...kept in the backup, in the same places...
  EXPECT_EQ(read(result.backup / "sunshine.conf"), "sunshine_name = Test\n");
  EXPECT_EQ(read(result.backup / "state" / "credentials.json"), R"({"version": 1, "username": "me"})");
  EXPECT_EQ(read(result.backup / "credentials" / "cacert.pem"), "cert");
  EXPECT_EQ(read(result.backup / "covers" / "game.png"), "png");
  EXPECT_EQ(read(result.backup / "sunshine.conf.bak"), "old");
  // ...and nothing else touched.
  EXPECT_EQ(read(config / "sunshine.log"), "log");
}

TEST_F(HostResetTest, SecondResetGetsItsOwnFolder) {
  auto first = host_reset::move_to_backup(targets(), config, "20261118-174200");
  write(config / "sunshine.conf", "new\n");
  auto second = host_reset::move_to_backup(targets(), config, "20261118-174200");
  ASSERT_TRUE(second.ok);
  EXPECT_NE(first.backup, second.backup);
  EXPECT_EQ(read(second.backup / "sunshine.conf"), "new\n");
  EXPECT_EQ(read(first.backup / "sunshine.conf"), "sunshine_name = Test\n");
}

TEST_F(HostResetTest, FilesElsewhereAreRenamedInPlace) {
  auto elsewhere = root / "custom";
  fs::create_directories(elsewhere);
  write(elsewhere / "apps.json", "apps");
  auto list = host_reset::targets_for(config / "sunshine.conf", elsewhere / "apps.json", {}, {}, {}, {}, {}, {});
  auto result = host_reset::move_to_backup(list, config, "20261118-174200");
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_FALSE(fs::exists(elsewhere / "apps.json"));
  EXPECT_EQ(read(elsewhere / "apps.json.backup-20261118-174200"), "apps");
}

TEST_F(HostResetTest, MovingTheBackupBackUndoesIt) {
  auto result = host_reset::move_to_backup(targets(), config, "20261118-174200");
  ASSERT_TRUE(result.ok);

  // What the guide says: move the backup folder's contents back into the config folder.
  for (const auto &entry : fs::directory_iterator {result.backup}) {
    auto target = config / entry.path().filename();
    if (fs::is_directory(entry.path()) && fs::exists(target)) {
      for (const auto &inner : fs::directory_iterator {entry.path()}) {
        fs::rename(inner.path(), target / inner.path().filename());
      }
    } else {
      fs::rename(entry.path(), target);
    }
  }

  EXPECT_EQ(read(config / "sunshine.conf"), "sunshine_name = Test\n");
  EXPECT_EQ(read(config / "apps.json"), R"({"apps": []})");
  EXPECT_EQ(read(config / "state" / "host.json"), R"({"version": 1, "uniqueid": "abc"})");
  EXPECT_EQ(read(config / "state" / "credentials.json"), R"({"version": 1, "username": "me"})");
  EXPECT_EQ(read(config / "credentials" / "cakey.pem"), "key");
  EXPECT_EQ(read(config / "covers" / "game.png"), "png");
}

TEST_F(HostResetTest, NothingToMoveIsFine) {
  auto result = host_reset::move_to_backup({config / "missing.conf"}, config, "20261118-174200");
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.moved.empty());
}
