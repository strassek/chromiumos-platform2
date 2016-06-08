// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_CONTAINER_MANAGER_IMPL_H_
#define LOGIN_MANAGER_CONTAINER_MANAGER_IMPL_H_

#include "login_manager/container_manager_interface.h"

#include <stdlib.h>

#include <map>
#include <memory>
#include <string>

#include <base/files/file_path.h>

#include <libcontainer/libcontainer.h>

namespace login_manager {

// Manages containers running in a session.
// Handles parsing of config.json and runtime.json to configure the container.
class ContainerManagerImpl : public ContainerManagerInterface {
 public:
  ContainerManagerImpl(const base::FilePath& containers_directory);
  ~ContainerManagerImpl() override;

  // ContainerManagerInterface:
  bool StartContainer(const std::string& name) override;
  bool WaitForContainerToExit(const std::string& name) override;
  bool KillContainer(const std::string& name) override;
  bool KillAllContainers() override;
  bool GetRootFsPath(const std::string& name,
                     base::FilePath* path_out) const override;
  bool GetContainerPID(const std::string& name, pid_t* pid_out) const override;

 private:
  // Map of the currently running containers.
  using ContainerPtr = std::unique_ptr<container, decltype(&container_destroy)>;
  std::map<std::string, ContainerPtr> container_map_;

  // Directory that holds the container config files.
  const base::FilePath containers_directory_;

  DISALLOW_COPY_AND_ASSIGN(ContainerManagerImpl);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_CONTAINER_MANAGER_IMPL_H_
