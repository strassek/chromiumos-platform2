// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MOCK_CELLULAR_H_
#define SHILL_CELLULAR_MOCK_CELLULAR_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "shill/cellular/cellular.h"

namespace shill {

class MockCellular : public Cellular {
 public:
  MockCellular(ModemInfo* modem_info,
               const std::string& link_name,
               const std::string& address,
               int interface_index,
               Type type,
               const std::string& service,
               const RpcIdentifier& path);
  ~MockCellular() override;

  MOCK_METHOD(void, Connect, (Error*), (override));
  MOCK_METHOD(void, Disconnect, (Error*, const char*), (override));
  MOCK_METHOD(void,
              OnPropertiesChanged,
              (const std::string&,
               const KeyValueStore&,
               const std::vector<std::string>&),
              (override));
  MOCK_METHOD(void, DestroyService, (), (override));
  MOCK_METHOD(void, StartPPP, (const std::string&), (override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockCellular);
};

}  // namespace shill

#endif  // SHILL_CELLULAR_MOCK_CELLULAR_H_
