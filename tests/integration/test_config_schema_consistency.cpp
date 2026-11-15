/**
 * @file tests/integration/test_config_schema_consistency.cpp
 * @brief Check that the settings schema and config.cpp cover exactly the same keys.
 */
#include "../tests_common.h"

// standard includes
#include <regex>
#include <set>
#include <string>

// local includes
#include "src/config.h"
#include "src/config_schema.h"
#include "src/file_handler.h"

TEST(ConfigSchemaConsistencyTest, SchemaMatchesConfigCpp) {
  // config.cpp is copied next to the tests (tests/CMakeLists.txt, INTEGRATION_TEST_FILES).
  const auto content = file_handler::read_file("src/config.cpp");
  ASSERT_FALSE(content.empty()) << "src/config.cpp not found next to the test executable";

  std::set<std::string> parsed;
  const std::regex pattern(R"DELIM(\b[a-z_]+_f\s*\(\s*vars\s*,\s*"([a-z0-9_]+)")DELIM");
  for (std::sregex_iterator it(content.begin(), content.end(), pattern), end; it != end; ++it) {
    parsed.insert((*it)[1].str());
  }
  ASSERT_GT(parsed.size(), 90u) << "the pattern no longer finds config.cpp's settings";

  std::set<std::string> schema;
  for (const auto &setting : config::schema::settings()) {
    schema.emplace(setting.key);
  }

  for (const auto &key : parsed) {
    EXPECT_TRUE(schema.contains(key)) << "config.cpp reads \"" << key << "\" but the schema does not list it";
  }
  for (const auto &key : schema) {
    EXPECT_TRUE(parsed.contains(key)) << "the schema lists \"" << key << "\" but config.cpp never reads it";
  }
}

TEST(ConfigSchemaConsistencyTest, DefaultsRoundTripThroughTheParser) {
  // What the API writes for each default must read back unchanged through the host's own
  // config file parser, and convert back to the same typed value.
  for (const auto &setting : config::schema::settings()) {
    if (!config::schema::available(setting)) {
      continue;
    }
    auto text = config::schema::to_config_text(setting, setting.default_value).text;
    if (text.empty()) {
      continue;
    }
    const std::string key {setting.key};
    auto vars = config::parse_config(key + " = " + text + "\n");
    ASSERT_TRUE(vars.contains(key)) << key;
    EXPECT_EQ(vars.at(key), text) << key;
    auto typed = config::schema::from_config_text(setting, vars.at(key));
    ASSERT_TRUE(typed) << key;
    EXPECT_EQ(config::schema::to_config_text(setting, *typed).text, text) << key;
  }
}
