/**
 * @file tests/unit/test_manage_api.cpp
 * @brief Test the checks in src/manage_api.* (the calls themselves: scripts/e2e/manage_check.py).
 */
#include "../tests_common.h"

// local includes
#include <src/crypto.h>
#include <src/manage_api.h>

using manage_api::validate_app;
using manage_api::validate_device_change;
using nlohmann::json;

TEST(ManageApiTests, AppAsTheWebUiSavesIt) {
  auto app = json::parse(R"({
    "name": "Steam Big Picture",
    "uuid": "6e4b7f3c-3a1f-4c8e-9d7a-2b1c0d9e8f7a",
    "cmd": "",
    "detached": ["steam://open/bigpicture"],
    "prep-cmd": [{"do": "a", "undo": "b", "elevated": false}],
    "state-cmd": [],
    "image-path": "steam.png",
    "exit-timeout": 5,
    "scale-factor": 100,
    "auto-detach": true,
    "wait-all": true,
    "elevated": false,
    "virtual-display": false,
    "gamepad": "",
    "something-newer": {"kept": true}
  })");
  EXPECT_EQ(validate_app(app), json::object());
}

TEST(ManageApiTests, AppProblemsArePerField) {
  auto errors = validate_app(json::parse(R"({
    "name": "  ",
    "cmd": 5,
    "elevated": "yes",
    "exit-timeout": -1,
    "scale-factor": 1000,
    "prep-cmd": [{"do": ["x"]}],
    "detached": "one command"
  })"));
  for (auto key : {"name", "cmd", "elevated", "exit-timeout", "scale-factor", "prep-cmd", "detached"}) {
    EXPECT_TRUE(errors.contains(key)) << key;
  }
  EXPECT_EQ(errors.size(), 7u);

  EXPECT_TRUE(validate_app(json::object()).contains("name"));
  EXPECT_TRUE(validate_app(json::array()).contains("app"));
  EXPECT_TRUE(validate_app({{"name", std::string(5000, 'a')}}).contains("name"));
  EXPECT_TRUE(validate_app({{"name", "a"}, {"prep-cmd", json::array({1})}}).contains("prep-cmd"));
  EXPECT_TRUE(validate_app({{"name", "a"}, {"exit-timeout", 1.5}}).contains("exit-timeout"));
}

TEST(ManageApiTests, DeviceChangeAcceptsWhatTheListShows) {
  auto change = json::parse(R"({
    "name": "Living room",
    "display_mode": "1920x1080x60",
    "enable_legacy_ordering": true,
    "allow_client_commands": false,
    "always_use_virtual_display": false,
    "do": [{"cmd": "echo hi", "elevated": false}],
    "undo": []
  })");
  change["perm"] = (std::uint32_t) crypto::PERM::_all;
  EXPECT_EQ(validate_device_change(change), json::object());
  EXPECT_EQ(validate_device_change(json::object()), json::object()) << "nothing to change is fine";
}

TEST(ManageApiTests, DeviceChangeProblemsArePerField) {
  auto errors = validate_device_change(json::parse(R"({
    "name": "",
    "display_mode": 60,
    "perm": -1,
    "allow_client_commands": 1,
    "do": [{"elevated": true}],
    "undo": [{"cmd": 3}],
    "uuid": "someone-else",
    "connected": true
  })"));
  for (auto key : {"name", "display_mode", "perm", "allow_client_commands", "do", "undo", "uuid", "connected"}) {
    EXPECT_TRUE(errors.contains(key)) << key;
  }

  // Bits outside the known permissions.
  EXPECT_TRUE(validate_device_change({{"perm", 0x80000000u}}).contains("perm"));
  EXPECT_TRUE(validate_device_change({{"perm", 1.0}}).contains("perm"));
  EXPECT_TRUE(validate_device_change(json::array()).contains("device"));
}

TEST(ManageApiTests, BadBodies) {
  json out;
  manage_api::reply_t error;
  EXPECT_FALSE(manage_api::parse_body("{nope", out, error));
  EXPECT_EQ(error.code, SimpleWeb::StatusCode::client_error_bad_request);
  EXPECT_EQ(error.body["status"], false);
  EXPECT_TRUE(manage_api::parse_body(R"({"name": "x"})", out, error));
  EXPECT_EQ(out["name"], "x");
}
