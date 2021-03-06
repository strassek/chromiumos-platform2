// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CLIENT_H_
#define LORGNETTE_SANE_CLIENT_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <brillo/errors/error.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

namespace lorgnette {

struct ValidOptionValues {
  std::vector<uint32_t> resolutions;
  std::vector<DocumentSource> sources;
  std::vector<std::string> color_modes;
};

enum FrameFormat {
  kGrayscale,
  kRGB,
};

struct ScanParameters {
  FrameFormat format;
  int bytes_per_line;
  int pixels_per_line;
  int lines;
  int depth;
};

// This class represents an active connection to a scanning device.
// At most 1 active connection to a particular device is allowed at once.
// This class is thread-compatible, but not thread-safe.
class SaneDevice {
 public:
  virtual ~SaneDevice() {}

  virtual bool GetValidOptionValues(brillo::ErrorPtr* error,
                                    ValidOptionValues* values) = 0;

  virtual bool SetScanResolution(brillo::ErrorPtr* error, int resolution) = 0;
  virtual bool GetDocumentSource(brillo::ErrorPtr* error,
                                 DocumentSource* source_out) = 0;
  virtual bool SetDocumentSource(brillo::ErrorPtr* error,
                                 const DocumentSource& source) = 0;
  virtual bool SetColorMode(brillo::ErrorPtr* error, ColorMode color_mode) = 0;
  virtual SANE_Status StartScan(brillo::ErrorPtr* error) = 0;
  virtual bool GetScanParameters(brillo::ErrorPtr* error,
                                 ScanParameters* parameters) = 0;
  virtual bool ReadScanData(brillo::ErrorPtr* error,
                            uint8_t* buf,
                            size_t count,
                            size_t* read_out) = 0;
};

// This class represents a connection to the scanner library SANE.  Once
// created, it will initialize a connection to SANE, and it will disconnect
// when destroyed.
// At most 1 connection to SANE is allowed to be active per process, so the
// user must be careful to ensure that is the case.
class SaneClient {
 public:
  virtual ~SaneClient() {}

  virtual bool ListDevices(brillo::ErrorPtr* error,
                           std::vector<ScannerInfo>* scanners_out) = 0;
  std::unique_ptr<SaneDevice> ConnectToDevice(brillo::ErrorPtr* error,
                                              const std::string& device_name);

 protected:
  virtual std::unique_ptr<SaneDevice> ConnectToDeviceInternal(
      brillo::ErrorPtr* error, const std::string& device_name) = 0;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_H_
