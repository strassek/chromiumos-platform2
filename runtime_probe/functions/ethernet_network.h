/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef RUNTIME_PROBE_FUNCTIONS_ETHERNET_NETWORK_H_
#define RUNTIME_PROBE_FUNCTIONS_ETHERNET_NETWORK_H_

#include <memory>
#include <string>

#include <base/optional.h>

#include "runtime_probe/function_templates/network.h"

namespace runtime_probe {

class EthernetNetworkFunction : public NetworkFunction {
 public:
  static constexpr auto function_name = "ethernet_network";
  std::string GetFunctionName() const override { return function_name; }

  static std::unique_ptr<ProbeFunction> FromDictionaryValue(
      const base::DictionaryValue& dict_value) {
    std::unique_ptr<EthernetNetworkFunction> instance{
        new EthernetNetworkFunction()};

    if (dict_value.size() != 0) {
      LOG(ERROR) << function_name << " dooes not take any arguement";
      return nullptr;
    }
    return instance;
  }

 protected:
  base::Optional<std::string> GetNetworkType() const override;

 private:
  static ProbeFunction::Register<EthernetNetworkFunction> register_;
};

/* Register the EthernetNetworkFunction */
REGISTER_PROBE_FUNCTION(EthernetNetworkFunction);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_ETHERNET_NETWORK_H_