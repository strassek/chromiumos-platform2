// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLUGIN_GOBI_2K_MODEM_HELPER_H_
#define PLUGIN_GOBI_2K_MODEM_HELPER_H_

#include "gobi_modem.h"

class Gobi2KModemHelper : public GobiModemHelper {
 public:
  Gobi2KModemHelper(gobi::Sdk* sdk) : GobiModemHelper(sdk) { };
  ~Gobi2KModemHelper() { };

  virtual void SetCarrier(GobiModem *modem,
                          GobiModemHandler *handler,
                          const std::string& carrier_name,
                          DBus::Error& error);
  DISALLOW_COPY_AND_ASSIGN(Gobi2KModemHelper);
};

#endif  // PLUGIN_GOBI_2K_MODEM_HELPER_H_
