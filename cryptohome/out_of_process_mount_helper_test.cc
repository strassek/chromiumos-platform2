// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for OutOfProcessMountHelper.

#include "cryptohome/out_of_process_mount_helper.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/cryptohome.h>
#include <gtest/gtest.h>

#include "cryptohome/make_tests.h"
#include "cryptohome/mock_platform.h"

#include "cryptohome/namespace_mounter_ipc.pb.h"

using brillo::cryptohome::home::kGuestUserName;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

const FilePath kImageDir("test_image_dir");
const FilePath kImageSaltFile = kImageDir.Append("salt");

constexpr pid_t kOOPHelperPid = 2;

constexpr int kInvalidFd = -1;

}  // namespace

namespace cryptohome {

class OutOfProcessMountHelperTest : public ::testing::Test {
 public:
  OutOfProcessMountHelperTest() {}
  virtual ~OutOfProcessMountHelperTest() {}

  void SetUp() {
    // Populate the system salt.
    helper_.SetUpSystemSalt();
    helper_.InjectSystemSalt(&platform_, kImageSaltFile);

    out_of_process_mounter_.reset(new OutOfProcessMountHelper(
        helper_.system_salt, true /* legacy_mount */, &platform_));
  }

  void TearDown() {
    out_of_process_mounter_ = nullptr;
    helper_.TearDownSystemSalt();
  }

  bool CreatePipe(base::ScopedFD* read_end, base::ScopedFD* write_end) {
    int pipe[2];
    bool success = base::CreateLocalNonBlockingPipe(pipe);
    if (success) {
      read_end->reset(pipe[0]);
      write_end->reset(pipe[1]);
    }
    return success;
  }

  base::ScopedFD GetDevNullFd() {
    return base::ScopedFD(HANDLE_EINTR(open("/dev/null", O_WRONLY)));
  }

  base::ScopedFD GetDevZeroFd() {
    return base::ScopedFD(HANDLE_EINTR(open("/dev/zero", O_RDONLY)));
  }

 protected:
  MakeTests helper_;
  NiceMock<MockPlatform> platform_;
  std::unique_ptr<OutOfProcessMountHelper> out_of_process_mounter_;

 private:
  DISALLOW_COPY_AND_ASSIGN(OutOfProcessMountHelperTest);
};

TEST_F(OutOfProcessMountHelperTest, MountGuestUserDirOOP) {
  brillo::ProcessMock* process = platform_.mock_process();
  EXPECT_CALL(*process, Start()).WillOnce(Return(true));
  EXPECT_CALL(*process, pid()).WillRepeatedly(Return(kOOPHelperPid));
  EXPECT_CALL(*process, Wait()).WillOnce(Return(0));

  // Allow reading from cryptohome's perspective.
  base::ScopedFD read_end, write_end;
  ASSERT_TRUE(CreatePipe(&read_end, &write_end));

  EXPECT_CALL(*process, GetPipe(STDOUT_FILENO))
      .WillOnce(Return(read_end.get()));

  // Writing from cryptohome's perspective always succeeds.
  base::ScopedFD dev_null = GetDevNullFd();
  ASSERT_TRUE(dev_null.is_valid());
  EXPECT_CALL(*process, GetPipe(STDIN_FILENO)).WillOnce(Return(dev_null.get()));

  constexpr char out_data = '0';
  ASSERT_TRUE(
      base::WriteFileDescriptor(write_end.get(), &out_data, sizeof(out_data)));
  ASSERT_TRUE(out_of_process_mounter_->PerformEphemeralMount(kGuestUserName));

  EXPECT_CALL(*process, Kill(SIGTERM, _)).WillOnce(Return(true));
  out_of_process_mounter_->TearDownEphemeralMount();
}

TEST_F(OutOfProcessMountHelperTest, MountGuestUserDirOOPWriteProtobuf) {
  brillo::ProcessMock* process = platform_.mock_process();
  EXPECT_CALL(*process, Start()).WillOnce(Return(true));
  EXPECT_CALL(*process, pid()).WillRepeatedly(Return(kOOPHelperPid));
  EXPECT_CALL(*process, Wait()).WillOnce(Return(0));

  // Reading from the helper always succeeds.
  base::ScopedFD dev_zero = GetDevZeroFd();
  ASSERT_TRUE(dev_zero.is_valid());
  EXPECT_CALL(*process, GetPipe(STDOUT_FILENO))
      .WillOnce(Return(dev_zero.get()));

  // Allow writing from cryptohome's perspective.
  base::ScopedFD read_end, write_end;
  ASSERT_TRUE(CreatePipe(&read_end, &write_end));
  EXPECT_CALL(*process, GetPipe(STDIN_FILENO))
      .WillOnce(Return(write_end.get()));

  ASSERT_TRUE(out_of_process_mounter_->PerformEphemeralMount(kGuestUserName));

  size_t proto_size = 0;
  ASSERT_TRUE(base::ReadFromFD(
      read_end.get(), reinterpret_cast<char*>(&proto_size), sizeof(size_t)));
  std::vector<char> proto_data(proto_size);
  ASSERT_TRUE(
      base::ReadFromFD(read_end.get(), proto_data.data(), proto_data.size()));

  OutOfProcessMountRequest r;
  ASSERT_TRUE(r.ParseFromArray(proto_data.data(), proto_size));
  EXPECT_EQ(r.username(), kGuestUserName);

  EXPECT_CALL(*process, Kill(SIGTERM, _)).WillOnce(Return(true));
  out_of_process_mounter_->TearDownEphemeralMount();
}

TEST_F(OutOfProcessMountHelperTest, MountGuestUserDirOOPFailsToStart) {
  brillo::ProcessMock* process = platform_.mock_process();
  EXPECT_CALL(*process, Start()).WillOnce(Return(false));
  ASSERT_FALSE(out_of_process_mounter_->PerformEphemeralMount(kGuestUserName));
}

TEST_F(OutOfProcessMountHelperTest, MountGuestUserDirOOPFailsToWriteProtobuf) {
  brillo::ProcessMock* process = platform_.mock_process();
  EXPECT_CALL(*process, Start()).WillOnce(Return(true));
  // After the PID is checked once and the process is killed, pid() should
  // return 0.
  EXPECT_CALL(*process, pid())
      .WillOnce(Return(kOOPHelperPid))
      .WillRepeatedly(Return(0));

  // Writing the protobuf fails.
  EXPECT_CALL(*process, GetPipe(STDIN_FILENO)).WillOnce(Return(kInvalidFd));

  // Reading from the helper always succeeds.
  base::ScopedFD dev_zero = GetDevZeroFd();
  ASSERT_TRUE(dev_zero.is_valid());
  EXPECT_CALL(*process, GetPipe(STDOUT_FILENO))
      .WillOnce(Return(dev_zero.get()));

  // If writing the protobuf fails, OOP mount helper should be killed.
  EXPECT_CALL(*process, Kill(SIGTERM, _)).WillOnce(Return(true));

  ASSERT_FALSE(out_of_process_mounter_->PerformEphemeralMount(kGuestUserName));
}

TEST_F(OutOfProcessMountHelperTest, MountGuestUserDirOOPFailsToReadAck) {
  brillo::ProcessMock* process = platform_.mock_process();
  EXPECT_CALL(*process, Start()).WillOnce(Return(true));
  // After the PID is checked once and the process is killed, pid() should
  // return 0.
  EXPECT_CALL(*process, pid())
      .WillOnce(Return(kOOPHelperPid))
      .WillRepeatedly(Return(0));

  // Writing the system salt succeeds.
  base::ScopedFD dev_null = GetDevNullFd();
  ASSERT_TRUE(dev_null.is_valid());
  EXPECT_CALL(*process, GetPipe(STDIN_FILENO)).WillOnce(Return(dev_null.get()));

  // Reading the ack fails.
  EXPECT_CALL(*process, GetPipe(STDOUT_FILENO)).WillOnce(Return(kInvalidFd));

  // If reading the ack fails, OOP mount helper should be killed.
  EXPECT_CALL(*process, Kill(SIGTERM, _)).WillOnce(Return(true));

  ASSERT_FALSE(out_of_process_mounter_->PerformEphemeralMount(kGuestUserName));
}

TEST_F(OutOfProcessMountHelperTest, MountGuestUserDirOOPFailsToPoke) {
  brillo::ProcessMock* process = platform_.mock_process();
  EXPECT_CALL(*process, Start()).WillOnce(Return(true));
  EXPECT_CALL(*process, pid()).WillRepeatedly(Return(kOOPHelperPid));

  // Writing the system salt works.
  base::ScopedFD write_to_helper = GetDevNullFd();
  ASSERT_TRUE(write_to_helper.is_valid());
  EXPECT_CALL(*process, GetPipe(STDIN_FILENO))
      .WillOnce(Return(write_to_helper.get()));

  // Reading from the helper always succeeds.
  base::ScopedFD read_from_helper = GetDevZeroFd();
  ASSERT_TRUE(read_from_helper.is_valid());
  EXPECT_CALL(*process, GetPipe(STDOUT_FILENO))
      .WillOnce(Return(read_from_helper.get()));

  ASSERT_TRUE(out_of_process_mounter_->PerformEphemeralMount(kGuestUserName));

  // Poking the helper fails.
  write_to_helper.reset();

  // If poking fails, OOP mount helper should be killed.
  EXPECT_CALL(*process, Kill(SIGTERM, _)).WillOnce(Return(true));
  out_of_process_mounter_->TearDownEphemeralMount();
}

TEST_F(OutOfProcessMountHelperTest, MountGuestUserDirOOPFailsToWait) {
  brillo::ProcessMock* process = platform_.mock_process();
  EXPECT_CALL(*process, Start()).WillOnce(Return(true));
  // After the PID is checked twice and the process is killed, pid() should
  // return 0.
  EXPECT_CALL(*process, pid())
      .WillOnce(Return(kOOPHelperPid))
      .WillOnce(Return(kOOPHelperPid))
      .WillRepeatedly(Return(0));

  // Reading and writing always succeed.
  base::ScopedFD read_from_helper = GetDevZeroFd();
  base::ScopedFD write_to_helper = GetDevNullFd();
  ASSERT_TRUE(read_from_helper.is_valid());
  ASSERT_TRUE(write_to_helper.is_valid());
  EXPECT_CALL(*process, GetPipe(STDIN_FILENO))
      .WillOnce(Return(write_to_helper.get()));
  EXPECT_CALL(*process, GetPipe(STDOUT_FILENO))
      .WillOnce(Return(read_from_helper.get()));

  ASSERT_TRUE(out_of_process_mounter_->PerformEphemeralMount(kGuestUserName));

  // If Wait()-ing fails, unmount should still proceed.
  EXPECT_CALL(*process, Wait()).WillOnce(Return(1));
  out_of_process_mounter_->TearDownEphemeralMount();
}

}  // namespace cryptohome