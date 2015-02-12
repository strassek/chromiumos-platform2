// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webserver/webservd/config.h"

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/values.h>
#include <chromeos/errors/error_codes.h>

#include "webserver/webservd/error_codes.h"

namespace webservd {

namespace {

const char kProtocolHandlersKey[] = "protocol_handlers";
const char kPortKey[] = "port";
const char kUseTLSKey[] = "use_tls";

// Default configuration for the web server.
const char kDefaultConfig[] = R"({
  "protocol_handlers": {
    "http": {
      "port": 80,
      "use_tls": false
    },
    "https": {
      "port": 443,
      "use_tls": true
    }
  }
})";

bool LoadHandlerConfig(const base::DictionaryValue* handler_value,
                       Config::ProtocolHandler* handler_config,
                       chromeos::ErrorPtr* error) {
  int port = 0;
  if (!handler_value->GetInteger(kPortKey, &port)) {
    chromeos::Error::AddTo(error,
                           FROM_HERE,
                           webservd::errors::kDomain,
                           webservd::errors::kInvalidConfig,
                           "Port is missing");
    return false;
  }
  if (port < 1 || port > 0xFFFF) {
    chromeos::Error::AddToPrintf(error,
                                 FROM_HERE,
                                 webservd::errors::kDomain,
                                 webservd::errors::kInvalidConfig,
                                 "Invalid port value: %d", port);
    return false;
  }
  handler_config->port = port;

  // Allow "use_tls" to be omitted, so not returning an error here.
  bool use_tls = false;
  if (handler_value->GetBoolean(kUseTLSKey, &use_tls))
    handler_config->use_tls = use_tls;

  return true;
}

}  // anonymous namespace

void LoadDefaultConfig(Config* config) {
  LOG(INFO) << "Loading default server configuration...";
  CHECK(LoadConfigFromString(kDefaultConfig, config, nullptr));
}

bool LoadConfigFromFile(const base::FilePath& json_file_path, Config* config) {
  std::string config_json;
  LOG(INFO) << "Loading server configuration from " << json_file_path.value();
  return base::ReadFileToString(json_file_path, &config_json) &&
         LoadConfigFromString(config_json, config, nullptr);
}

bool LoadConfigFromString(const std::string& config_json,
                          Config* config,
                          chromeos::ErrorPtr* error) {
  std::string error_msg;
  std::unique_ptr<const base::Value> value{base::JSONReader::ReadAndReturnError(
      config_json, base::JSON_ALLOW_TRAILING_COMMAS, nullptr, &error_msg)};

  if (!value) {
    chromeos::Error::AddToPrintf(error, FROM_HERE,
                                 chromeos::errors::json::kDomain,
                                 chromeos::errors::json::kParseError,
                                 "Error parsing server configuration: %s",
                                 error_msg.c_str());
    return false;
  }

  const base::DictionaryValue* dict_value = nullptr;  // Owned by |value|
  if (!value->GetAsDictionary(&dict_value)) {
    chromeos::Error::AddTo(error,
                           FROM_HERE,
                           chromeos::errors::json::kDomain,
                           chromeos::errors::json::kObjectExpected,
                           "JSON object is expected.");
    return false;
  }

  const base::DictionaryValue* protocol_handlers = nullptr;  // Owned by |value|
  if (dict_value->GetDictionary(kProtocolHandlersKey, &protocol_handlers)) {
    base::DictionaryValue::Iterator iterator{*protocol_handlers};
    while (!iterator.IsAtEnd()) {
      const base::DictionaryValue* handler_value = nullptr;  // Owned by |value|
      if (!iterator.value().GetAsDictionary(&handler_value)) {
        chromeos::Error::AddToPrintf(
            error,
            FROM_HERE,
            chromeos::errors::json::kDomain,
            chromeos::errors::json::kObjectExpected,
            "Protocol handler definition for '%s' must be a JSON object",
            iterator.key().c_str());
        return false;
      }

      Config::ProtocolHandler handler_config;
      if (!LoadHandlerConfig(handler_value, &handler_config, error)) {
        chromeos::Error::AddToPrintf(
            error,
            FROM_HERE,
            errors::kDomain,
            errors::kInvalidConfig,
            "Unable to parse config for protocol handler '%s'",
            iterator.key().c_str());
        return false;
      }
      config->protocol_handlers.emplace(iterator.key(),
                                        std::move(handler_config));
      iterator.Advance();
    }
  }
  return true;
}

}  // namespace webservd
