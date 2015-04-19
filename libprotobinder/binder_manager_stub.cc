// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libprotobinder/binder_manager_stub.h"

#include <base/logging.h>

#include "libprotobinder/binder_proxy.h"
#include "libprotobinder/ibinder.h"
#include "libprotobinder/iinterface.h"

namespace protobinder {

BinderManagerStub::BinderManagerStub() {}

BinderManagerStub::~BinderManagerStub() {}

void BinderManagerStub::ReportBinderDeath(BinderProxy* proxy) {
  CHECK(proxy);
  if (handles_requesting_death_notifications_.count(proxy->handle()))
    proxy->HandleDeathNotification();
}

void BinderManagerStub::SetTestInterface(
    BinderProxy* proxy,
    std::unique_ptr<IInterface> interface) {
  if (proxy)
    test_interfaces_[proxy->handle()] = std::move(interface);
  else
    test_interface_for_null_proxy_ = std::move(interface);
}

Status BinderManagerStub::Transact(uint32_t handle,
             uint32_t code,
             const Parcel& data,
             Parcel* reply,
             bool one_way) {
  return STATUS_OK();
}

void BinderManagerStub::IncWeakHandle(uint32_t handle) {}

void BinderManagerStub::DecWeakHandle(uint32_t handle) {}

bool BinderManagerStub::GetFdForPolling(int* fd) {
  // TODO(derat): Return an FD that can actually be used here.
  return true;
}

void BinderManagerStub::HandleEvent() {
}

void BinderManagerStub::RequestDeathNotification(BinderProxy* proxy) {
  CHECK(proxy);
  handles_requesting_death_notifications_.insert(proxy->handle());
}

void BinderManagerStub::ClearDeathNotification(BinderProxy* proxy) {
  CHECK(proxy);
  handles_requesting_death_notifications_.erase(proxy->handle());
}

IInterface* BinderManagerStub::CreateTestInterface(const IBinder* binder) {
  if (!binder)
    return test_interface_for_null_proxy_.release();

  const BinderProxy* proxy = binder->GetBinderProxy();
  if (!proxy)
    return nullptr;

  auto it = test_interfaces_.find(proxy->handle());
  if (it == test_interfaces_.end())
    return nullptr;

  std::unique_ptr<IInterface> interface = std::move(it->second);
  test_interfaces_.erase(it);
  return interface.release();
}

}  // namespace protobinder
