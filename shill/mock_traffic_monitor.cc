// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_traffic_monitor.h"

#include "shill/device.h"

namespace shill {

namespace {

void NoOpNetworkProblemDetectedCallback(int) {}

}  // namespace

MockTrafficMonitor::MockTrafficMonitor()
    : TrafficMonitor(
          nullptr, nullptr, base::Bind(&NoOpNetworkProblemDetectedCallback)) {}

MockTrafficMonitor::~MockTrafficMonitor() = default;

}  // namespace shill
