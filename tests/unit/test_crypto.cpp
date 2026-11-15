/**
 * @file tests/unit/test_crypto.cpp
 * @brief Client certificate identity tests (Sunshine 4d768847, GHSA-6jvv-jqr7-m6m3).
 */
#include "../tests_common.h"

// lib includes
#include <openssl/x509.h>

// local includes
#include "../certificate_test_utils.h"

#include <src/crypto.h>

namespace {
  crypto::p_named_cert_t make_paired_client(const std::string &cert_pem) {
    auto named_cert = std::make_shared<crypto::named_cert_t>();
    named_cert->name = "test client";
    named_cert->cert = cert_pem;
    return named_cert;
  }
}  // namespace

TEST(CryptoTest, CertificateChainAcceptsExactPairedCertificate) {
  const auto credentials = test_utils::certificates::generate_ca_credentials();
  auto certificate = crypto::x509(credentials.x509);

  crypto::cert_chain_t certificate_chain;
  auto paired_client = make_paired_client(credentials.x509);
  certificate_chain.add(paired_client);

  crypto::p_named_cert_t matched;
  EXPECT_EQ(certificate_chain.verify(certificate.get(), matched), nullptr);
  EXPECT_EQ(matched, paired_client);
}

TEST(CryptoTest, CertificateChainRejectsCertificateDerivedFromPairedKey) {
  const auto issuer_credentials = test_utils::certificates::generate_ca_credentials();
  const auto derived_credentials = test_utils::certificates::generate_derived_leaf(issuer_credentials);
  auto derived_certificate = crypto::x509(derived_credentials.x509);

  crypto::cert_chain_t certificate_chain;
  auto paired_client = make_paired_client(issuer_credentials.x509);
  certificate_chain.add(paired_client);

  crypto::p_named_cert_t matched;
  EXPECT_NE(certificate_chain.verify(derived_certificate.get(), matched), nullptr);
  EXPECT_EQ(matched, nullptr);
}

TEST(CryptoTest, CertificateChainMatchesTheRightClientAmongSeveral) {
  const auto first = test_utils::certificates::generate_ca_credentials("First client");
  const auto second = test_utils::certificates::generate_ca_credentials("Second client");
  auto second_certificate = crypto::x509(second.x509);

  crypto::cert_chain_t certificate_chain;
  auto first_client = make_paired_client(first.x509);
  auto second_client = make_paired_client(second.x509);
  certificate_chain.add(first_client);
  certificate_chain.add(second_client);

  crypto::p_named_cert_t matched;
  EXPECT_EQ(certificate_chain.verify(second_certificate.get(), matched), nullptr);
  EXPECT_EQ(matched, second_client);
}
