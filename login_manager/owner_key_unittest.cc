// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/owner_key.h"

#include <vector>

#include <base/basictypes.h>
#include <base/file_path.h>
#include <base/file_util.h>
#include <base/logging.h>
#include <base/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace login_manager {
class OwnerKeyTest : public ::testing::Test {
 public:
  OwnerKeyTest() {}
  virtual ~OwnerKeyTest() {}

  virtual void SetUp() {
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
    ASSERT_TRUE(file_util::CreateTemporaryFileInDir(tmpdir_.path(), &tmpfile_));
    ASSERT_EQ(2, file_util::WriteFile(tmpfile_, "a", 2));
  }

  void StartUnowned() {
    file_util::Delete(tmpfile_, false);
  }

  FilePath tmpfile_;

 private:
  ScopedTempDir tmpdir_;
  DISALLOW_COPY_AND_ASSIGN(OwnerKeyTest);
};

TEST_F(OwnerKeyTest, LoadKey) {
  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(OwnerKeyTest, NoKeyToLoad) {
  StartUnowned();
  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
}

TEST_F(OwnerKeyTest, NoKeyOnDiskAllowSetting) {
  StartUnowned();
  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  std::vector<uint8> fake(1, 1);
  ASSERT_TRUE(key.PopulateFromBuffer(fake));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(OwnerKeyTest, EnforceDiskCheckFirst) {
  std::vector<uint8> fake(1, 1);

  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_FALSE(key.PopulateFromBuffer(fake));
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_FALSE(key.HaveCheckedDisk());
}

TEST_F(OwnerKeyTest, RefuseToClobberInMemory) {
  std::vector<uint8> fake(1, 1);

  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  ASSERT_FALSE(key.PopulateFromBuffer(fake));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(OwnerKeyTest, RefuseToClobberOnDisk) {
  std::vector<uint8> fake(1, 1);

  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  ASSERT_FALSE(key.Persist());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

// Since base::SignatureVerifier already has its own tests, and we're really
// just wrapping that functionality here, we mostly rely on those tests to
// ensure correct behavior.  This test is just to make sure that we can
// pass data through appropriately and use the API correctly.
TEST_F(OwnerKeyTest, Verify) {
  // The input data in this test comes from real certificates.
  //
  // tbs_certificate ("to-be-signed certificate", the part of a certificate
  // that is signed), signature_algorithm, and algorithm come from the
  // certificate of bugs.webkit.org.
  //
  // public_key_info comes from the certificate of the issuer, Go Daddy Secure
  // Certification Authority.
  //
  // The bytes in the array initializers are formatted to expose the DER
  // encoding of the ASN.1 structures.

  // The data that is signed is the following ASN.1 structure:
  //    TBSCertificate  ::=  SEQUENCE  {
  //        ...  -- omitted, not important
  //        }
  const uint8 tbs_certificate[1017] = {
    0x30, 0x82, 0x03, 0xf5,  // a SEQUENCE of length 1013 (0x3f5)
    0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x03, 0x43, 0xdd, 0x63, 0x30, 0x0d,
    0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05,
    0x00, 0x30, 0x81, 0xca, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04,
    0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x10, 0x30, 0x0e, 0x06, 0x03, 0x55,
    0x04, 0x08, 0x13, 0x07, 0x41, 0x72, 0x69, 0x7a, 0x6f, 0x6e, 0x61, 0x31,
    0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x0a, 0x53, 0x63,
    0x6f, 0x74, 0x74, 0x73, 0x64, 0x61, 0x6c, 0x65, 0x31, 0x1a, 0x30, 0x18,
    0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x11, 0x47, 0x6f, 0x44, 0x61, 0x64,
    0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x2c, 0x20, 0x49, 0x6e, 0x63, 0x2e,
    0x31, 0x33, 0x30, 0x31, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x2a, 0x68,
    0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x65, 0x72, 0x74, 0x69, 0x66,
    0x69, 0x63, 0x61, 0x74, 0x65, 0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64,
    0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73,
    0x69, 0x74, 0x6f, 0x72, 0x79, 0x31, 0x30, 0x30, 0x2e, 0x06, 0x03, 0x55,
    0x04, 0x03, 0x13, 0x27, 0x47, 0x6f, 0x20, 0x44, 0x61, 0x64, 0x64, 0x79,
    0x20, 0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x20, 0x43, 0x65, 0x72, 0x74,
    0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x41, 0x75,
    0x74, 0x68, 0x6f, 0x72, 0x69, 0x74, 0x79, 0x31, 0x11, 0x30, 0x0f, 0x06,
    0x03, 0x55, 0x04, 0x05, 0x13, 0x08, 0x30, 0x37, 0x39, 0x36, 0x39, 0x32,
    0x38, 0x37, 0x30, 0x1e, 0x17, 0x0d, 0x30, 0x38, 0x30, 0x33, 0x31, 0x38,
    0x32, 0x33, 0x33, 0x35, 0x31, 0x39, 0x5a, 0x17, 0x0d, 0x31, 0x31, 0x30,
    0x33, 0x31, 0x38, 0x32, 0x33, 0x33, 0x35, 0x31, 0x39, 0x5a, 0x30, 0x79,
    0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55,
    0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x0a,
    0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f, 0x72, 0x6e, 0x69, 0x61, 0x31, 0x12,
    0x30, 0x10, 0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x09, 0x43, 0x75, 0x70,
    0x65, 0x72, 0x74, 0x69, 0x6e, 0x6f, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03,
    0x55, 0x04, 0x0a, 0x13, 0x0a, 0x41, 0x70, 0x70, 0x6c, 0x65, 0x20, 0x49,
    0x6e, 0x63, 0x2e, 0x31, 0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x0b,
    0x13, 0x0c, 0x4d, 0x61, 0x63, 0x20, 0x4f, 0x53, 0x20, 0x46, 0x6f, 0x72,
    0x67, 0x65, 0x31, 0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13,
    0x0c, 0x2a, 0x2e, 0x77, 0x65, 0x62, 0x6b, 0x69, 0x74, 0x2e, 0x6f, 0x72,
    0x67, 0x30, 0x81, 0x9f, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
    0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x81, 0x8d, 0x00, 0x30,
    0x81, 0x89, 0x02, 0x81, 0x81, 0x00, 0xa7, 0x62, 0x79, 0x41, 0xda, 0x28,
    0xf2, 0xc0, 0x4f, 0xe0, 0x25, 0xaa, 0xa1, 0x2e, 0x3b, 0x30, 0x94, 0xb5,
    0xc9, 0x26, 0x3a, 0x1b, 0xe2, 0xd0, 0xcc, 0xa2, 0x95, 0xe2, 0x91, 0xc0,
    0xf0, 0x40, 0x9e, 0x27, 0x6e, 0xbd, 0x6e, 0xde, 0x7c, 0xb6, 0x30, 0x5c,
    0xb8, 0x9b, 0x01, 0x2f, 0x92, 0x04, 0xa1, 0xef, 0x4a, 0xb1, 0x6c, 0xb1,
    0x7e, 0x8e, 0xcd, 0xa6, 0xf4, 0x40, 0x73, 0x1f, 0x2c, 0x96, 0xad, 0xff,
    0x2a, 0x6d, 0x0e, 0xba, 0x52, 0x84, 0x83, 0xb0, 0x39, 0xee, 0xc9, 0x39,
    0xdc, 0x1e, 0x34, 0xd0, 0xd8, 0x5d, 0x7a, 0x09, 0xac, 0xa9, 0xee, 0xca,
    0x65, 0xf6, 0x85, 0x3a, 0x6b, 0xee, 0xe4, 0x5c, 0x5e, 0xf8, 0xda, 0xd1,
    0xce, 0x88, 0x47, 0xcd, 0x06, 0x21, 0xe0, 0xb9, 0x4b, 0xe4, 0x07, 0xcb,
    0x57, 0xdc, 0xca, 0x99, 0x54, 0xf7, 0x0e, 0xd5, 0x17, 0x95, 0x05, 0x2e,
    0xe9, 0xb1, 0x02, 0x03, 0x01, 0x00, 0x01, 0xa3, 0x82, 0x01, 0xce, 0x30,
    0x82, 0x01, 0xca, 0x30, 0x09, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x04, 0x02,
    0x30, 0x00, 0x30, 0x0b, 0x06, 0x03, 0x55, 0x1d, 0x0f, 0x04, 0x04, 0x03,
    0x02, 0x05, 0xa0, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x25, 0x04, 0x16,
    0x30, 0x14, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01,
    0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x02, 0x30, 0x57,
    0x06, 0x03, 0x55, 0x1d, 0x1f, 0x04, 0x50, 0x30, 0x4e, 0x30, 0x4c, 0xa0,
    0x4a, 0xa0, 0x48, 0x86, 0x46, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f,
    0x63, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x65, 0x73,
    0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d,
    0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69, 0x74, 0x6f, 0x72, 0x79, 0x2f,
    0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79, 0x65, 0x78, 0x74, 0x65, 0x6e,
    0x64, 0x65, 0x64, 0x69, 0x73, 0x73, 0x75, 0x69, 0x6e, 0x67, 0x33, 0x2e,
    0x63, 0x72, 0x6c, 0x30, 0x52, 0x06, 0x03, 0x55, 0x1d, 0x20, 0x04, 0x4b,
    0x30, 0x49, 0x30, 0x47, 0x06, 0x0b, 0x60, 0x86, 0x48, 0x01, 0x86, 0xfd,
    0x6d, 0x01, 0x07, 0x17, 0x02, 0x30, 0x38, 0x30, 0x36, 0x06, 0x08, 0x2b,
    0x06, 0x01, 0x05, 0x05, 0x07, 0x02, 0x01, 0x16, 0x2a, 0x68, 0x74, 0x74,
    0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63,
    0x61, 0x74, 0x65, 0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79,
    0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69, 0x74,
    0x6f, 0x72, 0x79, 0x30, 0x7f, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05,
    0x07, 0x01, 0x01, 0x04, 0x73, 0x30, 0x71, 0x30, 0x23, 0x06, 0x08, 0x2b,
    0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x01, 0x86, 0x17, 0x68, 0x74, 0x74,
    0x70, 0x3a, 0x2f, 0x2f, 0x6f, 0x63, 0x73, 0x70, 0x2e, 0x67, 0x6f, 0x64,
    0x61, 0x64, 0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x30, 0x4a, 0x06, 0x08,
    0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x02, 0x86, 0x3e, 0x68, 0x74,
    0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69,
    0x63, 0x61, 0x74, 0x65, 0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64,
    0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69,
    0x74, 0x6f, 0x72, 0x79, 0x2f, 0x67, 0x64, 0x5f, 0x69, 0x6e, 0x74, 0x65,
    0x72, 0x6d, 0x65, 0x64, 0x69, 0x61, 0x74, 0x65, 0x2e, 0x63, 0x72, 0x74,
    0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0x48,
    0xdf, 0x60, 0x32, 0xcc, 0x89, 0x01, 0xb6, 0xdc, 0x2f, 0xe3, 0x73, 0xb5,
    0x9c, 0x16, 0x58, 0x32, 0x68, 0xa9, 0xc3, 0x30, 0x1f, 0x06, 0x03, 0x55,
    0x1d, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0xfd, 0xac, 0x61, 0x32,
    0x93, 0x6c, 0x45, 0xd6, 0xe2, 0xee, 0x85, 0x5f, 0x9a, 0xba, 0xe7, 0x76,
    0x99, 0x68, 0xcc, 0xe7, 0x30, 0x23, 0x06, 0x03, 0x55, 0x1d, 0x11, 0x04,
    0x1c, 0x30, 0x1a, 0x82, 0x0c, 0x2a, 0x2e, 0x77, 0x65, 0x62, 0x6b, 0x69,
    0x74, 0x2e, 0x6f, 0x72, 0x67, 0x82, 0x0a, 0x77, 0x65, 0x62, 0x6b, 0x69,
    0x74, 0x2e, 0x6f, 0x72, 0x67
  };

  // RSA signature, a big integer in the big-endian byte order.
  const uint8 signature[256] = {
    0x1e, 0x6a, 0xe7, 0xe0, 0x4f, 0xe7, 0x4d, 0xd0, 0x69, 0x7c, 0xf8, 0x8f,
    0x99, 0xb4, 0x18, 0x95, 0x36, 0x24, 0x0f, 0x0e, 0xa3, 0xea, 0x34, 0x37,
    0xf4, 0x7d, 0xd5, 0x92, 0x35, 0x53, 0x72, 0x76, 0x3f, 0x69, 0xf0, 0x82,
    0x56, 0xe3, 0x94, 0x7a, 0x1d, 0x1a, 0x81, 0xaf, 0x9f, 0xc7, 0x43, 0x01,
    0x64, 0xd3, 0x7c, 0x0d, 0xc8, 0x11, 0x4e, 0x4a, 0xe6, 0x1a, 0xc3, 0x01,
    0x74, 0xe8, 0x35, 0x87, 0x5c, 0x61, 0xaa, 0x8a, 0x46, 0x06, 0xbe, 0x98,
    0x95, 0x24, 0x9e, 0x01, 0xe3, 0xe6, 0xa0, 0x98, 0xee, 0x36, 0x44, 0x56,
    0x8d, 0x23, 0x9c, 0x65, 0xea, 0x55, 0x6a, 0xdf, 0x66, 0xee, 0x45, 0xe8,
    0xa0, 0xe9, 0x7d, 0x9a, 0xba, 0x94, 0xc5, 0xc8, 0xc4, 0x4b, 0x98, 0xff,
    0x9a, 0x01, 0x31, 0x6d, 0xf9, 0x2b, 0x58, 0xe7, 0xe7, 0x2a, 0xc5, 0x4d,
    0xbb, 0xbb, 0xcd, 0x0d, 0x70, 0xe1, 0xad, 0x03, 0xf5, 0xfe, 0xf4, 0x84,
    0x71, 0x08, 0xd2, 0xbc, 0x04, 0x7b, 0x26, 0x1c, 0xa8, 0x0f, 0x9c, 0xd8,
    0x12, 0x6a, 0x6f, 0x2b, 0x67, 0xa1, 0x03, 0x80, 0x9a, 0x11, 0x0b, 0xe9,
    0xe0, 0xb5, 0xb3, 0xb8, 0x19, 0x4e, 0x0c, 0xa4, 0xd9, 0x2b, 0x3b, 0xc2,
    0xca, 0x20, 0xd3, 0x0c, 0xa4, 0xff, 0x93, 0x13, 0x1f, 0xfc, 0xba, 0x94,
    0x93, 0x8c, 0x64, 0x15, 0x2e, 0x28, 0xa9, 0x55, 0x8c, 0x2c, 0x48, 0xd3,
    0xd3, 0xc1, 0x50, 0x69, 0x19, 0xe8, 0x34, 0xd3, 0xf1, 0x04, 0x9f, 0x0a,
    0x7a, 0x21, 0x87, 0xbf, 0xb9, 0x59, 0x37, 0x2e, 0xf4, 0x71, 0xa5, 0x3e,
    0xbe, 0xcd, 0x70, 0x83, 0x18, 0xf8, 0x8a, 0x72, 0x85, 0x45, 0x1f, 0x08,
    0x01, 0x6f, 0x37, 0xf5, 0x2b, 0x7b, 0xea, 0xb9, 0x8b, 0xa3, 0xcc, 0xfd,
    0x35, 0x52, 0xdd, 0x66, 0xde, 0x4f, 0x30, 0xc5, 0x73, 0x81, 0xb6, 0xe8,
    0x3c, 0xd8, 0x48, 0x8a
  };

  // The public key is specified as the following ASN.1 structure:
  //   SubjectPublicKeyInfo  ::=  SEQUENCE  {
  //       algorithm            AlgorithmIdentifier,
  //       subjectPublicKey     BIT STRING  }
  const uint8 public_key_info[294] = {
    0x30, 0x82, 0x01, 0x22,  // a SEQUENCE of length 290 (0x122)
      // algorithm
      0x30, 0x0d,  // a SEQUENCE of length 13
        0x06, 0x09,  // an OBJECT IDENTIFIER of length 9
          0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01,
        0x05, 0x00,  // a NULL of length 0
      // subjectPublicKey
      0x03, 0x82, 0x01, 0x0f,  // a BIT STRING of length 271 (0x10f)
        0x00,  // number of unused bits
        0x30, 0x82, 0x01, 0x0a,  // a SEQUENCE of length 266 (0x10a)
          // modulus
          0x02, 0x82, 0x01, 0x01,  // an INTEGER of length 257 (0x101)
            0x00, 0xc4, 0x2d, 0xd5, 0x15, 0x8c, 0x9c, 0x26, 0x4c, 0xec,
            0x32, 0x35, 0xeb, 0x5f, 0xb8, 0x59, 0x01, 0x5a, 0xa6, 0x61,
            0x81, 0x59, 0x3b, 0x70, 0x63, 0xab, 0xe3, 0xdc, 0x3d, 0xc7,
            0x2a, 0xb8, 0xc9, 0x33, 0xd3, 0x79, 0xe4, 0x3a, 0xed, 0x3c,
            0x30, 0x23, 0x84, 0x8e, 0xb3, 0x30, 0x14, 0xb6, 0xb2, 0x87,
            0xc3, 0x3d, 0x95, 0x54, 0x04, 0x9e, 0xdf, 0x99, 0xdd, 0x0b,
            0x25, 0x1e, 0x21, 0xde, 0x65, 0x29, 0x7e, 0x35, 0xa8, 0xa9,
            0x54, 0xeb, 0xf6, 0xf7, 0x32, 0x39, 0xd4, 0x26, 0x55, 0x95,
            0xad, 0xef, 0xfb, 0xfe, 0x58, 0x86, 0xd7, 0x9e, 0xf4, 0x00,
            0x8d, 0x8c, 0x2a, 0x0c, 0xbd, 0x42, 0x04, 0xce, 0xa7, 0x3f,
            0x04, 0xf6, 0xee, 0x80, 0xf2, 0xaa, 0xef, 0x52, 0xa1, 0x69,
            0x66, 0xda, 0xbe, 0x1a, 0xad, 0x5d, 0xda, 0x2c, 0x66, 0xea,
            0x1a, 0x6b, 0xbb, 0xe5, 0x1a, 0x51, 0x4a, 0x00, 0x2f, 0x48,
            0xc7, 0x98, 0x75, 0xd8, 0xb9, 0x29, 0xc8, 0xee, 0xf8, 0x66,
            0x6d, 0x0a, 0x9c, 0xb3, 0xf3, 0xfc, 0x78, 0x7c, 0xa2, 0xf8,
            0xa3, 0xf2, 0xb5, 0xc3, 0xf3, 0xb9, 0x7a, 0x91, 0xc1, 0xa7,
            0xe6, 0x25, 0x2e, 0x9c, 0xa8, 0xed, 0x12, 0x65, 0x6e, 0x6a,
            0xf6, 0x12, 0x44, 0x53, 0x70, 0x30, 0x95, 0xc3, 0x9c, 0x2b,
            0x58, 0x2b, 0x3d, 0x08, 0x74, 0x4a, 0xf2, 0xbe, 0x51, 0xb0,
            0xbf, 0x87, 0xd0, 0x4c, 0x27, 0x58, 0x6b, 0xb5, 0x35, 0xc5,
            0x9d, 0xaf, 0x17, 0x31, 0xf8, 0x0b, 0x8f, 0xee, 0xad, 0x81,
            0x36, 0x05, 0x89, 0x08, 0x98, 0xcf, 0x3a, 0xaf, 0x25, 0x87,
            0xc0, 0x49, 0xea, 0xa7, 0xfd, 0x67, 0xf7, 0x45, 0x8e, 0x97,
            0xcc, 0x14, 0x39, 0xe2, 0x36, 0x85, 0xb5, 0x7e, 0x1a, 0x37,
            0xfd, 0x16, 0xf6, 0x71, 0x11, 0x9a, 0x74, 0x30, 0x16, 0xfe,
            0x13, 0x94, 0xa3, 0x3f, 0x84, 0x0d, 0x4f,
          // public exponent
          0x02, 0x03,  // an INTEGER of length 3
            0x01, 0x00, 0x01
  };

  StartUnowned();
  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  std::vector<uint8> fake;
  fake.resize(sizeof(public_key_info));
  memcpy(&fake[0], public_key_info, fake.size());

  ASSERT_TRUE(key.PopulateFromBuffer(fake));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  EXPECT_TRUE(key.Verify(reinterpret_cast<const char*>(tbs_certificate),
                         sizeof(tbs_certificate),
                         reinterpret_cast<const char*>(signature),
                         sizeof(signature)));
}

}  // namespace login_manager
