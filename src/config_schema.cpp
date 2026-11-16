/**
 * @file src/config_schema.cpp
 * @brief Definitions for the settings schema.
 */
// standard includes
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <mutex>
#include <regex>

// local includes
#include "config.h"
#include "config_schema.h"
#include "confighttp.h"
#include "file_handler.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "rtsp.h"

namespace config::schema {
  using namespace std::literals;
  using nlohmann::json;

  namespace {
    constexpr unsigned W = platform::windows;
    constexpr unsigned L = platform::linux;
    constexpr unsigned M = platform::macos;
    constexpr unsigned ALL = platform::all;
    constexpr double INT_MAX_D = (double) std::numeric_limits<int>::max();

    using tier = tier_e;

    setting_t boolean(std::string_view key, std::string_view category, tier t, unsigned platforms, bool def) {
      return {key, category, type_e::boolean, t, platforms, def};
    }

    setting_t integer(std::string_view key, std::string_view category, tier t, unsigned platforms, int def, double min, double max) {
      return {key, category, type_e::integer, t, platforms, def, min, max};
    }

    setting_t number(std::string_view key, std::string_view category, tier t, unsigned platforms, double def, double min, double max) {
      return {key, category, type_e::number, t, platforms, def, min, max};
    }

    setting_t text(std::string_view key, std::string_view category, tier t, unsigned platforms) {
      return {key, category, type_e::string, t, platforms, ""};
    }

    setting_t path(std::string_view key, std::string_view category) {
      return {key, category, type_e::path, tier::dangerous, ALL, ""};
    }

    setting_t choice(std::string_view key, std::string_view category, tier t, unsigned platforms, std::string_view def, std::vector<choice_t> values) {
      return {key, category, type_e::choice, t, platforms, std::string {def}, {}, {}, std::move(values)};
    }

    setting_t special(std::string_view key, std::string_view category, type_e type, tier t, json def) {
      return {key, category, type, t, ALL, std::move(def)};
    }

    setting_t depends(setting_t setting, std::string_view key, std::vector<json> values) {
      setting.depends_on = key;
      setting.depends_on_values = std::move(values);
      return setting;
    }

    std::vector<setting_t> make_settings() {
      const std::vector<choice_t> coders {{"auto"}, {"cabac"}, {"cavlc"}};
      const std::vector<choice_t> dd_auto_manual {{"disabled"}, {"auto"}, {"manual"}};
      // Display device settings only act when the host changes the display for a stream.
      const std::vector<json> dd_on {"verify_only", "ensure_active", "ensure_primary", "ensure_only_display"};
      auto dd = [&](setting_t setting) {
        return depends(std::move(setting), "dd_configuration_option", dd_on);
      };

      return {
        // General
        text("sunshine_name", "general", tier::basic, ALL),
        choice("locale", "general", tier::basic, ALL, "en", {{"bg"}, {"cs"}, {"de"}, {"en"}, {"en_GB"}, {"en_US"}, {"es"}, {"fr"}, {"hu"}, {"it"}, {"ja"}, {"ko"}, {"pl"}, {"pt"}, {"pt_BR"}, {"ru"}, {"sv"}, {"tr"}, {"uk"}, {"vi"}, {"zh"}, {"zh_TW"}}),
        boolean("system_tray", "general", tier::basic, ALL, true),
        depends(boolean("hide_tray_controls", "general", tier::advanced, ALL, false), "system_tray", {true}),
        boolean("notify_pre_releases", "general", tier::basic, ALL, false),
        integer("min_log_level", "general", tier::advanced, ALL, 2, 0, 6),

        // Display
        boolean("headless_mode", "display", tier::basic, W, false),
        text("output_name", "display", tier::advanced, ALL),
        text("adapter_name", "display", tier::advanced, W | L),
        text("fallback_mode", "display", tier::advanced, ALL),
        boolean("double_refreshrate", "display", tier::advanced, W, false),
        boolean("isolated_virtual_display_option", "display", tier::advanced, W, false),
        choice("dd_configuration_option", "display", tier::advanced, W, "disabled", {{"disabled"}, {"verify_only"}, {"ensure_active"}, {"ensure_primary"}, {"ensure_only_display"}}),
        dd(choice("dd_resolution_option", "display", tier::advanced, W, "auto", dd_auto_manual)),
        depends(text("dd_manual_resolution", "display", tier::advanced, W), "dd_resolution_option", {"manual"}),
        dd(choice("dd_refresh_rate_option", "display", tier::advanced, W, "auto", dd_auto_manual)),
        depends(text("dd_manual_refresh_rate", "display", tier::advanced, W), "dd_refresh_rate_option", {"manual"}),
        dd({"dd_mode_remapping", "display", type_e::mode_remapping, tier::advanced, W, json {{"mixed", json::array()}, {"resolution_only", json::array()}, {"refresh_rate_only", json::array()}}}),
        dd(integer("dd_config_revert_delay", "display", tier::advanced, W, 3000, 0, INT_MAX_D)),
        dd(boolean("dd_config_revert_on_disconnect", "display", tier::advanced, W, false)),
        choice("dd_hdr_option", "display", tier::advanced, W, "auto", {{"disabled"}, {"auto"}}),
        integer("dd_wa_hdr_toggle_delay", "display", tier::advanced, W, 0, 0, 3000),

        // Video
        integer("max_bitrate", "video", tier::basic, ALL, 0, 0, INT_MAX_D),
        number("minimum_fps_target", "video", tier::advanced, ALL, 0, 0, 1000),
        boolean("limit_framerate", "video", tier::advanced, ALL, true),
        choice("encoder", "video", tier::advanced, ALL, "", {{""}, {"nvenc", W | L}, {"quicksync", W | L}, {"amdvce", W}, {"vaapi", L}, {"videotoolbox", M}, {"software"}}),
        choice("capture", "video", tier::advanced, W | L, "", {{""}, {"ddx", W}, {"wgc", W}, {"nvfbc", L}, {"wlr", L}, {"kms", L}, {"x11", L}}),
        integer("hevc_mode", "video", tier::advanced, ALL, 0, 0, 3),
        integer("av1_mode", "video", tier::advanced, ALL, 0, 0, 3),
        integer("qp", "video", tier::advanced, ALL, 28, 0, 255),
        integer("min_threads", "video", tier::advanced, ALL, 2, 1, 256),
        boolean("ignore_encoder_probe_failure", "video", tier::advanced, ALL, false),

        // Audio
        boolean("stream_audio", "audio", tier::basic, ALL, true),
        text("audio_sink", "audio", tier::advanced, ALL),
        text("virtual_sink", "audio", tier::advanced, ALL),
        boolean("keep_sink_default", "audio", tier::advanced, W, true),
        boolean("auto_capture_sink", "audio", tier::advanced, W, true),
        boolean("install_steam_audio_drivers", "audio", tier::advanced, W, true),

        // Input
        boolean("controller", "input", tier::basic, ALL, true),
        // Values come from the platform at runtime; see choices().
        depends(choice("gamepad", "input", tier::basic, ALL, "auto", {}), "controller", {true}),
        depends(boolean("motion_as_ds4", "input", tier::advanced, ALL, true), "controller", {true}),
        depends(boolean("touchpad_as_ds4", "input", tier::advanced, ALL, true), "controller", {true}),
        depends(boolean("ds4_back_as_touchpad_click", "input", tier::advanced, ALL, true), "controller", {true}),
        depends(boolean("ds5_inputtino_randomize_mac", "input", tier::advanced, L, true), "controller", {true}),
        depends(integer("back_button_timeout", "input", tier::advanced, ALL, -1, -1, INT_MAX_D), "controller", {true}),
        depends(boolean("forward_rumble", "input", tier::basic, ALL, true), "controller", {true}),
        boolean("keyboard", "input", tier::basic, ALL, true),
        depends(integer("key_repeat_delay", "input", tier::advanced, ALL, 500, 0, INT_MAX_D), "keyboard", {true}),
        depends(number("key_repeat_frequency", "input", tier::advanced, ALL, 24.9, 0, 1000), "keyboard", {true}),
        depends(boolean("always_send_scancodes", "input", tier::advanced, W, true), "keyboard", {true}),
        depends(boolean("key_rightalt_to_key_win", "input", tier::advanced, ALL, false), "keyboard", {true}),
        depends(special("keybindings", "input", type_e::keybindings, tier::advanced, "[0x10,0xA0,0x11,0xA2,0x12,0xA4]"), "keyboard", {true}),
        boolean("mouse", "input", tier::basic, ALL, true),
        depends(boolean("high_resolution_scrolling", "input", tier::advanced, ALL, true), "mouse", {true}),
        depends(boolean("native_pen_touch", "input", tier::advanced, W, true), "mouse", {true}),
        boolean("enable_input_only_mode", "input", tier::advanced, ALL, false),

        // Network and pairing
        boolean("enable_pairing", "network", tier::advanced, ALL, true),
        boolean("enable_discovery", "network", tier::advanced, ALL, true),
        boolean("upnp", "network", tier::advanced, ALL, false),
        choice("address_family", "network", tier::advanced, ALL, "ipv4", {{"ipv4"}, {"both"}}),
        text("external_ip", "network", tier::advanced, ALL),
        integer("port", "network", tier::dangerous, ALL, 47989, 1024 + nvhttp::PORT_HTTPS, 65535 - rtsp_stream::RTSP_SETUP_PORT),
        choice("origin_web_ui_allowed", "network", tier::dangerous, ALL, "lan", {{"pc"}, {"lan"}, {"wan"}}),
        integer("lan_encryption_mode", "network", tier::dangerous, ALL, 0, 0, 2),
        integer("wan_encryption_mode", "network", tier::dangerous, ALL, 1, 0, 2),
        integer("ping_timeout", "network", tier::advanced, ALL, 10000, -1, INT_MAX_D),
        integer("fec_percentage", "network", tier::advanced, ALL, 20, 1, 255),

        // Apps and commands
        special("global_prep_cmd", "apps", type_e::prep_commands, tier::dangerous, json::array()),
        special("global_state_cmd", "apps", type_e::prep_commands, tier::dangerous, json::array()),
        special("server_cmd", "apps", type_e::server_commands, tier::dangerous, json::array()),
        boolean("envvar_compatibility_mode", "apps", tier::advanced, ALL, false),
        boolean("legacy_ordering", "apps", tier::advanced, ALL, false),

        // NVIDIA NVENC
        integer("nvenc_preset", "nv", tier::advanced, W | L, 1, 1, 7),
        choice("nvenc_twopass", "nv", tier::advanced, W | L, "quarter_res", {{"disabled"}, {"quarter_res"}, {"full_res"}}),
        boolean("nvenc_spatial_aq", "nv", tier::advanced, W | L, false),
        integer("nvenc_vbv_increase", "nv", tier::advanced, W | L, 0, 0, 400),
        boolean("nvenc_realtime_hags", "nv", tier::advanced, W, true),
        boolean("nvenc_latency_over_power", "nv", tier::advanced, W, true),
        boolean("nvenc_opengl_vulkan_on_dxgi", "nv", tier::advanced, W, true),
        boolean("nvenc_h264_cavlc", "nv", tier::advanced, W | L, false),
        boolean("nvenc_intra_refresh", "nv", tier::advanced, W | L, false),

        // Intel Quick Sync
        choice("qsv_preset", "qsv", tier::advanced, W | L, "medium", {{"veryslow"}, {"slower"}, {"slow"}, {"medium"}, {"fast"}, {"faster"}, {"veryfast"}}),
        choice("qsv_coder", "qsv", tier::advanced, W | L, "auto", coders),
        boolean("qsv_slow_hevc", "qsv", tier::advanced, W | L, false),

        // AMD AMF
        choice("amd_usage", "amd", tier::advanced, W, "ultralowlatency", {{"transcoding"}, {"webcam"}, {"lowlatency_high_quality"}, {"lowlatency"}, {"ultralowlatency"}}),
        choice("amd_rc", "amd", tier::advanced, W, "vbr_latency", {{"cqp"}, {"vbr_latency"}, {"vbr_peak"}, {"cbr"}}),
        boolean("amd_enforce_hrd", "amd", tier::advanced, W, false),
        choice("amd_quality", "amd", tier::advanced, W, "balanced", {{"speed"}, {"balanced"}, {"quality"}}),
        boolean("amd_preanalysis", "amd", tier::advanced, W, false),
        boolean("amd_vbaq", "amd", tier::advanced, W, true),
        choice("amd_coder", "amd", tier::advanced, W, "auto", coders),

        // Apple VideoToolbox
        choice("vt_coder", "vt", tier::advanced, M, "auto", coders),
        choice("vt_software", "vt", tier::advanced, M, "auto", {{"auto"}, {"disabled"}, {"allowed"}, {"forced"}}),
        boolean("vt_realtime", "vt", tier::advanced, M, true),

        // VA-API
        boolean("vaapi_strict_rc_buffer", "vaapi", tier::advanced, L, false),

        // Software encoding
        choice("sw_preset", "sw", tier::advanced, ALL, "superfast", {{"ultrafast"}, {"superfast"}, {"veryfast"}, {"faster"}, {"fast"}, {"medium"}, {"slow"}, {"slower"}, {"veryslow"}}),
        choice("sw_tune", "sw", tier::advanced, ALL, "zerolatency", {{"film"}, {"animation"}, {"grain"}, {"stillimage"}, {"fastdecode"}, {"zerolatency"}}),

        // Files
        path("file_apps", "files"),
        path("file_state", "files"),
        path("credentials_file", "files"),
        path("log_path", "files"),
        path("pkey", "files"),
        path("cert", "files"),
      };
    }

    // Spellings the host's parser accepts for each choice value (config.cpp *_from_view).
    std::string_view canonical_choice(const setting_t &setting, std::string_view value) {
      if (setting.key.ends_with("_coder"sv)) {
        if (value == "ac"sv) {
          return "cabac"sv;
        }
        if (value == "vlc"sv) {
          return "cavlc"sv;
        }
      }
      return value;
    }

    std::optional<bool> parse_bool(std::string_view raw) {
      // Same spellings as config.cpp to_bool(), in any case; anything else is an error here, not "false".
      std::string text {raw};
      std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return (char) std::tolower(ch);
      });
      for (auto t : {"true"sv, "yes"sv, "enable"sv, "enabled"sv, "on"sv, "1"sv}) {
        if (text == t) {
          return true;
        }
      }
      for (auto f : {"false"sv, "no"sv, "disable"sv, "disabled"sv, "off"sv, "0"sv}) {
        if (text == f) {
          return false;
        }
      }
      return std::nullopt;
    }

    std::string_view trim(std::string_view text) {
      while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
      }
      while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
      }
      return text;
    }

    std::optional<long long> parse_integer(std::string_view text) {
      text = trim(text);
      long long value = 0;
      auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
      if (ec != std::errc {} || ptr != text.data() + text.size() || text.empty()) {
        return std::nullopt;
      }
      return value;
    }

    std::optional<double> parse_number(std::string_view text) {
      text = trim(text);
      double value = 0;
      auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
      if (ec != std::errc {} || ptr != text.data() + text.size() || text.empty() || !std::isfinite(value)) {
        return std::nullopt;
      }
      return value;
    }

    std::string range_text(const setting_t &setting) {
      auto fmt = [](double v) {
        if (v >= INT_MAX_D) {
          return "no limit"s;
        }
        return json(v == std::floor(v) ? json((long long) v) : json(v)).dump();
      };
      if (*setting.max >= INT_MAX_D) {
        return "at least "s + fmt(*setting.min);
      }
      return "between "s + fmt(*setting.min) + " and "s + fmt(*setting.max);
    }

    bool in_range(const setting_t &setting, double value) {
      return (!setting.min || value >= *setting.min) && (!setting.max || value <= *setting.max);
    }

    // A single-line value must stay one config line, and a leading '[' would make the
    // parser read on to the next ']' (possibly over following lines).
    std::string check_single_line(std::string_view value) {
      if (value.find_first_of("\r\n"sv) != std::string_view::npos || value.find('\0') != std::string_view::npos) {
        return "must be a single line";
      }
      if (!trim(value).empty() && trim(value).front() == '[') {
        return "must not start with '['";
      }
      return {};
    }

    std::string check_commands(const json &value, type_e type) {
      if (!value.is_array()) {
        return "must be a list";
      }
      const bool prep = type == type_e::prep_commands;
      for (const auto &item : value) {
        if (!item.is_object()) {
          return "every entry must be an object";
        }
        for (const auto &[field, field_value] : item.items()) {
          if (field == "elevated") {
            if (!field_value.is_boolean()) {
              return "\"elevated\" must be true or false";
            }
          } else if (prep ? (field == "do" || field == "undo") : (field == "name" || field == "cmd")) {
            if (!field_value.is_string()) {
              return "\"" + field + "\" must be text";
            }
          } else {
            return "unknown field \"" + field + "\"";
          }
        }
        if (!prep && (!item.contains("name") || !item.contains("cmd"))) {
          return "every entry needs \"name\" and \"cmd\"";
        }
      }
      return {};
    }

    std::string check_mode_remapping(const json &value) {
      if (!value.is_object()) {
        return "must be an object";
      }
      for (const auto &[group, entries] : value.items()) {
        if (group != "mixed" && group != "resolution_only" && group != "refresh_rate_only") {
          return "unknown group \"" + group + "\"";
        }
        if (!entries.is_array()) {
          return "\"" + group + "\" must be a list";
        }
        for (const auto &entry : entries) {
          if (!entry.is_object()) {
            return "every entry must be an object";
          }
          for (const auto &[field, field_value] : entry.items()) {
            if (field != "requested_resolution" && field != "requested_fps" && field != "final_resolution" && field != "final_refresh_rate") {
              return "unknown field \"" + field + "\"";
            }
            if (!field_value.is_string()) {
              return "\"" + field + "\" must be text";
            }
          }
        }
      }
      return {};
    }

    std::string check_keybindings(std::string_view text) {
      static const std::regex pattern(R"(^\[\s*(?:(?:0x[0-9a-fA-F]+|\d+)\s*(?:,\s*(?:0x[0-9a-fA-F]+|\d+)\s*)*)?\]$)");
      std::string value {trim(text)};
      if (!std::regex_match(value, pattern)) {
        return "must be a list of key codes like [0x10,0xA0]";
      }
      if (std::count(value.begin(), value.end(), ',') % 2 == 0 && value.find_first_not_of("[] \t") != std::string::npos) {
        return "must have an even number of key codes (pairs)";
      }
      return {};
    }

    const char *min_log_level_names[] = {"verbose", "debug", "info", "warning", "error", "fatal", "none"};
  }  // namespace

  const std::vector<setting_t> &settings() {
    static const std::vector<setting_t> table = make_settings();
    return table;
  }

  const setting_t *find(std::string_view key) {
    for (const auto &setting : settings()) {
      if (setting.key == key) {
        return &setting;
      }
    }
    return nullptr;
  }

  unsigned current_platform() {
#if defined(_WIN32)
    return platform::windows;
#elif defined(__APPLE__)
    return platform::macos;
#else
    return platform::linux;
#endif
  }

  bool available(const setting_t &setting) {
    return (setting.platforms & current_platform()) != 0;
  }

  std::vector<std::string> choices(const setting_t &setting) {
    std::vector<std::string> result;
    if (setting.key == "gamepad"sv) {
      result.emplace_back("auto");
      for (const auto &gamepad : platf::supported_gamepads(nullptr)) {
        if (gamepad.name != "auto"sv) {
          result.emplace_back(gamepad.name);
        }
      }
      return result;
    }
    for (const auto &choice : setting.choices) {
      if (choice.platforms & current_platform()) {
        result.emplace_back(choice.value);
      }
    }
    return result;
  }

  text_result_t to_config_text(const setting_t &setting, const json &value) {
    auto fail = [](std::string message) {
      return text_result_t {{}, std::move(message)};
    };

    switch (setting.type) {
      case type_e::boolean:
        {
          std::optional<bool> b;
          if (value.is_boolean()) {
            b = value.get<bool>();
          } else if (value.is_string()) {
            b = parse_bool(value.get<std::string>());
          }
          if (!b) {
            return fail("must be true or false");
          }
          return {*b ? "enabled" : "disabled", {}};
        }

      case type_e::integer:
        {
          std::optional<long long> n;
          if (value.is_number_integer()) {
            n = value.get<long long>();
          } else if (value.is_number_float() && std::floor(value.get<double>()) == value.get<double>()) {
            n = (long long) value.get<double>();
          } else if (value.is_string()) {
            auto text = value.get<std::string>();
            n = parse_integer(text);
            if (!n && setting.key == "min_log_level"sv) {
              for (int level = 0; level < 7; ++level) {
                if (text == min_log_level_names[level]) {
                  n = level;
                }
              }
            }
          }
          if (!n) {
            return fail("must be a whole number");
          }
          if (!in_range(setting, (double) *n)) {
            return fail("must be " + range_text(setting));
          }
          return {std::to_string(*n), {}};
        }

      case type_e::number:
        {
          std::optional<double> n;
          if (value.is_number()) {
            n = value.get<double>();
          } else if (value.is_string()) {
            n = parse_number(value.get<std::string>());
          }
          if (!n || !std::isfinite(*n)) {
            return fail("must be a number");
          }
          if (!in_range(setting, *n)) {
            return fail("must be " + range_text(setting));
          }
          return {json(*n).dump(), {}};
        }

      case type_e::choice:
        {
          if (!value.is_string()) {
            return fail("must be one of the listed values");
          }
          auto text = std::string {canonical_choice(setting, value.get<std::string>())};
          auto allowed = choices(setting);
          if (std::find(allowed.begin(), allowed.end(), text) == allowed.end()) {
            std::string list;
            for (const auto &a : allowed) {
              list += (list.empty() ? "" : ", ") + (a.empty() ? "\"\" (automatic)"s : a);
            }
            return fail("must be one of: " + list);
          }
          return {text, {}};
        }

      case type_e::string:
      case type_e::path:
        {
          if (!value.is_string()) {
            return fail("must be text");
          }
          auto text = value.get<std::string>();
          if (auto error = check_single_line(text); !error.empty()) {
            return fail(error);
          }
          return {std::string {trim(text)}, {}};
        }

      case type_e::prep_commands:
      case type_e::server_commands:
        {
          json parsed = value;
          if (value.is_string()) {
            parsed = json::parse(value.get<std::string>(), nullptr, false);
          }
          if (auto error = check_commands(parsed, setting.type); !error.empty()) {
            return fail(error);
          }
          return {parsed.empty() ? "" : parsed.dump(), {}};
        }

      case type_e::keybindings:
        {
          std::string text;
          if (value.is_array()) {
            text = "[";
            for (const auto &code : value) {
              if (!code.is_number_integer() || code.get<long long>() < 0 || code.get<long long>() > 0xFFFF) {
                return fail("key codes must be whole numbers from 0 to 65535");
              }
              text += (text.size() > 1 ? "," : "") + std::to_string(code.get<long long>());
            }
            text += "]";
          } else if (value.is_string()) {
            text = std::string {trim(value.get<std::string>())};
          } else {
            return fail("must be a list of key codes");
          }
          if (auto error = check_keybindings(text); !error.empty()) {
            return fail(error);
          }
          return {text, {}};
        }

      case type_e::mode_remapping:
        {
          json parsed = value;
          if (value.is_string()) {
            parsed = json::parse(value.get<std::string>(), nullptr, false);
          }
          if (auto error = check_mode_remapping(parsed); !error.empty()) {
            return fail(error);
          }
          return {parsed.dump(), {}};
        }
    }
    return fail("unsupported type");
  }

  std::optional<json> from_config_text(const setting_t &setting, std::string_view text, std::string *error) {
    auto fail = [error](std::string message) -> std::optional<json> {
      if (error) {
        *error = std::move(message);
      }
      return std::nullopt;
    };
    auto trimmed = trim(text);

    switch (setting.type) {
      case type_e::boolean:
        if (auto b = parse_bool(trimmed)) {
          return json(*b);
        }
        return fail("not true or false");

      case type_e::integer:
        {
          auto result = to_config_text(setting, json(std::string {trimmed}));
          if (!result.error.empty()) {
            return fail(result.error);
          }
          return json(*parse_integer(result.text));
        }

      case type_e::number:
        {
          auto n = parse_number(trimmed);
          if (!n) {
            return fail("must be a number");
          }
          if (!in_range(setting, *n)) {
            return fail("must be " + range_text(setting));
          }
          return json(*n);
        }

      case type_e::choice:
      case type_e::string:
      case type_e::path:
      case type_e::keybindings:
        {
          auto result = to_config_text(setting, json(std::string {trimmed}));
          if (!result.error.empty()) {
            return fail(result.error);
          }
          return json(result.text);
        }

      case type_e::prep_commands:
      case type_e::server_commands:
      case type_e::mode_remapping:
        {
          auto parsed = json::parse(trimmed, nullptr, false);
          if (parsed.is_discarded()) {
            return fail("not valid JSON");
          }
          auto result = to_config_text(setting, parsed);
          if (!result.error.empty()) {
            return fail(result.error);
          }
          return parsed;
        }
    }
    return fail("unsupported type");
  }

  change_t validate_change(const json &request) {
    change_t change;
    if (!request.is_object()) {
      change.errors[""] = "request must be an object with \"set\" and/or \"reset\"";
      return change;
    }
    for (const auto &[field, _] : request.items()) {
      if (field != "set" && field != "reset") {
        change.errors[""] = "unknown field \"" + field + "\"";
        return change;
      }
    }

    auto check_key = [&](const std::string &key) -> const setting_t * {
      const auto *setting = find(key);
      if (!setting) {
        change.errors[key] = "unknown setting";
        return nullptr;
      }
      if (!available(*setting)) {
        change.errors[key] = "not available on this platform";
        return nullptr;
      }
      return setting;
    };

    if (request.contains("set")) {
      const auto &set = request["set"];
      if (!set.is_object()) {
        change.errors[""] = "\"set\" must be an object";
        return change;
      }
      for (const auto &[key, value] : set.items()) {
        const auto *setting = check_key(key);
        if (!setting) {
          continue;
        }
        if (value.is_null()) {
          change.reset.insert(key);
          continue;
        }
        auto result = to_config_text(*setting, value);
        if (!result.error.empty()) {
          change.errors[key] = result.error;
        } else if (result.text.empty()) {
          change.reset.insert(key);
        } else {
          change.set.emplace(key, std::move(result.text));
        }
      }
    }

    if (request.contains("reset")) {
      const auto &reset = request["reset"];
      if (!reset.is_array()) {
        change.errors[""] = "\"reset\" must be a list of keys";
        return change;
      }
      for (const auto &key : reset) {
        if (!key.is_string()) {
          change.errors[""] = "\"reset\" must be a list of keys";
          return change;
        }
        auto name = key.get<std::string>();
        if (!check_key(name)) {
          continue;
        }
        if (change.set.contains(name)) {
          change.errors[name] = "both set and reset";
          continue;
        }
        change.reset.insert(name);
      }
    }
    return change;
  }

  json read_values(std::string_view file_content) {
    json values = json::object();
    json unknown = json::object();
    json invalid = json::object();
    for (const auto &[key, text] : config::parse_config(file_content)) {
      const auto *setting = find(key);
      if (!setting) {
        unknown[key] = text;
        continue;
      }
      std::string error;
      if (auto value = from_config_text(*setting, text, &error)) {
        values[key] = std::move(*value);
      } else {
        invalid[key] = {{"value", text}, {"error", error}};
      }
    }
    return {{"values", values}, {"unknown", unknown}, {"invalid", invalid}};
  }

  namespace {
    std::mutex config_file_mutex;
  }

  json read_config_file(const std::string &config_file) {
    std::lock_guard lock {config_file_mutex};
    return read_values(file_handler::read_file(config_file.c_str()));
  }

  apply_result_t apply_change(const change_t &change, const std::string &config_file) {
    apply_result_t result;
    bool restart = false;
    for (const auto &[key, _] : change.set) {
      result.changed.push_back(key);
      restart = restart || find(key)->restart_required;
    }
    for (const auto &key : change.reset) {
      result.changed.push_back(key);
      restart = restart || find(key)->restart_required;
    }

    std::lock_guard lock {config_file_mutex};
    const auto current = file_handler::read_file(config_file.c_str());
    const auto updated = config::update_config_text(current, change.set, change.reset);
    if (updated == current) {
      return result;
    }
    result.written = file_handler::write_file(config_file.c_str(), updated) == 0;
    result.restart_required = result.written && restart;
    return result;
  }

  std::string_view to_string(type_e type) {
    switch (type) {
      case type_e::boolean:
        return "boolean";
      case type_e::integer:
        return "integer";
      case type_e::number:
        return "number";
      case type_e::string:
        return "string";
      case type_e::choice:
        return "choice";
      case type_e::path:
        return "path";
      case type_e::prep_commands:
        return "prep_commands";
      case type_e::server_commands:
        return "server_commands";
      case type_e::keybindings:
        return "keybindings";
      case type_e::mode_remapping:
        return "mode_remapping";
    }
    return "unknown";
  }

  std::string_view to_string(tier_e tier) {
    switch (tier) {
      case tier_e::basic:
        return "basic";
      case tier_e::advanced:
        return "advanced";
      case tier_e::dangerous:
        return "dangerous";
    }
    return "unknown";
  }

  json describe(std::string_view locale, const std::string &locale_dir) {
    // Only known locale names reach the file system.
    auto known = choices(*find("locale"));
    std::string chosen {locale};
    if (std::find(known.begin(), known.end(), chosen) == known.end()) {
      chosen = config::sunshine.locale;
    }
    if (std::find(known.begin(), known.end(), chosen) == known.end()) {
      chosen = "en";
    }
    const std::string dir = locale_dir.empty() ? std::string {WEB_DIR "assets/locale/"} : locale_dir;
    auto load = [&dir](const std::string &name) {
      auto parsed = json::parse(file_handler::read_file((dir + name + ".json").c_str()), nullptr, false);
      if (parsed.is_object() && parsed.contains("config") && parsed["config"].is_object()) {
        return parsed["config"];
      }
      return json::object();
    };
    const json strings = load(chosen);
    const json english = chosen == "en" ? strings : load("en");
    auto text = [&](const std::string &id) -> std::string {
      for (const auto *source : {&strings, &english}) {
        if (auto it = source->find(id); it != source->end() && it->is_string() && !it->get<std::string>().empty()) {
          return it->get<std::string>();
        }
      }
      return {};
    };

    const std::string platform_name = current_platform() == platform::windows ? "windows" :
                                      current_platform() == platform::macos   ? "macos" :
                                                                                "linux";
    // Each language in its own name, so it can be found whatever the current locale is.
    static const std::map<std::string, std::string, std::less<>> language_names {
      {"bg", "Български (Bulgarian)"},
      {"cs", "Čeština (Czech)"},
      {"de", "Deutsch (German)"},
      {"en", "English"},
      {"en_GB", "English (UK)"},
      {"en_US", "English (US)"},
      {"es", "Español (Spanish)"},
      {"fr", "Français (French)"},
      {"hu", "Magyar (Hungarian)"},
      {"it", "Italiano (Italian)"},
      {"ja", "日本語 (Japanese)"},
      {"ko", "한국어 (Korean)"},
      {"pl", "Polski (Polish)"},
      {"pt", "Português (Portuguese)"},
      {"pt_BR", "Português do Brasil (Brazilian Portuguese)"},
      {"ru", "Русский (Russian)"},
      {"sv", "Svenska (Swedish)"},
      {"tr", "Türkçe (Turkish)"},
      {"uk", "Українська (Ukrainian)"},
      {"vi", "Tiếng Việt (Vietnamese)"},
      {"zh", "简体中文 (Chinese, Simplified)"},
      {"zh_TW", "繁體中文 (Chinese, Traditional)"},
    };

    json list = json::array();
    for (const auto &setting : settings()) {
      json platforms = json::array();
      if (setting.platforms & platform::windows) {
        platforms.push_back("windows");
      }
      if (setting.platforms & platform::linux) {
        platforms.push_back("linux");
      }
      if (setting.platforms & platform::macos) {
        platforms.push_back("macos");
      }

      json item {
        {"key", setting.key},
        {"category", setting.category},
        {"type", to_string(setting.type)},
        {"tier", to_string(setting.tier)},
        {"platforms", platforms},
        {"available", available(setting)},
        {"default", setting.default_value},
        {"restart_required", setting.restart_required},
        // Keys into the web UI locale files (config.<key> and config.<key>_desc).
        {"label", "config."s + std::string {setting.key}},
        {"description", "config."s + std::string {setting.key} + "_desc"},
      };
      const std::string key {setting.key};
      auto title = text(key);
      item["title"] = title.empty() ? key : title;
      auto help = text(key + "_desc");
      if (help.empty()) {
        // Some settings only have a platform-specific explanation.
        help = text(key + "_desc_" + platform_name);
        if (help.empty() && current_platform() != platform::windows) {
          help = text(key + "_desc_unix");
        }
      }
      item["help"] = help;
      if (!setting.depends_on.empty()) {
        item["depends_on"] = {{"key", setting.depends_on}, {"values", setting.depends_on_values}};
      }

      // Readable names for the allowed values, where the locale has them (config.<key>_<value>).
      json value_titles = json::object();
      if (setting.type == type_e::choice) {
        for (const auto &value : choices(setting)) {
          if (setting.key == "locale"sv) {
            if (auto it = language_names.find(value); it != language_names.end()) {
              value_titles[value] = it->second;
            }
            continue;
          }
          auto name = text(key + "_" + (value.empty() ? "auto"s : value));
          if (!name.empty()) {
            value_titles[value] = name;
          }
        }
      } else if (setting.type == type_e::integer && setting.max && *setting.max - *setting.min <= 10) {
        for (auto n = (long long) *setting.min; n <= (long long) *setting.max; ++n) {
          auto name = text(key + "_" + std::to_string(n));
          if (!name.empty()) {
            value_titles[std::to_string(n)] = name;
          }
        }
      }
      if (!value_titles.empty()) {
        item["value_titles"] = std::move(value_titles);
      }
      if (setting.min) {
        item["min"] = *setting.min;
      }
      if (setting.max) {
        item["max"] = *setting.max;
      }
      if (setting.type == type_e::choice) {
        item["choices"] = choices(setting);
      }
      list.push_back(std::move(item));
    }

    // In display order; "group" puts related categories together under one heading.
    struct category_t {
      const char *id;
      const char *title;  ///< When the locale has no config.category_<id>.
      const char *group;
    };

    const category_t category_list[] = {
      {"general", "General", ""},
      {"display", "Display", ""},
      {"video", "Video", ""},
      {"audio", "Audio", ""},
      {"input", "Input", ""},
      {"network", "Network and pairing", ""},
      {"apps", "Apps and commands", ""},
      {"nv", "NVIDIA NVENC", "encoders"},
      {"qsv", "Intel Quick Sync", "encoders"},
      {"amd", "AMD AMF", "encoders"},
      {"vt", "Apple VideoToolbox", "encoders"},
      {"vaapi", "VA-API", "encoders"},
      {"sw", "Software encoder", "encoders"},
      {"files", "Files", ""},
    };
    json categories = json::array();
    for (const auto &category : category_list) {
      auto title = text("category_"s + category.id);
      json item {{"id", category.id}, {"title", title.empty() ? std::string {category.title} : title}};
      if (*category.group) {
        auto group_title = text("category_group_"s + category.group);
        item["group"] = category.group;
        item["group_title"] = group_title.empty() ? "Encoders"s : group_title;
      }
      categories.push_back(std::move(item));
    }

    return {
      {"version", 1},
      {"platform", platform_name},
      {"locale", chosen},
      {"categories", categories},
      {"settings", std::move(list)},
    };
  }
}  // namespace config::schema
