/**
 * @file src/updater.cpp
 * @brief Definitions for updating the host from its own web UI or a paired client.
 */
// standard includes
#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <thread>

// lib includes
#include <curl/curl.h>
#include <openssl/evp.h>

// platform includes
#ifdef _WIN32
  #include <windows.h>
#endif

// local includes
#include "config.h"
#include "logging.h"
#include "platform/common.h"
#include "state_store.h"
#include "updater.h"
#include "version.h"

#ifdef _WIN32
  #include "platform/windows/misc.h"
#endif

using namespace std::literals;
namespace fs = std::filesystem;

namespace updater {
  namespace {
    bool parse_int(std::string_view text, int &out) {
      auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
      return ec == std::errc {} && ptr == text.data() + text.size();
    }

    /// "2026-11-18T01:42:35Z" -> "20261118"
    std::string date_of(std::string_view iso) {
      if (iso.size() < 10) {
        return {};
      }
      std::string date;
      date.append(iso.substr(0, 4)).append(iso.substr(5, 2)).append(iso.substr(8, 2));
      return date;
    }

    bool newer_release(const version_t &a, const version_t &b) {
      return std::tie(a.major, a.minor, a.patch) > std::tie(b.major, b.minor, b.patch);
    }

    /// Whether `candidate` is newer than the running `current` build.
    bool is_newer(const version_t &current, const release_t &candidate, const nlohmann::json &releases) {
      auto cand = parse_version(candidate.version);
      using enum version_t::kind_e;
      if (cand.kind == unknown) {
        return false;
      }
      if (current.kind == release && cand.kind == release) {
        return newer_release(cand, current);
      }
      if (current.kind == nightly && cand.kind == nightly) {
        return cand.date > current.date || (cand.date == current.date && cand.commit != current.commit);
      }
      if (current.kind == nightly) {
        // A release is newer than a nightly built before the day it came out.
        return date_of(candidate.published_at) > current.date;
      }
      // A nightly is newer than the running release when it was built after that release came out.
      std::string released;
      for (const auto &r : releases) {
        auto info = release_from_json(r);
        if (info && parse_version(info->version).kind == release && !newer_release(parse_version(info->version), current) && !newer_release(current, parse_version(info->version))) {
          released = date_of(info->published_at);
        }
      }
      return cand.date > released;
    }

    std::string iso_now() {
      auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
      return std::format("{:%FT%TZ}", now);
    }

    size_t write_string(char *data, size_t size, size_t count, void *user) {
      static_cast<std::string *>(user)->append(data, size * count);
      return size * count;
    }

    CURL *new_request(const std::string &url) {
      CURL *curl = curl_easy_init();
      if (!curl) {
        return nullptr;
      }
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "Norishine-updater");
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
      curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
      curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
      curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
      curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
#ifdef _WIN32
      curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif
      return curl;
    }

    std::optional<std::string> fetch_text(const std::string &url, std::string &error) {
      CURL *curl = new_request(url);
      if (!curl) {
        error = "Couldn't start a download.";
        return std::nullopt;
      }
      std::string body;
      struct curl_slist *headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
      auto result = curl_easy_perform(curl);
      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
      if (result != CURLE_OK) {
        error = std::string {"Couldn't reach GitHub: "} + curl_easy_strerror(result);
        return std::nullopt;
      }
      return body;
    }

    fs::path updates_dir() {
      return platf::appdata() / "updates";
    }

    fs::path status_file() {
      return updates_dir() / "update-status.json";
    }

    enum class state_e {
      idle,
      downloading,
      installing,
      failed,
    };

    std::string_view to_string(state_e state) {
      switch (state) {
        case state_e::downloading:
          return "downloading";
        case state_e::installing:
          return "installing";
        case state_e::failed:
          return "failed";
        default:
          return "idle";
      }
    }

    struct {
      std::mutex mutex;
      // Last check
      std::optional<std::chrono::steady_clock::time_point> checked;
      std::string checked_at;
      std::string check_error;
      nlohmann::json releases;
      bool checked_pre_releases = false;
      // Running update
      state_e state = state_e::idle;
      std::string error;
      std::uint64_t bytes = 0;
      std::uint64_t total = 0;
      std::optional<release_t> target;
    } shared;

    struct download_t {
      EVP_MD_CTX *sha = nullptr;
      std::ofstream *file = nullptr;
      std::uint64_t bytes = 0;
      std::uint64_t limit = 0;
      bool too_large = false;
    };

    size_t write_download(char *data, size_t size, size_t count, void *user) {
      auto *d = static_cast<download_t *>(user);
      auto n = size * count;
      d->bytes += n;
      if (d->bytes > d->limit) {
        d->too_large = true;
        return 0;  // aborts the transfer
      }
      EVP_DigestUpdate(d->sha, data, n);
      d->file->write(data, static_cast<std::streamsize>(n));
      {
        std::lock_guard lg {shared.mutex};
        shared.bytes = d->bytes;
      }
      return d->file->good() ? n : 0;
    }

    /// Downloads to `target` and checks size and SHA-256. Returns an error, or empty.
    std::string download(const release_t &release, const fs::path &target) {
      auto part = fs::path {target}.concat(".part");
      std::ofstream file {part, std::ios::binary | std::ios::trunc};
      if (!file) {
        return "Couldn't write to " + part.string() + ".";
      }
      std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> sha {EVP_MD_CTX_new(), EVP_MD_CTX_free};
      EVP_DigestInit_ex(sha.get(), EVP_sha256(), nullptr);

      download_t d {sha.get(), &file, 0, std::min(release.size, max_installer_size)};
      CURL *curl = new_request(release.asset_url);
      if (!curl) {
        return "Couldn't start the download.";
      }
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_download);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &d);
      // Give up if it stalls for a minute.
      curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
      curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
      auto result = curl_easy_perform(curl);
      curl_easy_cleanup(curl);
      file.close();

      auto fail = [&](std::string message) {
        std::error_code ec;
        fs::remove(part, ec);
        return message;
      };
      if (d.too_large) {
        return fail("The download was larger than the release says.");
      }
      if (result != CURLE_OK) {
        return fail(std::string {"The download failed: "} + curl_easy_strerror(result));
      }
      if (d.bytes != release.size) {
        return fail("The download is incomplete.");
      }
      unsigned char digest[EVP_MAX_MD_SIZE];
      unsigned int length = 0;
      EVP_DigestFinal_ex(sha.get(), digest, &length);
      std::string hex;
      for (unsigned int i = 0; i < length; ++i) {
        hex += std::format("{:02x}", digest[i]);
      }
      if (hex != release.sha256) {
        return fail("The download doesn't match the checksum GitHub published for it.");
      }
      std::error_code ec;
      fs::rename(part, target, ec);
      if (ec) {
        return fail("Couldn't save the installer: " + ec.message());
      }
      return {};
    }

#ifdef _WIN32
    fs::path exe_dir() {
      WCHAR path[MAX_PATH];
      GetModuleFileNameW(nullptr, path, _countof(path));
      return fs::path {path}.parent_path();
    }

    /// Starts apply-update.ps1 outside the service's job, so stopping the service doesn't end it.
    std::string launch_installer(const fs::path &installer) {
      std::error_code ec;
      auto script = updates_dir() / "apply-update.ps1";
      // Run a copy: the installer replaces the scripts folder while the script runs.
      fs::copy_file(exe_dir() / "scripts" / "apply-update.ps1", script, fs::copy_options::overwrite_existing, ec);
      if (ec) {
        return "Couldn't prepare the update script: " + ec.message();
      }

      WCHAR system[MAX_PATH];
      GetSystemDirectoryW(system, _countof(system));
      auto powershell = fs::path {system} / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
      auto quote = [](const fs::path &p) {
        return L"\"" + p.wstring() + L"\"";
      };
      std::wstring command = quote(powershell) + L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + quote(script) +
                             L" -Installer " + quote(installer) + L" -InstallDir " + quote(exe_dir()) + L" -StatusFile " + quote(status_file());

      STARTUPINFOW startup {sizeof(startup)};
      PROCESS_INFORMATION process {};
      auto flags = CREATE_BREAKAWAY_FROM_JOB | CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT;
      if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags, nullptr, updates_dir().c_str(), &startup, &process)) {
        auto error = GetLastError();
        return std::format("Couldn't start the update (Windows error {}).", error);
      }
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
      return {};
    }
#endif

    void run_update(release_t release) {
      std::error_code ec;
      fs::create_directories(updates_dir(), ec);
      // Only the newest installer is kept.
      for (const auto &entry : fs::directory_iterator {updates_dir(), ec}) {
        if (entry.path().extension() == ".exe" || entry.path().extension() == ".part") {
          fs::remove(entry.path(), ec);
        }
      }

      auto installer = updates_dir() / release.asset_name;
      BOOST_LOG(info) << "Update: downloading "sv << release.asset_url;
      auto failure = download(release, installer);

      if (failure.empty()) {
        state_store::write(status_file(), {
                                            {"state", "installing"},
                                            {"from", version::project},
                                            {"to", release.version},
                                            {"started", iso_now()},
                                          });
#ifdef _WIN32
        failure = launch_installer(installer);
#else
        failure = "Updating is only supported on Windows.";
#endif
      }

      std::lock_guard lg {shared.mutex};
      if (!failure.empty()) {
        BOOST_LOG(error) << "Update to "sv << release.version << " failed: "sv << failure;
        shared.state = state_e::failed;
        shared.error = failure;
        state_store::write(status_file(), {
                                            {"state", "failed"},
                                            {"from", version::project},
                                            {"to", release.version},
                                            {"finished", iso_now()},
                                            {"error", failure},
                                          });
        return;
      }
      BOOST_LOG(info) << "Update: installing "sv << release.version << "; the host restarts when it's done"sv;
      shared.state = state_e::installing;
    }

    /// Refreshes the release list if needed. The caller holds shared.mutex.
    void check_unlocked(bool refresh) {
      auto now = std::chrono::steady_clock::now();
      bool pre = config::sunshine.notify_pre_releases;
      bool stale = !shared.checked || now - *shared.checked > 10min || pre != shared.checked_pre_releases;
      bool may_refresh = !shared.checked || now - *shared.checked > 1min;
      if (!stale && !(refresh && may_refresh)) {
        return;
      }
      std::string error;
      auto body = fetch_text(std::string {releases_api}, error);
      shared.checked = now;
      shared.checked_at = iso_now();
      shared.checked_pre_releases = pre;
      if (!body) {
        shared.check_error = error;
        return;
      }
      auto parsed = nlohmann::json::parse(*body, nullptr, false);
      if (!parsed.is_array()) {
        shared.check_error = "GitHub answered with something other than a release list.";
        return;
      }
      shared.check_error.clear();
      shared.releases = std::move(parsed);
    }

    nlohmann::json release_json(const release_t &r) {
      return {
        {"version", r.version},
        {"name", r.name},
        {"url", r.page_url},
        {"published_at", r.published_at},
        {"prerelease", r.prerelease},
        {"size", r.size},
      };
    }
  }  // namespace

  version_t parse_version(std::string_view text) {
    version_t v;
    static const std::regex nightly {R"(^nightly-(\d{8})-([0-9a-f]{7,40})$)"};
    static const std::regex release {R"(^v?(\d+)\.(\d+)\.(\d+)$)"};
    std::string s {text};
    std::smatch m;
    if (std::regex_match(s, m, nightly)) {
      v.kind = version_t::kind_e::nightly;
      v.date = m[1];
      v.commit = m[2];
    } else if (std::regex_match(s, m, release) && parse_int(m[1].str(), v.major) && parse_int(m[2].str(), v.minor) && parse_int(m[3].str(), v.patch)) {
      v.kind = version_t::kind_e::release;
    }
    return v;
  }

  std::optional<release_t> release_from_json(const nlohmann::json &r) {
    if (!r.is_object() || r.value("draft", false)) {
      return std::nullopt;
    }
    static const std::regex installer {R"(^Norishine-Windows-x64-(.+)-Setup\.exe$)"};
    static const std::regex sha {R"(^sha256:([0-9a-f]{64})$)"};
    auto assets = r.find("assets");
    if (assets == r.end() || !assets->is_array()) {
      return std::nullopt;
    }
    for (const auto &a : *assets) {
      if (!a.is_object()) {
        continue;
      }
      std::string name = a.value("name", "");
      std::string url = a.value("browser_download_url", "");
      std::string digest = a.contains("digest") && a["digest"].is_string() ? a["digest"].get<std::string>() : "";
      std::smatch name_match;
      std::smatch sha_match;
      if (!std::regex_match(name, name_match, installer) || !std::regex_match(digest, sha_match, sha) || !url.starts_with(download_prefix)) {
        continue;
      }
      auto size = a.value("size", std::uint64_t {0});
      if (size == 0 || size > max_installer_size) {
        continue;
      }
      release_t result;
      result.version = name_match[1];
      result.name = r.value("name", "");
      result.page_url = r.value("html_url", "");
      result.published_at = r.value("published_at", "");
      result.prerelease = r.value("prerelease", false);
      result.asset_name = name;
      result.asset_url = url;
      result.sha256 = sha_match[1];
      result.size = size;
      return result;
    }
    return std::nullopt;
  }

  std::optional<release_t> choose(std::string_view current, const nlohmann::json &releases, bool include_pre_releases) {
    auto cur = parse_version(current);
    if (cur.kind == version_t::kind_e::unknown || !releases.is_array()) {
      return std::nullopt;
    }
    std::optional<release_t> best;
    for (const auto &r : releases) {
      auto info = release_from_json(r);
      if (!info || (info->prerelease && !include_pre_releases) || info->version == current) {
        continue;
      }
      if (!is_newer(cur, *info, releases)) {
        continue;
      }
      if (!best || info->published_at > best->published_at) {
        best = std::move(info);
      }
    }
    return best;
  }

  nlohmann::json last_update(const nlohmann::json &status, std::string_view current) {
    if (!status.is_object() || !status.contains("to")) {
      return nullptr;
    }
    std::string state = status.value("state", "");
    std::string to = status.value("to", "");
    nlohmann::json result = {
      {"from", status.value("from", "")},
      {"to", to},
      {"finished", status.value("finished", status.value("started", ""))},
      {"error", nullptr},
    };
    if (to == current && state != "failed") {
      result["result"] = "updated";
    } else if (state == "installing") {
      // The script didn't finish, and this isn't the new version.
      result["result"] = "failed";
      result["error"] = "The update didn't finish. This host is still " + std::string {current} + ".";
    } else if (state == "installed") {
      result["result"] = "failed";
      result["error"] = "The installer finished, but this host is still " + std::string {current} + ".";
    } else {
      result["result"] = "failed";
      result["error"] = status.value("error", "The update failed.");
    }
    return result;
  }

  std::optional<std::string> unsupported_reason() {
#ifdef _WIN32
    if (parse_version(version::project).kind == version_t::kind_e::unknown) {
      return "This is a development build; install a release or nightly to update from here.";
    }
    std::error_code ec;
    if (!fs::exists(exe_dir() / "Uninstall.exe", ec) || !fs::exists(exe_dir() / "scripts" / "apply-update.ps1", ec)) {
      return "This host wasn't installed with the Norishine installer.";
    }
    return std::nullopt;
#else
    return "Updating from here is only supported on Windows.";
#endif
  }

  nlohmann::json status(bool refresh) {
    auto reason = unsupported_reason();
    std::optional<nlohmann::json> saved = state_store::read(status_file());

    std::lock_guard lg {shared.mutex};
    check_unlocked(refresh);
    auto available = choose(version::project, shared.releases, config::sunshine.notify_pre_releases);

    nlohmann::json body = {
      {"status", true},
      {"current", version::project},
      {"include_pre_releases", config::sunshine.notify_pre_releases},
      {"supported", !reason},
      {"unsupported_reason", reason ? nlohmann::json(*reason) : nlohmann::json(nullptr)},
      {"checked_at", shared.checked_at.empty() ? nlohmann::json(nullptr) : nlohmann::json(shared.checked_at)},
      {"check_error", shared.check_error.empty() ? nlohmann::json(nullptr) : nlohmann::json(shared.check_error)},
      {"available", available ? release_json(*available) : nlohmann::json(nullptr)},
      {"state", to_string(shared.state)},
      {"error", shared.error.empty() ? nlohmann::json(nullptr) : nlohmann::json(shared.error)},
      {"target", shared.target ? release_json(*shared.target) : nlohmann::json(nullptr)},
      {"progress", shared.state == state_e::downloading ? nlohmann::json {{"bytes", shared.bytes}, {"total", shared.total}} : nlohmann::json(nullptr)},
      {"last_update", saved && shared.state == state_e::idle ? last_update(*saved, version::project) : nlohmann::json(nullptr)},
    };
    return body;
  }

  manage_api::reply_t start(const nlohmann::json &body) {
    using enum SimpleWeb::StatusCode;
    auto error = [](SimpleWeb::StatusCode code, std::string message) {
      return manage_api::reply_t {code, {{"status", false}, {"error", std::move(message)}}};
    };
    if (auto reason = unsupported_reason()) {
      return error(client_error_conflict, *reason);
    }
    if (!body.is_object() || !body.contains("version") || !body["version"].is_string()) {
      return error(client_error_bad_request, "Say which version to install: {\"version\": \"...\"}.");
    }
    std::string wanted = body["version"];

    std::lock_guard lg {shared.mutex};
    if (shared.state == state_e::downloading || shared.state == state_e::installing) {
      return error(client_error_conflict, "An update is already running.");
    }
    check_unlocked(false);
    auto available = choose(version::project, shared.releases, config::sunshine.notify_pre_releases);
    if (!available || available->version != wanted) {
      return error(client_error_conflict, available ? "The newest version is " + available->version + ", not " + wanted + "." : "There is no newer version to install.");
    }

    shared.state = state_e::downloading;
    shared.error.clear();
    shared.bytes = 0;
    shared.total = available->size;
    shared.target = available;
    std::thread {run_update, *available}.detach();
    return {success_accepted, {{"status", true}, {"state", "downloading"}, {"target", release_json(*available)}}};
  }
}  // namespace updater
