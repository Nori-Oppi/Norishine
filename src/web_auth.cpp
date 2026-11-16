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

  bool sessions_t::expire(wall_clock::time_point now) {
    // A clock set back by more than the lifetime would keep sessions forever; drop those too.
    return std::erase_if(by_hash, [&](const auto &item) {
             return now - item.second.created > lifetime || item.second.created - now > lifetime;
           }) > 0;
  }

  nlohmann::json sessions_t::to_json_locked() const {
    nlohmann::json list = nlohmann::json::array();
    for (const auto &[hash, session] : by_hash) {
      list.push_back({
        {"hash", hash},
        {"csrf", session.csrf},
        {"created", std::chrono::duration_cast<std::chrono::seconds>(session.created.time_since_epoch()).count()},
      });
    }
    return list;
  }

  void sessions_t::notify(const nlohmann::json &content) {
    if (changed) {
      changed(content);
    }
  }

  sessions_t::created_t sessions_t::create(wall_clock::time_point now) {
    created_t created {random_token(), random_token()};
    nlohmann::json content;
    {
      std::lock_guard lock {mutex};
      expire(now);
      while (by_hash.size() >= max_sessions) {
        auto oldest = std::min_element(by_hash.begin(), by_hash.end(), [](const auto &a, const auto &b) {
          return a.second.created < b.second.created;
        });
        by_hash.erase(oldest);
      }
      by_hash[sha256_hex(created.token)] = {created.csrf, now};
      content = to_json_locked();
    }
    notify(content);
    return created;
  }

  std::optional<std::string> sessions_t::find(std::string_view token, wall_clock::time_point now) {
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
    nlohmann::json content;
    {
      std::lock_guard lock {mutex};
      if (by_hash.erase(sha256_hex(token)) == 0) {
        return;
      }
      content = to_json_locked();
    }
    notify(content);
  }

  void sessions_t::clear() {
    {
      std::lock_guard lock {mutex};
      by_hash.clear();
    }
    notify(nlohmann::json::array());
  }

  std::size_t sessions_t::size() {
    std::lock_guard lock {mutex};
    return by_hash.size();
  }

  nlohmann::json sessions_t::to_json() {
    std::lock_guard lock {mutex};
    return to_json_locked();
  }

  void sessions_t::load(const nlohmann::json &saved, wall_clock::time_point now) {
    std::lock_guard lock {mutex};
    by_hash.clear();
    if (!saved.is_array()) {
      return;
    }
    for (const auto &item : saved) {
      if (!item.is_object() || !item.contains("hash") || !item["hash"].is_string() || !item.contains("csrf") ||
          !item["csrf"].is_string() || !item.contains("created") || !item["created"].is_number_integer()) {
        continue;
      }
      const auto hash = item["hash"].get<std::string>();
      const auto csrf = item["csrf"].get<std::string>();
      if (hash.size() != 64 || csrf.empty()) {
        continue;
      }
      by_hash[hash] = {csrf, wall_clock::time_point {std::chrono::seconds {item["created"].get<long long>()}}};
    }
    expire(now);
    while (by_hash.size() > max_sessions) {
      auto oldest = std::min_element(by_hash.begin(), by_hash.end(), [](const auto &a, const auto &b) {
        return a.second.created < b.second.created;
      });
      by_hash.erase(oldest);
    }
  }

  void sessions_t::on_change(std::function<void(const nlohmann::json &)> callback) {
    std::lock_guard lock {mutex};
    changed = std::move(callback);
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
