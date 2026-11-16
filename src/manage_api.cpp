/**
 * @file src/manage_api.cpp
 * @brief Definitions for apps, devices, sessions and displays in the management API.
 */
// standard includes
#include <algorithm>
#include <mutex>
#include <set>
#include <variant>

// local includes
#include "config.h"
#include "crypto.h"
#include "display_device.h"
#include "file_handler.h"
#include "httpcommon.h"
#include "logging.h"
#include "manage_api.h"
#include "nvhttp.h"
#include "process.h"
#include "uuid.h"

using namespace std::literals;
using SimpleWeb::StatusCode;

namespace manage_api {
  namespace {
    // Serialises read-modify-write of apps.json between the two servers' threads.
    std::mutex apps_mutex;

    constexpr std::size_t max_text = 4096;

    reply_t ok(nlohmann::json body = nlohmann::json::object()) {
      body["status"] = true;
      return {StatusCode::success_ok, std::move(body)};
    }

    reply_t fail(StatusCode code, std::string error) {
      return {code, {{"status", false}, {"error", std::move(error)}}};
    }

    reply_t invalid(nlohmann::json errors) {
      return {StatusCode::client_error_bad_request, {{"status", false}, {"errors", std::move(errors)}}};
    }

    bool is_text(const nlohmann::json &value) {
      return value.is_string() && value.get_ref<const std::string &>().size() <= max_text;
    }

    /**
     * @brief Check a list of commands: objects with text fields and an optional "elevated".
     */
    std::string check_commands(const nlohmann::json &list, std::initializer_list<std::string_view> text_fields) {
      if (!list.is_array()) {
        return "Must be a list.";
      }
      if (list.size() > 100) {
        return "Too many commands (at most 100).";
      }
      for (const auto &item : list) {
        if (!item.is_object()) {
          return "Each command must be an object.";
        }
        for (auto field : text_fields) {
          auto it = item.find(std::string {field});
          if (it != item.end() && !is_text(*it)) {
            return "\"" + std::string {field} + "\" must be text.";
          }
        }
        if (auto it = item.find("elevated"); it != item.end() && !it->is_boolean()) {
          return "\"elevated\" must be true or false.";
        }
      }
      return {};
    }

    nlohmann::json read_apps_file() {
      return nlohmann::json::parse(file_handler::read_file(config::stream.file_apps.c_str()));
    }

    /**
     * @brief Write apps.json and reload the app list.
     * @return Whether a running app was closed: reloading the list ends it, as in the web UI.
     */
    bool write_apps_file(const nlohmann::json &file) {
      if (file_handler::write_file(config::stream.file_apps.c_str(), file.dump(4)) != 0) {
        throw std::runtime_error("Couldn't write " + config::stream.file_apps);
      }
      bool was_running = proc::proc.running() > 0;
      proc::refresh(config::stream.file_apps);
      return was_running;
    }

    nlohmann::json &apps_of(nlohmann::json &file) {
      if (!file.contains("apps") || !file["apps"].is_array()) {
        file["apps"] = nlohmann::json::array();
      }
      return file["apps"];
    }

    nlohmann::json::iterator find_app(nlohmann::json &apps, const std::string &uuid) {
      return std::find_if(apps.begin(), apps.end(), [&](const nlohmann::json &app) {
        return app.is_object() && app.value("uuid", ""s) == uuid;
      });
    }

    /**
     * @brief Drop what this API adds to listed apps, and what migrate_apps drops too.
     */
    nlohmann::json stored_form(nlohmann::json app) {
      for (auto key : {"running", "launching", "index"}) {
        app.erase(key);
      }
      for (auto key : {"prep-cmd", "state-cmd", "detached"}) {
        if (app.contains(key) && app[key].empty()) {
          app.erase(key);
        }
      }
      return app;
    }

    template<class F>
    reply_t guarded(std::string_view what, F &&f) {
      try {
        return f();
      } catch (const std::exception &e) {
        BOOST_LOG(warning) << "Management API: "sv << what << ": "sv << e.what();
        return fail(StatusCode::server_error_internal_server_error, std::string {what} + " failed: " + e.what());
      }
    }

    double as_number(const display_device::FloatingPoint &value) {
      if (const auto *rational = std::get_if<display_device::Rational>(&value)) {
        return rational->m_denominator ? (double) rational->m_numerator / rational->m_denominator : 0.0;
      }
      return std::get<double>(value);
    }

    std::optional<nlohmann::json> find_device(const std::string &uuid) {
      for (const auto &device : nvhttp::get_all_clients()) {
        if (device.value("uuid", ""s) == uuid) {
          return device;
        }
      }
      return std::nullopt;
    }
  }  // namespace

  nlohmann::json validate_app(const nlohmann::json &app) {
    nlohmann::json errors = nlohmann::json::object();
    if (!app.is_object()) {
      errors["app"] = "An app must be a JSON object.";
      return errors;
    }

    if (auto it = app.find("name"); it == app.end() || !it->is_string() || it->get_ref<const std::string &>().find_first_not_of(" \t") == std::string::npos) {
      errors["name"] = "An app needs a name.";
    } else if (!is_text(*it)) {
      errors["name"] = "The name is too long.";
    }

    for (auto key : {"uuid", "cmd", "output", "image-path", "working-dir", "gamepad"}) {
      if (auto it = app.find(key); it != app.end() && !is_text(*it)) {
        errors[key] = "Must be text.";
      }
    }
    for (auto key : {"allow-client-commands", "auto-detach", "elevated", "exclude-global-prep-cmd", "exclude-global-state-cmd", "per-client-app-identity", "terminate-on-pause", "use-app-identity", "virtual-display", "wait-all"}) {
      if (auto it = app.find(key); it != app.end() && !it->is_boolean()) {
        errors[key] = "Must be true or false.";
      }
    }
    if (auto it = app.find("exit-timeout"); it != app.end() && (!it->is_number_integer() || *it < 0 || *it > 86400)) {
      errors["exit-timeout"] = "Must be a whole number of seconds from 0 to 86400.";
    }
    if (auto it = app.find("scale-factor"); it != app.end() && (!it->is_number_integer() || *it < 20 || *it > 400)) {
      errors["scale-factor"] = "Must be a whole number from 20 to 400 (percent).";
    }
    for (auto key : {"prep-cmd", "state-cmd"}) {
      if (auto it = app.find(key); it != app.end()) {
        if (auto problem = check_commands(*it, {"do", "undo"}); !problem.empty()) {
          errors[key] = problem;
        }
      }
    }
    if (auto it = app.find("detached"); it != app.end()) {
      if (!it->is_array() || it->size() > 100 || !std::all_of(it->begin(), it->end(), is_text)) {
        errors["detached"] = "Must be a list of commands (text).";
      }
    }
    return errors;
  }

  nlohmann::json validate_device_change(const nlohmann::json &change) {
    nlohmann::json errors = nlohmann::json::object();
    if (!change.is_object()) {
      errors["device"] = "The change must be a JSON object.";
      return errors;
    }

    static const std::set<std::string> known {"name", "display_mode", "perm", "enable_legacy_ordering", "allow_client_commands", "always_use_virtual_display", "do", "undo"};
    for (const auto &[key, value] : change.items()) {
      if (!known.contains(key)) {
        errors[key] = "This field can't be changed.";
      }
    }

    if (auto it = change.find("name"); it != change.end()) {
      if (!it->is_string() || it->get_ref<const std::string &>().find_first_not_of(" \t") == std::string::npos) {
        errors["name"] = "A device needs a name.";
      } else if (it->get_ref<const std::string &>().size() > 256) {
        errors["name"] = "The name is too long.";
      }
    }
    if (auto it = change.find("display_mode"); it != change.end() && (!it->is_string() || it->get_ref<const std::string &>().size() > 64)) {
      errors["display_mode"] = "Must be text such as 1920x1080x60, or empty.";
    }
    if (auto it = change.find("perm"); it != change.end()) {
      if (!it->is_number_unsigned() || (it->get<std::uint64_t>() & ~(std::uint64_t) crypto::PERM::_all) != 0) {
        errors["perm"] = "Must be a combination of the permission bits.";
      }
    }
    for (auto key : {"enable_legacy_ordering", "allow_client_commands", "always_use_virtual_display"}) {
      if (auto it = change.find(key); it != change.end() && !it->is_boolean()) {
        errors[key] = "Must be true or false.";
      }
    }
    for (auto key : {"do", "undo"}) {
      if (auto it = change.find(key); it != change.end()) {
        auto problem = check_commands(*it, {"cmd"});
        if (problem.empty() && !std::all_of(it->begin(), it->end(), [](const auto &item) { return item.contains("cmd"); })) {
          problem = "Each command needs \"cmd\".";
        }
        if (!problem.empty()) {
          errors[key] = problem;
        }
      }
    }
    return errors;
  }

  bool parse_body(const std::string &content, nlohmann::json &out, reply_t &error) {
    out = nlohmann::json::parse(content, nullptr, false);
    if (out.is_discarded()) {
      error = fail(StatusCode::client_error_bad_request, "Invalid JSON");
      return false;
    }
    return true;
  }

  reply_t list_apps() {
    return guarded("Reading apps", [] {
      nlohmann::json file;
      {
        std::lock_guard lock {apps_mutex};
        file = read_apps_file();
      }
      auto running = proc::proc.running() > 0 ? proc::proc.get_running_app_uuid() : ""s;
      auto apps = apps_of(file);
      for (auto &app : apps) {
        if (app.is_object()) {
          app["running"] = !running.empty() && app.value("uuid", ""s) == running;
        }
      }
      return ok({{"apps", apps}, {"running_app", running}});
    });
  }

  reply_t create_app(const nlohmann::json &input) {
    if (auto errors = validate_app(input); !errors.empty()) {
      return invalid(errors);
    }
    return guarded("Adding an app", [&] {
      auto app = stored_form(input);
      app["uuid"] = uuid_util::uuid_t::generate().string();
      std::lock_guard lock {apps_mutex};
      auto file = read_apps_file();
      apps_of(file).push_back(app);
      bool closed = write_apps_file(file);
      BOOST_LOG(info) << "Management API: added app ["sv << app["name"].get<std::string>() << ']';
      return ok({{"uuid", app["uuid"]}, {"closed_running_app", closed}});
    });
  }

  reply_t replace_app(const std::string &uuid, const nlohmann::json &input) {
    if (auto errors = validate_app(input); !errors.empty()) {
      return invalid(errors);
    }
    if (input.contains("uuid") && input["uuid"] != uuid) {
      return invalid({{"uuid", "Doesn't match the app being saved."}});
    }
    return guarded("Saving an app", [&]() -> reply_t {
      std::lock_guard lock {apps_mutex};
      auto file = read_apps_file();
      auto &apps = apps_of(file);
      auto it = find_app(apps, uuid);
      if (it == apps.end()) {
        return fail(StatusCode::client_error_not_found, "There is no app with this ID.");
      }
      *it = stored_form(input);
      (*it)["uuid"] = uuid;
      bool closed = write_apps_file(file);
      return ok({{"uuid", uuid}, {"closed_running_app", closed}});
    });
  }

  reply_t delete_app(const std::string &uuid) {
    return guarded("Removing an app", [&]() -> reply_t {
      std::lock_guard lock {apps_mutex};
      auto file = read_apps_file();
      auto &apps = apps_of(file);
      auto it = find_app(apps, uuid);
      if (it == apps.end()) {
        return fail(StatusCode::client_error_not_found, "There is no app with this ID.");
      }
      apps.erase(it);
      bool closed = write_apps_file(file);
      return ok({{"closed_running_app", closed}});
    });
  }

  reply_t order_apps(const nlohmann::json &input) {
    if (!input.is_object() || !input.contains("order") || !input["order"].is_array() ||
        !std::all_of(input["order"].begin(), input["order"].end(), [](const auto &v) { return v.is_string(); })) {
      return invalid({{"order", "Must be a list of app IDs."}});
    }
    return guarded("Reordering apps", [&] {
      std::lock_guard lock {apps_mutex};
      auto file = read_apps_file();
      auto &apps = apps_of(file);
      // Listed apps first, in the given order; apps left out keep their order after them.
      nlohmann::json ordered = nlohmann::json::array();
      for (const auto &uuid : input["order"]) {
        if (auto it = find_app(apps, uuid.get<std::string>()); it != apps.end()) {
          ordered.push_back(std::move(*it));
          apps.erase(it);
        }
      }
      for (auto &app : apps) {
        ordered.push_back(std::move(app));
      }
      apps = std::move(ordered);
      bool closed = write_apps_file(file);
      return ok({{"closed_running_app", closed}});
    });
  }

  reply_t launch_app(const std::string &uuid) {
    return guarded("Starting an app", [&]() -> reply_t {
      for (auto &app : proc::proc.get_apps()) {
        if (app.uuid != uuid) {
          continue;
        }
        crypto::named_cert_t host_cert {
          .name = "",
          .uuid = http::unique_id,
          .perm = crypto::PERM::_all,
        };
        BOOST_LOG(info) << "Management API: starting app ["sv << app.name << ']';
        auto launch_session = nvhttp::make_launch_session(true, false, {}, &host_cert);
        if (auto err = proc::proc.execute(app, launch_session)) {
          return fail(StatusCode::server_error_service_unavailable, err == 503 ? "Couldn't start video capture. Is a display connected and turned on?" : "The app couldn't be started.");
        }
        return ok();
      }
      return fail(StatusCode::client_error_not_found, "There is no app with this ID.");
    });
  }

  reply_t close_app() {
    proc::proc.terminate();
    return ok();
  }

  reply_t list_devices(std::string_view self_uuid) {
    auto devices = nvhttp::get_all_clients();
    for (auto &device : devices) {
      device["this_device"] = !self_uuid.empty() && device.value("uuid", ""s) == self_uuid;
    }
    return ok({{"devices", devices}});
  }

  reply_t patch_device(const std::string &uuid, const nlohmann::json &change, std::string_view self_uuid) {
    if (auto errors = validate_device_change(change); !errors.empty()) {
      return invalid(errors);
    }
    auto current = find_device(uuid);
    if (!current) {
      return fail(StatusCode::client_error_not_found, "There is no paired device with this ID.");
    }
    if (uuid == self_uuid && change.contains("perm") && change["perm"] != (*current)["perm"]) {
      return fail(StatusCode::client_error_forbidden, "A device can't change its own permissions. Use the host's web UI or another device.");
    }

    auto merged = *current;
    merged.update(change);
    auto ok_saved = nvhttp::update_device_info(
      uuid,
      merged["name"].get<std::string>(),
      merged.value("display_mode", ""s),
      nvhttp::extract_command_entries(merged, "do"),
      nvhttp::extract_command_entries(merged, "undo"),
      static_cast<crypto::PERM>(merged["perm"].get<std::uint32_t>() & static_cast<std::uint32_t>(crypto::PERM::_all)),
      merged.value("enable_legacy_ordering", true),
      merged.value("allow_client_commands", true),
      merged.value("always_use_virtual_display", false)
    );
    if (!ok_saved) {
      return fail(StatusCode::client_error_not_found, "There is no paired device with this ID.");
    }
    return ok({{"device", *find_device(uuid)}});
  }

  reply_t unpair_device(const std::string &uuid) {
    if (!find_device(uuid)) {
      return fail(StatusCode::client_error_not_found, "There is no paired device with this ID.");
    }
    nvhttp::unpair_client(uuid);
    return ok();
  }

  reply_t disconnect_device(const std::string &uuid) {
    if (!find_device(uuid)) {
      return fail(StatusCode::client_error_not_found, "There is no paired device with this ID.");
    }
    return ok({{"disconnected", nvhttp::find_and_stop_session(uuid, true)}});
  }

  reply_t list_displays() {
    return guarded("Listing displays", [] {
      nlohmann::json displays = nlohmann::json::array();
      const auto &selected = config::video.output_name;
      for (const auto &device : display_device::enumerate_devices()) {
        nlohmann::json item {
          {"id", device.m_device_id},
          {"name", device.m_display_name},
          {"friendly_name", device.m_friendly_name},
          {"active", device.m_info.has_value()},
          {"selected", !selected.empty() && (selected == device.m_device_id || selected == device.m_display_name)},
        };
        if (device.m_info) {
          const auto &info = *device.m_info;
          item["width"] = info.m_resolution.m_width;
          item["height"] = info.m_resolution.m_height;
          item["refresh_rate"] = as_number(info.m_refresh_rate);
          item["scale"] = as_number(info.m_resolution_scale);
          item["primary"] = info.m_primary;
          item["hdr"] = info.m_hdr_state ? nlohmann::json(*info.m_hdr_state == display_device::HdrState::Enabled) : nlohmann::json(nullptr);
        }
        displays.push_back(std::move(item));
      }
      return ok({{"displays", displays}, {"output_name", selected}});
    });
  }

  reply_t reset_displays() {
    if (!display_device::reset_persistence()) {
      return fail(StatusCode::server_error_internal_server_error, "Couldn't reset the saved display changes. See the host's log.");
    }
    return ok();
  }
}  // namespace manage_api
