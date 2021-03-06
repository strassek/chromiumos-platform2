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

#define LOG_TAG "mtkcam-customer_scenario_mgr"
//

#include <cutils/compiler.h>
#include <map>
#include <memory>
#include <mtkcam/3rdparty/customer/customer_scenario_mgr.h>
#include <mtkcam/utils/std/common.h>
#include <mtkcam/utils/std/Log.h>
#include <mtkcam/utils/std/Trace.h>
#include <property_lib.h>
#include <unordered_map>
#include <vector>

#define USE_CUSTOMER_SCENARIO_MGR 0

/******************************************************************************
 *
 ******************************************************************************/
#define __DEBUG  // enable function scope debug
#ifdef __DEBUG
#define FUNCTION_SCOPE                                                       \
  auto __scope_logger__ = [](char const* f) -> std::shared_ptr<const char> { \
    CAM_LOGD("[%s] + ", f);                                                  \
    return std::shared_ptr<const char>(                                      \
        f, [](char const* p) { CAM_LOGD("[%s] -", p); });                    \
  }(__FUNCTION__)
#else
#define FUNCTION_SCOPE
#endif

/******************************************************************************
 *
 ******************************************************************************/
using NSCam::NSPipelinePlugin::MTK_FEATURE_NR;
using NSCam::NSPipelinePlugin::NO_FEATURE_NORMAL;
using NSCam::NSPipelinePlugin::TP_FEATURE_FB;

// ==========================================================================
// TODO(MTK): Feature Combinations for customer
// #define  <feature combination>              (key feature         |
// post-processing features | ...)
//
// single cam capture feature combination
#define TP_FEATURE_COMBINATION_SINGLE \
  (NO_FEATURE_NORMAL | MTK_FEATURE_NR | TP_FEATURE_FB)

// streaming feature combination (TODO: it should be refined by streaming
// scenario feature)
#define TP_FEATURE_COMBINATION_VIDEO_NORMAL (NO_FEATURE_NORMAL)
// ===========================================================================
//
/******************************************************************************
 *
 ******************************************************************************/

namespace NSCam {
namespace v3 {
namespace pipeline {
namespace policy {
namespace scenariomgr {

bool gForceCustomerScenarioMgr = ::property_get_int32(
    "vendor.debug.camera.customer.scenario.force", USE_CUSTOMER_SCENARIO_MGR);

// TODO(MTK): add scenario/feature set by openId order for camera scenario by
// customer
const std::vector<std::unordered_map<int32_t, ScenarioFeatures>>
    gCustomerScenarioFeaturesMaps = {
        // openId = 0
        {// capture
         CAMERA_SCENARIO_START(MTK_CAMERA_SCENARIO_CAPTURE_NORMAL)
             ADD_CAMERA_FEATURE_SET(
                 NO_FEATURE_NORMAL,
                 TP_FEATURE_COMBINATION_SINGLE) CAMERA_SCENARIO_END
                 //
                 CAMERA_SCENARIO_START(
                     CUSTOMER_CAMERA_SCENARIO_CAPTURE_PRO) CAMERA_SCENARIO_END
                     //
                     // streaming (TODO: add features combination for streaming)
                     CAMERA_SCENARIO_START(MTK_CAMERA_SCENARIO_STREAMING_NORMAL)
                         ADD_CAMERA_FEATURE_SET(
                             NO_FEATURE_NORMAL,
                             TP_FEATURE_COMBINATION_VIDEO_NORMAL)
                             CAMERA_SCENARIO_END},
        // openId = 1
        {// capture
         CAMERA_SCENARIO_START(MTK_CAMERA_SCENARIO_CAPTURE_NORMAL)
             ADD_CAMERA_FEATURE_SET(
                 NO_FEATURE_NORMAL,
                 TP_FEATURE_COMBINATION_SINGLE) CAMERA_SCENARIO_END
                 //
                 CAMERA_SCENARIO_START(
                     CUSTOMER_CAMERA_SCENARIO_CAPTURE_PRO) CAMERA_SCENARIO_END
                     //
                     // streaming
                     CAMERA_SCENARIO_START(MTK_CAMERA_SCENARIO_STREAMING_NORMAL)
                         ADD_CAMERA_FEATURE_SET(
                             NO_FEATURE_NORMAL,
                             TP_FEATURE_COMBINATION_VIDEO_NORMAL)
                             CAMERA_SCENARIO_END},
        // openId = 2
        {// capture
         CAMERA_SCENARIO_START(MTK_CAMERA_SCENARIO_CAPTURE_NORMAL)
             ADD_CAMERA_FEATURE_SET(
                 NO_FEATURE_NORMAL,
                 TP_FEATURE_COMBINATION_SINGLE) CAMERA_SCENARIO_END
                 //
                 CAMERA_SCENARIO_START(
                     CUSTOMER_CAMERA_SCENARIO_CAPTURE_PRO) CAMERA_SCENARIO_END
                     //
                     // streaming
                     CAMERA_SCENARIO_START(MTK_CAMERA_SCENARIO_STREAMING_NORMAL)
                         ADD_CAMERA_FEATURE_SET(
                             NO_FEATURE_NORMAL,
                             TP_FEATURE_COMBINATION_VIDEO_NORMAL)
                             CAMERA_SCENARIO_END},
        // openId = 3
        {// capture
         CAMERA_SCENARIO_START(MTK_CAMERA_SCENARIO_CAPTURE_NORMAL)
             ADD_CAMERA_FEATURE_SET(
                 NO_FEATURE_NORMAL,
                 TP_FEATURE_COMBINATION_SINGLE) CAMERA_SCENARIO_END
                 //
                 CAMERA_SCENARIO_START(
                     CUSTOMER_CAMERA_SCENARIO_CAPTURE_PRO) CAMERA_SCENARIO_END
                     //
                     // streaming
                     CAMERA_SCENARIO_START(MTK_CAMERA_SCENARIO_STREAMING_NORMAL)
                         ADD_CAMERA_FEATURE_SET(
                             NO_FEATURE_NORMAL,
                             TP_FEATURE_COMBINATION_VIDEO_NORMAL)
                             CAMERA_SCENARIO_END},
        // openId = 4
        {// capture
         CAMERA_SCENARIO_START(MTK_CAMERA_SCENARIO_CAPTURE_NORMAL)
             ADD_CAMERA_FEATURE_SET(
                 NO_FEATURE_NORMAL,
                 TP_FEATURE_COMBINATION_SINGLE) CAMERA_SCENARIO_END
                 //
                 CAMERA_SCENARIO_START(
                     CUSTOMER_CAMERA_SCENARIO_CAPTURE_PRO) CAMERA_SCENARIO_END
                     //
                     // streaming
                     CAMERA_SCENARIO_START(MTK_CAMERA_SCENARIO_STREAMING_NORMAL)
                         ADD_CAMERA_FEATURE_SET(
                             NO_FEATURE_NORMAL,
                             TP_FEATURE_COMBINATION_VIDEO_NORMAL)
                             CAMERA_SCENARIO_END},

        // TODO(MTK): add more for openId = N
        // ...
};

auto customer_get_capture_scenario(int32_t* pScenario, /*eCameraScenario*/
                                   const ScenarioHint& scenarioHint,
                                   IMetadata const* pAppMetadata) -> bool {
  if (!gForceCustomerScenarioMgr) {
    MY_LOGD("not support: USE_CUSTOMER_SCENARIO_MGR(%d), set forced(%d))",
            USE_CUSTOMER_SCENARIO_MGR, gForceCustomerScenarioMgr);
    return false;
  }

  if (CC_UNLIKELY(pAppMetadata == nullptr)) {
    MY_LOGE("pAppMetadata is invalid nullptr!");
    return false;
  }

  FUNCTION_SCOPE;

  *pScenario = CUSTOMER_CAMERA_SCENARIO_UNKNOW;
  MY_LOGD("scenarioHint(isCcaptureScenarioIndex:%d)",
          scenarioHint.captureScenarioIndex);

  // TODO(MTK): customer can modified the logic/flow to decide the streaming
  // scenario.
  if (scenarioHint.captureScenarioIndex >
      0) {  // force by vendor tag (ex:Pro mode))
    MY_LOGI("forced captureScenarioIndex:%d",
            scenarioHint.captureScenarioIndex);
    *pScenario = scenarioHint.captureScenarioIndex;
  } else {
    MY_LOGI("no dedicated scenario, normal scenario");
    *pScenario = MTK_CAMERA_SCENARIO_CAPTURE_NORMAL;
  }

  MY_LOGI("scenario:%d", *pScenario);
  return true;
}

auto customer_get_streaming_scenario(int32_t* pScenario, /*eCameraScenario*/
                                     const ScenarioHint& scenarioHint,
                                     IMetadata const* pAppMetadata) -> bool {
  if (!gForceCustomerScenarioMgr) {
    MY_LOGD("not support: USE_CUSTOMER_SCENARIO_MGR(%d), set forced(%d))",
            USE_CUSTOMER_SCENARIO_MGR, gForceCustomerScenarioMgr);
    return false;
  }

  if (CC_UNLIKELY(pAppMetadata == nullptr)) {
    MY_LOGE("pAppMetadata is invalid nullptr!");
    return false;
  }

  FUNCTION_SCOPE;

  *pScenario = CUSTOMER_CAMERA_SCENARIO_UNKNOW;
  MY_LOGD("scenarioHint(streamingScenarioIndex:%d)",
          scenarioHint.streamingScenarioIndex);

  // TODO(MTK): customer can modified the logic/flow to decide the streaming
  // scenario.
  if (scenarioHint.streamingScenarioIndex > 0) {  // forced by vendor tag
    MY_LOGI("forced streamingScenarioIndex:%d",
            scenarioHint.streamingScenarioIndex);
    *pScenario = scenarioHint.streamingScenarioIndex;
  } else {
    MY_LOGI("no dedicated scenario, normal scenario");
    *pScenario = MTK_CAMERA_SCENARIO_STREAMING_NORMAL;
  }

  MY_LOGI("scenario:%d", *pScenario);
  return true;
}

auto customer_get_features_table_by_scenario(
    int32_t openId,
    int32_t const scenario, /*eCameraScenario*/
    ScenarioFeatures* pScenarioFeatures) -> bool {
  if (!gForceCustomerScenarioMgr) {
    MY_LOGD("not support: USE_CUSTOMER_SCENARIO_MGR(%d), set forced(%d))",
            USE_CUSTOMER_SCENARIO_MGR, gForceCustomerScenarioMgr);
    return false;
  }

  FUNCTION_SCOPE;

  size_t tableSize = gCustomerScenarioFeaturesMaps.size();
  MY_LOGD("scenario:%d, table size:%zu", scenario, tableSize);

  if (openId >= static_cast<int32_t>(tableSize)) {
    MY_LOGE(
        "cannot query featuresTable, openId(%d) is out of "
        "gCustomerScenarioFeaturesMaps size(%zu)",
        openId, tableSize);
    return false;
  }
  auto scenarioFeaturesMap = gCustomerScenarioFeaturesMaps[openId];

  auto iter_got = scenarioFeaturesMap.find(scenario);
  if (iter_got != scenarioFeaturesMap.end()) {
    *pScenarioFeatures = iter_got->second;
    MY_LOGI("find features for scenario(%d : %s)", scenario,
            (*pScenarioFeatures).scenarioName.c_str());
  } else {
    MY_LOGE(
        "cannot find features for openId(%d), scenario(%d) in "
        "gScenarioFeaturesMap",
        openId, scenario);
    return false;
  }

  return true;
}

};  // namespace scenariomgr
};  // namespace policy
};  // namespace pipeline
};  // namespace v3
};  // namespace NSCam
