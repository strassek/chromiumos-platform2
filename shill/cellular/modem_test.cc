// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem.h"

#include <tuple>

#include <ModemManager/ModemManager.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/cellular/cellular.h"
#include "shill/cellular/cellular_capability.h"
#include "shill/cellular/mock_cellular.h"
#include "shill/cellular/mock_modem.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/manager.h"
#include "shill/mock_dbus_properties_proxy.h"
#include "shill/mock_device_info.h"
#include "shill/net/mock_rtnl_handler.h"
#include "shill/net/rtnl_handler.h"
#include "shill/test_event_dispatcher.h"

using std::string;
using testing::_;
using testing::AnyNumber;
using testing::ByMove;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;
using testing::Test;

namespace shill {

namespace {

const int kTestInterfaceIndex = 5;
const char kLinkName[] = "usb0";
const char kService[] = "org.freedesktop.ModemManager1";
const RpcIdentifier kPath("/org/freedesktop/ModemManager1/Modem/0");
const unsigned char kAddress[] = {0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5};
const char kAddressAsString[] = "A0B1C2D3E4F5";

}  // namespace

class ModemTest : public Test {
 public:
  ModemTest()
      : modem_info_(nullptr, &dispatcher_, nullptr, nullptr),
        device_info_(modem_info_.manager()),
        modem_(new StrictModem(kService, kPath, &modem_info_)) {}

  void SetUp() override;
  void TearDown() override;

  void ReplaceSingletons() { modem_->rtnl_handler_ = &rtnl_handler_; }

  void SetupRealModem() {
    real_modem_.reset(new Modem(kService, kPath, &modem_info_));
    real_modem_->rtnl_handler_ = &rtnl_handler_;
    EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex, _))
        .WillOnce(DoAll(SetArgPointee<1>(expected_address_), Return(true)));
  }

  CellularRefPtr GetRealModemDevice() { return real_modem_->device_; }
  CellularCapability* GetRealModemCapability() {
    return real_modem_->device_->capability_.get();
  }

 protected:
  EventDispatcherForTest dispatcher_;
  MockModemInfo modem_info_;
  MockDeviceInfo device_info_;
  std::unique_ptr<StrictModem> modem_;
  std::unique_ptr<Modem> real_modem_;
  MockRTNLHandler rtnl_handler_;
  ByteString expected_address_;
};

void ModemTest::SetUp() {
  EXPECT_EQ(kService, modem_->service_);
  EXPECT_EQ(kPath, modem_->path_);
  ReplaceSingletons();
  expected_address_ = ByteString(kAddress, base::size(kAddress));

  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(kLinkName))
      .WillRepeatedly(Return(kTestInterfaceIndex));

  EXPECT_CALL(*modem_info_.mock_manager(), device_info())
      .WillRepeatedly(Return(&device_info_));
}

void ModemTest::TearDown() {
  modem_.reset();
}

MATCHER_P2(HasPropertyWithValueU32, key, value, "") {
  return arg.template Contains<uint32_t>(key) &&
         value == arg.template Get<uint32_t>(key);
}

TEST_F(ModemTest, PendingDevicePropertiesAndCreate) {
  static const char kSentinel[] = "sentinel";
  static const uint32_t kSentinelValue = 17;

  InterfaceToProperties properties;
  properties[MM_DBUS_INTERFACE_MODEM].Set<uint32_t>(kSentinel, kSentinelValue);

  EXPECT_CALL(*modem_, GetLinkName(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(string(kLinkName)), Return(true)));
  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(StrEq(kLinkName)))
      .WillRepeatedly(Return(kTestInterfaceIndex));

  // The first time we call CreateDeviceFromModemProperties,
  // GetMacAddress will fail.
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*modem_, GetModemInterface())
      .WillRepeatedly(Return(MM_DBUS_INTERFACE_MODEM));
  modem_->CreateDeviceFromModemProperties(properties);
  EXPECT_EQ(nullptr, modem_->device_);

  // On the second time, we allow GetMacAddress to succeed.  Now we
  // expect a device to be built
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex, _))
      .WillOnce(DoAll(SetArgPointee<1>(expected_address_), Return(true)));

  // modem will take ownership
  MockCellular* cellular = new MockCellular(
      &modem_info_, kLinkName, kAddressAsString, kTestInterfaceIndex,
      Cellular::kType3gpp, kService, kPath);

  EXPECT_CALL(*modem_,
              ConstructCellular(StrEq(kLinkName), StrEq(kAddressAsString),
                                kTestInterfaceIndex))
      .WillOnce(Return(cellular));

  EXPECT_CALL(*cellular,
              OnPropertiesChanged(
                  _, HasPropertyWithValueU32(kSentinel, kSentinelValue), _));
  EXPECT_CALL(device_info_, RegisterDevice(_));
  modem_->OnDeviceInfoAvailable(kLinkName);

  EXPECT_NE(nullptr, modem_->device_);
  EXPECT_EQ(base::ToLowerASCII(kAddressAsString), cellular->mac_address());

  // Add expectations for the eventual |modem_| destruction.
  EXPECT_CALL(*cellular, DestroyService());
}

TEST_F(ModemTest, EarlyDeviceProperties) {
  // OnDeviceInfoAvailable called before
  // CreateDeviceFromModemProperties: Do nothing
  modem_->OnDeviceInfoAvailable(kLinkName);
  EXPECT_EQ(nullptr, modem_->device_);
}

TEST_F(ModemTest, CreateDeviceEarlyFailures) {
  InterfaceToProperties properties;

  EXPECT_CALL(*modem_, ConstructCellular(_, _, _)).Times(0);
  EXPECT_CALL(*modem_, GetModemInterface())
      .WillRepeatedly(Return(MM_DBUS_INTERFACE_MODEM));

  // No modem interface properties:  no device created
  modem_->CreateDeviceFromModemProperties(properties);
  EXPECT_EQ(nullptr, modem_->device_);

  properties[MM_DBUS_INTERFACE_MODEM] = KeyValueStore();

  // Link name, but no ifindex: no device created
  EXPECT_CALL(*modem_, GetLinkName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(string(kLinkName)), Return(true)));
  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(StrEq(kLinkName)))
      .WillOnce(Return(-1));
  modem_->CreateDeviceFromModemProperties(properties);
  EXPECT_EQ(nullptr, modem_->device_);

  // The params are good, but the device is blocked.
  EXPECT_CALL(*modem_, GetLinkName(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(string(kLinkName)), Return(true)));
  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(StrEq(kLinkName)))
      .WillOnce(Return(kTestInterfaceIndex));
  EXPECT_CALL(device_info_, GetMacAddress(kTestInterfaceIndex, _))
      .WillOnce(DoAll(SetArgPointee<1>(expected_address_), Return(true)));
  EXPECT_CALL(device_info_, IsDeviceBlocked(kLinkName))
      .WillRepeatedly(Return(true));
  modem_->CreateDeviceFromModemProperties(properties);
  EXPECT_EQ(nullptr, modem_->device_);

  // No link name: see CreateDevicePPP.
}

TEST_F(ModemTest, CreateDevicePPP) {
  InterfaceToProperties properties;
  properties[MM_DBUS_INTERFACE_MODEM] = KeyValueStore();

  string dev_name(
      base::StringPrintf(Modem::kFakeDevNameFormat, Modem::fake_dev_serial_));

  // |modem_| will take ownership.
  MockCellular* cellular = new MockCellular(
      &modem_info_, dev_name, Modem::kFakeDevAddress,
      Modem::kFakeDevInterfaceIndex, Cellular::kType3gpp, kService, kPath);

  EXPECT_CALL(*modem_, GetModemInterface())
      .WillRepeatedly(Return(MM_DBUS_INTERFACE_MODEM));
  // No link name: assumed to be a PPP dongle.
  EXPECT_CALL(*modem_, GetLinkName(_, _)).WillOnce(Return(false));
  EXPECT_CALL(*modem_,
              ConstructCellular(dev_name, StrEq(Modem::kFakeDevAddress),
                                Modem::kFakeDevInterfaceIndex))
      .WillOnce(Return(cellular));
  EXPECT_CALL(device_info_, RegisterDevice(_));

  modem_->CreateDeviceFromModemProperties(properties);
  EXPECT_NE(nullptr, modem_->device_);

  // Add expectations for the eventual |modem_| destruction.
  EXPECT_CALL(*cellular, DestroyService());
}

TEST_F(ModemTest, GetDeviceParams) {
  string mac_address;
  int interface_index = 2;
  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(_)).WillOnce(Return(-1));
  EXPECT_CALL(device_info_, GetMacAddress(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(modem_->GetDeviceParams(&mac_address, &interface_index));
  EXPECT_EQ(-1, interface_index);

  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(_)).WillOnce(Return(-2));
  EXPECT_CALL(device_info_, GetMacAddress(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(modem_->GetDeviceParams(&mac_address, &interface_index));
  EXPECT_EQ(-2, interface_index);

  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(_)).WillOnce(Return(1));
  EXPECT_CALL(device_info_, GetMacAddress(_, _)).WillOnce(Return(false));
  EXPECT_FALSE(modem_->GetDeviceParams(&mac_address, &interface_index));
  EXPECT_EQ(1, interface_index);

  EXPECT_CALL(rtnl_handler_, GetInterfaceIndex(_)).WillOnce(Return(2));
  EXPECT_CALL(device_info_, GetMacAddress(2, _))
      .WillOnce(DoAll(SetArgPointee<1>(expected_address_), Return(true)));
  EXPECT_TRUE(modem_->GetDeviceParams(&mac_address, &interface_index));
  EXPECT_EQ(2, interface_index);
  EXPECT_EQ(kAddressAsString, mac_address);
}

TEST_F(ModemTest, CreateDeviceMM1) {
  SetupRealModem();

  InterfaceToProperties properties;

  KeyValueStore modem_properties;
  modem_properties.Set<uint32_t>(MM_MODEM_PROPERTY_UNLOCKREQUIRED,
                                 MM_MODEM_LOCK_NONE);
  std::vector<std::tuple<std::string, uint32_t>> ports = {
      std::make_tuple(kLinkName, MM_MODEM_PORT_TYPE_NET)};
  modem_properties.SetVariant(MM_MODEM_PROPERTY_PORTS, brillo::Any(ports));
  properties[MM_DBUS_INTERFACE_MODEM] = modem_properties;

  KeyValueStore modem3gpp_properties;
  modem3gpp_properties.Set<uint32_t>(
      MM_MODEM_MODEM3GPP_PROPERTY_REGISTRATIONSTATE,
      MM_MODEM_3GPP_REGISTRATION_STATE_HOME);
  properties[MM_DBUS_INTERFACE_MODEM_MODEM3GPP] = modem3gpp_properties;

  EXPECT_CALL(*(modem_info_.mock_control_interface()),
              CreateDBusPropertiesProxy(kPath, kService))
      .WillOnce(Return(ByMove(std::make_unique<MockDBusPropertiesProxy>())));
  real_modem_->CreateDeviceMM1(properties);
  EXPECT_NE(nullptr, GetRealModemDevice());
  EXPECT_TRUE(GetRealModemCapability()->IsRegistered());
}

}  // namespace shill
