// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_PORT_H_
#define TYPECD_PORT_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest_prod.h>

#include "typecd/cable.h"
#include "typecd/partner.h"

namespace typecd {

// This class is used to represent a Type C Port. It can be used to access PD
// state associated with the port, and will also contain handles to the object
// representing a peripheral (i.e "Partner") if one is connected to the port.
class Port {
 public:
  static std::unique_ptr<Port> CreatePort(const base::FilePath& syspath);
  Port(const base::FilePath& syspath, int port_num);

  void AddPartner(const base::FilePath& path);
  void RemovePartner();

  void AddCable(const base::FilePath& path);
  void RemoveCable();
  // Add/remove an alternate mode for the partner.
  void AddRemovePartnerAltMode(const base::FilePath& path, bool added);

  // Read and return the current port data role from sysfs.
  // Returns either "host" or "device" on success, empty string on failure.
  std::string GetDataRole();

  // Check whether we can enter DP Alt Mode. This should check for the presence
  // of required attributes on the Partner and (if applicable) Cable.
  bool CanEnterDPAltMode();

  // Check whether we can enter Thunderbolt Compatibility Alt Mode. This should
  // check for the presence of required attributes on the Partner and
  // (if applicable) Cable.
  bool CanEnterTBTCompatibilityMode();

 private:
  friend class PortTest;
  FRIEND_TEST(PortTest, TestBasicAdd);
  FRIEND_TEST(PortTest, TestDPAltModeEntryCheckTrue);
  FRIEND_TEST(PortTest, TestDPAltModeEntryCheckFalse);
  FRIEND_TEST(PortTest, TestTBTCompatibilityModeEntryCheckTrue);

  bool IsPartnerAltModePresent(uint16_t altmode_sid);

  // Sysfs path used to access partner PD information.
  base::FilePath syspath_;
  // Port number as described by the Type C connector class framework.
  int port_num_;
  std::unique_ptr<Cable> cable_;
  std::unique_ptr<Partner> partner_;
};

}  // namespace typecd

#endif  // TYPECD_PORT_H_
