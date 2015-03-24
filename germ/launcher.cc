// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "germ/launcher.h"

#include <sys/types.h>

#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/minijail/minijail.h>
#include <chromeos/process.h>

#include "germ/environment.h"

namespace {
const char* kSandboxedServiceTemplate = "germ_template";
}  // namespace

namespace germ {

// Starts with the top half of user ids.
class UidService final {
 public:
  UidService() { min_uid_ = 1 << ((sizeof(uid_t) * 8) >> 1); }
  ~UidService() {}

  uid_t GetUid() {
    return static_cast<uid_t>(base::RandInt(min_uid_, 2 * min_uid_));
  }

 private:
  uid_t min_uid_;
};

Launcher::Launcher() {
  uid_service_.reset(new UidService());
}

Launcher::~Launcher() {}

int Launcher::RunInteractive(const std::string& name,
                             const std::vector<std::string>& argv) {
  std::vector<char*> command_line;
  for (const auto& t : argv) {
    command_line.push_back(const_cast<char*>(t.c_str()));
  }
  // Minijail will use the underlying char* array as 'argv',
  // so null-terminate it.
  command_line.push_back(nullptr);

  chromeos::Minijail* minijail = chromeos::Minijail::GetInstance();

  uid_t uid = uid_service_->GetUid();
  Environment env(uid, uid);

  int status;
  minijail->RunSyncAndDestroy(env.GetForInteractive(), command_line, &status);
  return status;
}

int Launcher::RunService(const std::string& name,
                         const std::vector<std::string>& argv) {
  // initctl start germ_template NAME=yes ENVIRONMENT= COMMANDLINE=/usr/bin/yes
  uid_t uid = uid_service_->GetUid();
  Environment env(uid, uid);

  chromeos::ProcessImpl initctl;
  initctl.AddArg("/sbin/initctl");
  initctl.AddArg("start");
  initctl.AddArg(kSandboxedServiceTemplate);
  initctl.AddArg(base::StringPrintf("NAME=%s", name.c_str()));
  initctl.AddArg(env.GetForService());
  std::string command_line = JoinString(argv, ' ');
  initctl.AddArg(base::StringPrintf("COMMANDLINE=%s", command_line.c_str()));

  // Since we're running 'initctl', and not the executable itself,
  // we wait for it to exit.
  return initctl.Run();
}

}  // namespace germ
