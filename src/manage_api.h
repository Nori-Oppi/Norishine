/**
 * @file src/manage_api.h
 * @brief Apps, devices, sessions and displays for the /api/v1 management API.
 *
 * The same calls are served to paired clients on the GameStream HTTPS port (nvhttp,
 * certificate and permission checked there) and to the web UI (confighttp, signed-in
 * session checked there). These functions only do the work and say what to answer.
 */
#pragma once

// standard includes
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>
#include <Simple-Web-Server/status_code.hpp>

namespace manage_api {
  /**
   * @brief What to answer: an HTTP status and a JSON body with "status" (true or false)
   *        and, on failure, "error" (one message) or "errors" (one per field).
   */
  struct reply_t {
    SimpleWeb::StatusCode code;
    nlohmann::json body;
  };

  /**
   * @brief Features these calls add to serverinfo's Norishine/Features list.
   */
  inline constexpr std::string_view features = "apps,apps.write,devices,devices.write,sessions.write,displays";

  /**
   * @brief Check an app as it would be stored in apps.json.
   * @return One message per problem, keyed by field name; empty when the app is fine.
   *         Fields this host doesn't know are kept as they are and not checked.
   */
  nlohmann::json validate_app(const nlohmann::json &app);

  /**
   * @brief Check a device change for PATCH /api/v1/devices/{uuid}.
   * @return One message per problem, keyed by field name; empty when the change is fine.
   */
  nlohmann::json validate_device_change(const nlohmann::json &change);

  // Apps (apps.json). Changes are written to the file and take effect at once.
  reply_t list_apps();
  reply_t create_app(const nlohmann::json &app);
  reply_t replace_app(const std::string &uuid, const nlohmann::json &app);
  reply_t delete_app(const std::string &uuid);
  reply_t order_apps(const nlohmann::json &input);
  reply_t launch_app(const std::string &uuid);
  reply_t close_app();

  /**
   * @brief Paired devices.
   * @param self_uuid The requesting device, marked "this_device"; empty for the web UI.
   */
  reply_t list_devices(std::string_view self_uuid);

  /**
   * @brief Change some fields of a paired device; fields left out keep their value.
   * @param self_uuid The requesting device. A device can't change its own permissions,
   *        so it can't lock itself out or raise its own rights.
   */
  reply_t patch_device(const std::string &uuid, const nlohmann::json &change, std::string_view self_uuid);
  reply_t unpair_device(const std::string &uuid);

  /**
   * @brief End the stream of a device, if it has one.
   */
  reply_t disconnect_device(const std::string &uuid);

  /**
   * @brief Displays the host can stream, with their current mode.
   */
  reply_t list_displays();

  /**
   * @brief Forget the display changes Norishine would otherwise restore (the web UI's
   *        "reset display device persistence").
   */
  reply_t reset_displays();

  /**
   * @brief Parse a JSON request body; on failure, the reply to send.
   */
  bool parse_body(const std::string &content, nlohmann::json &out, reply_t &error);
}  // namespace manage_api
