// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "shill/eap_credentials.h"

#include <base/optional.h>
#include <base/stl_util.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>
#include <libpasswordprovider/fake_password_provider.h>
#include <libpasswordprovider/password.h>
#include <libpasswordprovider/password_provider.h>
#include <libpasswordprovider/password_provider_test_utils.h>

#include "shill/key_value_store.h"
#include "shill/mock_certificate_file.h"
#include "shill/mock_log.h"
#include "shill/mock_metrics.h"
#include "shill/mock_store.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/technology.h"

using base::FilePath;
using std::string;
using std::vector;
using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Mock;
using testing::Return;
using testing::SetArgPointee;

namespace shill {

class EapCredentialsTest : public testing::Test {
 public:
  EapCredentialsTest() = default;
  ~EapCredentialsTest() override = default;

 protected:
  void PopulateSupplicantProperties() {
    eap_.PopulateSupplicantProperties(&certificate_file_, &params_);
  }

  void SetAnonymousIdentity(const string& anonymous_identity) {
    eap_.anonymous_identity_ = anonymous_identity;
  }
  void SetCACertPEM(const vector<string>& ca_cert_pem) {
    eap_.ca_cert_pem_ = ca_cert_pem;
  }
  void SetCertId(const string& cert_id) { eap_.cert_id_ = cert_id; }
  void SetCACertId(const string& ca_cert_id) { eap_.ca_cert_id_ = ca_cert_id; }
  void SetEap(const string& eap) { eap_.eap_ = eap; }
  void SetIdentity(const string& identity) { eap_.identity_ = identity; }
  void SetInnerEap(const string& inner_eap) { eap_.inner_eap_ = inner_eap; }
  void SetTLSVersionMax(const string& tls_version_max) {
    eap_.tls_version_max_ = tls_version_max;
  }
  void SetKeyId(const string& key_id) { eap_.key_id_ = key_id; }
  const string& GetPassword() { return eap_.password_; }
  void SetPassword(const string& password) { eap_.password_ = password; }
  void SetPin(const string& pin) { eap_.pin_ = pin; }
  void SetUseProactiveKeyCaching(bool use_proactive_key_caching) {
    eap_.use_proactive_key_caching_ = use_proactive_key_caching;
  }
  void SetSubjectAlternativeNameMatch(vector<string> altsubject_match_list) {
    eap_.subject_alternative_name_match_list_ = altsubject_match_list;
  }
  void SetUseSystemCAs(bool use_system_cas) {
    eap_.use_system_cas_ = use_system_cas;
  }
  void SetUseLoginPassword(bool use_login_password) {
    eap_.use_login_password_ = use_login_password;
  }
  bool IsReset() {
    return eap_.anonymous_identity_.empty() && eap_.cert_id_.empty() &&
           eap_.identity_.empty() && eap_.key_id_.empty() &&
           eap_.password_.empty() && eap_.pin_.empty() &&
           eap_.ca_cert_id_.empty() && eap_.ca_cert_pem_.empty() &&
           eap_.eap_.empty() && eap_.inner_eap_.empty() &&
           eap_.tls_version_max_.empty() && eap_.subject_match_.empty() &&
           eap_.subject_alternative_name_match_list_.empty() &&
           eap_.use_system_cas_ == true &&
           eap_.use_proactive_key_caching_ == false &&
           eap_.use_login_password_ == false;
  }

  const string& GetKeyManagement() { return eap_.key_management_; }
  bool SetEapPassword(const string& password, Error* error) {
    return eap_.SetEapPassword(password, error);
  }

  void SaveLoginPassword(const string& password_str) {
    eap_.password_provider_ =
        std::make_unique<password_provider::FakePasswordProvider>();

    auto password = password_provider::test::CreatePassword(password_str);
    ASSERT_TRUE(password);

    eap_.password_provider_->SavePassword(*password);
  }

  EapCredentials eap_;
  MockCertificateFile certificate_file_;
  KeyValueStore params_;
};

TEST_F(EapCredentialsTest, PropertyStore) {
  PropertyStore store;
  eap_.InitPropertyStore(&store);
  const string kIdentity("Cross-Eyed Mary");
  Error error;
  EXPECT_TRUE(store.SetStringProperty(kEapIdentityProperty, kIdentity, &error));
  EXPECT_EQ(kIdentity, eap_.identity());
}

TEST_F(EapCredentialsTest, Connectable) {
  // Empty EAP credentials should not make a 802.1x network connectable.
  EXPECT_FALSE(eap_.IsConnectable());

  // Identity alone is not enough.
  SetIdentity("Steel Monkey");
  EXPECT_FALSE(eap_.IsConnectable());

  // Set a password.
  SetPassword("Angry Tapir");

  // Empty "EAP" parameter is treated like "not EAP-TLS", and connectable.
  EXPECT_TRUE(eap_.IsConnectable());

  // Some other non-TLS EAP type.
  SetEap("DodgeBall");
  EXPECT_TRUE(eap_.IsConnectable());

  // EAP-TLS requires certificate parameters, and cares not for passwords.
  SetEap("TLS");
  EXPECT_FALSE(eap_.IsConnectable());

  // Clearing the password won't help.
  SetPassword("");
  EXPECT_FALSE(eap_.IsConnectable());

  // A cert-id + key-id isn't sufficient.
  SetCertId("client-cert-id");
  SetKeyId("client-key-id");
  EXPECT_FALSE(eap_.IsConnectable());

  // We need a PIN for the key id in addition.
  SetPin("pin");
  EXPECT_TRUE(eap_.IsConnectable());

  // If we clear the "EAP" property, a password is required but a client
  // cert is not.
  SetCertId("");
  EXPECT_FALSE(eap_.IsConnectable());

  SetEap("");
  SetPassword("Angry Tapir");
  EXPECT_TRUE(eap_.IsConnectable());
}

TEST_F(EapCredentialsTest, ConnectableUsingPassphrase) {
  EXPECT_FALSE(eap_.IsConnectableUsingPassphrase());

  // No password.
  SetIdentity("TestIdentity");
  EXPECT_FALSE(eap_.IsConnectableUsingPassphrase());

  // Success.
  SetPassword("TestPassword");
  EXPECT_TRUE(eap_.IsConnectableUsingPassphrase());

  // Clear identity.
  SetIdentity("");
  EXPECT_FALSE(eap_.IsConnectableUsingPassphrase());
}

TEST_F(EapCredentialsTest, IsEapAuthenticationProperty) {
  EXPECT_TRUE(EapCredentials::IsEapAuthenticationProperty(
      kEapAnonymousIdentityProperty));
  EXPECT_TRUE(EapCredentials::IsEapAuthenticationProperty(kEapCertIdProperty));
  EXPECT_TRUE(
      EapCredentials::IsEapAuthenticationProperty(kEapIdentityProperty));
  EXPECT_TRUE(EapCredentials::IsEapAuthenticationProperty(kEapKeyIdProperty));
  EXPECT_TRUE(EapCredentials::IsEapAuthenticationProperty(kEapKeyMgmtProperty));
  EXPECT_TRUE(
      EapCredentials::IsEapAuthenticationProperty(kEapPasswordProperty));
  EXPECT_TRUE(EapCredentials::IsEapAuthenticationProperty(kEapPinProperty));
  EXPECT_TRUE(EapCredentials::IsEapAuthenticationProperty(
      kEapUseLoginPasswordProperty));

  // It's easier to test that this function returns TRUE in every situation
  // that it should, than to test all the cases it should return FALSE in.
  EXPECT_FALSE(EapCredentials::IsEapAuthenticationProperty(kEapCaCertProperty));
  EXPECT_FALSE(
      EapCredentials::IsEapAuthenticationProperty(kEapCaCertIdProperty));
  EXPECT_FALSE(
      EapCredentials::IsEapAuthenticationProperty(kEapCaCertPemProperty));
  EXPECT_FALSE(EapCredentials::IsEapAuthenticationProperty(kEapMethodProperty));
  EXPECT_FALSE(
      EapCredentials::IsEapAuthenticationProperty(kEapPhase2AuthProperty));
  EXPECT_FALSE(EapCredentials::IsEapAuthenticationProperty(
      kEapRemoteCertificationProperty));
  EXPECT_FALSE(
      EapCredentials::IsEapAuthenticationProperty(kEapSubjectMatchProperty));
  EXPECT_FALSE(EapCredentials::IsEapAuthenticationProperty(
      kEapUseProactiveKeyCachingProperty));
  EXPECT_FALSE(
      EapCredentials::IsEapAuthenticationProperty(kEapUseSystemCasProperty));
}

TEST_F(EapCredentialsTest, LoadAndSave) {
  MockStore store;
  // For the values we're not testing...
  EXPECT_CALL(store, GetCryptedString(_, _, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(store, GetString(_, _, _)).WillRepeatedly(Return(false));

  const string kId("storage-id");
  const string kIdentity("Purple Onion");
  EXPECT_CALL(
      store, GetCryptedString(kId, _,
                              EapCredentials::kStorageCredentialEapIdentity, _))
      .WillOnce(DoAll(SetArgPointee<3>(kIdentity), Return(true)));
  const string kManagement("Shave and a Haircut");
  EXPECT_CALL(store,
              GetString(kId, EapCredentials::kStorageEapKeyManagement, _))
      .WillOnce(DoAll(SetArgPointee<2>(kManagement), Return(true)));
  const string kPassword("Two Bits");
  EXPECT_CALL(
      store, GetCryptedString(kId, _,
                              EapCredentials::kStorageCredentialEapPassword, _))
      .WillOnce(DoAll(SetArgPointee<3>(kPassword), Return(true)));

  eap_.Load(&store, kId);
  Mock::VerifyAndClearExpectations(&store);

  EXPECT_EQ(kIdentity, eap_.identity());
  EXPECT_EQ(kManagement, eap_.key_management());
  EXPECT_EQ(kPassword, GetPassword());

  // Authentication properties are deleted from the store if they are empty,
  // so we expect the fields that we haven't set to be deleted.
  EXPECT_CALL(store, DeleteKey(_, _)).Times(AnyNumber());
  EXPECT_CALL(store,
              DeleteKey(kId, EapCredentials::kStorageCredentialEapIdentity));
  EXPECT_CALL(store, SetString(kId, EapCredentials::kStorageEapKeyManagement,
                               kManagement));
  EXPECT_CALL(store,
              DeleteKey(kId, EapCredentials::kStorageCredentialEapPassword));
  eap_.Save(&store, kId, false);
  Mock::VerifyAndClearExpectations(&store);

  // Authentication properties are deleted from the store if they are empty,
  // so we expect the fields that we haven't set to be deleted.
  EXPECT_CALL(store, DeleteKey(_, _)).Times(AnyNumber());
  EXPECT_CALL(
      store,
      SetString(kId, EapCredentials::kStorageCredentialEapIdentity, kIdentity));
  EXPECT_CALL(store, SetString(kId, EapCredentials::kStorageEapKeyManagement,
                               kManagement));
  EXPECT_CALL(
      store,
      SetString(kId, EapCredentials::kStorageCredentialEapPassword, kPassword));
  eap_.Save(&store, kId, true);
}

TEST_F(EapCredentialsTest, OutputConnectionMetrics) {
  Error unused_error;
  SetEap(kEapMethodPEAP);
  SetInnerEap(kEapPhase2AuthPEAPMSCHAPV2);

  MockMetrics metrics;
  EXPECT_CALL(metrics, SendEnumToUMA("Network.Shill.Wifi.EapOuterProtocol",
                                     Metrics::kEapOuterProtocolPeap,
                                     Metrics::kEapOuterProtocolMax));
  EXPECT_CALL(metrics, SendEnumToUMA("Network.Shill.Wifi.EapInnerProtocol",
                                     Metrics::kEapInnerProtocolPeapMschapv2,
                                     Metrics::kEapInnerProtocolMax));
  eap_.OutputConnectionMetrics(&metrics, Technology::kWifi);
}

TEST_F(EapCredentialsTest, PopulateSupplicantProperties) {
  SetIdentity("testidentity");
  SetPin("xxxx");
  PopulateSupplicantProperties();
  // Test that only non-empty 802.1x properties are populated.
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapIdentity));
  EXPECT_FALSE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapKeyId));
  EXPECT_FALSE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapCaCert));

  // Test that CA path is set by default.
  EXPECT_TRUE(params_.Contains<string>(WPASupplicant::kNetworkPropertyCaPath));

  // Test that hardware-backed security arguments are not set, since
  // neither key-id nor cert-id were set.
  EXPECT_FALSE(params_.Contains<string>(WPASupplicant::kNetworkPropertyEapPin));
  EXPECT_FALSE(
      params_.Contains<uint32_t>(WPASupplicant::kNetworkPropertyEngine));
  EXPECT_FALSE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEngineId));

  // Test EAP version translation.  The "phase1" supplicant parameter is
  // normally empty, but it will contain a "tls_disable" flag if this
  // service requests an old TLS version.
  EXPECT_FALSE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapOuterEap));

  SetTLSVersionMax("1.2");
  PopulateSupplicantProperties();
  EXPECT_FALSE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapOuterEap));

  SetTLSVersionMax("1.0");
  PopulateSupplicantProperties();
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapOuterEap));
  string phase1 =
      params_.Get<string>(WPASupplicant::kNetworkPropertyEapOuterEap);
  EXPECT_EQ(string::npos, phase1.find("disable_tlsv1_0=1"));
  EXPECT_NE(string::npos, phase1.find("disable_tlsv1_1=1"));
  EXPECT_NE(string::npos, phase1.find("disable_tlsv1_2=1"));
}

TEST_F(EapCredentialsTest, PopulateSupplicantPropertiesNoSystemCAs) {
  SetIdentity("testidentity");
  SetUseSystemCAs(false);
  PopulateSupplicantProperties();
  // Test that CA path is not set if use_system_cas is explicitly false.
  EXPECT_FALSE(params_.Contains<string>(WPASupplicant::kNetworkPropertyCaPath));
}

TEST_F(EapCredentialsTest,
       PopulateSupplicantPropertiesProactiveKeyCachingDisabledByDefault) {
  SetIdentity("testidentity");
  PopulateSupplicantProperties();

  ASSERT_TRUE(params_.Contains<uint32_t>(
      WPASupplicant::kNetworkPropertyEapProactiveKeyCaching));

  const uint32_t kProactiveKeyCachingDisabled(0);

  EXPECT_EQ(kProactiveKeyCachingDisabled,
            params_.Get<uint32_t>(
                WPASupplicant::kNetworkPropertyEapProactiveKeyCaching));
}

TEST_F(EapCredentialsTest,
       PopulateSupplicantPropertiesEnableProactiveKeyCaching) {
  SetIdentity("testidentity");
  SetUseProactiveKeyCaching(true);
  PopulateSupplicantProperties();

  ASSERT_TRUE(params_.Contains<uint32_t>(
      WPASupplicant::kNetworkPropertyEapProactiveKeyCaching));

  const uint32_t kProactiveKeyCachingEnabled(1);

  EXPECT_EQ(kProactiveKeyCachingEnabled,
            params_.Get<uint32_t>(
                WPASupplicant::kNetworkPropertyEapProactiveKeyCaching));
}

TEST_F(EapCredentialsTest,
       PopulateSupplicantPropertiesDisableProactiveKeyCaching) {
  SetIdentity("testidentity");
  SetUseProactiveKeyCaching(false);
  PopulateSupplicantProperties();

  ASSERT_TRUE(params_.Contains<uint32_t>(
      WPASupplicant::kNetworkPropertyEapProactiveKeyCaching));

  const uint32_t kProactiveKeyCachingDisabled(0);

  EXPECT_EQ(kProactiveKeyCachingDisabled,
            params_.Get<uint32_t>(
                WPASupplicant::kNetworkPropertyEapProactiveKeyCaching));
}

TEST_F(EapCredentialsTest, PopulateSupplicantPropertiesUsingHardwareAuth) {
  SetIdentity("testidentity");
  SetKeyId("key_id");
  SetPin("xxxx");
  SetEap("PEAP");
  PopulateSupplicantProperties();
  // Test that EAP engine parameters are not set if the authentication type
  // is not one that accepts a client certificate.
  EXPECT_FALSE(params_.Contains<string>(WPASupplicant::kNetworkPropertyEapPin));
  EXPECT_FALSE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapKeyId));
  EXPECT_FALSE(
      params_.Contains<uint32_t>(WPASupplicant::kNetworkPropertyEngine));
  EXPECT_FALSE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEngineId));

  // Test that EAP engine parameters are set if key_id is set and the
  // authentication type accepts a client certificate.
  params_.Clear();
  SetEap("TLS");
  PopulateSupplicantProperties();
  EXPECT_TRUE(params_.Contains<string>(WPASupplicant::kNetworkPropertyEapPin));
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapKeyId));
  EXPECT_TRUE(
      params_.Contains<uint32_t>(WPASupplicant::kNetworkPropertyEngine));
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEngineId));

  // An empty EAP parameter should be considered to be possibly "TLS".
  params_.Clear();
  SetEap("");
  PopulateSupplicantProperties();
  EXPECT_TRUE(params_.Contains<string>(WPASupplicant::kNetworkPropertyEapPin));
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapKeyId));
  EXPECT_TRUE(
      params_.Contains<uint32_t>(WPASupplicant::kNetworkPropertyEngine));
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEngineId));

  // Test that EAP engine parameters are set if ca_cert_id is set even if the
  // authentication type does not accept a client certificate.  However,
  // the client key id should not be provided.
  params_.Clear();
  SetEap("PEAP");
  SetCACertId("certid");
  PopulateSupplicantProperties();
  EXPECT_TRUE(params_.Contains<string>(WPASupplicant::kNetworkPropertyEapPin));
  EXPECT_FALSE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapKeyId));
  EXPECT_TRUE(
      params_.Contains<uint32_t>(WPASupplicant::kNetworkPropertyEngine));
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEngineId));
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapCaCertId));
}

TEST_F(EapCredentialsTest, PopulateSupplicantPropertiesPEM) {
  const vector<string> kPemCert{"-pem-certificate-here-"};
  SetCACertPEM(kPemCert);
  const string kPEMCertfile("/tmp/pem-cert");
  FilePath pem_cert(kPEMCertfile);
  EXPECT_CALL(certificate_file_, CreatePEMFromStrings(kPemCert))
      .WillOnce(Return(pem_cert));

  PopulateSupplicantProperties();
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapCaCert));
  if (params_.Contains<string>(WPASupplicant::kNetworkPropertyEapCaCert)) {
    EXPECT_EQ(kPEMCertfile,
              params_.Get<string>(WPASupplicant::kNetworkPropertyEapCaCert));
  }
}

TEST_F(EapCredentialsTest, Reset) {
  EXPECT_TRUE(IsReset());
  EXPECT_TRUE(GetKeyManagement().empty());
  SetAnonymousIdentity("foo");
  SetCACertId("foo");
  SetCACertPEM(vector<string>{"foo"});
  SetCertId("foo");
  SetEap("foo");
  SetIdentity("foo");
  SetInnerEap("foo");
  SetKeyId("foo");
  SetPassword("foo");
  SetPin("foo");
  SetUseSystemCAs(false);
  SetUseProactiveKeyCaching(true);
  SetUseLoginPassword(false);
  SetSubjectAlternativeNameMatch(vector<string>{"foo"});
  eap_.SetKeyManagement("foo", nullptr);
  EXPECT_FALSE(IsReset());
  EXPECT_FALSE(GetKeyManagement().empty());
  eap_.Reset();
  EXPECT_TRUE(IsReset());
  EXPECT_FALSE(GetKeyManagement().empty());
}

TEST_F(EapCredentialsTest, SetKeyManagement) {
  const string kKeyManagement0("foo");
  eap_.SetKeyManagement(kKeyManagement0, nullptr);
  EXPECT_EQ(kKeyManagement0, GetKeyManagement());

  const string kKeyManagement1("bar");
  eap_.SetKeyManagement(kKeyManagement1, nullptr);
  EXPECT_EQ(kKeyManagement1, GetKeyManagement());

  // We should not be able to set the key management to an empty string.
  eap_.SetKeyManagement("", nullptr);
  EXPECT_EQ(kKeyManagement1, GetKeyManagement());
}

// Custom property setters should return false, and make no changes, if
// the new value is the same as the old value.
TEST_F(EapCredentialsTest, CustomSetterNoopChange) {
  // SetEapKeyManagement
  {
    const string kKeyManagement("foo");
    Error error;
    // Set to known value.
    EXPECT_TRUE(eap_.SetKeyManagement(kKeyManagement, &error));
    EXPECT_TRUE(error.IsSuccess());
    // Set to same value.
    EXPECT_FALSE(eap_.SetKeyManagement(kKeyManagement, &error));
    EXPECT_TRUE(error.IsSuccess());
  }

  // SetEapPassword
  {
    const string kPassword("foo");
    Error error;
    // Set to known value.
    EXPECT_TRUE(SetEapPassword(kPassword, &error));
    EXPECT_TRUE(error.IsSuccess());
    // Set to same value.
    EXPECT_FALSE(SetEapPassword(kPassword, &error));
    EXPECT_TRUE(error.IsSuccess());
  }
}

TEST_F(EapCredentialsTest, TestUseLoginPassword) {
  const string kPasswordStr("thepassword");
  SaveLoginPassword(kPasswordStr);

  SetUseLoginPassword(true);
  PopulateSupplicantProperties();

  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapCaPassword));
  string used_password =
      params_.Get<string>(WPASupplicant::kNetworkPropertyEapCaPassword);
  EXPECT_EQ(used_password, kPasswordStr);
}

TEST_F(EapCredentialsTest, TestDontUseLoginPassword) {
  const string kPasswordStr("thepassword");
  SaveLoginPassword(kPasswordStr);

  SetUseLoginPassword(false);
  PopulateSupplicantProperties();

  EXPECT_FALSE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapCaPassword));
}

TEST_F(EapCredentialsTest, TestSubjectAlternativeNameMatchTranslation) {
  const vector<string> subject_alternative_name_match_list(
      {"{\"Type\":\"EMAIL\",\"Value\":\"my_email_1\"}",
       "{\"Type\":\"EMAIL\",\"Value\":\"my_email_2\"}",
       "{\"Type\":\"EMAIL\",\"Value\":\"my;email\"}",
       "{\"Type\":\"DNS\",\"Value\":\"my_dns\"}",
       "{\"Type\":\"URI\",\"Value\":\"my_uri\"}"});
  string expected_translated =
      "EMAIL:my_email_1;EMAIL:my_email_2;EMAIL:my;email;DNS:my_dns;URI:my_uri";
  base::Optional<string> altsubject_match =
      EapCredentials::TranslateSubjectAlternativeNameMatch(
          subject_alternative_name_match_list);
  EXPECT_TRUE(altsubject_match.has_value());
  EXPECT_EQ(altsubject_match.value(), expected_translated);
}

TEST_F(EapCredentialsTest, TestSubjectAlternativeNameMatchTranslationFailure) {
  const vector<string> subject_alternative_name_match_list(
      {"{\"TYPE\":\"EMAIL\",\"Value\":\"my;email\"}"});
  base::Optional<string> altsubject_match =
      EapCredentials::TranslateSubjectAlternativeNameMatch(
          subject_alternative_name_match_list);
  EXPECT_FALSE(altsubject_match.has_value());
}

TEST_F(EapCredentialsTest, TestEapInnerAuthMschapv2NoRetryFlag) {
  // If no EAP inner auth is set, no additional  mschapv2_retry flag is added.
  SetInnerEap("");
  PopulateSupplicantProperties();
  EXPECT_FALSE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapInnerEap));

  // If an EAP inner auth different than MSCHPAV2 is set, also expect no change.
  SetInnerEap("auth=MD5");
  PopulateSupplicantProperties();
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapInnerEap));
  {
    const std::string inner_eap =
        params_.Get<string>(WPASupplicant::kNetworkPropertyEapInnerEap);
    EXPECT_EQ(inner_eap, "auth=MD5");
  }

  // If EAP inner auth is set to MSCHAPV2, the flag should be added.
  SetInnerEap("auth=MSCHAPV2");
  PopulateSupplicantProperties();
  EXPECT_TRUE(
      params_.Contains<string>(WPASupplicant::kNetworkPropertyEapInnerEap));
  {
    const std::string inner_eap =
        params_.Get<string>(WPASupplicant::kNetworkPropertyEapInnerEap);
    EXPECT_EQ(inner_eap, "auth=MSCHAPV2 mschapv2_retry=0");
  }
}

}  // namespace shill
