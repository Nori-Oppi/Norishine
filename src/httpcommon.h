/**
 * @file src/httpcommon.h
 * @brief Declarations for common HTTP.
 */
#pragma once

// standard includes
#include <string>
#include <string_view>

// lib includes
#include <curl/curl.h>

// local includes
#include "network.h"
#include "thread_safe.h"
#include "uuid.h"

namespace http {

  int init();
  int create_creds(const std::string &pkey, const std::string &cert);
  int save_user_creds(
    const std::string &file,
    const std::string &username,
    const std::string &password,
    bool run_our_mouth = false
  );

  int reload_user_creds(const std::string &file);
  bool download_file(const std::string &url, const std::string &file, long ssl_version = CURL_SSLVERSION_TLSv1_2);
  std::string url_escape(const std::string &url);
  std::string url_get_host(const std::string &url);

  /**
   * @brief Text written to the log in place of a sensitive value.
   */
  constexpr std::string_view REDACTED = "[redacted]";

  /**
   * @brief Whether a request header carries a credential and must not be logged.
   * @param name Header name, compared case-insensitively.
   */
  bool is_sensitive_header(std::string_view name);

  /**
   * @brief Whether a GameStream query parameter carries a key or pairing secret
   * and must not be logged.
   * @param name Parameter name, compared case-insensitively.
   */
  bool is_sensitive_query_arg(std::string_view name);

  /**
   * @brief Return `value`, or REDACTED if `sensitive` is set.
   */
  inline std::string_view redact_if(bool sensitive, std::string_view value) {
    return sensitive ? REDACTED : value;
  }

  extern std::string unique_id;
  extern uuid_util::uuid_t uuid;
  extern net::net_e origin_web_ui_allowed;

}  // namespace http
