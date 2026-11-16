/**
 * @file tests/unit/test_config_schema.cpp
 * @brief Test src/config_schema.* and config::update_config_text().
 */
#include "../tests_common.h"

// standard includes
#include <set>
#include <string>

// local includes
#include <src/config.h>
#include <src/config_schema.h>

using namespace config::schema;
using nlohmann::json;

namespace {
  const setting_t &setting(std::string_view key) {
    const auto *s = find(key);
    EXPECT_NE(s, nullptr) << key;
    return *s;
  }

  std::string text_ok(std::string_view key, const json &value) {
    auto result = to_config_text(setting(key), value);
    EXPECT_TRUE(result.error.empty()) << key << ": " << result.error;
    return result.text;
  }

  std::string text_error(std::string_view key, const json &value) {
    auto result = to_config_text(setting(key), value);
    EXPECT_FALSE(result.error.empty()) << key << " accepted " << value.dump();
    return result.error;
  }
}  // namespace

TEST(ConfigSchemaTests, KeysAreUniqueAndComplete) {
  std::set<std::string_view> keys;
  for (const auto &s : settings()) {
    EXPECT_TRUE(keys.insert(s.key).second) << "duplicate " << s.key;
    EXPECT_FALSE(s.category.empty()) << s.key;
    EXPECT_NE(s.platforms, 0u) << s.key;
  }
  EXPECT_EQ(keys.size(), 103u);
  EXPECT_EQ(find("dd_wa_hdr_toggle"), nullptr) << "dead setting: config.cpp never reads it";
}

TEST(ConfigSchemaTests, DependenciesPointToEarlierSettings) {
  std::set<std::string_view> seen;
  for (const auto &s : settings()) {
    if (!s.depends_on.empty()) {
      const auto *other = find(s.depends_on);
      ASSERT_NE(other, nullptr) << s.key;
      EXPECT_TRUE(seen.contains(s.depends_on)) << s.key << " should come after " << s.depends_on;
      EXPECT_EQ(other->category, s.category) << s.key;
      EXPECT_FALSE(s.depends_on_values.empty()) << s.key;
      for (const auto &value : s.depends_on_values) {
        EXPECT_TRUE(to_config_text(*other, value).error.empty()) << s.key << ": " << value.dump();
      }
    }
    seen.insert(s.key);
  }
  auto schema = describe();
  for (const auto &s : schema["settings"]) {
    if (s["key"] == "dd_manual_resolution") {
      EXPECT_EQ(s["depends_on"], json({{"key", "dd_resolution_option"}, {"values", {"manual"}}}));
    }
  }
}

TEST(ConfigSchemaTests, EveryDefaultPassesValidation) {
  for (const auto &s : settings()) {
    if (!available(s)) {
      continue;
    }
    auto result = to_config_text(s, s.default_value);
    EXPECT_TRUE(result.error.empty()) << s.key << ": " << result.error;
  }
}

TEST(ConfigSchemaTests, Booleans) {
  EXPECT_EQ(text_ok("headless_mode", true), "enabled");
  EXPECT_EQ(text_ok("headless_mode", "Enabled"), "enabled") << "the parser ignores case";
  EXPECT_EQ(text_ok("headless_mode", "FALSE"), "disabled");
  EXPECT_EQ(text_ok("headless_mode", false), "disabled");
  EXPECT_EQ(text_ok("headless_mode", "on"), "enabled");
  EXPECT_EQ(text_ok("headless_mode", "disabled"), "disabled");
  text_error("headless_mode", "maybe");
  text_error("headless_mode", 1);
  EXPECT_EQ(from_config_text(setting("headless_mode"), "enabled"), json(true));
  EXPECT_EQ(from_config_text(setting("headless_mode"), "no"), json(false));
  EXPECT_FALSE(from_config_text(setting("headless_mode"), "sometimes"));
}

TEST(ConfigSchemaTests, IntegersAndRanges) {
  EXPECT_EQ(text_ok("max_bitrate", 50000), "50000");
  EXPECT_EQ(text_ok("max_bitrate", "50000"), "50000");
  EXPECT_EQ(text_ok("max_bitrate", 50000.0), "50000");
  EXPECT_NE(text_error("max_bitrate", -1).find("at least 0"), std::string::npos);
  text_error("max_bitrate", 1.5);
  text_error("max_bitrate", "12abc");
  EXPECT_EQ(text_ok("port", 49989), "49989");
  EXPECT_NE(text_error("port", 80).find("between"), std::string::npos);
  text_error("port", 65535);
  EXPECT_EQ(text_ok("back_button_timeout", -1), "-1");
  EXPECT_EQ(text_ok("min_log_level", "debug"), "1");
  EXPECT_EQ(from_config_text(setting("min_log_level"), "warning"), json(3));
  EXPECT_EQ(from_config_text(setting("fec_percentage"), " 20 "), json(20));
}

TEST(ConfigSchemaTests, Numbers) {
  EXPECT_EQ(text_ok("key_repeat_frequency", 24.9), "24.9");
  EXPECT_EQ(text_ok("minimum_fps_target", "30"), "30.0");
  text_error("minimum_fps_target", 5000);
  text_error("minimum_fps_target", "fast");
}

TEST(ConfigSchemaTests, Choices) {
  EXPECT_EQ(text_ok("origin_web_ui_allowed", "wan"), "wan");
  auto error = text_error("origin_web_ui_allowed", "internet");
  EXPECT_NE(error.find("pc, lan, wan"), std::string::npos) << error;
  EXPECT_EQ(text_ok("qsv_coder", "vlc"), "cavlc") << "alias the host also accepts";
  EXPECT_EQ(text_ok("encoder", ""), "") << "empty means automatic";
  text_error("encoder", "magic");
#ifdef _WIN32
  EXPECT_EQ(text_ok("capture", "ddx"), "ddx");
  text_error("capture", "wgcc");
  text_error("capture", "kms");  // Linux only
#endif
  auto gamepads = choices(setting("gamepad"));
  ASSERT_FALSE(gamepads.empty());
  EXPECT_EQ(gamepads.front(), "auto");
  EXPECT_EQ(std::set<std::string>(gamepads.begin(), gamepads.end()).size(), gamepads.size()) << "no duplicates";
}

TEST(ConfigSchemaTests, StringsStayOneLine) {
  EXPECT_EQ(text_ok("sunshine_name", "Living room PC #1"), "Living room PC #1");
  EXPECT_EQ(text_ok("sunshine_name", "  padded  "), "padded");
  text_error("sunshine_name", "name\nport = 1");
  text_error("sunshine_name", "name\r");
  text_error("sunshine_name", "[unbalanced");
  text_error("file_apps", "C:/apps.json\nother");
  text_error("sunshine_name", 5);
}

TEST(ConfigSchemaTests, Commands) {
  json prep = json::array({{{"do", "cmd /c echo hi"}, {"undo", ""}, {"elevated", false}}});
  EXPECT_EQ(text_ok("global_prep_cmd", prep), prep.dump());
  EXPECT_EQ(text_ok("global_prep_cmd", json::array()), "") << "empty list removes the setting";
  text_error("global_prep_cmd", json::array({{{"do", 5}}}));
  text_error("global_prep_cmd", json::array({{{"run", "x"}}}));
  json server = json::array({{{"name", "Bubbles"}, {"cmd", "bubbles.scr"}, {"elevated", false}}});
  EXPECT_EQ(text_ok("server_cmd", server.dump()), server.dump()) << "JSON text is accepted too";
  text_error("server_cmd", json::array({{{"name", "x"}}}));
  EXPECT_EQ(from_config_text(setting("server_cmd"), server.dump()), server);
}

TEST(ConfigSchemaTests, KeybindingsAndRemapping) {
  EXPECT_EQ(text_ok("keybindings", "[0x10,0xA0]"), "[0x10,0xA0]");
  EXPECT_EQ(text_ok("keybindings", json::array({16, 160})), "[16,160]");
  text_error("keybindings", "[0x10]");
  text_error("keybindings", "0x10,0xA0");
  json remap {{"mixed", json::array({{{"requested_resolution", "1920x1080"}, {"final_resolution", "2560x1440"}}})}};
  EXPECT_EQ(text_ok("dd_mode_remapping", remap), remap.dump());
  text_error("dd_mode_remapping", json {{"other", json::array()}});
}

TEST(ConfigSchemaTests, ValidateChange) {
  auto change = validate_change(json {
    {"set", {{"headless_mode", true}, {"max_bitrate", "50000"}, {"sunshine_name", ""}, {"encoder", nullptr}}},
    {"reset", {"port"}},
  });
  EXPECT_TRUE(change.errors.empty()) << change.errors.dump();
  EXPECT_EQ(change.set.at("headless_mode"), "enabled");
  EXPECT_EQ(change.set.at("max_bitrate"), "50000");
  EXPECT_TRUE(change.reset.contains("sunshine_name")) << "empty string resets";
  EXPECT_TRUE(change.reset.contains("encoder")) << "null resets";
  EXPECT_TRUE(change.reset.contains("port"));

  auto bad = validate_change(json {
    {"set", {{"no_such_setting", 1}, {"port", 1}, {"headless_mode", true}}},
    {"reset", {"headless_mode"}},
  });
  EXPECT_TRUE(bad.errors.contains("no_such_setting"));
  EXPECT_TRUE(bad.errors.contains("port"));
  EXPECT_TRUE(bad.errors.contains("headless_mode")) << "set and reset at once";

  EXPECT_TRUE(validate_change(json::array()).errors.contains(""));
  EXPECT_TRUE(validate_change(json {{"set", {}}, {"extra", 1}}).errors.contains(""));
  EXPECT_TRUE(validate_change(json {{"reset", "port"}}).errors.contains(""));
#ifdef _WIN32
  EXPECT_EQ(validate_change(json {{"set", {{"vt_coder", "auto"}}}}).errors.value("vt_coder", ""), "not available on this platform");
#endif
}

TEST(ConfigSchemaTests, ReadValues) {
  auto result = read_values("headless_mode = enabled\nmax_bitrate = 5000\nport = 80\nmystery = 1\n# comment = x\n");
  EXPECT_EQ(result["values"]["headless_mode"], json(true));
  EXPECT_EQ(result["values"]["max_bitrate"], json(5000));
  EXPECT_TRUE(result["invalid"].contains("port"));
  EXPECT_EQ(result["unknown"]["mystery"], json("1"));
  EXPECT_FALSE(result["values"].contains("comment"));
}

TEST(ConfigSchemaTests, Describe) {
  auto schema = describe();
  EXPECT_EQ(schema["version"], 1);
  ASSERT_EQ(schema["settings"].size(), settings().size());
  const auto &first = schema["settings"][0];
  for (auto field : {"key", "category", "type", "tier", "platforms", "available", "default", "restart_required", "label", "description"}) {
    EXPECT_TRUE(first.contains(field)) << field;
  }
  for (const auto &s : schema["settings"]) {
    if (s["type"] == "choice") {
      EXPECT_TRUE(s.contains("choices")) << s["key"];
    }
    if (s["key"] == "port") {
      EXPECT_EQ(s["min"], 1019);
      EXPECT_EQ(s["max"], 65514);
      EXPECT_EQ(s["tier"], "dangerous");
    }
  }
}

TEST(ConfigSchemaTests, DescribeWithReadableStrings) {
  // Locale files are copied next to the tests (tests/CMakeLists.txt).
  const std::string dir = "src_assets/common/assets/web/public/assets/locale/";
  auto find_setting = [](const json &schema, std::string_view key) {
    for (const auto &s : schema["settings"]) {
      if (s["key"] == key) {
        return s;
      }
    }
    return json();
  };

  auto english = describe("en", dir);
  EXPECT_EQ(english["locale"], "en");
  EXPECT_EQ(find_setting(english, "max_bitrate")["title"], "Bitrate limit");
  EXPECT_FALSE(find_setting(english, "headless_mode")["help"].get<std::string>().empty());
  ASSERT_EQ(english["categories"].size(), 15u);
  EXPECT_EQ(english["categories"][0]["title"], "General");
  EXPECT_FALSE(english["categories"][0].contains("expert"));
  EXPECT_FALSE(english["categories"][0]["description"].get<std::string>().empty());

  // Optional fields added later: units, names for special values, expert categories.
  auto bitrate = find_setting(english, "max_bitrate");
  EXPECT_EQ(bitrate["unit"], "kbps");
  EXPECT_EQ(bitrate["value_titles"]["0"], "No limit");
  EXPECT_EQ(bitrate["value_titles"].size(), 1u) << "only the special value is named";
  EXPECT_EQ(find_setting(english, "back_button_timeout")["value_titles"]["-1"], "Off");
  EXPECT_EQ(find_setting(english, "minimum_fps_target")["value_titles"]["0"], "Automatic");
  EXPECT_FALSE(find_setting(english, "port").contains("value_titles"));
  bool fixes_expert = false;
  for (const auto &category : english["categories"]) {
    if (category["id"] == "fixes") {
      fixes_expert = category.value("expert", false);
    }
  }
  EXPECT_TRUE(fixes_expert);
  // HDR is only changed when the host prepares the display.
  EXPECT_EQ(find_setting(english, "dd_hdr_option")["depends_on"]["key"], "dd_configuration_option");

  // Every setting on this platform has an explanation and a known category, and every
  // choice has a readable name.
  std::set<std::string> category_ids;
  for (const auto &category : english["categories"]) {
    category_ids.insert(category["id"].get<std::string>());
  }
  for (const auto &s : english["settings"]) {
    EXPECT_TRUE(category_ids.contains(s["category"].get<std::string>())) << s["key"];
    if (!s["available"].get<bool>()) {
      continue;
    }
    EXPECT_FALSE(s["help"].get<std::string>().empty()) << s["key"] << " has no help text";
    EXPECT_NE(s["title"], s["key"]) << s["key"] << " has no title";
    if (s["type"] == "choice" && s["key"] != "gamepad") {
      for (const auto &choice : s["choices"]) {
        EXPECT_TRUE(s.contains("value_titles") && s["value_titles"].contains(choice.get<std::string>())) << s["key"] << " = " << choice;
      }
    }
  }
  EXPECT_EQ(find_setting(english, "hevc_mode")["value_titles"].size(), 4u);
  EXPECT_EQ(find_setting(english, "locale")["value_titles"]["de"], "Deutsch (German)");

  // German has no headless_mode string: English fills the gap.
  auto german = describe("de", dir);
  EXPECT_EQ(german["locale"], "de");
  EXPECT_EQ(find_setting(german, "headless_mode")["title"], "Stream to a virtual display");

  // Unknown locales never reach the file system; they fall back.
  auto other = describe("../../secrets", dir);
  EXPECT_NE(other["locale"], "../../secrets");

  // No locale files at all: the key is the title.
  auto bare = describe("en", "no-such-directory/");
  EXPECT_EQ(find_setting(bare, "max_bitrate")["title"], "max_bitrate");
  EXPECT_EQ(find_setting(bare, "max_bitrate")["help"], "");
}

TEST(ConfigUpdateTextTests, ReplacesInPlaceAndKeepsComments) {
  const std::string before =
    "# My settings\n"
    "headless_mode = disabled\n"
    "\n"
    "# Added by the Norishine installer\n"
    "port = 48989\n"
    "sunshine_name = Living room\n";
  auto after = config::update_config_text(before, {{"headless_mode", "enabled"}, {"max_bitrate", "50000"}}, {"port"});
  EXPECT_EQ(after,
            "# My settings\n"
            "headless_mode = enabled\n"
            "\n"
            "# Added by the Norishine installer\n"
            "sunshine_name = Living room\n"
            "max_bitrate = 50000\n");
}

TEST(ConfigUpdateTextTests, MultiLineListsAndDuplicates) {
  const std::string before =
    "global_prep_cmd = [\n"
    "  {\"do\": \"a\"}\n"
    "]\n"
    "qp = 20\n"
    "qp = 30\n";
  auto after = config::update_config_text(before, {{"global_prep_cmd", "[{\"do\":\"b\"}]"}, {"qp", "25"}}, {});
  EXPECT_EQ(after, "global_prep_cmd = [{\"do\":\"b\"}]\nqp = 25\n");
  EXPECT_EQ(config::parse_config(after).at("qp"), "25");
}

TEST(ConfigUpdateTextTests, CrlfEmptyAndNoTrailingNewline) {
  EXPECT_EQ(config::update_config_text("", {{"qp", "25"}}, {}), "qp = 25\n");
  EXPECT_EQ(config::update_config_text("qp = 20", {{"min_threads", "4"}}, {}), "qp = 20\nmin_threads = 4\n");
  EXPECT_EQ(config::update_config_text("qp = 20\r\nmin_threads = 2\r\n", {{"qp", "25"}}, {"min_threads"}), "qp = 25\r\n");
  EXPECT_EQ(config::update_config_text("qp = 20\n", {}, {}), "qp = 20\n");
}
