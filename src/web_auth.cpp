/**
 * @file src/web_auth.cpp
 * @brief Definitions for web UI sign-in.
 */
// standard includes
#include <algorithm>
#include <charconv>
#include <vector>

// lib includes
#include <openssl/crypto.h>
#include <openssl/evp.h>

// local includes
#include "crypto.h"
#include "utility.h"
#include "web_auth.h"

using namespace std::literals;

namespace web_auth {
  namespace {
    constexpr std::string_view prefix = "pbkdf2-sha256$";
    constexpr int iterations = 310000;  // OWASP's 2021 figure; a login takes a fraction of a second

    std::string pbkdf2(std::string_view password, std::string_view salt, int rounds) {
      unsigned char out[32];
      if (!PKCS5_PBKDF2_HMAC(password.data(), (int) password.size(), (const unsigned char *) salt.data(), (int) salt.size(), rounds, EVP_sha256(), sizeof(out), out)) {
        return {};
      }
      return util::hex_vec(out, false);
    }

    std::string sha256_hex(std::string_view text) {
      return util::hex(crypto::hash(text)).to_string();
    }
  }  // namespace

  std::string hash_password(std::string_view password, std::string_view salt) {
    return std::string {prefix} + std::to_string(iterations) + "$" + pbkdf2(password, salt, iterations);
  }

  bool is_legacy_hash(std::string_view stored) {
    return !stored.starts_with(prefix);
  }

  bool verify_password(std::string_view password, std::string_view salt, std::string_view stored) {
    if (is_legacy_hash(stored)) {
      // Apollo and Sunshine: hex SHA-256 of password + salt.
      return equal_secret(sha256_hex(std::string {password} + std::string {salt}), stored);
    }
    auto rest = stored.substr(prefix.size());
    auto dollar = rest.find('$');
    if (dollar == std::string_view::npos) {
      return false;
    }
    int rounds = 0;
    auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + dollar, rounds);
    if (ec != std::errc {} || ptr != rest.data() + dollar || rounds < 1 || rounds > 10'000'000) {
      return false;
    }
    auto expected = pbkdf2(password, salt, rounds);
    return !expected.empty() && equal_secret(expected, rest.substr(dollar + 1));
  }

  bool equal_secret(std::string_view a, std::string_view b) {
    // Compare digests so the length of the secret doesn't show either.
    auto ha = crypto::hash(a);
    auto hb = crypto::hash(b);
    return CRYPTO_memcmp(ha.data(), hb.data(), ha.size()) == 0 && a.size() == b.size();
  }

  std::string random_token(std::size_t length) {
    return crypto::rand_alphabet(length, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"sv);
  }

  sessions_t::sessions_t(std::chrono::seconds lifetime, std::size_t max_sessions):
      lifetime {lifetime},
      max_sessions {max_sessions} {
  }

  void sessions_t::expire(clock::time_point now) {
    std::erase_if(by_hash, [&](const auto &item) {
      return now - item.second.created > lifetime;
    });
  }

  sessions_t::created_t sessions_t::create(clock::time_point now) {
    created_t created {random_token(), random_token()};
    std::lock_guard lock {mutex};
    expire(now);
    while (by_hash.size() >= max_sessions) {
      auto oldest = std::min_element(by_hash.begin(), by_hash.end(), [](const auto &a, const auto &b) {
        return a.second.created < b.second.created;
      });
      by_hash.erase(oldest);
    }
    by_hash[sha256_hex(created.token)] = {created.csrf, now};
    return created;
  }

  std::optional<std::string> sessions_t::find(std::string_view token, clock::time_point now) {
    if (token.empty()) {
      return std::nullopt;
    }
    std::lock_guard lock {mutex};
    expire(now);
    auto it = by_hash.find(sha256_hex(token));
    if (it == by_hash.end()) {
      return std::nullopt;
    }
    return it->second.csrf;
  }

  void sessions_t::remove(std::string_view token) {
    std::lock_guard lock {mutex};
    by_hash.erase(sha256_hex(token));
  }

  void sessions_t::clear() {
    std::lock_guard lock {mutex};
    by_hash.clear();
  }

  std::size_t sessions_t::size() {
    std::lock_guard lock {mutex};
    return by_hash.size();
  }

  std::chrono::seconds login_limiter_t::wait_time(const std::string &address, clock::time_point now) {
    std::lock_guard lock {mutex};
    auto it = entries.find(address);
    if (it == entries.end()) {
      return 0s;
    }
    if (now - it->second.last_failure > forget_after) {
      entries.erase(it);
      return 0s;
    }
    if (it->second.locked_until <= now) {
      return 0s;
    }
    return std::chrono::ceil<std::chrono::seconds>(it->second.locked_until - now);
  }

  void login_limiter_t::failed(const std::string &address, clock::time_point now) {
    std::lock_guard lock {mutex};
    // Bound the memory an attacker with many addresses can use.
    if (entries.size() > 10000) {
      std::erase_if(entries, [&](const auto &item) {
        return now - item.second.last_failure > forget_after;
      });
    }
    auto &entry = entries[address];
    if (entry.failures > 0 && now - entry.last_failure > forget_after) {
      entry = {};
    }
    entry.failures++;
    entry.last_failure = now;
    if (entry.failures >= free_attempts) {
      auto doublings = std::min(entry.failures - free_attempts, 10);
      auto lockout = std::min<std::chrono::seconds>(first_lockout * (1 << doublings), max_lockout);
      entry.locked_until = now + lockout;
    }
  }

  void login_limiter_t::succeeded(const std::string &address) {
    std::lock_guard lock {mutex};
    entries.erase(address);
  }
}  // namespace web_auth
