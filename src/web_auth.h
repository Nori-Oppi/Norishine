/**
 * @file src/web_auth.h
 * @brief Web UI sign-in: password hashes, sessions with CSRF tokens, and login throttling.
 */
#pragma once

// standard includes
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace web_auth {
  using clock = std::chrono::steady_clock;

  /**
   * @brief Hash a web UI password with PBKDF2-HMAC-SHA256.
   * @return "pbkdf2-sha256$<iterations>$<hex>", stored in the credentials file.
   */
  std::string hash_password(std::string_view password, std::string_view salt);

  /**
   * @brief Whether a stored hash uses the old format (one round of SHA-256), which
   *        verify_password() still accepts and the caller should replace.
   */
  bool is_legacy_hash(std::string_view stored);

  /**
   * @brief Check a password against a stored hash (either format), in constant time.
   */
  bool verify_password(std::string_view password, std::string_view salt, std::string_view stored);

  /**
   * @brief Compare two secrets without leaking where they differ.
   */
  bool equal_secret(std::string_view a, std::string_view b);

  /**
   * @brief A random token that is safe in cookies and headers (letters and digits).
   */
  std::string random_token(std::size_t length = 43);

  /**
   * @brief Signed-in browser sessions. Tokens are only kept as hashes.
   */
  class sessions_t {
  public:
    struct created_t {
      std::string token;  ///< Goes in the HttpOnly session cookie.
      std::string csrf;  ///< Must come back in the X-CSRF-Token header on changes.
    };

    explicit sessions_t(std::chrono::seconds lifetime, std::size_t max_sessions = 32);

    created_t create(clock::time_point now = clock::now());

    /**
     * @brief The CSRF token of a valid session, or nothing when the token is unknown or expired.
     */
    std::optional<std::string> find(std::string_view token, clock::time_point now = clock::now());

    void remove(std::string_view token);
    void clear();
    std::size_t size();

  private:
    struct session_t {
      std::string csrf;
      clock::time_point created;
    };

    std::chrono::seconds lifetime;
    std::size_t max_sessions;
    std::mutex mutex;
    std::map<std::string, session_t, std::less<>> by_hash;

    void expire(clock::time_point now);
  };

  /**
   * @brief Slows down password guessing: after a few failed logins from one address,
   *        further attempts from it are refused for a while, doubling each time.
   */
  class login_limiter_t {
  public:
    static constexpr int free_attempts = 5;
    static constexpr std::chrono::seconds first_lockout {30};
    static constexpr std::chrono::seconds max_lockout {15 * 60};
    static constexpr std::chrono::seconds forget_after {60 * 60};

    /**
     * @brief How long this address must wait before trying again (zero: it may try now).
     */
    std::chrono::seconds wait_time(const std::string &address, clock::time_point now = clock::now());

    void failed(const std::string &address, clock::time_point now = clock::now());
    void succeeded(const std::string &address);

  private:
    struct entry_t {
      int failures = 0;
      clock::time_point last_failure;
      clock::time_point locked_until;
    };

    std::mutex mutex;
    std::map<std::string, entry_t> entries;
  };
}  // namespace web_auth
