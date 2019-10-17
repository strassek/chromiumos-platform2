// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <brillo/dbus/dbus_connection.h>

#include "debugd/dbus-proxies.h"
#include "diagnostics/cros_healthd/cros_healthd.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "diagnostics/cros_healthd/utils/disk_utils.h"

int main(int argc, char** argv) {
  DEFINE_bool(probe_block_devices, false,
              "Exercise the ProbeNonRemovableBlockDeviceInfo routine");
  DEFINE_bool(probe_battery_metrics, false,
              "Exercise the ProbeBatteryInfo routine");
  DEFINE_bool(probe_cached_vpd, false,
              "Exercise the ProbeCachedVPDInfo routine");

  brillo::FlagHelper::Init(
      argc, argv, "cros_healthd - Device telemetry and diagnostics daemon.");

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // Setting up the D-bus connection and initiating debugdProxy, which
  // cros_healthd will use to speak to debugd.
  brillo::DBusConnection connection;
  scoped_refptr<dbus::Bus> bus = connection.Connect();
  if (!bus) {
    LOG(ERROR) << "cros_healthd: Failed to connect to system D-bus bus";
    return EXIT_FAILURE;
  }
  auto proxy = std::make_unique<org::chromium::debugdProxy>(bus);

  if (FLAGS_probe_block_devices) {
    base::FilePath root_dir{"/"};
    auto devices = diagnostics::FetchNonRemovableBlockDevicesInfo(root_dir);
    VLOG(1) << "Found " << devices.size() << " non-removable block device(s)."
            << std::endl;
    printf("path,size,type,manfid,name,serial\n");
    for (const auto& device : devices) {
      printf("%s,%ld,%s,0x%x,%s,0x%x\n", device->path.c_str(), device->size,
             device->type.c_str(), static_cast<int>(device->manufacturer_id),
             device->name.c_str(), device->serial);
    }
  } else if (FLAGS_probe_battery_metrics) {
    diagnostics::BatteryFetcher battery_fetcher(proxy.get());
    auto batteries = battery_fetcher.FetchBatteryInfo();
    printf(
        "charge_full,charge_full_design,cycle_count,serial_number,"
        "vendor(manufacturer),voltage_now,voltage_min_design,"
        "manufacture_date_smart,temperature_smart,model_name,charge_now\n");
    if (batteries.size() == 0) {
      VLOG(1) << "No main battery information found.";
    } else if (batteries.size() == 1) {
      VLOG(1) << "Found information for main battery.";
      const auto& battery = batteries[0];
      printf("%f,%f,%ld,%s,%s,%f,%f,%ld,%ld,%s,%f\n", battery->charge_full,
             battery->charge_full_design, battery->cycle_count,
             battery->serial_number.c_str(), battery->vendor.c_str(),
             battery->voltage_now, battery->voltage_min_design,
             battery->manufacture_date_smart, battery->temperature_smart,
             battery->model_name.c_str(), battery->charge_now);
    }
  } else if (FLAGS_probe_cached_vpd) {
    auto vpd_info = diagnostics::FetchCachedVpdInfo(base::FilePath("/"));
    std::string sku_number = vpd_info->sku_number;
    if (sku_number == "") {
      LOG(ERROR) << "Unable to read sku_number.";
      return EXIT_FAILURE;
    }
    printf("sku_number: %s\n", sku_number.c_str());
  } else {
    diagnostics::CrosHealthd daemon(std::move(proxy));
    return daemon.Run();
  }
  return EXIT_SUCCESS;
}
