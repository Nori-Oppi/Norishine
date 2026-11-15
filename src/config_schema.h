/**
 * @file src/config_schema.h
 * @brief Declarations for the settings schema: one table describing every config setting.
 *
 * The table is the reference for the management API and the settings UIs: key, type,
 * default, range or allowed values, platforms, tier and category. config.cpp still parses
 * the file; a test checks that both cover exactly the same keys.
 */
#pragma once

// standard includes
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

namespace config::schema {
  /**
   * @brief Value types. The config file stores every value as text; these say how.
   */
  enum class type_e {
    boolean,  ///< "enabled" or "disabled" (true/false in JSON).
    integer,  ///< Whole number, optionally limited by min and max.
    number,  ///< Decimal number, optionally limited by min and max.
    string,  ///< Single-line text.
    choice,  ///< One of a fixed list of values.
    path,  ///< Single-line file path.
    prep_commands,  ///< List of {do, undo, elevated} commands (JSON).
    server_commands,  ///< List of {name, cmd, elevated} commands (JSON).
    keybindings,  ///< List of key code pairs, written like [0x10,0xA0].
    mode_remapping,  ///< Display mode remapping lists (JSON object).
  };

  /**
   * @brief How prominent a setting is in a UI.
   */
  enum class tier_e {
    basic,  ///< Shown by default.
    advanced,  ///< Shown when advanced settings are on.
    dangerous,  ///< Can break the host or expose it; needs confirmation.
  };

  /**
   * @brief Platform bits.
   */
  namespace platform {
    constexpr unsigned windows = 1;
    constexpr unsigned linux = 2;
    constexpr unsigned macos = 4;
    constexpr unsigned all = windows | linux | macos;
  }  // namespace platform

  /**
   * @brief One allowed value of a choice setting.
   */
  struct choice_t {
    std::string_view value;
    unsigned platforms = platform::all;
  };

  /**
   * @brief One setting.
   */
  struct setting_t {
    std::string_view key;
    std::string_view category;  ///< Same ids as the web UI tabs (general, input, av, ...).
    type_e type;
    tier_e tier;
    unsigned platforms;
    nlohmann::json default_value;  ///< Typed default; "" for a value the host fills in itself.
    std::optional<double> min {};
    std::optional<double> max {};
    std::vector<choice_t> choices {};  ///< For choice settings; "" means automatic.
    bool restart_required = true;  ///< Settings are read at startup, so all need a restart for now.
  };

  /**
   * @brief Every setting, in UI order.
   */
  const std::vector<setting_t> &settings();

  /**
   * @brief Find a setting by key.
   * @return The setting, or nullptr if there is none.
   */
  const setting_t *find(std::string_view key);

  /**
   * @brief The platform bit of this build.
   */
  unsigned current_platform();

  /**
   * @brief Whether a setting applies on this platform.
   */
  bool available(const setting_t &setting);

  /**
   * @brief The allowed values of a choice setting on this platform.
   */
  std::vector<std::string> choices(const setting_t &setting);

  /**
   * @brief Result of converting a value.
   */
  struct text_result_t {
    std::string text;  ///< Config file text; empty means "remove the setting" (use the default).
    std::string error;  ///< Empty on success.
  };

  /**
   * @brief Validate a JSON value and convert it to config file text.
   * @param setting The setting.
   * @param value The value, typed (true, 28, "auto", [...]) or as text ("enabled", "28").
   */
  text_result_t to_config_text(const setting_t &setting, const nlohmann::json &value);

  /**
   * @brief Convert config file text to a typed JSON value.
   * @param setting The setting.
   * @param text The value as stored in the config file.
   * @param error Set to the reason when the text is not a valid value.
   * @return The typed value, or nothing when invalid.
   */
  std::optional<nlohmann::json> from_config_text(const setting_t &setting, std::string_view text, std::string *error = nullptr);

  /**
   * @brief The schema as JSON, for GET /api/v1/config/schema.
   */
  nlohmann::json describe();

  /**
   * @brief A validated change request.
   */
  struct change_t {
    std::map<std::string, std::string, std::less<>> set;  ///< Key to config file text.
    std::set<std::string, std::less<>> reset;  ///< Keys to remove (back to the default).
    nlohmann::json errors = nlohmann::json::object();  ///< Key (or "" for the request) to message.
  };

  /**
   * @brief Validate a change request: {"set": {key: value, ...}, "reset": [key, ...]}.
   *
   * Every key is checked; nothing should be written when `errors` is not empty. A null or
   * empty value in `set` counts as a reset.
   */
  change_t validate_change(const nlohmann::json &request);

  /**
   * @brief Current settings from config file text, for GET /api/v1/config.
   * @return {"values": {key: typed}, "unknown": {key: text}, "invalid": {key: {value, error}}}.
   */
  nlohmann::json read_values(std::string_view file_content);

  /**
   * @brief Names used in JSON.
   */
  std::string_view to_string(type_e type);
  std::string_view to_string(tier_e tier);
}  // namespace config::schema
