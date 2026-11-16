/**
 * @file src/state_store.h
 * @brief Where the host keeps its state, and versioned migrations of that layout.
 *
 * Up to version 0 (Apollo, Sunshine and Norishine before M4) everything lived in one
 * file, `sunshine_state.json`: the host's unique ID, the paired devices and, unless
 * `credentials_file` pointed elsewhere, the web UI account. Version 1 splits it:
 *
 *   state/host.json         machine state: unique ID ({"version": 1, "uniqueid": ...})
 *   state/pairing.json      paired devices ({"version": 1, "named_devices": [...]})
 *   state/credentials.json  web UI account ({"version": 1, "username", "password", "salt"})
 *
 * The folder sits next to the old file ("state" for sunshine_state.json, "<name>_state"
 * for any other name). Settings stay in sunshine.conf and apps in apps.json. Migrations
 * only go forward; the old file is kept as `<name>.v0.bak`.
 */
#pragma once

// standard includes
#include <filesystem>
#include <optional>
#include <string>

// lib includes
#include <nlohmann/json.hpp>

namespace state_store {
  constexpr int current_version = 1;

  struct paths_t {
    std::filesystem::path legacy;  ///< The single state file of version 0 (file_state).
    std::filesystem::path dir;  ///< Folder of the split files.
    std::filesystem::path host;
    std::filesystem::path pairing;
    std::filesystem::path credentials;  ///< Web UI account; may be a separate, user-chosen file.
    bool credentials_in_legacy = true;  ///< Version 0 kept the account in the legacy file.
  };

  /**
   * @brief The file layout for the configured file_state and credentials_file.
   * A credentials_file equal to file_state (the default) means "inside the state".
   */
  paths_t paths_for(const std::filesystem::path &file_state, const std::filesystem::path &credentials_file);

  /**
   * @brief The layout for the current configuration (config::nvhttp.file_state and
   *        config::sunshine.credentials_file as read from the config file).
   */
  const paths_t &paths();

  /**
   * @brief Set the layout to use from now on (startup, and tests).
   */
  void set_paths(const paths_t &paths);

  struct migration_result_t {
    bool ok = true;
    int from_version = 0;  ///< Version found on disk (current_version when nothing was to do).
    std::string backup;  ///< The file the old state was kept in, if any.
    std::string error;
  };

  /**
   * @brief Bring the state on disk to current_version.
   *
   * Safe to run on every start: it does nothing when the state is current, and an
   * interrupted migration is completed the next time. When the old file can't be read,
   * nothing is changed and `ok` is false.
   */
  migration_result_t migrate(const paths_t &paths);

  /**
   * @brief Work out the layout from the configuration, migrate it and use it from now on.
   *
   * Points config::sunshine.credentials_file at the account file of the new layout (unless
   * the migration failed). With the FRESH_STATE flag nothing is migrated or written.
   */
  void init();

  /**
   * @brief False when the state could not be migrated: then nothing writes state files
   *        during this run, so the old file is never overwritten or retired.
   */
  bool writable();

  /**
   * @brief Read one of the state files.
   * @return The JSON object, an empty object when the file doesn't exist, or nothing
   *         when it exists but can't be parsed (so callers never overwrite it).
   */
  std::optional<nlohmann::json> read(const std::filesystem::path &file);

  /**
   * @brief Write one of the state files atomically, stamped with current_version (or the
   *        newer version it already had).
   * @return True on success.
   */
  bool write(const std::filesystem::path &file, nlohmann::json content);
}  // namespace state_store
