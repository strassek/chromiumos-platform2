// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MOCK_MM1_SIM_PROXY_H_
#define SHILL_CELLULAR_MOCK_MM1_SIM_PROXY_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/cellular/mm1_sim_proxy_interface.h"

namespace shill {
namespace mm1 {

class MockSimProxy : public SimProxyInterface {
 public:
  MockSimProxy();
  ~MockSimProxy() override;

  MOCK_METHOD(void,
              SendPin,
              (const std::string&, Error*, const ResultCallback&, int),
              (override));
  MOCK_METHOD(void,
              SendPuk,
              (const std::string&,
               const std::string&,
               Error*,
               const ResultCallback&,
               int),
              (override));
  MOCK_METHOD(
      void,
      EnablePin,
      (const std::string&, const bool, Error*, const ResultCallback&, int),
      (override));
  MOCK_METHOD(void,
              ChangePin,
              (const std::string&,
               const std::string&,
               Error*,
               const ResultCallback&,
               int),
              (override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSimProxy);
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_CELLULAR_MOCK_MM1_SIM_PROXY_H_
