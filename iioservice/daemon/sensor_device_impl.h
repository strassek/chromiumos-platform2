// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_SENSOR_DEVICE_IMPL_H_
#define IIOSERVICE_DAEMON_SENSOR_DEVICE_IMPL_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/memory/weak_ptr.h>
#include <base/sequenced_task_runner.h>
#include <base/threading/thread.h>
#include <mojo/public/cpp/bindings/receiver_set.h>
#include <mojo/public/cpp/bindings/remote.h>

#include <libmems/iio_context.h>
#include <libmems/iio_device.h>

#include "iioservice/daemon/common_types.h"
#include "iioservice/daemon/samples_handler.h"
#include "mojo/sensor.mojom.h"

namespace iioservice {

class SensorDeviceImpl final : public cros::mojom::SensorDevice {
 public:
  static void SensorDeviceImplDeleter(SensorDeviceImpl* device);
  using ScopedSensorDeviceImpl =
      std::unique_ptr<SensorDeviceImpl, decltype(&SensorDeviceImplDeleter)>;

  static ScopedSensorDeviceImpl Create(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      libmems::IioContext* context);

  ~SensorDeviceImpl();

  void AddReceiver(int32_t iio_device_id,
                   mojo::PendingReceiver<cros::mojom::SensorDevice> request);

  void SetTimeout(uint32_t timeout) override;

  // using GetAttributeCallback = base::OnceCallback<void(const
  // base::Optional<std::string>&);
  void GetAttribute(const std::string& attr_name,
                    GetAttributeCallback callback) override;

  // using SetFrequencyCallback = base::OnceCallback<void(double)>;
  void SetFrequency(double frequency, SetFrequencyCallback callback) override;

  void StartReadingSamples(
      mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> observer)
      override;

  void StopReadingSamples() override;

  // using GetAllChannelIdsCallback =
  //     base::OnceCallback<void(const std::vector<std::string>&)>;
  void GetAllChannelIds(GetAllChannelIdsCallback callback) override;

  // using SetChannelsEnabledCallback = base::OnceCallback<void(const
  // std::vector<int32_t>&)>;
  void SetChannelsEnabled(const std::vector<int32_t>& iio_chn_indices,
                          bool en,
                          SetChannelsEnabledCallback callback) override;

  // using GetChannelsEnabledCallback = base::OnceCallback<void(const
  // std::vector<bool>&)>;
  void GetChannelsEnabled(const std::vector<int32_t>& iio_chn_indices,
                          GetChannelsEnabledCallback callback) override;

  // using GetChannelsAttributesCallback = base::OnceCallback<void(const
  // std::vector<base::Optional<std::string>>&)>;
  void GetChannelsAttributes(const std::vector<int32_t>& iio_chn_indices,
                             const std::string& attr_name,
                             GetChannelsAttributesCallback callback) override;

 private:
  SensorDeviceImpl(scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
                   libmems::IioContext* context,
                   std::unique_ptr<base::Thread> thread,
                   bool use_fifo);

  void AddReceiverOnThread(
      int32_t iio_device_id,
      mojo::PendingReceiver<cros::mojom::SensorDevice> request);

  void ConnectionErrorCallback();
  void RemoveClient(mojo::ReceiverId id);

  bool AddSamplesHandlerIfNotSet(libmems::IioDevice* iio_device);

  void OnSampleUpdatedCallback(mojo::ReceiverId id,
                               libmems::IioDevice::IioSample sample);
  void OnErrorOccurredCallback(mojo::ReceiverId id,
                               cros::mojom::ObserverErrorType type);

  scoped_refptr<base::SequencedTaskRunner> ipc_task_runner_;
  libmems::IioContext* context_;  // non-owned
  mojo::ReceiverSet<cros::mojom::SensorDevice> receiver_set_;
  std::unique_ptr<base::Thread> sample_thread_;
  bool use_fifo_ = true;

  std::map<mojo::ReceiverId, ClientData> clients_;

  std::map<libmems::IioDevice*, SamplesHandler::ScopedSamplesHandler>
      samples_handlers_;

  base::WeakPtrFactory<SensorDeviceImpl> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_SENSOR_DEVICE_IMPL_H_
