/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_3DNR_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_3DNR_H_

#include <camera_custom_3dnr_base.h>

int VISIBILITY_PUBLIC get_3dnr_iso_enable_threshold_low(void);
int VISIBILITY_PUBLIC get_3dnr_iso_enable_threshold_high(void);
int VISIBILITY_PUBLIC get_3dnr_max_iso_increase_percentage(void);
int VISIBILITY_PUBLIC get_3dnr_hw_power_off_threshold(void);
int VISIBILITY_PUBLIC get_3dnr_hw_power_reopen_delay(void);
int VISIBILITY_PUBLIC get_3dnr_gmv_threshold(int force3DNR);

MBOOL VISIBILITY_PUBLIC is_vhdr_profile(MUINT8 ispProfile);

class VISIBILITY_PUBLIC NR3DCustom : public NR3DCustomBase {
 private:
  // DO NOT create instance
  NR3DCustom() {}

 public:
  static MBOOL isEnabled3DNR30() { return true; }

  static MBOOL isSupportRSC();
  static MBOOL isEnabledRSC(MUINT32 mask);

  static MINT32 get_3dnr_off_iso_threshold(MUINT8 ispProfile = 0,
                                           MBOOL useAdbValue = 0);
};

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_INC_CAMERA_CUSTOM_3DNR_H_
