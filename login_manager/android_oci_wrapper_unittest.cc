// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/android_oci_wrapper.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/mock_system_utils.h"

using ::testing::DoAll;
using ::testing::Ge;
using ::testing::Invoke;
using ::testing::Le;
using ::testing::Ne;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::_;

namespace login_manager {

namespace {

class AndroidOciWrapperTest : public ::testing::Test {
 public:
  AndroidOciWrapperTest() = default;
  ~AndroidOciWrapperTest() override = default;

  void SetUp() override {
    containers_directory_ = base::MakeUnique<base::ScopedTempDir>();
    ASSERT_TRUE(containers_directory_->CreateUniqueTempDir());

    impl_ = base::MakeUnique<AndroidOciWrapper>(&system_utils_,
                                                containers_directory_->path());
  }

 protected:
  void StartContainerAsParent() {
    run_oci_pid_ = 9063;
    container_pid_ = 9064;

    EXPECT_CALL(system_utils_, fork()).WillOnce(Return(run_oci_pid_));

    EXPECT_CALL(system_utils_, Wait(run_oci_pid_, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(0), Return(run_oci_pid_)));

    base::FilePath run_path =
        base::FilePath(ContainerManagerInterface::kContainerRunPath)
            .Append(AndroidOciWrapper::kContainerId)
            .Append(AndroidOciWrapper::kContainerPidName);
    std::string container_pid_str = base::IntToString(container_pid_) + "\n";
    EXPECT_CALL(system_utils_, ReadFileToString(run_path, _))
        .WillOnce(DoAll(SetArgPointee<1>(container_pid_str), Return(true)));

    ASSERT_TRUE(CallStartContainer());
  }

  bool CallStartContainer() {
    return impl_->StartContainer(base::Bind(
        &AndroidOciWrapperTest::ExitCallback, base::Unretained(this)));
  }

  void ExpectKill(bool forceful, int exit_code) {
    std::vector<std::string> argv;
    argv.push_back(AndroidOciWrapper::kRunOciPath);
    if (forceful)
      argv.push_back(AndroidOciWrapper::kRunOciKillSignal);
    argv.push_back(AndroidOciWrapper::kRunOciKillCommand);
    argv.push_back(AndroidOciWrapper::kContainerId);
    EXPECT_CALL(system_utils_, LaunchAndWait(argv, _))
        .WillOnce(DoAll(SetArgPointee<1>(exit_code), Return(true)));
  }

  void ExpectDestroy(int exit_code) {
    const std::vector<std::string> argv = {
        AndroidOciWrapper::kRunOciPath,
        AndroidOciWrapper::kRunOciDestroyCommand,
        AndroidOciWrapper::kContainerId};
    EXPECT_CALL(system_utils_, LaunchAndWait(argv, _))
        .WillOnce(DoAll(SetArgPointee<1>(exit_code), Return(true)));
  }

  void ExitCallback(pid_t pid, bool clean) {
    ASSERT_EQ(pid, container_pid_);

    callback_called_ = true;
    clean_exit_ = clean;
  }

  MockSystemUtils system_utils_;
  std::unique_ptr<base::ScopedTempDir> containers_directory_;

  std::unique_ptr<AndroidOciWrapper> impl_;

  pid_t run_oci_pid_ = 0;
  pid_t container_pid_ = 0;

  bool callback_called_ = false;
  bool clean_exit_ = false;

 private:
  DISALLOW_COPY_AND_ASSIGN(AndroidOciWrapperTest);
};

TEST_F(AndroidOciWrapperTest, KillOnLaunchTimeOut) {
  run_oci_pid_ = 9063;
  container_pid_ = 9064;

  EXPECT_CALL(system_utils_, fork()).WillOnce(Return(run_oci_pid_));

  EXPECT_CALL(system_utils_, Wait(run_oci_pid_, _, _)).WillOnce(Return(0));

  EXPECT_CALL(system_utils_, ProcessGroupIsGone(run_oci_pid_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_utils_, kill(-run_oci_pid_, -1, SIGKILL))
      .WillOnce(Return(0));

  EXPECT_FALSE(CallStartContainer());
}

TEST_F(AndroidOciWrapperTest, ContainerIsManaged) {
  StartContainerAsParent();

  EXPECT_TRUE(impl_->IsManagedJob(container_pid_));
}

TEST_F(AndroidOciWrapperTest, GetRootFsPath) {
  StartContainerAsParent();

  base::FilePath path =
      base::FilePath(ContainerManagerInterface::kContainerRunPath)
          .Append(AndroidOciWrapper::kContainerId)
          .Append(AndroidOciWrapper::kRootFsPath);

  base::FilePath actual_path;
  ASSERT_TRUE(impl_->GetRootFsPath(&actual_path));
  EXPECT_EQ(path, actual_path);
}

TEST_F(AndroidOciWrapperTest, GetContainerPID) {
  StartContainerAsParent();

  pid_t pid;
  ASSERT_TRUE(impl_->GetContainerPID(&pid));
  EXPECT_EQ(container_pid_, pid);
}

TEST_F(AndroidOciWrapperTest, CleanUpOnExit) {
  clean_exit_ = true;

  StartContainerAsParent();

  ExpectDestroy(0 /* exit_code */);

  siginfo_t status;
  impl_->HandleExit(status);

  EXPECT_TRUE(callback_called_);
  EXPECT_FALSE(clean_exit_);
}

TEST_F(AndroidOciWrapperTest, GracefulShutdownOnRequest) {
  StartContainerAsParent();

  ExpectKill(false /* forceful */, 0 /* exit_code */);

  impl_->RequestJobExit();
}

TEST_F(AndroidOciWrapperTest, ForcefulShutdownAfterGracefulShutdownFailed) {
  StartContainerAsParent();

  ExpectKill(false /* forceful */, -1 /* exit_code */);
  ExpectKill(true /* forceful */, 0 /* exit_code */);

  impl_->RequestJobExit();
}

TEST_F(AndroidOciWrapperTest, KillJobOnEnsure) {
  StartContainerAsParent();

  base::TimeDelta delta = base::TimeDelta::FromSeconds(11);
  EXPECT_CALL(system_utils_, ProcessIsGone(container_pid_, delta))
      .WillOnce(Return(false));

  EXPECT_CALL(system_utils_, kill(container_pid_, _, SIGKILL))
      .WillOnce(Return(true));

  EXPECT_CALL(
      system_utils_,
      ProcessIsGone(container_pid_, Le(base::TimeDelta::FromSeconds(5))))
      .WillOnce(Return(true));

  ExpectDestroy(0 /* exit_code */);

  impl_->EnsureJobExit(delta);
}

TEST_F(AndroidOciWrapperTest, CleanExitAfterRequest) {
  StartContainerAsParent();

  ExpectKill(false /* forceful */, 0 /* exit_code */);

  impl_->RequestJobExit();

  base::TimeDelta delta = base::TimeDelta::FromSeconds(11);
  EXPECT_CALL(system_utils_, ProcessIsGone(container_pid_, delta))
      .WillOnce(Return(true));

  ExpectDestroy(0 /* exit_code */);

  impl_->EnsureJobExit(delta);

  EXPECT_TRUE(callback_called_);
  EXPECT_TRUE(clean_exit_);
}

TEST_F(AndroidOciWrapperTest, StartContainerChildProcess) {
  EXPECT_CALL(system_utils_, fork()).WillOnce(Return(0));

  EXPECT_CALL(system_utils_,
              ChangeBlockedSignals(SIG_SETMASK, std::vector<int>()))
      .WillOnce(Return(true));

  base::FilePath container_absolute_path =
      containers_directory_->path().Append("android");
  EXPECT_CALL(system_utils_, chdir(container_absolute_path))
      .WillOnce(Return(0));

  base::FilePath proc_fd_path(AndroidOciWrapper::kProcFdPath);
  std::vector<base::FilePath> fds = {
      proc_fd_path.Append("0"), proc_fd_path.Append("1"),
      proc_fd_path.Append("2"), proc_fd_path.Append("5"),
      proc_fd_path.Append("13")};
  EXPECT_CALL(system_utils_,
              EnumerateFiles(proc_fd_path, base::FileEnumerator::FILES, _))
      .WillOnce(DoAll(SetArgPointee<2>(fds), Return(true)));

  // It should never close stdin, stdout and stderr.
  EXPECT_CALL(system_utils_, close(0)).Times(0);
  EXPECT_CALL(system_utils_, close(1)).Times(0);
  EXPECT_CALL(system_utils_, close(2)).Times(0);
  EXPECT_CALL(system_utils_, close(5)).WillOnce(Return(0));
  EXPECT_CALL(system_utils_, close(13)).WillOnce(Return(0));

  EXPECT_CALL(system_utils_, setsid()).WillOnce(Return(0));

  base::FilePath run_android_script_path =
      containers_directory_->path().Append("android/run_android");
  EXPECT_CALL(system_utils_, execve(run_android_script_path, _, _));

  CallStartContainer();
}

}  // namespace

}  // namespace login_manager
