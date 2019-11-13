// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/run_loop.h>
#include <base/synchronization/waitable_event.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/file_descriptor.h>
#include <dbus/bus.h>
#include <dbus/cros_healthd/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <mojo/edk/embedder/embedder.h>
#include <mojo/edk/embedder/platform_channel_pair.h>

namespace diagnostics {
namespace {
// Saves |response| to |response_destination|.
void OnMojoResponseReceived(
    chromeos::cros_healthd::mojom::TelemetryInfoPtr* response_destination,
    base::Closure quit_closure,
    chromeos::cros_healthd::mojom::TelemetryInfoPtr response) {
  *response_destination = std::move(response);
  quit_closure.Run();
}

// Sends |raw_fd| to cros_healthd via D-Bus. Sets |token_out| to a unique token
// which can be used to create a message pipe to cros_healthd.
void DoDBusBootstrap(int raw_fd,
                     base::WaitableEvent* event,
                     std::string* token_out) {
  dbus::Bus::Options bus_options;
  bus_options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(bus_options);

  CHECK(bus->Connect());

  dbus::ObjectProxy* cros_healthd_service_proxy = bus->GetObjectProxy(
      diagnostics::kCrosHealthdServiceName,
      dbus::ObjectPath(diagnostics::kCrosHealthdServicePath));

  brillo::dbus_utils::FileDescriptor fd(raw_fd);
  brillo::ErrorPtr error;
  auto response = brillo::dbus_utils::CallMethodAndBlock(
      cros_healthd_service_proxy, kCrosHealthdServiceInterface,
      kCrosHealthdBootstrapMojoConnectionMethod, &error, fd,
      false /* is_chrome */);

  if (!response) {
    LOG(ERROR) << "No response received.";
    return;
  }

  dbus::MessageReader reader(response.get());
  if (!reader.PopString(token_out)) {
    LOG(ERROR) << "Failed to pop string.";
    return;
  }

  event->Signal();
}
}  // namespace

CrosHealthdMojoAdapter::CrosHealthdMojoAdapter() {
  CHECK(mojo_thread_.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0)))
      << "Failed starting the mojo thread.";

  CHECK(dbus_thread_.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0)))
      << "Failed starting the D-Bus thread.";

  mojo::edk::Init();
  mojo::edk::InitIPCSupport(mojo_thread_.task_runner());
}

CrosHealthdMojoAdapter::~CrosHealthdMojoAdapter() {
  mojo::edk::ShutdownIPCSupport(base::Bind(&base::DoNothing));
}

chromeos::cros_healthd::mojom::TelemetryInfoPtr
CrosHealthdMojoAdapter::GetTelemetryInfo(
    const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>&
        categories_to_probe) {
  if (!cros_healthd_service_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::TelemetryInfoPtr response;
  base::RunLoop run_loop;
  cros_healthd_service_->ProbeTelemetryInfo(
      categories_to_probe,
      base::Bind(&OnMojoResponseReceived, &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

void CrosHealthdMojoAdapter::Connect() {
  mojo::edk::PlatformChannelPair channel;
  std::string token;

  // Pass the other end of the pipe to cros_healthd. Wait for this task to run,
  // since we need the resulting token to continue.
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  dbus_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&DoDBusBootstrap, channel.PassClientHandle().release().handle,
                 &event, &token));
  event.Wait();

  mojo::edk::SetParentPipeHandle(channel.PassServerHandle());

  // Bind our end of |pipe| to our CrosHealthdServicePtr. The daemon
  // should bind its end to a CrosHealthdService implementation.
  cros_healthd_service_.Bind(
      chromeos::cros_healthd::mojom::CrosHealthdServicePtrInfo(
          mojo::edk::CreateChildMessagePipe(token), 0u /* version */));
}

}  // namespace diagnostics