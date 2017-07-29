// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_BACKLIGHT_INTERFACE_H_
#define POWER_MANAGER_POWERD_SYSTEM_BACKLIGHT_INTERFACE_H_

#include <stdint.h>

#include <base/logging.h>
#include <base/macros.h>
#include <base/time/time.h>

namespace power_manager {
namespace system {

class BacklightObserver;

// Interface for getting and setting the backlight level from hardware.
class BacklightInterface {
 public:
  BacklightInterface() {}
  virtual ~BacklightInterface() {}

  // Adds or removes an observer.
  virtual void AddObserver(BacklightObserver* observer) = 0;
  virtual void RemoveObserver(BacklightObserver* observer) = 0;

  // Returns true iff the underlying backlight device is present.
  // If not, other methods may report failure.
  virtual bool DeviceExists() = 0;

  // Gets the maximum brightness level (in an an arbitrary device-specific
  // range; note that 0 is always the minimum allowable value, though).
  virtual int64_t GetMaxBrightnessLevel() = 0;

  // Gets the current brightness level (in an an arbitrary device-specific
  // range).
  virtual int64_t GetCurrentBrightnessLevel() = 0;

  // Sets the backlight to |level| over |interval|. Returns false on failure.
  virtual bool SetBrightnessLevel(int64_t level, base::TimeDelta interval) = 0;

  // Sets the resume backlight to |level|.  Returns false on failure.
  virtual bool SetResumeBrightnessLevel(int64_t level) = 0;

  // Returns true if the brightness is currently being animated.
  virtual bool TransitionInProgress() const = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(BacklightInterface);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_BACKLIGHT_INTERFACE_H_
