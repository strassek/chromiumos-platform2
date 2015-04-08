// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_SERVER_DBUS_SERVICE_H_
#define ATTESTATION_SERVER_DBUS_SERVICE_H_

#include <base/memory/scoped_ptr.h>
#include <chromeos/dbus/dbus_method_response.h>
#include <chromeos/dbus/dbus_object.h>
#include <dbus/bus.h>

#include "attestation/common/attestation_interface.h"

namespace attestation {

using CompletionAction =
    chromeos::dbus_utils::AsyncEventSequencer::CompletionAction;

// Handles D-Bus calls to the attestation daemon.
class DBusService {
 public:
  // DBusService does not take ownership of |service|; it must remain valid for
  // the lifetime of the DBusService instance.
  DBusService(const scoped_refptr<dbus::Bus>& bus,
              AttestationInterface* service);
  virtual ~DBusService() = default;

  // Connects to D-Bus system bus and exports methods.
  void Register(const CompletionAction& callback);

  // Useful for testing.
  void set_service(AttestationInterface* service) {
    service_ = service;
  }

 private:
  friend class DBusServiceTest;

  // Handles a CreateGoogleAttestedKey D-Bus call.
  void HandleCreateGoogleAttestedKey(
      scoped_ptr<chromeos::dbus_utils::DBusMethodResponse<
          const CreateGoogleAttestedKeyReply&>> response,
      const CreateGoogleAttestedKeyRequest& request);

  chromeos::dbus_utils::DBusObject dbus_object_;
  AttestationInterface* service_;

  DISALLOW_COPY_AND_ASSIGN(DBusService);
};

}  // namespace attestation

#endif  // ATTESTATION_SERVER_DBUS_SERVICE_H_
