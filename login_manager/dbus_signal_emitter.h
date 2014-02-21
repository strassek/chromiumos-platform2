// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_DBUS_SIGNAL_EMITTER_H_
#define LOGIN_MANAGER_DBUS_SIGNAL_EMITTER_H_

#include <string>
#include <vector>

#include <base/basictypes.h>
#include <base/compiler_specific.h>

namespace login_manager {

// Simple mockable interface for emitting DBus signals.
class DBusSignalEmitterInterface {
 public:
  // Strings for encoding boolean status in signals.
  static const char kSignalSuccess[];
  static const char kSignalFailure[];

  virtual ~DBusSignalEmitterInterface();

  // Broadcasts |signal_name| from the session manager DBus interface.
  virtual void EmitSignal(const std::string& signal_name) = 0;

  // Broadcasts |signal_name| from the session manager DBus interface,
  // with kSignalSuccess if |success| is true, kSignalFailure otherwise.
  virtual void EmitSignalWithSuccessFailure(const std::string& signal_name,
                                            const bool success) = 0;

  // Broadcasts |signal_name| from the session manager DBus interface,
  // optionally adding |payload| as args if it is not empty.
  virtual void EmitSignalWithString(const std::string& signal_name,
                                    const std::string& payload) = 0;
};

// Simple mockable interface for emitting DBus signals.
class DBusSignalEmitter : public DBusSignalEmitterInterface {
 public:
  DBusSignalEmitter();
  virtual ~DBusSignalEmitter();

  virtual void EmitSignal(const std::string& signal_name) OVERRIDE;
  virtual void EmitSignalWithSuccessFailure(const std::string& signal_name,
                                            const bool success) OVERRIDE;
  virtual void EmitSignalWithString(const std::string& signal_name,
                                    const std::string& payload) OVERRIDE;

 private:
  // Does the actual work of emitting the signal.
  void EmitSignalFrom(const std::string& interface,
                      const std::string& signal_name,
                      const std::vector<std::string>& payload);
  DISALLOW_COPY_AND_ASSIGN(DBusSignalEmitter);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_DBUS_SIGNAL_EMITTER_H_
