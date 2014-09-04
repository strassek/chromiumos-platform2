// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCHROMEOS_CHROMEOS_HTTP_HTTP_TRANSPORT_H_
#define LIBCHROMEOS_CHROMEOS_HTTP_HTTP_TRANSPORT_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/macros.h>
#include <chromeos/chromeos_export.h>
#include <chromeos/errors/error.h>

namespace chromeos {
namespace http {

using HeaderList = std::vector<std::pair<std::string, std::string>>;

CHROMEOS_EXPORT extern const char kErrorDomain[];

class Request;
class Connection;

///////////////////////////////////////////////////////////////////////////////
// Transport is a base class for specific implementation of HTTP communication.
// This class (and its underlying implementation) is used by http::Request and
// http::Response classes to provide HTTP functionality to the clients.
///////////////////////////////////////////////////////////////////////////////
class CHROMEOS_EXPORT Transport {
 public:
  Transport() = default;
  virtual ~Transport() = default;

  // Creates a connection object and initializes it with the specified data.
  // |transport| is a shared pointer to this transport object instance,
  // used to maintain the object alive as long as the connection exists.
  virtual std::unique_ptr<Connection> CreateConnection(
      std::shared_ptr<Transport> transport,
      const std::string& url,
      const std::string& method,
      const HeaderList& headers,
      const std::string& user_agent,
      const std::string& referer,
      chromeos::ErrorPtr* error) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(Transport);
};

}  // namespace http
}  // namespace chromeos

#endif  // LIBCHROMEOS_CHROMEOS_HTTP_HTTP_TRANSPORT_H_
