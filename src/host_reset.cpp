/**
 * @file src/host_reset.cpp
 * @brief Definitions for resetting the host and the setup wizard's state.
 */
// standard includes
#include <algorithm>
#include <atomic>
#include <cctype>
#include <ctime>
#include <cwctype>
#include <format>
#include <iostream>

// local includes
#include "config.h"
#include "entry_handler.h"
#include "host_reset.h"
#include "logging.h"
#include "platform/common.h"
#include "state_store.h"

#ifdef _WIN32
  #include <windows.h>
#endif

// lib includes
#include <nlohmann/json.hpp>

using namespace std::literals;
namespace fs = std::filesystem;

namespace host_reset {
  namespace {
    std::atomic<bool> reset_requested {false};

    fs::path normal(const fs::path &p) {
      std::error_code ec;
      auto absolute = fs::weakly_canonical(p, ec);
      return (ec ? fs::absolute(p, ec) : absolute).lexically_normal();
    }

    bool same_name(const fs::path &a, const fs::path &b) {
#ifdef _WIN32
      auto x = a.wstring();
      auto y = b.wstring();
      return x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin(), [](wchar_t c, wchar_t d) {
               return std::towlower(c) == std::towlower(d);
             });
#else
      return a == b;
#endif
    }

    /// The path of `p` inside `dir`, or empty when it isn't strictly inside.
    fs::path inside(const fs::path &p, const fs::path &dir) {
      auto child = normal(p);
      auto parent = normal(dir);
      auto c = child.begin();
      for (auto d = parent.begin(); d != parent.end(); ++d, ++c) {
        if (d->empty()) {
          continue;  // trailing separator
        }
        if (c == child.end() || !same_name(*c, *d)) {
          return {};
        }
      }
      fs::path rel;
      for (; c != child.end(); ++c) {
        rel /= *c;
      }
      return rel;
    }

    fs::path setup_file() {
      return state_store::paths().dir / "setup.json";
    }

#ifdef _WIN32
    /// Runs scripts/other-hosts.ps1 next to sunshine.exe and returns its exit code and output.
    std::optional<std::pair<int, std::string>> run_other_hosts(const std::wstring &arguments) {
      WCHAR exe[MAX_PATH];
      GetModuleFileNameW(nullptr, exe, _countof(exe));
      auto root = fs::path {exe}.parent_path();
      auto script = root / "scripts" / "other-hosts.ps1";
      std::error_code ec;
      if (!fs::exists(script, ec)) {
        BOOST_LOG(warning) << "Setup: "sv << script.string() << " is missing"sv;
        return std::nullopt;
      }

      WCHAR system[MAX_PATH];
      GetSystemDirectoryW(system, _countof(system));
      auto powershell = fs::path {system} / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
      auto quote = [](const fs::path &p) {
        return L"\"" + p.wstring() + L"\"";
      };
      std::wstring command = quote(powershell) + L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + quote(script) +
                             L" -Root " + quote(root) + L" " + arguments;

      SECURITY_ATTRIBUTES sa {sizeof(sa), nullptr, TRUE};
      HANDLE read_end = nullptr;
      HANDLE write_end = nullptr;
      if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
        return std::nullopt;
      }
      SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

      STARTUPINFOW startup {sizeof(startup)};
      startup.dwFlags = STARTF_USESTDHANDLES;
      startup.hStdOutput = write_end;
      startup.hStdError = write_end;
      startup.hStdInput = nullptr;
      PROCESS_INFORMATION process {};
      bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, root.c_str(), &startup, &process);
      CloseHandle(write_end);
      if (!started) {
        CloseHandle(read_end);
        return std::nullopt;
      }

      std::string output;
      char buffer[4096];
      DWORD got = 0;
      while (ReadFile(read_end, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
        output.append(buffer, got);
      }
      CloseHandle(read_end);
      WaitForSingleObject(process.hProcess, 120000);
      DWORD code = 1;
      GetExitCodeProcess(process.hProcess, &code);
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
      return std::make_pair(static_cast<int>(code), output);
    }
#endif
  }  // namespace

  std::vector<fs::path> targets_for(
    const fs::path &config_file,
    const fs::path &apps_file,
    const fs::path &state_legacy,
    const fs::path &state_dir,
    const fs::path &credentials_file,
    const fs::path &pkey,
    const fs::path &cert,
    const fs::path &covers_dir
  ) {
    std::vector<fs::path> all;
    for (const auto &p : {config_file, apps_file, state_legacy, state_dir, credentials_file, pkey, cert, covers_dir}) {
      if (!p.empty()) {
        all.push_back(p);
      }
    }
    // The copies kept next to the settings and apps: write backups, the import's, version 0's.
    for (const auto &p : {config_file, apps_file, state_legacy}) {
      for (auto suffix : {".bak", ".before-import", ".v0.bak"}) {
        if (!p.empty()) {
          auto copy = p;
          copy += suffix;
          all.push_back(copy);
        }
      }
    }

    std::vector<fs::path> result;
    for (const auto &p : all) {
      bool covered = false;
      for (const auto &other : all) {
        if (&other != &p && !inside(p, other).empty()) {
          covered = true;  // moves with the folder it is in
        }
      }
      bool duplicate = std::any_of(result.begin(), result.end(), [&](const fs::path &r) {
        return same_name(normal(r), normal(p));
      });
      if (!covered && !duplicate) {
        result.push_back(p);
      }
    }
    return result;
  }

  std::vector<fs::path> targets() {
    const auto &state = state_store::paths();
    return targets_for(
      config::sunshine.config_file,
      config::stream.file_apps,
      state.legacy,
      state.dir,
      state.credentials,
      config::nvhttp.pkey,
      config::nvhttp.cert,
      platf::appdata() / "covers"
    );
  }

  fs::path base() {
    return normal(config::sunshine.config_file).parent_path();
  }

  std::string stamp(std::chrono::system_clock::time_point when) {
    auto t = std::chrono::system_clock::to_time_t(when);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
    return text;
  }

  result_t move_to_backup(const std::vector<fs::path> &list, const fs::path &base_dir, const std::string &when) {
    result_t result;
    std::error_code ec;

    auto name = "Norishine-backup-" + when;
    result.backup = base_dir / name;
    for (int n = 2; fs::exists(result.backup, ec); ++n) {
      result.backup = base_dir / std::format("{}-{}", name, n);
    }

    for (const auto &from : list) {
      if (!fs::exists(from, ec)) {
        continue;
      }
      fs::path to;
      if (auto rel = inside(from, base_dir); !rel.empty()) {
        to = result.backup / rel;
        fs::create_directories(to.parent_path(), ec);
        if (ec) {
          result.ok = false;
          result.error = "can't create " + to.parent_path().string() + ": " + ec.message();
          return result;
        }
      } else {
        to = from;
        to += ".backup-" + when;
      }
      fs::rename(from, to, ec);
      if (ec) {
        result.ok = false;
        result.error = "can't move " + from.string() + ": " + ec.message();
        return result;
      }
      result.moved.emplace_back(from, to);
    }
    return result;
  }

  void request() {
    reset_requested = true;
  }

  bool requested() {
    return reset_requested;
  }

  void finish_if_requested() {
    if (!reset_requested) {
      return;
    }
    auto result = move_to_backup(targets(), base(), stamp(std::chrono::system_clock::now()));
    for (const auto &[from, to] : result.moved) {
      BOOST_LOG(info) << "Reset: moved "sv << from.string() << " to "sv << to.string();
    }
    if (result.ok) {
      BOOST_LOG(info) << "Reset: done; the backup is "sv << result.backup.string();
    } else {
      BOOST_LOG(error) << "Reset: stopped, "sv << result.error << ". Nothing was deleted."sv;
    }
  }

  int reset_from_command_line() {
#ifdef _WIN32
    bool service = service_ctrl::is_service_running();
    if (service) {
      std::cout << "Stopping Norishine..."sv << std::endl;
      if (!service_ctrl::stop_service()) {
        std::cout << "Couldn't stop the Norishine service. Run this as administrator."sv << std::endl;
        return 1;
      }
    } else if (service_ctrl::is_ui_port_listening()) {
      std::cout << "Norishine is running. Quit it first, then run this again."sv << std::endl;
      return 1;
    }
#endif

    auto result = move_to_backup(targets(), base(), stamp(std::chrono::system_clock::now()));
    for (const auto &[from, to] : result.moved) {
      std::cout << "Moved "sv << from.string() << " to "sv << to.string() << std::endl;
    }
    int code = 0;
    if (result.ok) {
      std::cout << "Norishine is reset. The backup is "sv << result.backup.string() << std::endl;
    } else {
      std::cout << "Reset stopped: "sv << result.error << ". Nothing was deleted; run this as administrator."sv << std::endl;
      code = 1;
    }

#ifdef _WIN32
    if (service) {
      service_ctrl::start_service();
      std::cout << "Open the web UI to set it up again."sv << std::endl;
    }
#endif
    return code;
  }

  bool setup_pending() {
    auto content = state_store::read(setup_file());
    return content && content->value("pending", false);
  }

  void set_setup_pending(bool pending) {
    if (!state_store::writable()) {
      return;
    }
    state_store::write(setup_file(), {{"pending", pending}});
  }

  std::optional<other_host_t> find_other_host() {
#ifdef _WIN32
    auto run = run_other_hosts(L"-Action find");
    if (!run || run->first != 0) {
      return std::nullopt;
    }
    // The last line is the answer: JSON, or "null".
    auto text = run->second;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
      text.pop_back();
    }
    auto newline = text.find_last_of('\n');
    auto line = text.substr(newline == std::string::npos ? 0 : newline + 1);
    auto found = nlohmann::json::parse(line, nullptr, false);
    if (!found.is_object() || !found.contains("name")) {
      return std::nullopt;
    }
    return other_host_t {found.value("name", ""s), found.value("installed", false)};
#else
    return std::nullopt;
#endif
  }

  import_result_t import_other_host_settings() {
    import_result_t result;
#ifdef _WIN32
    auto quote = [](const std::string &p) {
      return L"\"" + fs::path {p}.wstring() + L"\"";
    };
    auto run = run_other_hosts(L"-Action import-settings -ConfigFile " + quote(config::sunshine.config_file) + L" -AppsFile " + quote(config::stream.file_apps));
    if (!run) {
      result.log = "Couldn't run the import script.";
      return result;
    }
    result.ok = run->first == 0;
    result.log = run->second;
    BOOST_LOG(info) << "Setup: import from another host ("sv << run->first << "): "sv << run->second;
#else
    result.log = "Importing is only available on Windows.";
#endif
    return result;
  }
}  // namespace host_reset
