// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_ROUTINE_FACTORY_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_ROUTINE_FACTORY_H_

#include <memory>

#include <base/macros.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/routines/diag_routine.h"
#include "diagnostics/wilco_dtc_supportd/routine_factory.h"
#include "wilco_dtc_supportd.pb.h"  // NOLINT(build/include)

namespace diagnostics {

// Implementation of RoutineFactory that should only be used for
// testing.
class FakeRoutineFactory final : public RoutineFactory {
 public:
  FakeRoutineFactory();

  // RoutineFactory overrides:
  ~FakeRoutineFactory() override;
  std::unique_ptr<DiagnosticRoutine> CreateRoutine(
      const grpc_api::RunRoutineRequest& request) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeRoutineFactory);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_ROUTINE_FACTORY_H_