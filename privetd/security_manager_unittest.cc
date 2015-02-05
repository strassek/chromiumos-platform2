// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "privetd/security_manager.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <crypto/p224_spake.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "privetd/openssl_utils.h"

using testing::Eq;
using testing::_;

namespace privetd {

namespace {

bool IsBase64Char(char c) {
  return isalnum(c) || (c == '+') || (c == '/') || (c == '=');
}

bool IsBase64(const std::string& text) {
  return !text.empty() &&
         !std::any_of(text.begin(), text.end(),
                      std::not1(std::ref(IsBase64Char)));
}

class MockPairingCallbacks {
 public:
  MOCK_METHOD3(OnPairingStart, void(const std::string& session_id,
                                    PairingType pairing_type,
                                    const std::string& code));
  MOCK_METHOD1(OnPairingEnd, void(const std::string& session_id));
};

}  // namespace

class SecurityManagerTest : public testing::Test {
 public:
  void SetUp() override {
    chromeos::Blob fingerprint;
    fingerprint.resize(256 / 8);
    base::RandBytes(fingerprint.data(), fingerprint.size());
    security_.SetCertificateFingerprint(fingerprint);
  }

 protected:
  const base::Time time_ = base::Time::FromTimeT(1410000000);
  base::MessageLoop message_loop_;
  SecurityManager security_{"1234"};
};

TEST_F(SecurityManagerTest, IsBase64) {
  EXPECT_TRUE(IsBase64(security_.CreateAccessToken(AuthScope::kGuest, time_)));
}

TEST_F(SecurityManagerTest, CreateSameToken) {
  EXPECT_EQ(security_.CreateAccessToken(AuthScope::kGuest, time_),
            security_.CreateAccessToken(AuthScope::kGuest, time_));
}

TEST_F(SecurityManagerTest, CreateTokenDifferentScope) {
  EXPECT_NE(security_.CreateAccessToken(AuthScope::kGuest, time_),
            security_.CreateAccessToken(AuthScope::kOwner, time_));
}

TEST_F(SecurityManagerTest, CreateTokenDifferentTime) {
  EXPECT_NE(security_.CreateAccessToken(AuthScope::kGuest, time_),
            security_.CreateAccessToken(AuthScope::kGuest,
                                        base::Time::FromTimeT(1400000000)));
}

TEST_F(SecurityManagerTest, CreateTokenDifferentInstance) {
  EXPECT_NE(security_.CreateAccessToken(AuthScope::kGuest, time_),
            SecurityManager("555").CreateAccessToken(AuthScope::kGuest, time_));
}

TEST_F(SecurityManagerTest, ParseAccessToken) {
  // Multiple attempts with random secrets.
  for (size_t i = 0; i < 1000; ++i) {
    SecurityManager security{"0987"};
    std::string token = security.CreateAccessToken(AuthScope::kUser, time_);
    base::Time time2;
    EXPECT_EQ(AuthScope::kUser, security.ParseAccessToken(token, &time2));
    // Token timestamp resolution is one second.
    EXPECT_GE(1, std::abs((time_ - time2).InSeconds()));
  }
}

/*
TEST_F(SecurityManagerTest, TlsData) {
  security_.InitTlsData();

  std::string key_str = security_.GetTlsPrivateKey().to_string();
  EXPECT_TRUE(
      StartsWithASCII(key_str, "-----BEGIN RSA PRIVATE KEY-----", false));
  EXPECT_TRUE(EndsWith(key_str, "-----END RSA PRIVATE KEY-----\n", false));

  const chromeos::Blob& cert = security_.GetTlsCertificate();
  std::string cert_str(cert.begin(), cert.end());
  EXPECT_TRUE(StartsWithASCII(cert_str, "-----BEGIN CERTIFICATE-----", false));
  EXPECT_TRUE(EndsWith(cert_str, "-----END CERTIFICATE-----\n", false));
}
*/

TEST_F(SecurityManagerTest, PairingNoSession) {
  std::string fingerprint;
  std::string signature;
  EXPECT_EQ(Error::kUnknownSession,
            security_.ConfirmPairing("123", "345", &fingerprint, &signature));
}

TEST_F(SecurityManagerTest, Pairing) {
  std::vector<std::pair<std::string, std::string> > fingerprints(2);
  for (auto& fingerprint : fingerprints) {
    std::string session_id;
    std::string device_commitment;
    EXPECT_EQ(Error::kNone,
              security_.StartPairing(PairingType::kEmbeddedCode,
                                     CryptoType::kSpake_p224, &session_id,
                                     &device_commitment));
    EXPECT_FALSE(session_id.empty());
    EXPECT_FALSE(device_commitment.empty());

    crypto::P224EncryptedKeyExchange spake{
        crypto::P224EncryptedKeyExchange::kPeerTypeServer, "1234"};

    std::string client_commitment =
        Base64Encode(chromeos::SecureBlob(spake.GetMessage()));
    EXPECT_EQ(Error::kNone, security_.ConfirmPairing(
                                session_id, client_commitment,
                                &fingerprint.first, &fingerprint.second));
    spake.ProcessMessage(device_commitment);

    EXPECT_TRUE(IsBase64(fingerprint.first));
    EXPECT_TRUE(IsBase64(fingerprint.second));
  }

  // Same certificate.
  EXPECT_EQ(fingerprints.front().first, fingerprints.back().first);

  // Signed with different secret.
  EXPECT_NE(fingerprints.front().second, fingerprints.back().second);
}

TEST_F(SecurityManagerTest, NotifiesListenersOfSessionStartAndEnd) {
  testing::StrictMock<MockPairingCallbacks> callbacks;
  security_.RegisterPairingListeners(
      base::Bind(&MockPairingCallbacks::OnPairingStart,
                 base::Unretained(&callbacks)),
      base::Bind(&MockPairingCallbacks::OnPairingEnd,
                 base::Unretained(&callbacks)));
  for (auto commitment_suffix :
       std::vector<std::string>{"", "invalid_commitment"}) {
    // StartPairing should notify us that a new session has begun.
    std::string session_id;
    std::string device_commitment;
    EXPECT_CALL(callbacks, OnPairingStart(_, PairingType::kEmbeddedCode, _));
    EXPECT_EQ(Error::kNone,
              security_.StartPairing(PairingType::kEmbeddedCode,
                                     CryptoType::kSpake_p224, &session_id,
                                     &device_commitment));
    EXPECT_FALSE(session_id.empty());
    EXPECT_FALSE(device_commitment.empty());
    testing::Mock::VerifyAndClearExpectations(&callbacks);

    // ConfirmPairing should notify us that the session has ended.
    EXPECT_CALL(callbacks, OnPairingEnd(Eq(session_id)));
    crypto::P224EncryptedKeyExchange spake{
        crypto::P224EncryptedKeyExchange::kPeerTypeServer, "1234"};
    std::string client_commitment =
        Base64Encode(chromeos::SecureBlob(spake.GetMessage()));
    std::string fingerprint, signature;
    // Regardless of whether the commitment is valid or not, we should get a
    // callback indicating that the pairing session is gone.
    security_.ConfirmPairing(session_id, client_commitment + commitment_suffix,
                             &fingerprint, &signature);
    testing::Mock::VerifyAndClearExpectations(&callbacks);
  }
}

TEST_F(SecurityManagerTest, CancelPairing) {
  testing::StrictMock<MockPairingCallbacks> callbacks;
  security_.RegisterPairingListeners(
      base::Bind(&MockPairingCallbacks::OnPairingStart,
                 base::Unretained(&callbacks)),
      base::Bind(&MockPairingCallbacks::OnPairingEnd,
                 base::Unretained(&callbacks)));
  std::string session_id;
  std::string device_commitment;
  EXPECT_CALL(callbacks, OnPairingStart(_, PairingType::kEmbeddedCode, _));
  EXPECT_EQ(Error::kNone,
            security_.StartPairing(PairingType::kEmbeddedCode,
                                   CryptoType::kSpake_p224, &session_id,
                                   &device_commitment));
  EXPECT_CALL(callbacks, OnPairingEnd(Eq(session_id)));
  EXPECT_EQ(Error::kNone, security_.CancelPairing(session_id));
}

}  // namespace privetd
