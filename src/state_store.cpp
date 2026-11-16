/**
 * @file src/state_store.cpp
 * @brief Definitions for the state layout and its migrations.
 */
// standard includes
#include <fstream>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>

// local includes
#include "config.h"
#include "file_handler.h"
#include "logging.h"
#include "state_store.h"

using namespace std::literals;
namespace fs = std::filesystem;
using nlohmann::json;

namespace state_store {
  namespace {
    std::mutex paths_mutex;
    paths_t current_paths;
    std::atomic<bool> state_writable {true};

    bool same_file(const fs::path &a, const fs::path &b) {
      std::error_code ec;
      if (fs::exists(a, ec) && fs::exists(b, ec)) {
        return fs::equivalent(a, b, ec);
      }
      return fs::weakly_canonical(a, ec) == fs::weakly_canonical(b, ec);
    }

    json without_version(json content) {
      content.erase("version");
      return content;
    }

    /**
     * @brief Version 0 to 1: split the single state file.
     */
    bool split_v0(const paths_t &p, migration_result_t &result) {
      json legacy = json::object();
      std::error_code ec;
      if (fs::exists(p.legacy, ec)) {
        auto parsed = read(p.legacy);
        if (!parsed) {
          result.error = "can't read " + p.legacy.string();
          return false;
        }
        legacy = std::move(*parsed);
      }

      const json root = legacy.contains("root") && legacy["root"].is_object() ? legacy["root"] : json::object();

      // Paired devices. Apollo's "named_devices", and Sunshine's older "devices" list,
      // move as they are; nvhttp converts the old list when it loads them.
      json pairing = json::object();
      for (auto key : {"named_devices"s, "devices"s}) {
        if (root.contains(key)) {
          pairing[key] = root[key];
        }
      }

      // Machine state. Without a unique ID the host makes one when it starts.
      json host = json::object();
      if (root.contains("uniqueid")) {
        host["uniqueid"] = root["uniqueid"];
      }

      // Web UI account, when it was kept in the state file.
      std::optional<json> credentials;
      if (p.credentials_in_legacy && legacy.contains("username") && legacy.contains("password") && legacy.contains("salt")) {
        credentials = json {{"username", legacy["username"]}, {"password", legacy["password"]}, {"salt", legacy["salt"]}};
      }

      // Write the account and devices first, and host.json (which marks version 1) last,
      // so an interrupted migration runs again from the start.
      std::error_code mk;
      fs::create_directories(p.dir, mk);
      if (credentials && !write(p.credentials, *credentials)) {
        result.error = "can't write " + p.credentials.string();
        return false;
      }
      if (!write(p.pairing, pairing)) {
        result.error = "can't write " + p.pairing.string();
        return false;
      }
      if (!write(p.host, host)) {
        result.error = "can't write " + p.host.string();
        return false;
      }
      return true;
    }

    struct migration_t {
      int from;
      const char *description;
      std::function<bool(const paths_t &, migration_result_t &)> run;
    };

    const std::vector<migration_t> &migrations() {
      static const std::vector<migration_t> list {
        {0, "split the state file into host, pairing and credentials", split_v0},
      };
      return list;
    }

    int version_on_disk(const paths_t &p) {
      if (auto host = read(p.host); host && host->contains("version") && (*host)["version"].is_number_integer()) {
        return (*host)["version"].get<int>();
      }
      return 0;
    }

    // Keep the version 0 file as a backup, out of the way of the new layout.
    bool retire_legacy(const paths_t &p, migration_result_t &result) {
      std::error_code ec;
      if (!fs::exists(p.legacy, ec)) {
        return true;
      }
      fs::path backup = p.legacy;
      backup += ".v0.bak";
      if (fs::exists(backup, ec)) {
        backup = p.legacy;
        backup += ".v0." + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".bak";
      }
      fs::rename(p.legacy, backup, ec);
      if (ec) {
        result.error = "can't rename " + p.legacy.string() + ": " + ec.message();
        return false;
      }
      result.backup = backup.string();
      return true;
    }
  }  // namespace

  paths_t paths_for(const fs::path &file_state, const fs::path &credentials_file) {
    paths_t p;
    p.legacy = file_state;
    const auto stem = file_state.stem().string();
    p.dir = file_state.parent_path() / (stem == "sunshine_state" ? "state"s : stem + "_state");
    p.host = p.dir / "host.json";
    p.pairing = p.dir / "pairing.json";
    p.credentials_in_legacy = credentials_file.empty() || same_file(credentials_file, file_state);
    p.credentials = p.credentials_in_legacy ? p.dir / "credentials.json" : credentials_file;
    return p;
  }

  const paths_t &paths() {
    std::lock_guard lock {paths_mutex};
    return current_paths;
  }

  void set_paths(const paths_t &p) {
    std::lock_guard lock {paths_mutex};
    current_paths = p;
  }

  migration_result_t migrate(const paths_t &p) {
    migration_result_t result;
    result.from_version = version_on_disk(p);
    if (result.from_version > current_version) {
      BOOST_LOG(warning) << "State in "sv << p.dir.string() << " is from a newer Norishine (version "sv << result.from_version << "); using it as it is"sv;
      return result;
    }

    for (const auto &m : migrations()) {
      if (m.from < result.from_version) {
        continue;
      }
      BOOST_LOG(info) << "State: migrating from version "sv << m.from << ": "sv << m.description;
      if (!m.run(p, result)) {
        result.ok = false;
        BOOST_LOG(error) << "State migration from version "sv << m.from << " failed: "sv << result.error << ". Nothing was removed."sv;
        return result;
      }
    }

    // Also completes a migration that stopped between writing host.json and the rename.
    if (!retire_legacy(p, result)) {
      result.ok = false;
      BOOST_LOG(error) << "State migration: "sv << result.error;
      return result;
    }
    if (!result.backup.empty()) {
      BOOST_LOG(info) << "State: the previous state file is kept as "sv << result.backup;
    }
    return result;
  }

  void init() {
    auto p = paths_for(config::nvhttp.file_state, config::sunshine.credentials_file);
    set_paths(p);
    if (config::sunshine.flags[config::flag::FRESH_STATE]) {
      state_writable = false;
      return;
    }
    state_writable = migrate(p).ok;
    // After a failed migration the account is still read from where it was, so the web UI
    // never offers to create a new one while the old one exists.
    if (state_writable) {
      config::sunshine.credentials_file = p.credentials.string();
    }
  }

  bool writable() {
    return state_writable;
  }

  std::optional<json> read(const fs::path &file) {
    std::error_code ec;
    if (!fs::exists(file, ec)) {
      return json::object();
    }
    auto content = file_handler::read_file(file.string().c_str());
    auto parsed = json::parse(content, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
      BOOST_LOG(error) << "Couldn't read "sv << file.string();
      return std::nullopt;
    }
    return parsed;
  }

  bool write(const fs::path &file, json content) {
    // Never lower the version of a file a newer Norishine wrote.
    int version = current_version;
    if (content.contains("version") && content["version"].is_number_integer()) {
      version = std::max(version, content["version"].get<int>());
    }
    content = without_version(std::move(content));
    json stamped {{"version", version}};
    stamped.update(content);
    std::error_code ec;
    if (file.has_parent_path()) {
      fs::create_directories(file.parent_path(), ec);
    }
    return file_handler::write_file(file.string().c_str(), stamped.dump(4)) == 0;
  }
}  // namespace state_store
