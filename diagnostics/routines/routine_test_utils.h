// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_ROUTINES_ROUTINE_TEST_UTILS_H_
#define DIAGNOSTICS_ROUTINES_ROUTINE_TEST_UTILS_H_

#include <string>

#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// All of the utilities in this file are for use in testing only.

// Verifies that the given RoutineUpdateUnion is an interactive update with the
// specified user message.
void VerifyInteractiveUpdate(
    const chromeos::cros_healthd::mojom::RoutineUpdateUnionPtr& update_union,
    chromeos::cros_healthd::mojom::DiagnosticRoutineUserMessageEnum
        expected_user_message);

// Verifies that the given RoutineUpdateUnion is a noninteractive update with
// the specified status and status message.
void VerifyNonInteractiveUpdate(
    const chromeos::cros_healthd::mojom::RoutineUpdateUnionPtr& update_union,
    chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum expected_status,
    const std::string& expected_status_message);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_ROUTINES_ROUTINE_TEST_UTILS_H_
