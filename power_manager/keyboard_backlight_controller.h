// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_KEYBOARD_BACKLIGHT_CONTROLLER_H_
#define POWER_MANAGER_KEYBOARD_BACKLIGHT_CONTROLLER_H_

#include <glib.h>

#include "base/compiler_specific.h"
#include "power_manager/backlight_controller.h"
#include "power_manager/signal_callback.h"
#include "power_manager/video_detector.h"

namespace power_manager {

class BacklightInterface;
class KeyboardBacklightControllerTest;

// Controls the keyboard backlight for devices with such a backlight.
class KeyboardBacklightController : public BacklightController,
                                    public VideoDetectorObserver {
 public:
  explicit KeyboardBacklightController(BacklightInterface* backlight);
  virtual ~KeyboardBacklightController();

  // Implementation of BacklightController
  virtual bool Init() OVERRIDE;
  virtual void SetAmbientLightSensor(AmbientLightSensor* sensor) OVERRIDE;
  virtual void SetMonitorReconfigure(
      MonitorReconfigure* monitor_reconfigure) OVERRIDE { };
  virtual void SetObserver(BacklightControllerObserver* observer) OVERRIDE;
  virtual double GetTargetBrightnessPercent() OVERRIDE;
  virtual bool GetCurrentBrightnessPercent(double* percent) OVERRIDE;
  virtual bool SetCurrentBrightnessPercent(double percent,
                                           BrightnessChangeCause cause,
                                           TransitionStyle style) OVERRIDE;
  virtual bool IncreaseBrightness(BrightnessChangeCause cause) OVERRIDE;
  virtual bool DecreaseBrightness(bool allow_off,
                                  BrightnessChangeCause cause) OVERRIDE;
  virtual bool SetPowerState(PowerState new_state) OVERRIDE;
  virtual PowerState GetPowerState() const OVERRIDE;
  virtual bool OnPlugEvent(bool is_plugged) OVERRIDE { return true; };
  virtual void SetAlsBrightnessOffsetPercent(double percent) OVERRIDE;
  virtual bool IsBacklightActiveOff() OVERRIDE;
  virtual int GetNumAmbientLightSensorAdjustments() const OVERRIDE;
  virtual int GetNumUserAdjustments() const OVERRIDE;

  // BacklightInterfaceObserver implementation:
  virtual void OnBacklightDeviceChanged() OVERRIDE;

  // Implementation of VideoDetectorObserver
  virtual void OnVideoDetectorEvent(base::TimeTicks last_activity_time,
                                    bool is_fullscreen) OVERRIDE;

 private:
  friend class KeyboardBacklightControllerTest;
  FRIEND_TEST(KeyboardBacklightControllerTest, Init);
  FRIEND_TEST(KeyboardBacklightControllerTest, GetCurrentBrightnessPercent);
  FRIEND_TEST(KeyboardBacklightControllerTest, SetCurrentBrightnessPercent);
  FRIEND_TEST(KeyboardBacklightControllerTest, SetAlsBrightnessOffsetPercent);
  FRIEND_TEST(KeyboardBacklightControllerTest, OnVideoDetectorEvent);
  FRIEND_TEST(KeyboardBacklightControllerTest, UpdateBacklightEnabled);
  FRIEND_TEST(KeyboardBacklightControllerTest, SetBrightnessLevel);
  FRIEND_TEST(KeyboardBacklightControllerTest, HaltVideoTimeout);
  FRIEND_TEST(KeyboardBacklightControllerTest, VideoTimeout);
  FRIEND_TEST(KeyboardBacklightControllerTest, LevelToPercent);
  FRIEND_TEST(KeyboardBacklightControllerTest, PercentToLevel);

  // The backlight level and enabledness are separate values so that the code
  // that sets the level does not need to be aware of the fact the light in
  // certain circumstances might be disabled and the disable/enable code doesn't
  // need to be aware of the level logic.
  void UpdateBacklightEnabled();

  // Instantaneously sets the backlight device's brightness level.
  void SetBrightnessLevel(int64 new_level);

  // Halt the video timeout timer and the callback for the video timeout.
  void HaltVideoTimeout();
  SIGNAL_CALLBACK_0(KeyboardBacklightController, gboolean, VideoTimeout);

  int64 PercentToLevel(double percent) const;
  double LevelToPercent(int64 level) const;

  bool is_initialized_;

  // Backlight used for dimming. Non-owned.
  BacklightInterface* backlight_;

  // Ambient light snesor associated with this controller. This pointer is used
  // for updating the sensor about the status of the backlight. Non-owned.
  AmbientLightSensor* sensor_;

  // Observer that needs to be alerted of changes. Normally this is the powerd
  // daemon. Non-owned.
  BacklightControllerObserver* observer_;

  PowerState state_;

  // State bits dealing with the video and full screen don't enable the
  // backlight case.
  bool is_video_playing_;
  bool is_fullscreen_;
  bool backlight_enabled_;

  // Maximum brightness level exposed by the backlight driver.
  // 0 is always the minimum.
  int64 max_level_;

  // Current level that the backlight is set to, this might vary from the target
  // percentage due to the backlight being disabled.
  int64 current_level_;

  // Current percentage that is trying to be set, might not be set if backlight
  // is disabled.
  double target_percent_;

  // Value returned when we add a timer for the video timeout. This is
  // needed for reseting the timer when the next event occurs.
  guint32 video_timeout_timer_id_;

  // Counters for stat tracking.
  int num_als_adjustments_;
  int num_user_adjustments_;

  DISALLOW_COPY_AND_ASSIGN(KeyboardBacklightController);
};  // class KeyboardBacklightController

}  // namespace power_manager

#endif  // POWER_MANAGER_KEYBOARD_BACKLIGHT_CONTROLLER_H_
