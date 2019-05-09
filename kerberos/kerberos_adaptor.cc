// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "kerberos/kerberos_adaptor.h"

#include <string>
#include <utility>

#include <base/compiler_specific.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <brillo/dbus/dbus_object.h>

#include "kerberos/error_strings.h"
#include "kerberos/platform_helper.h"
#include "kerberos/proto_bindings/kerberos_service.pb.h"

namespace kerberos {

namespace {

using ByteArray = KerberosAdaptor::ByteArray;

// Serializes |proto| to a vector of bytes. CHECKs for success (should
// never fail if there are no required proto fields).
ByteArray SerializeProto(const google::protobuf::MessageLite& proto) {
  ByteArray proto_blob(proto.ByteSizeLong());
  CHECK(proto.SerializeToArray(proto_blob.data(), proto_blob.size()));
  return proto_blob;
}

// Parses a proto from an array of bytes |proto_blob|. Returns
// ERROR_PARSE_REQUEST_FAILED on error.
WARN_UNUSED_RESULT ErrorType ParseProto(google::protobuf::MessageLite* proto,
                                        const ByteArray& proto_blob) {
  if (!proto->ParseFromArray(proto_blob.data(), proto_blob.size())) {
    LOG(ERROR) << "Failed to parse proto";
    return ERROR_PARSE_REQUEST_FAILED;
  }
  return ERROR_NONE;
}

void PrintRequest(const char* method_name) {
  LOG(INFO) << ">>> " << method_name;
}

void PrintResult(const char* method_name, ErrorType error) {
  if (error == ERROR_NONE)
    LOG(INFO) << "<<< " << method_name << " succeeded";
  else
    LOG(ERROR) << "<<< " << method_name << " failed: " << GetErrorString(error);
}

}  // namespace

KerberosAdaptor::KerberosAdaptor(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
    : org::chromium::KerberosAdaptor(this),
      dbus_object_(std::move(dbus_object)),
      // TODO(https://crbug.com/951718): Figure out user hash and set storage
      // dir to daemon store folder.
      manager_(base::FilePath("/tmp/kerberosd"),
               base::BindRepeating(&KerberosAdaptor::OnKerberosFilesChanged,
                                   base::Unretained(this))) {
  base::CreateDirectory(base::FilePath("/tmp/kerberosd"));
  manager_.LoadAccounts();
}

KerberosAdaptor::~KerberosAdaptor() = default;

void KerberosAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
        completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
}

ByteArray KerberosAdaptor::AddAccount(const ByteArray& request_blob) {
  PrintRequest(__FUNCTION__);
  AddAccountRequest request;
  ErrorType error = ParseProto(&request, request_blob);

  if (error == ERROR_NONE)
    error = manager_.AddAccount(request.principal_name());

  PrintResult(__FUNCTION__, error);
  AddAccountResponse response;
  response.set_error(error);
  return SerializeProto(response);
}

ByteArray KerberosAdaptor::RemoveAccount(const ByteArray& request_blob) {
  PrintRequest(__FUNCTION__);
  RemoveAccountRequest request;
  ErrorType error = ParseProto(&request, request_blob);

  if (error == ERROR_NONE)
    error = manager_.RemoveAccount(request.principal_name());

  PrintResult(__FUNCTION__, error);
  RemoveAccountResponse response;
  response.set_error(error);
  return SerializeProto(response);
}

ByteArray KerberosAdaptor::ListAccounts(const ByteArray& request_blob) {
  PrintRequest(__FUNCTION__);
  ListAccountsRequest request;
  ErrorType error = ParseProto(&request, request_blob);

  // Note: request is empty right now, but keeping it for future changes.
  std::vector<Account> accounts;
  if (error == ERROR_NONE)
    error = manager_.ListAccounts(&accounts);

  PrintResult(__FUNCTION__, error);
  ListAccountsResponse response;
  response.set_error(error);
  for (const auto& account : accounts)
    *response.add_accounts() = account;
  return SerializeProto(response);
}

ByteArray KerberosAdaptor::SetConfig(const ByteArray& request_blob) {
  PrintRequest(__FUNCTION__);
  SetConfigRequest request;
  ErrorType error = ParseProto(&request, request_blob);

  if (error == ERROR_NONE)
    error = manager_.SetConfig(request.principal_name(), request.krb5conf());

  PrintResult(__FUNCTION__, error);
  SetConfigResponse response;
  response.set_error(error);
  return SerializeProto(response);
}

ByteArray KerberosAdaptor::AcquireKerberosTgt(
    const ByteArray& request_blob, const base::ScopedFD& password_fd) {
  PrintRequest(__FUNCTION__);
  AcquireKerberosTgtRequest request;
  ErrorType error = ParseProto(&request, request_blob);

  base::Optional<std::string> password;
  if (error == ERROR_NONE) {
    password = ReadPipeToString(password_fd.get());
    if (!password.has_value()) {
      LOG(ERROR) << "Failed to read password pipe";
      error = ERROR_LOCAL_IO;
    }
  }

  if (error == ERROR_NONE)
    error = manager_.AcquireTgt(request.principal_name(), password.value());

  PrintResult(__FUNCTION__, error);
  AcquireKerberosTgtResponse response;
  response.set_error(error);
  return SerializeProto(response);
}

ByteArray KerberosAdaptor::GetKerberosFiles(const ByteArray& request_blob) {
  PrintRequest(__FUNCTION__);
  GetKerberosFilesRequest request;
  ErrorType error = ParseProto(&request, request_blob);

  GetKerberosFilesResponse response;
  if (error == ERROR_NONE) {
    error = manager_.GetKerberosFiles(request.principal_name(),
                                      response.mutable_files());
  }

  PrintResult(__FUNCTION__, error);
  response.set_error(error);
  return SerializeProto(response);
}

void KerberosAdaptor::OnKerberosFilesChanged(
    const std::string& principal_name) {
  LOG(INFO) << "Firing signal UserKerberosFilesChanged";
  SendKerberosFilesChangedSignal(principal_name);
}

}  // namespace kerberos
