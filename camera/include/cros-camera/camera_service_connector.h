//
// Copyright (c) 2020 Corel Corporation. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CAMERA_SERVICE_CONNECTOR_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CAMERA_SERVICE_CONNECTOR_H_

#include <errno.h>
#include <stdint.h>

#include "cros-camera/export.h"

#ifdef __cplusplus
extern "C" {
#endif

// Options for cros_cam_init().
//   Current api version: 0
//   Change logs:
//     v0: It works but the public api is not stable yet.
typedef struct cros_cam_init_option_t_ {
  int api_version;    // the expected api version
  void* reserved[8];  // reserved for future use
} cros_cam_init_option_t;

// Camera lens facing enum
enum {
  CROS_CAM_FACING_BACK = 0,     // world facing
  CROS_CAM_FACING_FRONT = 1,    // user facing
  CROS_CAM_FACING_EXTERNAL = 2  // external webcams
};

// Format descriptor
typedef struct cros_cam_format_info_t_ {
  uint32_t fourcc;  // format type (FOURCC code)
  int width;        // frame width in pixels
  int height;       // frame heght in pixels
  int fps;          // frame rate in frames per second
} cros_cam_format_info_t;

// Camera descriptor
//   At least one format expected (format_count >= 1)
typedef struct cros_cam_info_t_ {
  int id;                               // device id
  int facing;                           // one of lens facing enum value
  const char* name;                     // user friendly camera name, UTF8
  int format_count;                     // number of format descriptors
  cros_cam_format_info_t* format_info;  // pointer to array of formats
  void* reserved[8];                    // reserved for future use
} cros_cam_info_t;

// Callback type for camera information
//   Pointer to camera info valid only until the callback returns
//
// Params:
//   context    - arbitrary user context
//   info       - camera descriptor
//   is_removed - hotplug notification
//                0     - device added
//                !0    - device has been removed
// Returns:
//   0   - rearm callback (continue to receive add/remove notifications)
//   <>0 - deregister callback
typedef int (*cros_cam_get_cam_info_cb_t)(void* context,
                                          const cros_cam_info_t* info,
                                          int is_removed);

// Plane descriptor
//   Stores the data for a plane of a frame.
typedef struct cros_cam_plane_t_ {
  int stride;         // stride (pixel line) size in bytes, 0 if unused
  int size;           // size of the data, 0 if the data plane is unused
  uint8_t* data;      // data, null if unused
  void* reserved[8];  // reserved for future use
} cros_cam_plane_t;

// Frame (captured data) descriptor
//   format should be same as requested in start call
//   pointer to frame data valid only until the callback returns
// format::fourcc explicitly defines how many data planes are used and its
// meaning, for example
//   'DMB1', 'JPEG' and 'MJPG' - only palne[0] with compressed data, the size
//       may vary between calls, stride unused (should be 0)
//   'NV12' - two planes: plane[0] is Y, palne[1] is interleaved UV, the size of
//       planes is fixed (defined by width, height and stride), generally stride
//       == width
//   'I420' - three planes: plane[0] is Y, palne[1] is U, plane[2] is V
//   'YUY2' - one plane: plane[0] is interleaved YUV data
typedef struct cros_cam_frame_t_ {
  cros_cam_format_info_t format;  // frame format information
  cros_cam_plane_t planes[4];     // frame data for each plane
  void* reserved[8];              // reserved for future use
} cros_cam_frame_t;

// Capture request descriptor
typedef struct cros_cam_capture_request_t_ {
  int id;                          // camera device to capture
  cros_cam_format_info_t* format;  // format to capture
  void* reserved[8];               // reserved for future use
} cros_cam_capture_request_t;

// Capture result descriptor
typedef struct cros_cam_capture_result_t_ {
  int status;               // 0 for success, -errno for error
  cros_cam_frame_t* frame;  // captured frame. null if error
  void* reserved[8];        // reserved for future use
} cros_cam_capture_result_t;

// Callback type for capture
//   pointer to result valid only until the callback returns
//
// Params:
//   context    - arbitrary user context
//   result     - capture result with frame
// Returns:
//   0   - continue capture
//   <>0 - stop capture
typedef int (*cros_cam_capture_cb_t)(void* context,
                                     const cros_cam_capture_result_t* result);

//
// General initialization.
//   Should be a first call before other library calls.
//   Other library calls allowed only if it succeeded.
//   Should be called only once, i.e. sequence "init" -> "exit" -> "init" is
//   prohibited
//
// Params:
//   option - initialization option
//
// Returns:
//   0  - on success
//   <0 - on failure, for instance:
//     -ENOMEM for OOM
//     -EACCES if process doesn't have permissions to use this API
//     -EPERM if called more than once
CROS_CAMERA_EXPORT int cros_cam_init(const cros_cam_init_option_t* option);

//
// General cleanup, no other library calls and callbacks allowed after it.
// Can be scheduled by atexit()
// TODO(lnishan): Figure out the detailed semantics of this function.
// Should wait returns from callbacks
// Abort capture on all devices
// Returns:
//   0  - on success
//   <0 - on failure
CROS_CAMERA_EXPORT int cros_cam_exit();

//
// Get information about cameras and subscribe for hotplug notifications
//   Callback will be called synchronously (in the same thread) N times (where N
//   is the number of cameras present) to fill the initial list of cameras.
//   Hotplug notifications are async and callback uses own thread
//   There is possible to start capture from callback
//
// Params:
//   callback - callback to receive camera information, or null to unsubscribe
//   context  - arbitrary context data that directly passed to their callback
// Returns:
//   0  - on success
//   <0 - on failure
CROS_CAMERA_EXPORT int cros_cam_get_cam_info(
    cros_cam_get_cam_info_cb_t callback, void* context);

//
// Start capture
//   Callback is called in context of other (capture) thread
//
// Params:
//   request    - capture parameters, pointers are only valid during the call
//   callback   - callback used to receive frames
//   context    - arbitrary contex data that directly passed to their callback
// Returns:
//   0  - on success
//   <0 - on failure
CROS_CAMERA_EXPORT int cros_cam_start_capture(
    const cros_cam_capture_request_t* request,
    cros_cam_capture_cb_t callback,
    void* context);

//
// Stop capture
//   Should wait return from capture callback
//
// Params:
//   id         - the camera device on which we want to stop
// Returns:
//   0  - on success
//   <0 - on failure
CROS_CAMERA_EXPORT int cros_cam_stop_capture(int id);

#ifdef __cplusplus
}
#endif

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CAMERA_SERVICE_CONNECTOR_H_
