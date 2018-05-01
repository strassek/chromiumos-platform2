// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <memory>
#include <string>

#include <base/at_exit.h>
#include <base/files/scoped_file.h>
#include <base/message_loop/message_loop.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_split.h>
#include <brillo/flag_helper.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>
#include <vm_concierge/proto_bindings/service.pb.h>

#include "vm_tools/common/constants.h"
#include "vm_tools/vsh/scoped_termios.h"
#include "vm_tools/vsh/utils.h"
#include "vm_tools/vsh/vsh_client.h"

using std::string;
using vm_tools::vsh::ScopedTermios;
using vm_tools::vsh::VshClient;

namespace {

constexpr int kDefaultTimeoutMs = 30 * 1000;

constexpr char kVshUsage[] =
    "vsh client\n"
    "Usage: vsh [flags] -- ENV1=VALUE1 ENV2=VALUE2 command arg1 arg2...";

bool GetCid(const std::string& vm_name, unsigned int* cid) {
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(std::move(opts)));

  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system bus";
    return false;
  }

  dbus::ObjectProxy* proxy = bus->GetObjectProxy(
      vm_tools::concierge::kVmConciergeServiceName,
      dbus::ObjectPath(vm_tools::concierge::kVmConciergeServicePath));
  if (!proxy) {
    LOG(ERROR) << "Unable to get dbus proxy for "
               << vm_tools::concierge::kVmConciergeServiceName;
    return false;
  }

  dbus::MethodCall method_call(vm_tools::concierge::kVmConciergeInterface,
                               vm_tools::concierge::kGetVmInfoMethod);
  dbus::MessageWriter writer(&method_call);

  vm_tools::concierge::GetVmInfoRequest request;
  request.set_name(vm_name);

  if (!writer.AppendProtoAsArrayOfBytes(request)) {
    LOG(ERROR) << "Failed to encode GetVmInfo protobuf";
    return false;
  }

  std::unique_ptr<dbus::Response> dbus_response =
      proxy->CallMethodAndBlock(&method_call, kDefaultTimeoutMs);
  if (!dbus_response) {
    LOG(ERROR) << "Failed to send dbus message to concierge service";
    return false;
  }

  dbus::MessageReader reader(dbus_response.get());
  vm_tools::concierge::GetVmInfoResponse response;
  if (!reader.PopArrayOfBytesAsProto(&response)) {
    LOG(ERROR) << "Failed to parse response protobuf";
    return false;
  }

  if (!response.success()) {
    LOG(ERROR) << "Failed to get VM info for " << vm_name;
    return false;
  }

  *cid = response.vm_info().cid();
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  brillo::InitLog(brillo::kLogToStderrIfTty);

  DEFINE_uint64(cid, 0, "Cid of VM");
  DEFINE_string(vm_name, "", "Target VM name");
  DEFINE_string(user, "chronos", "Target user in the VM");

  brillo::FlagHelper::Init(argc, argv, kVshUsage);

  if ((FLAGS_cid != 0 && !FLAGS_vm_name.empty()) ||
      (FLAGS_cid == 0 && FLAGS_vm_name.empty())) {
    LOG(ERROR) << "Exactly one of --cid or --vm_name is required";
    return EXIT_FAILURE;
  }
  unsigned int cid;
  if (FLAGS_cid != 0) {
    cid = FLAGS_cid;
    if (static_cast<uint64_t>(cid) != FLAGS_cid) {
      LOG(ERROR) << "Cid value (" << FLAGS_cid << ") is too large.  Largest "
                 << "valid value is "
                 << std::numeric_limits<unsigned int>::max();
      return EXIT_FAILURE;
    }
  } else {
    if (!GetCid(FLAGS_vm_name, &cid))
      return EXIT_FAILURE;
  }

  brillo::BaseMessageLoop message_loop;
  message_loop.SetAsCurrent();

  auto client = VshClient::Create(cid, FLAGS_user);

  if (!client) {
    return EXIT_FAILURE;
  }

  base::ScopedFD ttyfd(
      HANDLE_EINTR(open(vm_tools::vsh::kDevTtyPath,
                        O_RDONLY | O_NOCTTY | O_CLOEXEC | O_NONBLOCK)));
  if (!ttyfd.is_valid()) {
    PLOG(ERROR) << "Failed to open /dev/tty";
    return EXIT_FAILURE;
  }

  // Set terminal to raw mode. Note that the client /must/ cleanly exit
  // the message loop below to restore termios settings.
  ScopedTermios termios(std::move(ttyfd));
  if (isatty(termios.GetRawFD()) &&
      !termios.SetTermiosMode(ScopedTermios::TermiosMode::RAW)) {
    return EXIT_FAILURE;
  }

  message_loop.Run();

  return client->exit_code();
}
