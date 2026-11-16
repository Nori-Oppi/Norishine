/**
 * @file tests/unit/test_web_auth.cpp
 * @brief Test src/web_auth.*: password hashes, sessions and login throttling.
 */
#include "../tests_common.h"

// local includes
#include <src/crypto.h>
#include <src/utility.h>
#include <src/web_auth.h>

using namespace std::chrono_literals;
using namespace web_auth;

TEST(WebAuthTests, NewHashesUsePbkdf2) {
  auto stored = hash_password("correct horse", "salt1234");
  EXPECT_TRUE(stored.starts_with("pbkdf2-sha256$"));
  EXPECT_FALSE(is_legacy_hash(stored));
  EXPECT_TRUE(verify_password("correct horse", "salt1234", stored));
  EXPECT_FALSE(verify_password("correct horsE", "salt1234", stored));
  EXPECT_FALSE(verify_password("correct horse", "salt1235", stored));
  EXPECT_NE(stored, hash_password("correct horse", "other salt"));
}

TEST(WebAuthTests, LegacyHashesStillWork) {
  // Apollo and Sunshine store hex SHA-256 of password + salt.
  auto legacy = util::hex(crypto::hash(std::string {"hunter22"} + "abc")).to_string();
  EXPECT_TRUE(is_legacy_hash(legacy));
  EXPECT_TRUE(verify_password("hunter22", "abc", legacy));
  EXPECT_FALSE(verify_password("hunter23", "abc", legacy));
}

TEST(WebAuthTests, MalformedHashesFail) {
  for (auto stored : {"pbkdf2-sha256$", "pbkdf2-sha256$abc$00", "pbkdf2-sha256$0$00", "pbkdf2-sha256$99999999999$00", "pbkdf2-sha256$1000"}) {
    EXPECT_FALSE(verify_password("x", "y", stored)) << stored;
  }
  EXPECT_FALSE(verify_password("", "", ""));
}

TEST(WebAuthTests, EqualSecret) {
  EXPECT_TRUE(equal_secret("abc", "abc"));
  EXPECT_FALSE(equal_secret("abc", "abd"));
  EXPECT_FALSE(equal_secret("abc", "abcd"));
  EXPECT_TRUE(equal_secret("", ""));
}

TEST(WebAuthTests, RandomTokensAreCookieSafe) {
  auto token = random_token();
  EXPECT_EQ(token.size(), 43u);
  EXPECT_EQ(token.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"), std::string::npos);
  EXPECT_NE(token, random_token());
}

TEST(WebAuthTests, SessionsAreIndependentAndExpire) {
  sessions_t sessions {100s, 3};
  auto t0 = clock::now();
  auto a = sessions.create(t0);
  auto b = sessions.create(t0 + 10s);
  EXPECT_NE(a.token, b.token);
  EXPECT_NE(a.csrf, b.csrf);
  EXPECT_EQ(sessions.find(a.token, t0 + 20s), a.csrf);
  EXPECT_EQ(sessions.find(b.token, t0 + 20s), b.csrf);
  EXPECT_FALSE(sessions.find("not a token", t0));
  EXPECT_FALSE(sessions.find("", t0));

  // Signing out one browser leaves the other signed in.
  sessions.remove(a.token);
  EXPECT_FALSE(sessions.find(a.token, t0 + 20s));
  EXPECT_TRUE(sessions.find(b.token, t0 + 20s));

  // Expiry is counted from sign-in.
  EXPECT_FALSE(sessions.find(b.token, t0 + 111s));
}

TEST(WebAuthTests, OldestSessionMakesRoom) {
  sessions_t sessions {1000s, 2};
  auto t0 = clock::now();
  auto a = sessions.create(t0);
  auto b = sessions.create(t0 + 1s);
  auto c = sessions.create(t0 + 2s);
  EXPECT_EQ(sessions.size(), 2u);
  EXPECT_FALSE(sessions.find(a.token, t0 + 3s));
  EXPECT_TRUE(sessions.find(b.token, t0 + 3s));
  EXPECT_TRUE(sessions.find(c.token, t0 + 3s));
  sessions.clear();
  EXPECT_EQ(sessions.size(), 0u);
}

TEST(WebAuthTests, LoginLimiterLocksOutAndDoubles) {
  login_limiter_t limiter;
  auto t0 = clock::now();
  for (int i = 0; i < login_limiter_t::free_attempts - 1; ++i) {
    limiter.failed("10.0.0.2", t0);
    EXPECT_EQ(limiter.wait_time("10.0.0.2", t0), 0s) << i;
  }
  limiter.failed("10.0.0.2", t0);
  EXPECT_EQ(limiter.wait_time("10.0.0.2", t0), 30s);
  EXPECT_EQ(limiter.wait_time("10.0.0.3", t0), 0s) << "other addresses are not affected";
  EXPECT_EQ(limiter.wait_time("10.0.0.2", t0 + 31s), 0s);

  limiter.failed("10.0.0.2", t0 + 31s);
  EXPECT_EQ(limiter.wait_time("10.0.0.2", t0 + 31s), 60s);

  // Capped.
  for (int i = 0; i < 20; ++i) {
    limiter.failed("10.0.0.2", t0 + 40s);
  }
  EXPECT_EQ(limiter.wait_time("10.0.0.2", t0 + 40s), login_limiter_t::max_lockout);

  // A success clears it, and so does a long quiet period.
  limiter.succeeded("10.0.0.2");
  EXPECT_EQ(limiter.wait_time("10.0.0.2", t0 + 40s), 0s);
  for (int i = 0; i < login_limiter_t::free_attempts; ++i) {
    limiter.failed("10.0.0.4", t0);
  }
  EXPECT_GT(limiter.wait_time("10.0.0.4", t0), 0s);
  limiter.failed("10.0.0.4", t0 + 2h);
  EXPECT_EQ(limiter.wait_time("10.0.0.4", t0 + 2h), 0s);
}
