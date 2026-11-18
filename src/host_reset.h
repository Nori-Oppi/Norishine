/**
 * @file src/host_reset.h
 * @brief Resetting the host (roadmap D9) and the first-start setup wizard's state.
 *
 * A reset never deletes: the settings file, apps, state (unique ID, paired devices, web UI
 * account, sessions, recommendations), the host's certificate and the app covers are moved
 * to `Norishine-backup-<date>-<time>` in the folder of the settings file, keeping their
 * places relative to that folder. Files kept elsewhere (custom paths) are renamed next to
 * where they are, to `<name>.backup-<date>-<time>`. Moving the backup folder's contents
 * back undoes the reset.
 *
 * The web UI asks for a reset with request(); the files move when the host has stopped
 * (finish_if_requested() at the end of main), and the host then starts again with no
 * account, which opens the setup wizard.
 */
#pragma once

// standard includes
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace host_reset {
  struct result_t {
    bool ok = true;
    std::filesystem::path backup;  ///< The backup folder (may not exist when nothing was in the base folder).
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved;  ///< From, to.
    std::string error;
  };

  /**
   * @brief What a reset moves, from the given locations. Duplicates and paths inside
   *        another listed folder are left out.
   */
  std::vector<std::filesystem::path> targets_for(
    const std::filesystem::path &config_file,
    const std::filesystem::path &apps_file,
    const std::filesystem::path &state_legacy,
    const std::filesystem::path &state_dir,
    const std::filesystem::path &credentials_file,
    const std::filesystem::path &pkey,
    const std::filesystem::path &cert,
    const std::filesystem::path &covers_dir
  );

  /**
   * @brief targets_for() with the running host's configuration.
   */
  std::vector<std::filesystem::path> targets();

  /**
   * @brief The folder the backup goes into: the folder of the settings file.
   */
  std::filesystem::path base();

  /**
   * @brief "20261118-174200" for the given time, in local time.
   */
  std::string stamp(std::chrono::system_clock::time_point when);

  /**
   * @brief Move the targets that exist to a backup named with `stamp`.
   *
   * Stops at the first file that can't be moved and reports it; files already moved stay
   * in the backup (nothing is deleted either way).
   */
  result_t move_to_backup(const std::vector<std::filesystem::path> &targets, const std::filesystem::path &base, const std::string &stamp);

  /**
   * @brief Reset when this run of the host ends.
   */
  void request();

  bool requested();

  /**
   * @brief Called once the host has stopped: does the requested reset, if any.
   */
  void finish_if_requested();

  /**
   * @brief The command line: `norishine --reset`. Stops the service if it runs, moves the
   *        files, and starts the service again.
   * @return Exit code.
   */
  int reset_from_command_line();

  /**
   * @brief Whether the first-start setup wizard is still to be finished. Set when the web
   *        UI account is created on a host without one, cleared when the wizard ends.
   */
  bool setup_pending();

  void set_setup_pending(bool pending);

  /**
   * @brief An Apollo or Vibepollo on this PC whose settings the wizard can import.
   */
  struct other_host_t {
    std::string name;
    bool installed = false;  ///< Still installed (else its configuration was left behind).
  };

  /**
   * @brief Look for one with scripts/other-hosts.ps1 (-Action find). Windows only.
   */
  std::optional<other_host_t> find_other_host();

  struct import_result_t {
    bool ok = false;
    std::string log;  ///< The script's output.
  };

  /**
   * @brief Copy its settings and apps over this host's, with other-hosts.ps1
   *        (-Action import-settings). The previous files are kept as `<name>.before-import`.
   */
  import_result_t import_other_host_settings();
}  // namespace host_reset
