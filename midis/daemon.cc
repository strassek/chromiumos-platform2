// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/memory/ptr_util.h>

#include "midis/daemon.h"

namespace midis {

Daemon::Daemon()
    : device_tracker_(base::MakeUnique<DeviceTracker>()),
      client_tracker_(base::MakeUnique<ClientTracker>()) {}

Daemon::~Daemon() {}

int Daemon::OnInit() {
  if (!device_tracker_->InitDeviceTracker()) {
    return -1;
  }

  client_tracker_->SetDeviceTracker(device_tracker_.get());
  if (!client_tracker_->InitClientTracker()) {
    return -1;
  }

  return 0;
}

}  // namespace midis
