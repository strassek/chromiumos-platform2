// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "libmems/common_types.h"
#include "libmems/iio_channel_impl.h"
#include "libmems/iio_context_impl.h"
#include "libmems/iio_device_impl.h"
#include "libmems/iio_device_trigger_impl.h"

#define ERROR_BUFFER_SIZE 256

namespace libmems {

namespace {

constexpr int kNumSamples = 1;

};  // namespace

base::Optional<int> IioDeviceImpl::GetIdFromString(const char* id_str) {
  return IioDevice::GetIdAfterPrefix(id_str, kDeviceIdPrefix);
}

std::string IioDeviceImpl::GetStringFromId(int id) {
  return base::StringPrintf("%s%d", kDeviceIdPrefix, id);
}

IioDeviceImpl::IioDeviceImpl(IioContextImpl* ctx, iio_device* dev)
    : IioDevice(),
      context_(ctx),
      device_(dev),
      buffer_(nullptr, IioBufferDeleter) {
  CHECK(context_);
  CHECK(device_);
}

IioContext* IioDeviceImpl::GetContext() const {
  return context_;
}

const char* IioDeviceImpl::GetName() const {
  return iio_device_get_name(device_);
}

int IioDeviceImpl::GetId() const {
  const char* id_str = iio_device_get_id(device_);

  auto id = GetIdFromString(id_str);
  DCHECK(id.has_value());
  return id.value();
}

base::FilePath IioDeviceImpl::GetPath() const {
  std::string id_str = GetStringFromId(GetId());
  auto path = base::FilePath("/sys/bus/iio/devices").Append(id_str);
  CHECK(base::DirectoryExists(path));
  return path;
}

base::Optional<std::string> IioDeviceImpl::ReadStringAttribute(
    const std::string& name) const {
  char data[kReadAttrBufferSize] = {0};
  ssize_t len = iio_device_attr_read(device_, name.c_str(), data, sizeof(data));
  if (len < 0) {
    LOG(WARNING) << "Attempting to read attribute " << name
                 << " failed: " << len;
    return base::nullopt;
  }
  return std::string(data, len);
}

base::Optional<int64_t> IioDeviceImpl::ReadNumberAttribute(
    const std::string& name) const {
  long long val = 0;  // NOLINT(runtime/int)
  int error = iio_device_attr_read_longlong(device_, name.c_str(), &val);
  if (error) {
    LOG(WARNING) << "Attempting to read attribute " << name
                 << " failed: " << error;
    return base::nullopt;
  }
  return val;
}

base::Optional<double> IioDeviceImpl::ReadDoubleAttribute(
    const std::string& name) const {
  double val = 0;
  int error = iio_device_attr_read_double(device_, name.c_str(), &val);
  if (error) {
    LOG(WARNING) << "Attempting to read attribute " << name
                 << " failed: " << error;
    return base::nullopt;
  }
  return val;
}

bool IioDeviceImpl::WriteStringAttribute(const std::string& name,
                                         const std::string& value) {
  int error = iio_device_attr_write_raw(device_, name.c_str(), value.data(),
                                        value.size());
  if (error < 0) {
    LOG(WARNING) << "Attempting to write attribute " << name
                 << " failed: " << error;
    return false;
  }
  return true;
}

bool IioDeviceImpl::WriteNumberAttribute(const std::string& name,
                                         int64_t value) {
  int error = iio_device_attr_write_longlong(device_, name.c_str(), value);
  if (error) {
    LOG(WARNING) << "Attempting to write attribute " << name
                 << " failed: " << error;
    return false;
  }
  return true;
}

bool IioDeviceImpl::WriteDoubleAttribute(const std::string& name,
                                         double value) {
  int error = iio_device_attr_write_double(device_, name.c_str(), value);
  if (error) {
    LOG(WARNING) << "Attempting to write attribute " << name
                 << " failed: " << error;
    return false;
  }
  return true;
}

iio_device* IioDeviceImpl::GetUnderlyingIioDevice() const {
  return device_;
}

bool IioDeviceImpl::SetTrigger(IioDevice* trigger_device) {
  // Reset the old - if any - and then add the new trigger.
  int error = iio_device_set_trigger(device_, NULL);
  if (error) {
    LOG(WARNING) << "Unable to clean trigger of device " << GetId()
                 << ", error: " << error;
    return false;
  }
  if (trigger_device == nullptr)
    return true;

  const iio_device* impl_device = nullptr;
  int id = trigger_device->GetId();
  if (id == -2) {
    impl_device = iio_context_find_device(GetContext()->GetCurrentContext(),
                                          kIioSysfsTrigger);
  } else {
    std::string id_str = IioDeviceTriggerImpl::GetStringFromId(id);
    impl_device = iio_context_find_device(GetContext()->GetCurrentContext(),
                                          id_str.c_str());
  }
  if (!impl_device) {
    LOG(WARNING) << "cannot find device " << id << " in the current context";
    return false;
  }

  error = iio_device_set_trigger(device_, impl_device);
  if (error) {
    LOG(WARNING) << "Unable to set trigger for device " << GetId()
                 << " to be device " << trigger_device->GetId()
                 << ", error: " << error;
    return false;
  }
  return true;
}

IioDevice* IioDeviceImpl::GetTrigger() {
  const iio_device* trigger;
  int error = iio_device_get_trigger(device_, &trigger);
  if (error) {
    LOG(WARNING) << "Unable to get trigger for device " << GetId();
    return nullptr;
  }

  if (trigger == nullptr)
    return nullptr;

  const char* id_str = iio_device_get_id(trigger);
  auto id = IioDeviceTriggerImpl::GetIdFromString(id_str);

  IioDevice* trigger_device = nullptr;
  if (id.has_value())
    trigger_device = GetContext()->GetTriggerById(id.value());

  if (trigger_device == nullptr) {
    LOG(WARNING) << GetId() << " has trigger device " << id_str
                 << "which cannot be found in this context";
  }

  return trigger_device;
}

std::vector<IioChannel*> IioDeviceImpl::GetAllChannels() {
  std::vector<IioChannel*> channels;
  uint32_t chn_count = iio_device_get_channels_count(device_);

  for (int i = 0; i < chn_count; ++i) {
    iio_channel* channel = iio_device_get_channel(device_, i);
    if (channel == nullptr) {
      LOG(WARNING) << "Unable to get " << i
                   << "th channel from device: " << GetId();
      continue;
    }

    channels.push_back(GetChannel(iio_channel_get_id(channel)));
  }

  return channels;
}

IioChannel* IioDeviceImpl::GetChannel(const std::string& name) {
  auto k = channels_.find(name);
  if (k != channels_.end())
    return k->second.get();
  iio_channel* channel = iio_device_find_channel(device_, name.c_str(), true);
  if (channel == nullptr)
    channel = iio_device_find_channel(device_, name.c_str(), false);
  if (channel == nullptr)
    return nullptr;
  channels_.emplace(name, std::make_unique<IioChannelImpl>(channel));
  return channels_[name].get();
}

base::Optional<size_t> IioDeviceImpl::GetSampleSize() const {
  ssize_t sample_size = iio_device_get_sample_size(device_);
  if (sample_size < 0) {
    char errMsg[kErrorBufferSize];
    iio_strerror(errno, errMsg, sizeof(errMsg));
    LOG(WARNING) << "Unable to get sample size: " << errMsg;
    return base::nullopt;
  }

  return static_cast<size_t>(sample_size);
}

bool IioDeviceImpl::EnableBuffer(size_t count) {
  if (!WriteNumberAttribute("buffer/length", count))
    return false;
  if (!WriteNumberAttribute("buffer/enable", 1))
    return false;

  return true;
}

bool IioDeviceImpl::DisableBuffer() {
  return WriteNumberAttribute("buffer/enable", 0);
}

bool IioDeviceImpl::IsBufferEnabled(size_t* count) const {
  bool enabled = (ReadNumberAttribute("buffer/enable").value_or(0) == 1);
  if (enabled && count)
    *count = ReadNumberAttribute("buffer/length").value_or(0);

  return enabled;
}

bool IioDeviceImpl::ReadEvent(std::vector<uint8_t>* event) {
  if (!CreateBuffer())
    return false;

  event->clear();

  ssize_t ret = iio_buffer_refill(buffer_.get());
  if (ret < 0) {
    char errMsg[kErrorBufferSize];
    iio_strerror(-ret, errMsg, sizeof(errMsg));
    LOG(ERROR) << "Unable to refill buffer: " << errMsg;
    buffer_.reset();

    return false;
  }

  const auto buf_step = iio_buffer_step(buffer_.get());
  size_t sample_size = GetSampleSize().value_or(0);

  // There is something wrong when refilling the buffer.
  if (buf_step != sample_size) {
    LOG(ERROR) << "sample_size doesn't match in refill: " << buf_step
               << ", sample_size: " << sample_size;
    buffer_.reset();

    return false;
  }

  uint8_t* start = reinterpret_cast<uint8_t*>(iio_buffer_start(buffer_.get()));
  size_t len = reinterpret_cast<intptr_t>(iio_buffer_end(buffer_.get())) -
               reinterpret_cast<intptr_t>(start);

  event->reserve(len);
  event->insert(event->begin(), start, start + len);

  return true;
}

// static
void IioDeviceImpl::IioBufferDeleter(iio_buffer* buffer) {
  iio_buffer_cancel(buffer);
  iio_buffer_destroy(buffer);
}

bool IioDeviceImpl::CreateBuffer() {
  if (buffer_ &&
      iio_device_get_sample_size(device_) == iio_buffer_step(buffer_.get()))
    return true;

  buffer_.reset();
  buffer_.reset(iio_device_create_buffer(device_, kNumSamples, false));

  if (!buffer_) {
    char errMsg[kErrorBufferSize];
    iio_strerror(errno, errMsg, sizeof(errMsg));
    LOG(ERROR) << "Unable to allocate buffer: " << errMsg;
    return false;
  }

  return true;
}

}  // namespace libmems
