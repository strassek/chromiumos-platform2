// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_STATE_CHANGE_REPORTER_INTERFACE_H_
#define DLCSERVICE_STATE_CHANGE_REPORTER_INTERFACE_H_

#include <dlcservice/proto_bindings/dlcservice.pb.h>

namespace dlcservice {

class StateChangeReporterInterface {
 public:
  virtual ~StateChangeReporterInterface() = default;

  // Is called by whomever changes the state of a DLC.
  virtual void DlcStateChanged(const DlcState& dlc_state) = 0;

 protected:
  StateChangeReporterInterface() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(StateChangeReporterInterface);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_STATE_CHANGE_REPORTER_INTERFACE_H_