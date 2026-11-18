/**
 * @file src/updater.h
 * @brief Declarations for updating the host from its own web UI or a paired client.
 *
 * The host checks GitHub for a newer release, downloads its Windows installer, checks
 * the SHA-256 GitHub publishes for it, and starts `apply-update.ps1` outside the service's
 * process group. The script runs the installer silently (which stops and restarts the
 * service), makes sure the service is running again, and records the result for the new
 * host to report.
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "manage_api.h"

namespace updater {
  /// Where releases come from.
  inline constexpr std::string_view releases_api = "https://api.github.com/repos/Nori-Oppi/Norishine/releases?per_page=20";
  /// Installers are only downloaded from under this address.
  inline constexpr std::string_view download_prefix = "https://github.com/Nori-Oppi/Norishine/releases/download/";
  /// Larger downloads are refused.
  inline constexpr std::uint64_t max_installer_size = 300ull * 1024 * 1024;

  /**
   * @brief A build version: a release ("1.2.3", tags "v1.2.3") or a nightly
   * ("nightly-20261118-8c6c99d"). Anything else (development builds) is `unknown`.
   */
  struct version_t {
    enum class kind_e {
      unknown,
      release,
      nightly,
    } kind = kind_e::unknown;

    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string date;  ///< Nightlies: YYYYMMDD (UTC).
    std::string commit;  ///< Nightlies: short commit.
  };

  version_t parse_version(std::string_view text);

  /**
   * @brief A release this host could install.
   */
  struct release_t {
    std::string version;  ///< From the installer's name, e.g. "nightly-20261118-8c6c99d" or "1.2.0".
    std::string name;  ///< Release title.
    std::string page_url;  ///< Release page.
    std::string published_at;  ///< ISO 8601, UTC.
    bool prerelease = false;
    std::string asset_name;
    std::string asset_url;
    std::string sha256;  ///< Lower-case hex.
    std::uint64_t size = 0;
  };

  /**
   * @brief Read one entry of GitHub's release list. Empty when it has no Windows x64
   * installer, no SHA-256 digest, a download address outside `download_prefix`, or is a draft.
   */
  std::optional<release_t> release_from_json(const nlohmann::json &release);

  /**
   * @brief The newest release that is newer than `current`, if any.
   * @param current This host's version (version::project).
   * @param releases GitHub's release list.
   * @param include_pre_releases Also consider pre-releases (nightlies).
   */
  std::optional<release_t> choose(std::string_view current, const nlohmann::json &releases, bool include_pre_releases);

  /**
   * @brief Read the result of the last update, written by apply-update.ps1.
   * @param status The status file's contents.
   * @param current This host's version.
   * @return `from`, `to`, `result` ("updated" or "failed"), `finished` and `error`; null if none.
   */
  nlohmann::json last_update(const nlohmann::json &status, std::string_view current);

  /**
   * @brief Whether this host can update itself, and why not.
   * @return Empty when it can; otherwise a reason for the user.
   */
  std::optional<std::string> unsupported_reason();

  /**
   * @brief Update state for `GET /api/v1/update`. Checks GitHub when the last check is
   * older than ten minutes, or when `refresh` is set (at most once a minute).
   */
  nlohmann::json status(bool refresh);

  /**
   * @brief Start updating to `version`, which must be the one `status` offers.
   * @return 202 when started; 400 for a malformed body; 409 when this host can't update,
   * an update is running, or `version` isn't the one on offer.
   */
  manage_api::reply_t start(const nlohmann::json &body);
}  // namespace updater
