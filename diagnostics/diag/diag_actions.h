// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_DIAG_DIAG_ACTIONS_H_
#define DIAGNOSTICS_DIAG_DIAG_ACTIONS_H_

#include <cstdint>
#include <string>

#include <base/macros.h>
#include <base/optional.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"

namespace diagnostics {

// This class is responsible for providing the actions corresponding to the
// command-line arguments for the diag tool. Only capable of running a single
// routine at a time.
class DiagActions final {
 public:
  // The two TimeDelta inputs are used to configure this instance's polling
  // behavior - the time between polls, and the maximum time before giving up on
  // a running routine.
  DiagActions(base::TimeDelta polling_interval,
              base::TimeDelta maximum_execution_time);
  ~DiagActions();

  // Print a list of routines available on the platform. Returns true iff all
  // available routines were successfully converted to human-readable strings
  // and printed.
  bool ActionGetRoutines();
  // Run a particular diagnostic routine. See mojo/cros_healthd.mojom for
  // details on the individual routines. Returns true iff the routine completed.
  // Note that this does not mean the routine succeeded, only that it started,
  // ran, and was removed.
  bool ActionRunAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type);
  bool ActionRunBatteryCapacityRoutine(uint32_t low_mah, uint32_t high_mah);
  bool ActionRunBatteryHealthRoutine(uint32_t maximum_cycle_count,
                                     uint32_t percent_battery_wear_allowed);
  bool ActionRunCpuCacheRoutine(const base::TimeDelta& exec_duration);
  bool ActionRunCpuStressRoutine(const base::TimeDelta& exec_duration);
  bool ActionRunFloatingPointAccuracyRoutine(
      const base::TimeDelta& exec_duration);
  bool ActionRunNvmeSelfTestRoutine(
      chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type);
  bool ActionRunNvmeWearLevelRoutine(uint32_t wear_level_threshold);
  bool ActionRunSmartctlCheckRoutine();
  bool ActionRunUrandomRoutine(uint32_t length_seconds);

  // Cancels the next routine run, when that routine reports a progress percent
  // greater than or equal to |percent|. Should be called before running the
  // routine to be cancelled.
  void ForceCancelAtPercent(uint32_t percent);

 private:
  // Helper function to determine when a routine has finished. Also does any
  // necessary cleanup.
  bool PollRoutineAndProcessResult();
  // Displays the user message from |interactive_result|, then blocks for user
  // input. After receiving input, resets the polling time and continues to
  // poll.
  bool ProcessInteractiveResultAndContinue(
      chromeos::cros_healthd::mojom::InteractiveRoutineUpdatePtr
          interactive_result);
  // Displays information from a noninteractive routine update and removes the
  // routine corresponding to |id_|.
  bool ProcessNonInteractiveResultAndEnd(
      chromeos::cros_healthd::mojom::NonInteractiveRoutineUpdatePtr
          noninteractive_result);
  // Attempts to remove the routine corresponding to |id_|.
  void RemoveRoutine();

  // Used to send mojo requests to cros_healthd.
  CrosHealthdMojoAdapter adapter_;
  // ID of the routine being run.
  int32_t id_ = chromeos::cros_healthd::mojom::kFailedToStartId;

  // If |force_cancel_| is true, the next routine run will be cancelled when its
  // progress is greater than or equal to |cancellation_percent_|.
  bool force_cancel_ = false;
  uint32_t cancellation_percent_ = 0;

  // Polling interval.
  const base::TimeDelta kPollingInterval;
  // Maximum time we're willing to wait for a routine to finish.
  const base::TimeDelta kMaximumExecutionTime;

  DISALLOW_COPY_AND_ASSIGN(DiagActions);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_DIAG_DIAG_ACTIONS_H_