// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera3_module_fixture.h"

namespace camera3_test {

// Camera module

Camera3Module::Camera3Module() : cam_module_(NULL) {
  Create();
}

Camera3Module::~Camera3Module() {
  if (cam_hal_handle_) {
    dlclose(cam_hal_handle_);
  }
}

void Camera3Module::Create() {
  const camera_module_t* cam_module = NULL;
  const char kCameraHalDllName[] = "camera_hal.so";

  cam_hal_handle_ = dlopen(kCameraHalDllName, RTLD_NOW);
  ASSERT_TRUE(NULL != cam_hal_handle_) << "Failed to dlopen: " << dlerror();

  cam_module = static_cast<const camera_module_t*>(
      dlsym(cam_hal_handle_, HAL_MODULE_INFO_SYM_AS_STR));

  ASSERT_TRUE(NULL != cam_module) << "Camera module is invalid";
  ASSERT_TRUE(NULL != cam_module->get_number_of_cameras)
      << "get_number_of_cameras is not implemented";
  ASSERT_TRUE(NULL != cam_module->get_camera_info)
      << "get_camera_info is not implemented";
  ASSERT_TRUE(NULL != cam_module->common.methods->open)
      << "open() is unimplemented";

  // Everything looks good
  cam_module_ = cam_module;
}

int Camera3Module::Initialize() {
  return cam_module_ ? 0 : -ENODEV;
}

int Camera3Module::GetNumberOfCameras() const {
  if (!cam_module_) {
    return -ENODEV;
  }

  return cam_module_->get_number_of_cameras();
}

std::vector<int> Camera3Module::GetCameraIds() const {
  if (!cam_module_) {
    return std::vector<int>();
  }

  int num_cams = cam_module_->get_number_of_cameras();
  std::vector<int> ids(num_cams);
  for (int i = 0; i < num_cams; i++) {
    ids[i] = i;
  }

  return ids;
}

bool Camera3Module::IsFormatAvailable(int cam_id, int format) const {
  if (!cam_module_) {
    return false;
  }

  camera_info info;

  EXPECT_EQ(0, cam_module_->get_camera_info(cam_id, &info))
      << "Can't get camera info for" << cam_id;
  if (testing::Test::HasFailure()) {
    return false;
  }

  camera_metadata_ro_entry_t avaliable_config = {};
  EXPECT_EQ(
      0,
      find_camera_metadata_ro_entry(
          const_cast<camera_metadata_t*>(info.static_camera_characteristics),
          ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &avaliable_config));
  EXPECT_NE(0u, avaliable_config.count)
      << "Camera stream configuration is empty";
  EXPECT_EQ(0u, avaliable_config.count % 4)
      << "Camera stream configuration parsing error";
  if (testing::Test::HasFailure()) {
    return false;
  }

  for (uint32_t i = 0; i < avaliable_config.count; i += 4) {
    if (avaliable_config.data.i32[i] == format) {
      return true;
    }
  }

  return false;
}

camera3_device* Camera3Module::OpenDevice(int cam_id) const {
  if (!cam_module_) {
    return NULL;
  }

  hw_device_t* device = NULL;
  char cam_id_name[3];
  snprintf(cam_id_name, 3, "%d", cam_id);

  if (cam_module_->common.methods->open((const hw_module_t*)cam_module_,
                                        cam_id_name, &device) == 0) {
    return reinterpret_cast<camera3_device_t*>(device);
  }

  return NULL;
}

int Camera3Module::GetCameraInfo(int cam_id, camera_info* info) const {
  if (!cam_module_) {
    return -ENODEV;
  }

  return cam_module_->get_camera_info(cam_id, info);
}

// Test fixture

void Camera3ModuleFixture::SetUp() {
  ASSERT_EQ(0, cam_module_.Initialize())
      << "Camera module initialization fails";
}

// Test cases

TEST_F(Camera3ModuleFixture, NumberOfCameras) {
  ASSERT_GE(cam_module_.GetNumberOfCameras(), 0) << "No cameras found";
  ASSERT_LT(cam_module_.GetNumberOfCameras(), kMaxNumCameras)
      << "Too many cameras found";
}

TEST_F(Camera3ModuleFixture, OpenDeviceOfBadIndices) {
  int bad_indices[] = {-1, cam_module_.GetNumberOfCameras(),
                       cam_module_.GetNumberOfCameras() + 1};
  for (size_t i = 0; i < arraysize(bad_indices); ++i) {
    ASSERT_TRUE(NULL == cam_module_.OpenDevice(bad_indices[i]))
        << "Open camera device of bad index " << bad_indices[i];
  }
}

TEST_F(Camera3ModuleFixture, IsActiveArraySizeSubsetOfPixelArraySize) {
  for (int cam_id = 0; cam_id < cam_module_.GetNumberOfCameras(); cam_id++) {
    camera_info info;
    ASSERT_EQ(0, cam_module_.GetCameraInfo(cam_id, &info))
        << "Can't get camera info for " << cam_id;

    camera_metadata_ro_entry_t entry;
    ASSERT_EQ(
        0,
        find_camera_metadata_ro_entry(
            const_cast<camera_metadata_t*>(info.static_camera_characteristics),
            ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, &entry))
        << "Can't find the sensor pixel array size.";
    int pixel_array_w = entry.data.i32[0];
    int pixel_array_h = entry.data.i32[1];

    ASSERT_EQ(
        0,
        find_camera_metadata_ro_entry(
            const_cast<camera_metadata_t*>(info.static_camera_characteristics),
            ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, &entry))
        << "Can't find the sensor active array size.";
    int active_array_w = entry.data.i32[0];
    int active_array_h = entry.data.i32[1];

    ASSERT_LE(active_array_h, pixel_array_h);
    ASSERT_LE(active_array_w, pixel_array_w);
  }
}

TEST_F(Camera3ModuleFixture, OpenDevice) {
  for (int cam_id = 0; cam_id < cam_module_.GetNumberOfCameras(); cam_id++) {
    camera3_device* cam_dev = cam_module_.OpenDevice(cam_id);
    ASSERT_TRUE(NULL != cam_dev) << "Camera open() returned a NULL device";
    ASSERT_TRUE(NULL != cam_dev->common.close)
        << "Camera close() is not implemented";
    cam_dev->common.close(&cam_dev->common);
  }
}

TEST_F(Camera3ModuleFixture, OpenDeviceTwice) {
  for (int cam_id = 0; cam_id < cam_module_.GetNumberOfCameras(); cam_id++) {
    camera3_device* cam_dev = cam_module_.OpenDevice(cam_id);
    ASSERT_TRUE(NULL != cam_dev) << "Camera open() returned a NULL device";
    // Open the device again
    camera3_device* cam_bad_dev = cam_module_.OpenDevice(cam_id);
    ASSERT_TRUE(NULL == cam_bad_dev) << "Opening camera device " << cam_id
                                     << " should have failed";
    ASSERT_TRUE(NULL != cam_dev->common.close)
        << "Camera close() is not implemented";
    cam_dev->common.close(&cam_dev->common);
  }
}

TEST_F(Camera3ModuleFixture, RequiredFormats) {
  for (int cam_id = 0; cam_id < cam_module_.GetNumberOfCameras(); cam_id++) {
    ASSERT_TRUE(cam_module_.IsFormatAvailable(cam_id, HAL_PIXEL_FORMAT_BLOB))
        << "Camera stream configuration does not support JPEG";
    ASSERT_TRUE(
        cam_module_.IsFormatAvailable(cam_id, HAL_PIXEL_FORMAT_YCbCr_420_888))
        << "Camera stream configuration does not support flexible YUV";
  }
}

}  // namespace camera3_test
