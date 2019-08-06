// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBFS_FUSE_SESSION_H_
#define SMBFS_FUSE_SESSION_H_

#include <fuse_lowlevel.h>

#include <memory>
#include <vector>

#include <base/callback.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/macros.h>
#include <base/sequence_checker.h>

namespace smbfs {

class Filesystem;

class FuseSession {
 public:
  FuseSession(std::unique_ptr<Filesystem> fs, fuse_chan* chan);
  ~FuseSession();

  // Start processing FUSE requests. |stop_callback| is run if the filesystem is
  // disconnected by the kernel.
  bool Start(base::OnceClosure stop_callback);

 private:
  // FUSE low-level operations. See fuse_lowlevel_ops in fuse_lowlevel.h for
  // details.
  static void FuseDestroy(void* userdata);
  static void FuseLookup(fuse_req_t request,
                         fuse_ino_t parent_inode,
                         const char* name);
  static void FuseGetAttr(fuse_req_t request,
                          fuse_ino_t inode,
                          fuse_file_info* info);
  void Destroy();
  void Lookup(fuse_req_t request, fuse_ino_t parent_inode, const char* name);
  void GetAttr(fuse_req_t request, fuse_ino_t inode, fuse_file_info* info);

  // Callback for channel FD read watcher.
  void OnChannelReadable();

  // Stops processing FUSE requests and runs the |stop_callback_| provided by
  // Start(). May be be called multiple times, but will only run
  // |stop_callback_| on the first call.
  void RequestStop();

  std::unique_ptr<Filesystem> fs_;
  fuse_chan* const chan_;
  fuse_session* session_ = nullptr;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> read_watcher_;
  base::OnceClosure stop_callback_;

  // Buffer used for reading and processing fuse requests.
  std::vector<char> read_buffer_;

  base::SequenceChecker sequence_checker_;

  DISALLOW_COPY_AND_ASSIGN(FuseSession);
};

}  // namespace smbfs

#endif  // SMBFS_FUSE_SESSION_H_
