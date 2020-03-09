// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/sandboxed_worker.h"

#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>

#include "bindings/worker_common.pb.h"
#include "system-proxy/protobuf_util.h"

namespace {
constexpr char kSystemProxyWorkerBin[] = "/usr/sbin/system_proxy_worker";
constexpr char kSeccompFilterPath[] =
    "/usr/share/policy/system-proxy-worker-seccomp.policy";
constexpr int kMaxWorkerMessageSize = 2048;
// Size of the buffer array used to read data from the worker's stderr.
constexpr int kWorkerBufferSize = 1024;

}  // namespace

namespace system_proxy {

SandboxedWorker::SandboxedWorker() : jail_(minijail_new()) {}

void SandboxedWorker::Start() {
  DCHECK(!IsRunning()) << "Worker is already running.";

  if (!jail_)
    return;

  minijail_namespace_net(jail_.get());
  minijail_no_new_privs(jail_.get());
  minijail_use_seccomp_filter(jail_.get());
  minijail_parse_seccomp_filters(jail_.get(), kSeccompFilterPath);

  int child_stdin = -1, child_stdout = -1, child_stderr = -1;

  std::vector<char*> args_ptr;

  args_ptr.push_back(const_cast<char*>(kSystemProxyWorkerBin));
  args_ptr.push_back(nullptr);

  // Execute the command.
  int res =
      minijail_run_pid_pipes(jail_.get(), args_ptr[0], args_ptr.data(), &pid_,
                             &child_stdin, &child_stdout, &child_stderr);

  if (res != 0) {
    LOG(ERROR) << "Failed to start sandboxed worker: " << strerror(-res);
    return;
  }

  // Make sure the pipes never block.
  if (!base::SetNonBlocking(child_stdin))
    LOG(WARNING) << "Failed to set stdin non-blocking";
  if (!base::SetNonBlocking(child_stdout))
    LOG(WARNING) << "Failed to set stdout non-blocking";
  if (!base::SetNonBlocking(child_stderr))
    LOG(WARNING) << "Failed to set stderr non-blocking";

  stdin_pipe_.reset(child_stdin);
  stdout_pipe_.reset(child_stdout);
  stderr_pipe_.reset(child_stderr);

  stdout_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      stdout_pipe_.get(),
      base::BindRepeating(&SandboxedWorker::OnMessageReceived,
                          base::Unretained(this)));

  stderr_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      stderr_pipe_.get(),
      base::BindRepeating(&SandboxedWorker::OnMessageReceived,
                          base::Unretained(this)));
}

void SandboxedWorker::SetUsernameAndPassword(const std::string& username,
                                             const std::string& password) {
  Credentials credentials;
  credentials.set_username(username);
  credentials.set_password(password);
  WorkerConfigs configs;
  *configs.mutable_credentials() = credentials;
  if (!WriteProtobuf(stdin_pipe_.get(), configs)) {
    LOG(ERROR) << "Failed to set credentials for worker " << pid_;
  }
}

void SandboxedWorker::SetListeningAddress(uint32_t addr, int port) {
  SocketAddress address;
  address.set_addr(addr);
  address.set_port(port);
  WorkerConfigs configs;
  *configs.mutable_listening_address() = address;

  if (!WriteProtobuf(stdin_pipe_.get(), configs)) {
    LOG(ERROR) << "Failed to set local proy address for worker " +
                      std::to_string(pid_);
  }
}

bool SandboxedWorker::Stop() {
  if (is_being_terminated_)
    return true;
  LOG(INFO) << "Killing " << pid_;
  is_being_terminated_ = true;

  if (kill(pid_, SIGTERM) < 0) {
    if (errno == ESRCH) {
      // No process or group found for pid, assume already terminated.
      return true;
    }
    PLOG(ERROR) << "Failed to terminate process " << pid_;
    return false;
  }
  return true;
}

bool SandboxedWorker::IsRunning() {
  return pid_ != 0 && !is_being_terminated_;
}

void SandboxedWorker::OnMessageReceived() {
  WorkerRequest request;

  if (!ReadProtobuf(stdout_pipe_.get(), &request)) {
    LOG(ERROR) << "Failed to read request from worker " << pid_;
    // The message is corrupted or the pipe closed, either way stop listening.
    stdout_watcher_ = nullptr;
    return;
  }
  if (request.has_log_request()) {
    LOG(INFO) << "[worker: " << pid_ << "]" << request.log_request().message();
  }
}

void SandboxedWorker::OnErrorReceived() {
  std::vector<char> buf;
  buf.resize(kWorkerBufferSize);

  std::string message;
  std::string worker_msg = "[worker: " + std::to_string(pid_) + "] ";

  ssize_t count = kWorkerBufferSize;
  ssize_t total_count = 0;

  while (count == kWorkerBufferSize) {
    count = HANDLE_EINTR(read(stderr_pipe_.get(), buf.data(), buf.size()));

    if (count < 0) {
      PLOG(ERROR) << worker_msg << "Failed to read from stdio";
      return;
    }

    if (count == 0) {
      if (!message.empty())
        break;  // Full message was read at the first iteration.

      PLOG(INFO) << worker_msg << "Pipe closed";
      // Stop watching, otherwise the handler will fire forever.
      stderr_watcher_ = nullptr;
    }

    total_count += count;
    if (total_count > kMaxWorkerMessageSize) {
      LOG(ERROR) << "Failure to read message from woker: message size exceeds "
                    "maximum allowed";
      stderr_watcher_ = nullptr;
      return;
    }
    message.append(buf.begin(), buf.begin() + count);
  }

  LOG(ERROR) << worker_msg << message;
}

}  // namespace system_proxy