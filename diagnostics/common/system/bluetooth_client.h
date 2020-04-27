// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_BLUETOOTH_CLIENT_H_
#define DIAGNOSTICS_COMMON_SYSTEM_BLUETOOTH_CLIENT_H_

#include <string>

#include <base/observer_list.h>
#include <base/observer_list_types.h>
#include <base/macros.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>
#include <dbus/property.h>

namespace diagnostics {

// BluetoothClient is used for monitoring objects representing Bluetooth
// Adapters and Devices.
class BluetoothClient {
 public:
  // Structure of properties associated with bluetooth adapters.
  struct AdapterProperties : public dbus::PropertySet {
    // The Bluetooth device address of the adapter.
    dbus::Property<std::string> address;

    // The Bluetooth system name, e.g. hci0.
    dbus::Property<std::string> name;

    // Whether the adapter radio is powered.
    dbus::Property<bool> powered;

    AdapterProperties(
        dbus::ObjectProxy* object_proxy,
        const dbus::PropertySet::PropertyChangedCallback& callback);
    ~AdapterProperties() override;
  };

  // Structure of properties associated with bluetooth devices.
  struct DeviceProperties : public dbus::PropertySet {
    // The Bluetooth device address of the device.
    dbus::Property<std::string> address;

    // The Bluetooth friendly name of the device.
    dbus::Property<std::string> name;

    // Indicates that the device is currently connected.
    dbus::Property<bool> connected;

    // Object path of the adapter the device belongs to.
    dbus::Property<dbus::ObjectPath> adapter;

    DeviceProperties(
        dbus::ObjectProxy* object_proxy,
        const dbus::PropertySet::PropertyChangedCallback& callback);
    ~DeviceProperties() override;
  };

  // Interface for observing bluetooth adapters and devices changes.
  class Observer : public base::CheckedObserver {
   public:
    virtual ~Observer() = default;

    // Called when the adapter with object path |adapter_path| is added to the
    // system.
    virtual void AdapterAdded(const dbus::ObjectPath& adapter_path,
                              const AdapterProperties& properties) = 0;

    // Called when the adapter with object path |adapter_path| is removed from
    // the system.
    virtual void AdapterRemoved(const dbus::ObjectPath& adapter_path) = 0;

    // Called when the adapter with object path |adapter_path| has a change in
    // value of the property.
    virtual void AdapterPropertyChanged(
        const dbus::ObjectPath& adapter_path,
        const AdapterProperties& properties) = 0;

    // Called when the device with object path |device_path| is added to the
    // system.
    virtual void DeviceAdded(const dbus::ObjectPath& device_path,
                             const DeviceProperties& properties) = 0;

    // Called when the device with object path |device_path| is removed from
    // the system.
    virtual void DeviceRemoved(const dbus::ObjectPath& device_path) = 0;

    // Called when the device with object path |device_path| has a
    // change in value of the property.
    virtual void DevicePropertyChanged(const dbus::ObjectPath& device_path,
                                       const DeviceProperties& properties) = 0;
  };

  BluetoothClient();
  virtual ~BluetoothClient();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 protected:
  base::ObserverList<Observer> observers_;

 private:
  DISALLOW_COPY_AND_ASSIGN(BluetoothClient);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_BLUETOOTH_CLIENT_H_