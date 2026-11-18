/**
 * @file tests/unit/test_updater.cpp
 * @brief Test src/updater.*: versions, picking the release to install, and update results.
 */
#include "../tests_common.h"

// local includes
#include <src/updater.h>

using namespace updater;

namespace {
  const std::string sha_a(64, 'a');

  nlohmann::json release(const std::string &version, const std::string &published, bool pre, const std::string &digest = "sha256:" + sha_a) {
    auto name = "Norishine-Windows-x64-" + version + "-Setup.exe";
    return {
      {"name", pre ? "Nightly (" + version + ")" : "Norishine " + version},
      {"tag_name", pre ? "nightly" : "v" + version},
      {"html_url", "https://github.com/Nori-Oppi/Norishine/releases/tag/x"},
      {"published_at", published},
      {"prerelease", pre},
      {"draft", false},
      {"assets", nlohmann::json::array({
                   {{"name", "notes.txt"}, {"size", 10}, {"browser_download_url", "https://github.com/Nori-Oppi/Norishine/releases/download/x/notes.txt"}, {"digest", "sha256:" + sha_a}},
                   {{"name", name}, {"size", 25000000}, {"browser_download_url", "https://github.com/Nori-Oppi/Norishine/releases/download/x/" + name}, {"digest", digest}},
                 })},
    };
  }
}  // namespace

TEST(UpdaterTests, ParsesVersions) {
  auto n = parse_version("nightly-20261118-8c6c99d");
  EXPECT_EQ(n.kind, version_t::kind_e::nightly);
  EXPECT_EQ(n.date, "20261118");
  EXPECT_EQ(n.commit, "8c6c99d");

  auto r = parse_version("v1.12.3");
  EXPECT_EQ(r.kind, version_t::kind_e::release);
  EXPECT_EQ(r.major, 1);
  EXPECT_EQ(r.minor, 12);
  EXPECT_EQ(r.patch, 3);
  EXPECT_EQ(parse_version("1.0.0").kind, version_t::kind_e::release);

  for (auto dev : {"0.0.0.dirty", "0.0.0.abc1234", "", "nightly-2026111-8c6c99d", "1.2", "nightly-20261118-XYZ1234"}) {
    EXPECT_EQ(parse_version(dev).kind, version_t::kind_e::unknown) << dev;
  }
}

TEST(UpdaterTests, ReadsReleaseEntries) {
  auto info = release_from_json(release("nightly-20261118-8c6c99d", "2026-11-18T01:42:35Z", true));
  ASSERT_TRUE(info);
  EXPECT_EQ(info->version, "nightly-20261118-8c6c99d");
  EXPECT_EQ(info->asset_name, "Norishine-Windows-x64-nightly-20261118-8c6c99d-Setup.exe");
  EXPECT_EQ(info->sha256, sha_a);
  EXPECT_EQ(info->size, 25000000u);
  EXPECT_TRUE(info->prerelease);
}

TEST(UpdaterTests, RefusesReleasesItCannotVerify) {
  EXPECT_FALSE(release_from_json(release("1.0.0", "2026-12-01T00:00:00Z", false, "")));
  EXPECT_FALSE(release_from_json(release("1.0.0", "2026-12-01T00:00:00Z", false, "sha1:abc")));
  EXPECT_FALSE(release_from_json(release("1.0.0", "2026-12-01T00:00:00Z", false, "sha256:" + std::string(64, 'G'))));

  auto elsewhere = release("1.0.0", "2026-12-01T00:00:00Z", false);
  elsewhere["assets"][1]["browser_download_url"] = "https://example.com/Norishine-Windows-x64-1.0.0-Setup.exe";
  EXPECT_FALSE(release_from_json(elsewhere));

  auto huge = release("1.0.0", "2026-12-01T00:00:00Z", false);
  huge["assets"][1]["size"] = max_installer_size + 1;
  EXPECT_FALSE(release_from_json(huge));

  auto draft = release("1.0.0", "2026-12-01T00:00:00Z", false);
  draft["draft"] = true;
  EXPECT_FALSE(release_from_json(draft));

  EXPECT_FALSE(release_from_json(nlohmann::json::array()));
  EXPECT_FALSE(release_from_json({{"assets", "nope"}}));
}

TEST(UpdaterTests, NewerNightly) {
  auto list = nlohmann::json::array({release("nightly-20261120-aaaaaaa", "2026-11-20T01:00:00Z", true)});
  auto pick = choose("nightly-20261118-8c6c99d", list, true);
  ASSERT_TRUE(pick);
  EXPECT_EQ(pick->version, "nightly-20261120-aaaaaaa");

  // Pre-releases off: nightlies aren't offered.
  EXPECT_FALSE(choose("nightly-20261118-8c6c99d", list, false));
  // Already on it, or on a newer one.
  EXPECT_FALSE(choose("nightly-20261120-aaaaaaa", list, true));
  EXPECT_FALSE(choose("nightly-20261121-bbbbbbb", list, true));
  // Same day, another build: offered (the nightly release is replaced, not added).
  EXPECT_TRUE(choose("nightly-20261120-ccccccc", list, true));
}

TEST(UpdaterTests, ReleasesAndNightlies) {
  auto list = nlohmann::json::array({
    release("nightly-20261205-aaaaaaa", "2026-12-05T01:00:00Z", true),
    release("1.1.0", "2026-12-01T12:00:00Z", false),
    release("1.0.0", "2026-11-25T12:00:00Z", false),
  });

  // On a release: the newer release, or the newer nightly with pre-releases on.
  EXPECT_EQ(choose("1.0.0", list, false)->version, "1.1.0");
  EXPECT_EQ(choose("1.0.0", list, true)->version, "nightly-20261205-aaaaaaa");
  EXPECT_FALSE(choose("1.1.0", list, false));
  EXPECT_EQ(choose("1.1.0", list, true)->version, "nightly-20261205-aaaaaaa");

  // On a nightly from before 1.1.0: 1.1.0 is newer; with pre-releases the newer nightly wins.
  EXPECT_EQ(choose("nightly-20261130-bbbbbbb", list, false)->version, "1.1.0");
  EXPECT_EQ(choose("nightly-20261130-bbbbbbb", list, true)->version, "nightly-20261205-aaaaaaa");
  // On a nightly after 1.1.0 with pre-releases off: nothing older is offered.
  EXPECT_FALSE(choose("nightly-20261203-bbbbbbb", list, false));

  // Development builds never get an update offered.
  EXPECT_FALSE(choose("0.0.0.dirty", list, true));
  EXPECT_FALSE(choose("1.0.0", nlohmann::json::object(), true));
}

TEST(UpdaterTests, ReportsTheLastUpdate) {
  EXPECT_TRUE(last_update(nullptr, "1.0.0").is_null());
  EXPECT_TRUE(last_update(nlohmann::json::object(), "1.0.0").is_null());

  auto ok = last_update({{"state", "installed"}, {"from", "1.0.0"}, {"to", "1.1.0"}, {"finished", "2026-12-02T10:00:00Z"}}, "1.1.0");
  EXPECT_EQ(ok["result"], "updated");
  EXPECT_EQ(ok["from"], "1.0.0");
  EXPECT_TRUE(ok["error"].is_null());

  // The new host starts before the script writes "installed": still a success.
  EXPECT_EQ(last_update({{"state", "installing"}, {"from", "1.0.0"}, {"to", "1.1.0"}}, "1.1.0")["result"], "updated");

  auto stuck = last_update({{"state", "installing"}, {"from", "1.0.0"}, {"to", "1.1.0"}}, "1.0.0");
  EXPECT_EQ(stuck["result"], "failed");
  EXPECT_TRUE(stuck["error"].is_string());

  auto failed = last_update({{"state", "failed"}, {"from", "1.0.0"}, {"to", "1.1.0"}, {"error", "The installer stopped with code 2."}}, "1.0.0");
  EXPECT_EQ(failed["result"], "failed");
  EXPECT_EQ(failed["error"], "The installer stopped with code 2.");

  EXPECT_EQ(last_update({{"state", "installed"}, {"from", "1.0.0"}, {"to", "1.1.0"}}, "1.0.0")["result"], "failed");
}

TEST(UpdaterTests, StartRefusesBadRequests) {
  // Test builds are development builds: they can't update themselves.
  ASSERT_TRUE(unsupported_reason());
  auto reply = start({{"version", "1.1.0"}});
  EXPECT_EQ(reply.code, SimpleWeb::StatusCode::client_error_conflict);
  EXPECT_FALSE(reply.body["status"]);
  EXPECT_TRUE(reply.body["error"].is_string());
}
