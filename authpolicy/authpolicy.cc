// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/authpolicy.h"

#include <utility>
#include <vector>

#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <dbus/authpolicy/dbus-constants.h>
#include <dbus/login_manager/dbus-constants.h>

#include "authpolicy/authpolicy_metrics.h"
#include "authpolicy/path_service.h"
#include "authpolicy/proto_bindings/active_directory_info.pb.h"
#include "authpolicy/samba_interface.h"
#include "bindings/device_management_backend.pb.h"

namespace em = enterprise_management;

using brillo::dbus_utils::DBusObject;
using brillo::dbus_utils::ExtractMethodCallResults;

namespace authpolicy {

const char kChromeUserPolicyType[] = "google/chromeos/user";
const char kChromeDevicePolicyType[] = "google/chromeos/device";

namespace {

void PrintError(const char* msg, ErrorType error) {
  if (error == ERROR_NONE)
    LOG(INFO) << msg << " succeeded";
  else
    LOG(INFO) << msg << " failed with code " << error;
}

const char* GetSessionManagerStoreMethod(bool is_user_policy) {
  return is_user_policy
             ? login_manager::kSessionManagerStoreUnsignedPolicyForUser
             : login_manager::kSessionManagerStoreUnsignedPolicy;
}

DBusCallType GetPolicyDBusCallType(bool is_user_policy) {
  return is_user_policy ? DBUS_CALL_REFRESH_USER_POLICY
                        : DBUS_CALL_REFRESH_DEVICE_POLICY;
}

// Serializes |proto| to the byte array |proto_blob|. Returns ERROR_NONE on
// success and ERROR_PARSE_FAILED otherwise.
template <typename ProtoType>
ErrorType SerializeProto(ProtoType proto, std::vector<uint8_t>* proto_blob) {
  std::string buffer;
  if (!proto.SerializeToString(&buffer)) {
    LOG(ERROR) << "Failed to serialize proto";
    return ERROR_PARSE_FAILED;
  }
  proto_blob->assign(buffer.begin(), buffer.end());
  return ERROR_NONE;
}

}  // namespace

// static
std::unique_ptr<DBusObject> AuthPolicy::GetDBusObject(
    brillo::dbus_utils::ExportedObjectManager* object_manager) {
  return std::make_unique<DBusObject>(
      object_manager, object_manager->GetBus(),
      org::chromium::AuthPolicyAdaptor::GetObjectPath());
}

AuthPolicy::AuthPolicy(AuthPolicyMetrics* metrics,
                       const PathService* path_service)
    : org::chromium::AuthPolicyAdaptor(this),
      metrics_(metrics),
      samba_(base::ThreadTaskRunnerHandle::Get(),
             metrics,
             path_service,
             base::Bind(&AuthPolicy::OnUserKerberosFilesChanged,
                        base::Unretained(this))),
      weak_ptr_factory_(this) {}

ErrorType AuthPolicy::Initialize(bool expect_config) {
  return samba_.Initialize(expect_config);
}

void AuthPolicy::RegisterAsync(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object,
    const AsyncEventSequencer::CompletionAction& completion_callback) {
  DCHECK(!dbus_object_);
  dbus_object_ = std::move(dbus_object);
  // Make sure the task runner passed to |samba_| in the constructor is actually
  // the D-Bus task runner. This guarantees that automatic TGT renewal won't
  // interfere with D-Bus calls. Note that |GetDBusTaskRunner()| returns a
  // TaskRunner, which is a base class of SingleThreadTaskRunner accepted by
  // |samba_|.
  CHECK_EQ(base::ThreadTaskRunnerHandle::Get(),
           dbus_object_->GetBus()->GetDBusTaskRunner());
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
  session_manager_proxy_ = dbus_object_->GetBus()->GetObjectProxy(
      login_manager::kSessionManagerServiceName,
      dbus::ObjectPath(login_manager::kSessionManagerServicePath));
  DCHECK(session_manager_proxy_);
}

void AuthPolicy::AuthenticateUser(const std::string& user_principal_name,
                                  const std::string& account_id,
                                  const dbus::FileDescriptor& password_fd,
                                  int32_t* int_error,
                                  std::vector<uint8_t>* account_info_blob) {
  LOG(INFO) << "Received 'AuthenticateUser' request";
  ScopedTimerReporter timer(TIMER_AUTHENTICATE_USER);

  authpolicy::ActiveDirectoryAccountInfo account_info;
  ErrorType error = samba_.AuthenticateUser(user_principal_name, account_id,
                                            password_fd.value(), &account_info);
  if (error == ERROR_NONE)
    error = SerializeProto(account_info, account_info_blob);
  PrintError("AuthenticateUser", error);
  metrics_->ReportDBusResult(DBUS_CALL_AUTHENTICATE_USER, error);
  *int_error = static_cast<int>(error);
}

void AuthPolicy::GetUserStatus(const std::string& account_id,
                               int32_t* int_error,
                               std::vector<uint8_t>* user_status_blob) {
  LOG(INFO) << "Received 'GetUserStatus' request";
  ScopedTimerReporter timer(TIMER_GET_USER_STATUS);

  authpolicy::ActiveDirectoryUserStatus user_status;
  ErrorType error = samba_.GetUserStatus(account_id, &user_status);
  if (error == ERROR_NONE)
    error = SerializeProto(user_status, user_status_blob);
  PrintError("GetUserStatus", error);
  metrics_->ReportDBusResult(DBUS_CALL_GET_USER_STATUS, error);
  *int_error = static_cast<int>(error);
}

void AuthPolicy::GetUserKerberosFiles(
    const std::string& account_id,
    int32_t* int_error,
    std::vector<uint8_t>* kerberos_files_blob) {
  LOG(INFO) << "Received 'GetUserKerberosFiles' request";
  ScopedTimerReporter timer(TIMER_GET_USER_KERBEROS_FILES);

  authpolicy::KerberosFiles kerberos_files;
  ErrorType error = samba_.GetUserKerberosFiles(account_id, &kerberos_files);
  if (error == ERROR_NONE)
    error = SerializeProto(kerberos_files, kerberos_files_blob);
  PrintError("GetUserKerberosFiles", error);
  metrics_->ReportDBusResult(DBUS_CALL_GET_USER_KERBEROS_FILES, error);
  *int_error = static_cast<int>(error);
}

int32_t AuthPolicy::JoinADDomain(const std::string& machine_name,
                                 const std::string& user_principal_name,
                                 const dbus::FileDescriptor& password_fd) {
  LOG(INFO) << "Received 'JoinADDomain' request";
  ScopedTimerReporter timer(TIMER_JOIN_AD_DOMAIN);

  ErrorType error = samba_.JoinMachine(machine_name, user_principal_name,
                                       password_fd.value());
  PrintError("JoinADDomain", error);
  metrics_->ReportDBusResult(DBUS_CALL_JOIN_AD_DOMAIN, error);
  return error;
}

void AuthPolicy::RefreshUserPolicy(PolicyResponseCallback callback,
                                   const std::string& account_id_key) {
  LOG(INFO) << "Received 'RefreshUserPolicy' request";
  auto timer = std::make_unique<ScopedTimerReporter>(TIMER_REFRESH_USER_POLICY);

  // Fetch GPOs for the current user.
  protos::GpoPolicyData gpo_policy_data;
  ErrorType error = samba_.FetchUserGpos(account_id_key, &gpo_policy_data);
  PrintError("User policy fetch and parsing", error);

  // Return immediately on error.
  if (error != ERROR_NONE) {
    metrics_->ReportDBusResult(DBUS_CALL_REFRESH_USER_POLICY, error);
    callback->Return(error);
    return;
  }

  // Send policy to Session Manager.
  StorePolicy(gpo_policy_data, &account_id_key, std::move(timer),
              std::move(callback));
}

void AuthPolicy::RefreshDevicePolicy(PolicyResponseCallback callback) {
  LOG(INFO) << "Received 'RefreshDevicePolicy' request";
  auto timer =
      std::make_unique<ScopedTimerReporter>(TIMER_REFRESH_DEVICE_POLICY);

  // Fetch GPOs for the device.
  protos::GpoPolicyData gpo_policy_data;
  ErrorType error = samba_.FetchDeviceGpos(&gpo_policy_data);
  PrintError("Device policy fetch and parsing", error);

  // Return immediately on error.
  if (error != ERROR_NONE) {
    metrics_->ReportDBusResult(DBUS_CALL_REFRESH_DEVICE_POLICY, error);
    callback->Return(error);
    return;
  }

  // Send policy to Session Manager.
  StorePolicy(gpo_policy_data, nullptr, std::move(timer), std::move(callback));
}

std::string AuthPolicy::SetDefaultLogLevel(int32_t level) {
  LOG(INFO) << "Received 'SetDefaultLogLevel' request";
  if (level < AuthPolicyFlags::kMinLevel ||
      level > AuthPolicyFlags::kMaxLevel) {
    std::string message = base::StringPrintf("Level must be between %i and %i.",
                                             AuthPolicyFlags::kMinLevel,
                                             AuthPolicyFlags::kMaxLevel);
    LOG(ERROR) << message;
    return message;
  }
  samba_.SetDefaultLogLevel(static_cast<AuthPolicyFlags::DefaultLevel>(level));
  return std::string();
}

void AuthPolicy::OnUserKerberosFilesChanged() {
  LOG(INFO) << "Firing signal UserKerberosFilesChanged";
  SendUserKerberosFilesChangedSignal();
}

void AuthPolicy::StorePolicy(const protos::GpoPolicyData& gpo_policy_data,
                             const std::string* account_id_key,
                             std::unique_ptr<ScopedTimerReporter> timer,
                             PolicyResponseCallback callback) {
  // Note: Only policy_value required here, the other data only impacts
  // signature, but since we don't sign, we don't need it.
  const bool is_user_policy = account_id_key != nullptr;
  const char* const policy_type =
      is_user_policy ? kChromeUserPolicyType : kChromeDevicePolicyType;

  em::PolicyData em_policy_data;
  em_policy_data.set_policy_value(gpo_policy_data.user_or_device_policy());
  em_policy_data.set_policy_type(policy_type);
  if (is_user_policy) {
    em_policy_data.set_username(samba_.user_sam_account_name());
    // Device id in the proto also could be used as an account/client id.
    em_policy_data.set_device_id(samba_.user_account_id());
  } else {
    em_policy_data.set_device_id(samba_.machine_name());
  }
  em_policy_data.set_management_mode(em::PolicyData::ENTERPRISE_MANAGED);
  // Note: No signature required here, Active Directory policy is unsigned!

  em::PolicyFetchResponse policy_response;
  std::string response_blob;
  if (!em_policy_data.SerializeToString(
          policy_response.mutable_policy_data()) ||
      !policy_response.SerializeToString(&response_blob)) {
    LOG(ERROR) << "Failed to serialize policy data";
    const DBusCallType call_type = GetPolicyDBusCallType(is_user_policy);
    metrics_->ReportDBusResult(call_type, ERROR_STORE_POLICY_FAILED);
    callback->Return(ERROR_STORE_POLICY_FAILED);
    return;
  }

  const char* const method = GetSessionManagerStoreMethod(is_user_policy);
  dbus::MethodCall method_call(login_manager::kSessionManagerInterface, method);
  dbus::MessageWriter writer(&method_call);
  if (account_id_key)
    writer.AppendString(*account_id_key);
  writer.AppendArrayOfBytes(
      reinterpret_cast<const uint8_t*>(response_blob.data()),
      response_blob.size());
  session_manager_proxy_->CallMethod(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::Bind(&AuthPolicy::OnPolicyStored, weak_ptr_factory_.GetWeakPtr(),
                 is_user_policy, base::Passed(&timer),
                 base::Passed(&callback)));
}

void AuthPolicy::OnPolicyStored(
    bool is_user_policy,
    std::unique_ptr<ScopedTimerReporter> /* timer */,
    PolicyResponseCallback callback,
    dbus::Response* response) {
  const char* const method = GetSessionManagerStoreMethod(is_user_policy);
  brillo::ErrorPtr brillo_error;
  std::string msg;
  if (!response) {
    // In case of error, session_manager_proxy_ prints out the error string and
    // response is empty.
    msg =
        base::StringPrintf("Call to %s failed. No response or error.", method);
  } else if (!ExtractMethodCallResults(response, &brillo_error)) {
    // Response is expected have no call results.
    msg = base::StringPrintf(
        "Call to %s failed. %s", method,
        brillo_error ? brillo_error->GetMessage().c_str() : "Unknown error.");
  }

  ErrorType error;
  if (!msg.empty()) {
    LOG(ERROR) << msg;
    error = ERROR_STORE_POLICY_FAILED;
  } else {
    LOG(INFO) << "Call to " << method << " succeeded.";
    error = ERROR_NONE;
  }
  const DBusCallType call_type = GetPolicyDBusCallType(is_user_policy);
  metrics_->ReportDBusResult(call_type, error);
  callback->Return(error);
}

}  // namespace authpolicy
