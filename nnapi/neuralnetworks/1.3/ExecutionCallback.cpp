// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the boilerplate implementation of the IAllocator HAL interface,
// generated by the hidl-gen tool and then modified for use on Chrome OS.
// Modifications include:
// - Removal of non boiler plate client and server related code.
// - Reformatting to meet the Chrome OS coding standards.
//
// Originally generated with the command:
// $ hidl-gen -o output -L c++ -r android.hardware:hardware/interfaces \
//   android.hardware.neuralnetworks@1.3

#define LOG_TAG "android.hardware.neuralnetworks@1.3::ExecutionCallback"

#include <android/hardware/neuralnetworks/1.3/IExecutionCallback.h>
#include <hidl/Status.h>

namespace android {
namespace hardware {
namespace neuralnetworks {
namespace V1_3 {

const char* IExecutionCallback::descriptor(
    "android.hardware.neuralnetworks@1.3::IExecutionCallback");

::android::hardware::Return<void> IExecutionCallback::interfaceChain(
    interfaceChain_cb _hidl_cb) {
  _hidl_cb({
      ::android::hardware::neuralnetworks::V1_3::IExecutionCallback::descriptor,
      ::android::hardware::neuralnetworks::V1_2::IExecutionCallback::descriptor,
      ::android::hardware::neuralnetworks::V1_0::IExecutionCallback::descriptor,
      ::android::hidl::base::V1_0::IBase::descriptor,
  });
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IExecutionCallback::debug(
    const ::android::hardware::hidl_handle& fd,
    const ::android::hardware::hidl_vec<::android::hardware::hidl_string>&
        options) {
  (void)fd;
  (void)options;
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IExecutionCallback::interfaceDescriptor(
    interfaceDescriptor_cb _hidl_cb) {
  _hidl_cb(::android::hardware::neuralnetworks::V1_3::IExecutionCallback::
               descriptor);
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IExecutionCallback::getHashChain(
    getHashChain_cb _hidl_cb) {
  _hidl_cb(
      {/* 127ba11efb8220dc3aec9a8f441b59eaf1c68d7f03f577833e1824de75a36b17 */
       (uint8_t[32]){18,  123, 161, 30, 251, 130, 32,  220, 58,  236, 154,
                     143, 68,  27,  89, 234, 241, 198, 141, 127, 3,   245,
                     119, 131, 62,  24, 36,  222, 117, 163, 107, 23},
       /* 92714960d1a53fc2ec557302b41c7cc93d2636d8364a44bd0f85be0c92927ff8 */
       (uint8_t[32]){146, 113, 73, 96,  209, 165, 63,  194, 236, 85, 115,
                     2,   180, 28, 124, 201, 61,  38,  54,  216, 54, 74,
                     68,  189, 15, 133, 190, 12,  146, 146, 127, 248},
       /* 12e8dca4ab7d8aadd0ef8f1b438021938e2396139e85db2ed65783b08800aa52 */
       (uint8_t[32]){18,  232, 220, 164, 171, 125, 138, 173, 208, 239, 143,
                     27,  67,  128, 33,  147, 142, 35,  150, 19,  158, 133,
                     219, 46,  214, 87,  131, 176, 136, 0,   170, 82},
       /* ec7fd79ed02dfa85bc499426adae3ebe23ef0524f3cd6957139324b83b18ca4c */
       (uint8_t[32]){236, 127, 215, 158, 208, 45,  250, 133, 188, 73,  148,
                     38,  173, 174, 62,  190, 35,  239, 5,   36,  243, 205,
                     105, 87,  19,  147, 36,  184, 59,  24,  202, 76}});
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IExecutionCallback::setHALInstrumentation() {
  return ::android::hardware::Void();
}

::android::hardware::Return<bool> IExecutionCallback::linkToDeath(
    const ::android::sp<::android::hardware::hidl_death_recipient>& recipient,
    uint64_t cookie) {
  (void)cookie;
  return (recipient != nullptr);
}

::android::hardware::Return<void> IExecutionCallback::ping() {
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IExecutionCallback::getDebugInfo(
    getDebugInfo_cb _hidl_cb) {
  ::android::hidl::base::V1_0::DebugInfo info = {};
  info.pid = -1;
  info.ptr = 0;
  info.arch =
#if defined(__LP64__)
      ::android::hidl::base::V1_0::DebugInfo::Architecture::IS_64BIT;
#else
      ::android::hidl::base::V1_0::DebugInfo::Architecture::IS_32BIT;
#endif
  _hidl_cb(info);
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IExecutionCallback::notifySyspropsChanged() {
  ::android::report_sysprop_change();
  return ::android::hardware::Void();
}

::android::hardware::Return<bool> IExecutionCallback::unlinkToDeath(
    const ::android::sp<::android::hardware::hidl_death_recipient>& recipient) {
  return (recipient != nullptr);
}

::android::hardware::Return<::android::sp<
    ::android::hardware::neuralnetworks::V1_3::IExecutionCallback>>
IExecutionCallback::castFrom(
    const ::android::sp<
        ::android::hardware::neuralnetworks::V1_3::IExecutionCallback>& parent,
    bool /* emitError */) {
  return parent;
}

::android::hardware::Return<::android::sp<
    ::android::hardware::neuralnetworks::V1_3::IExecutionCallback>>
IExecutionCallback::castFrom(
    const ::android::sp<::android::hidl::base::V1_0::IBase>& /*parent*/,
    bool /*emitError*/) {
  return nullptr;
  // TODO(jmpollock): b/159130631 Make this actually do a valid cast without
  //                  pulling in too many dependencies from hidl/transport .
  // return ::android::hardware::details::castInterface<IExecutionCallback,
  // ::android::hidl::base::V1_0::IBase, BpHwExecutionCallback>(
  //         parent, "android.hardware.neuralnetworks@1.3::IExecutionCallback",
  //         emitError);
}

}  // namespace V1_3
}  // namespace neuralnetworks
}  // namespace hardware
}  // namespace android
