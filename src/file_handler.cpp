/**
 * @file file_handler.cpp
 * @brief Definitions for file handling functions.
 */

// standard includes
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

// platform includes
#ifdef _WIN32
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
#endif

// local includes
#include "file_handler.h"
#include "logging.h"

namespace {
  /**
   * @brief Create or truncate a file, write the contents and flush them to disk.
   */
  bool write_and_flush(const std::filesystem::path &path, const std::string_view &contents) {
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      return false;
    }
    bool ok = true;
    const char *data = contents.data();
    std::size_t left = contents.size();
    while (ok && left > 0) {
      DWORD chunk = (DWORD) std::min<std::size_t>(left, 1u << 30);
      DWORD written = 0;
      ok = WriteFile(file, data, chunk, &written, nullptr) && written > 0;
      data += written;
      left -= written;
    }
    ok = ok && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
#else
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
      return false;
    }
    bool ok = true;
    const char *data = contents.data();
    std::size_t left = contents.size();
    while (ok && left > 0) {
      auto written = ::write(fd, data, left);
      ok = written > 0;
      if (ok) {
        data += written;
        left -= (std::size_t) written;
      }
    }
    ok = ok && ::fsync(fd) == 0;
    ok = (::close(fd) == 0) && ok;
    return ok;
#endif
  }

  /**
   * @brief Replace `target` with `source` in one step.
   */
  bool replace_file(const std::filesystem::path &source, const std::filesystem::path &target) {
#ifdef _WIN32
    return MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    return std::rename(source.c_str(), target.c_str()) == 0;
#endif
  }
}  // namespace

namespace file_handler {
  using namespace std::literals;
  std::string get_parent_directory(const std::string &path) {
    // remove any trailing path separators
    std::string trimmed_path = path;
    while (!trimmed_path.empty() && trimmed_path.back() == '/') {
      trimmed_path.pop_back();
    }

    std::filesystem::path p(trimmed_path);
    return p.parent_path().string();
  }

  bool make_directory(const std::string &path) {
    // first, check if the directory already exists
    if (std::filesystem::exists(path)) {
      return true;
    }

    return std::filesystem::create_directories(path);
  }

  std::string read_file(const char *path) {
    if (!std::filesystem::exists(path)) {
      BOOST_LOG(debug) << "Missing file: " << path;
      return {};
    }

    std::ifstream in(path);
    return std::string {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
  }

  int write_file(const char *path, const std::string_view &contents) {
    const std::filesystem::path target {path};
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    std::filesystem::path backup = target;
    backup += ".bak";

    if (!write_and_flush(temporary, contents)) {
      BOOST_LOG(error) << "Couldn't write "sv << temporary.string();
      std::error_code ec;
      std::filesystem::remove(temporary, ec);
      return -1;
    }

    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
#ifndef _WIN32
      // Keep the original's permissions (for example a private key readable only by us).
      std::filesystem::permissions(temporary, std::filesystem::status(target, ec).permissions(), ec);
#endif
      std::filesystem::copy_file(target, backup, std::filesystem::copy_options::overwrite_existing, ec);
      if (ec) {
        BOOST_LOG(warning) << "Couldn't back up "sv << target.string() << ": "sv << ec.message();
      }
    }

    if (replace_file(temporary, target)) {
      return 0;
    }

    // Replacing can fail when another program holds the file open without allowing
    // deletion. Fall back to writing in place rather than failing the save.
    BOOST_LOG(warning) << "Couldn't replace "sv << target.string() << " atomically; writing it in place"sv;
    std::filesystem::remove(temporary, ec);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      return -1;
    }
    out << contents;
    out.close();
    return out ? 0 : -1;
  }
}  // namespace file_handler
