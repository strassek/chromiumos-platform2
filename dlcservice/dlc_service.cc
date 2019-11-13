// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dlc_service.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/message_loop/message_loop.h>
#include <brillo/errors/error.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <update_engine/dbus-constants.h>

#include "dlcservice/boot_slot.h"
#include "dlcservice/utils.h"

using base::Callback;
using base::File;
using base::FilePath;
using base::ScopedTempDir;
using std::pair;
using std::string;
using std::unique_ptr;
using std::vector;
using update_engine::Operation;
using update_engine::StatusResult;

namespace dlcservice {

namespace {

// Permissions for DLC module directories.
constexpr mode_t kDlcModuleDirectoryPerms = 0755;

// Creates a directory with permissions required for DLC modules.
bool CreateDirWithDlcPermissions(const FilePath& path) {
  File::Error file_err;
  if (!base::CreateDirectoryAndGetError(path, &file_err)) {
    LOG(ERROR) << "Failed to create directory '" << path.value()
               << "': " << File::ErrorToString(file_err);
    return false;
  }
  if (!base::SetPosixFilePermissions(path, kDlcModuleDirectoryPerms)) {
    LOG(ERROR) << "Failed to set directory permissions for '" << path.value()
               << "'";
    return false;
  }
  return true;
}

// Creates a directory with an empty image file and resizes it to the given
// size.
bool CreateImageFile(const FilePath& path, int64_t image_size) {
  if (!CreateDirWithDlcPermissions(path.DirName())) {
    return false;
  }
  constexpr uint32_t file_flags =
      File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE;
  File file(path, file_flags);
  if (!file.IsValid()) {
    LOG(ERROR) << "Failed to create image file '" << path.value()
               << "': " << File::ErrorToString(file.error_details());
    return false;
  }
  if (!file.SetLength(image_size)) {
    LOG(ERROR) << "Failed to reserve backup file for DLC module image '"
               << path.value() << "'";
    return false;
  }
  return true;
}

// Sets the D-Bus error object with error code and error message after which
// logs the error message will also be logged.
void LogAndSetError(brillo::ErrorPtr* err,
                    const string& code,
                    const string& msg) {
  if (err)
    *err = brillo::Error::Create(FROM_HERE, brillo::errors::dbus::kDomain, code,
                                 msg);
  LOG(ERROR) << msg;
}

}  // namespace

DlcService::DlcService(
    unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
        image_loader_proxy,
    unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
        update_engine_proxy,
    unique_ptr<BootSlot> boot_slot,
    const FilePath& manifest_dir,
    const FilePath& content_dir)
    : image_loader_proxy_(std::move(image_loader_proxy)),
      update_engine_proxy_(std::move(update_engine_proxy)),
      boot_slot_(std::move(boot_slot)),
      manifest_dir_(manifest_dir),
      content_dir_(content_dir),
      weak_ptr_factory_(this) {
  // Get current boot slot.
  string boot_disk_name;
  int num_slots = -1;
  int current_boot_slot = -1;
  if (!boot_slot_->GetCurrentSlot(&boot_disk_name, &num_slots,
                                  &current_boot_slot))
    LOG(FATAL) << "Can not get current boot slot.";

  current_boot_slot_name_ = current_boot_slot == 0 ? imageloader::kSlotNameA
                                                   : imageloader::kSlotNameB;

  // Register D-Bus signal callbacks.
  update_engine_proxy_->RegisterStatusUpdateAdvancedSignalHandler(
      base::Bind(&DlcService::OnStatusUpdateAdvancedSignal,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&DlcService::OnStatusUpdateAdvancedSignalConnected,
                 weak_ptr_factory_.GetWeakPtr()));

  // Initalize installed DLC modules.
  for (auto installed_dlc_id : utils::ScanDirectory(content_dir_))
    installed_dlc_modules_[installed_dlc_id];

  // Initialize supported DLC modules.
  supported_dlc_modules_ = utils::ScanDirectory(manifest_dir_);
}

void DlcService::LoadDlcModuleImages() {
  // Load all installed DLC modules.
  for (auto installed_dlc_module_itr = installed_dlc_modules_.begin();
       installed_dlc_module_itr != installed_dlc_modules_.end();
       /* Don't increment here */) {
    const string& installed_dlc_module_id = installed_dlc_module_itr->first;
    string& installed_dlc_module_root = installed_dlc_module_itr->second;

    if (base::PathExists(FilePath(installed_dlc_module_root))) {
      ++installed_dlc_module_itr;
      continue;
    }

    string mount_point;
    if (!MountDlc(installed_dlc_module_id, &mount_point, nullptr)) {
      LOG(ERROR) << "Failed to mount DLC module during load: "
                 << installed_dlc_module_id;
      if (!DeleteDlc(installed_dlc_module_id, nullptr)) {
        LOG(ERROR) << "Failed to delete an unmountable DLC module: "
                   << installed_dlc_module_id;
      }
      installed_dlc_modules_.erase(installed_dlc_module_itr++);
    } else {
      installed_dlc_module_root =
          utils::GetDlcRootInModulePath(FilePath(mount_point)).value();
      ++installed_dlc_module_itr;
    }
  }
}

bool DlcService::Install(const DlcModuleList& dlc_module_list_in,
                         brillo::ErrorPtr* err) {
  if (dlc_module_list_in.dlc_module_infos().empty()) {
    LogAndSetError(err, kErrorInvalidDlc,
                   "Must provide at least one DLC to install");
    return false;
  }

  // Pick up DLC(s) from |DlcModuleList| passed in which have no roots.
  DlcRootMap unique_dlcs = utils::ToDlcRootMap(
      dlc_module_list_in,
      [](DlcModuleInfo dlc) { return dlc.dlc_root().empty(); });

  // Check that no duplicate DLC(s) were passed in.
  if (unique_dlcs.size() != dlc_module_list_in.dlc_module_infos_size()) {
    // Note: nice to have for log which was duplicate, but not necessary ATM.
    LogAndSetError(err, kErrorInvalidDlc,
                   "Must not pass in duplicate DLC(s) to install");
    return false;
  }

  LoadDlcModuleImages();

  // Go through already installed DLC(s) and set the roots if found.
  for (const auto& unique_dlc : unique_dlcs) {
    const string& id = unique_dlc.first;
    if (installed_dlc_modules_.find(id) != installed_dlc_modules_.end())
      unique_dlcs[id] = installed_dlc_modules_[id];
  }

  // This is the entire unique DLC(s) asked to be installed.
  DlcModuleList unique_dlc_module_list =
      utils::ToDlcModuleList(unique_dlcs, [](DlcId, DlcRoot) { return true; });
  // This is the unique DLC(s) that actually need to be installed.
  DlcModuleList unique_dlc_module_list_to_install = utils::ToDlcModuleList(
      unique_dlcs, [](DlcId, DlcRoot root) { return root.empty(); });
  // Copy over the Omaha URL.
  unique_dlc_module_list_to_install.set_omaha_url(
      dlc_module_list_in.omaha_url());

  // Check if there is nothing to install.
  if (unique_dlc_module_list_to_install.dlc_module_infos_size() == 0) {
    InstallStatus install_status = utils::CreateInstallStatus(
        Status::COMPLETED, kErrorNone, unique_dlc_module_list, 1.);
    SendOnInstallStatusSignal(install_status);
    return true;
  }
  Operation update_engine_operation;
  if (!GetUpdateEngineStatus(&update_engine_operation)) {
    LogAndSetError(err, kErrorInternal,
                   "Failed to get the status of Update Engine.");
    return false;
  }
  switch (update_engine_operation) {
    case update_engine::UPDATED_NEED_REBOOT:
      LogAndSetError(err, kErrorNeedReboot,
                     "Update Engine applied update, device needs a reboot.");
      return false;
    case update_engine::IDLE:
      break;
    default:
      LogAndSetError(err, kErrorBusy,
                     "Update Engine is performing operations.");
      return false;
  }

  // Note: this holds the list of directories that were created and need to be
  // freed in case an error happens.
  vector<unique_ptr<ScopedTempDir>> scoped_paths;

  for (const DlcModuleInfo& dlc_module :
       unique_dlc_module_list_to_install.dlc_module_infos()) {
    FilePath path;
    const string& id = dlc_module.dlc_id();
    auto scoped_path = std::make_unique<ScopedTempDir>();

    if (!CreateDlc(id, &path, err))
      return false;

    if (!scoped_path->Set(path)) {
      LOG(ERROR) << "Failed when scoping path during install: " << path.value();
      return false;
    }

    scoped_paths.emplace_back(std::move(scoped_path));
  }

  // Invokes update_engine to install the DLC module.
  if (!update_engine_proxy_->AttemptInstall(unique_dlc_module_list_to_install,
                                            nullptr)) {
    // TODO(kimjae): need update engine to propagate correct error message by
    // passing in |ErrorPtr| and being set within update engine, current default
    // is to indicate that update engine is updating because there is no way an
    // install should have taken place if not through dlcservice. (could also be
    // the case that an update applied between the time of the last status check
    // above, but just return |kErrorBusy| because the next time around if an
    // update has been applied and is in a reboot needed state, it will indicate
    // correctly then).
    LogAndSetError(err, kErrorBusy,
                   "Update Engine failed to schedule install operations.");
    return false;
  }

  // This means update_engine was restarted/crashed during install and
  // dlcservice requires cleanup of DLC(s) that were previously being thought to
  // have been being installed.
  if (!dlc_modules_being_installed_.dlc_module_infos().empty())
    SendFailedSignalAndCleanup();

  dlc_modules_being_installed_ = unique_dlc_module_list;
  // Note: Do NOT add to installed indication. Let
  // |OnStatusUpdateAdvancedSignal()| handle since hat's truly when the DLC(s)
  // are installed.

  // Safely take ownership of scoped paths for them not to be freed.
  for (const auto& scoped_path : scoped_paths)
    scoped_path->Take();

  return true;
}

bool DlcService::Uninstall(const string& id_in, brillo::ErrorPtr* err) {
  LoadDlcModuleImages();
  if (installed_dlc_modules_.find(id_in) == installed_dlc_modules_.end()) {
    LOG(INFO) << "Uninstalling DLC id that's not installed: " << id_in;
    return true;
  }

  Operation update_engine_operation;
  if (!GetUpdateEngineStatus(&update_engine_operation)) {
    LogAndSetError(err, kErrorInternal,
                   "Failed to get the status of Update Engine");
    return false;
  }
  switch (update_engine_operation) {
    case update_engine::IDLE:
    case update_engine::UPDATED_NEED_REBOOT:
      break;
    default:
      LogAndSetError(err, kErrorBusy, "Install or update is in progress.");
      return false;
  }

  // This means update_engine was restarted and requires cleanup of DlC(s) that
  // were previously being thought to have been being installed.
  if (!dlc_modules_being_installed_.dlc_module_infos().empty())
    SendFailedSignalAndCleanup();

  if (!UnmountDlc(id_in, err))
    return false;

  if (!DeleteDlc(id_in, err))
    return false;

  LOG(INFO) << "Uninstalling DLC id:" << id_in;
  installed_dlc_modules_.erase(id_in);
  return true;
}

bool DlcService::GetInstalled(DlcModuleList* dlc_module_list_out,
                              brillo::ErrorPtr* err) {
  LoadDlcModuleImages();
  *dlc_module_list_out = utils::ToDlcModuleList(
      installed_dlc_modules_, [](DlcId, DlcRoot) { return true; });
  return true;
}

void DlcService::SendFailedSignalAndCleanup() {
  SendOnInstallStatusSignal(
      utils::CreateInstallStatus(Status::FAILED, kErrorInternal, {}, 0.));
  for (const auto& dlc_module :
       dlc_modules_being_installed_.dlc_module_infos()) {
    const string& dlc_id = dlc_module.dlc_id();
    if (!DeleteDlc(dlc_id, nullptr))
      LOG(ERROR) << "Failed to delete DLC(" << dlc_id << ") during cleanup.";
  }
  dlc_modules_being_installed_.clear_dlc_module_infos();
}

bool DlcService::HandleStatusResult(const StatusResult& status_result) {
  // If we are not installing any DLC(s), no need to even handle status result.
  if (dlc_modules_being_installed_.dlc_module_infos().empty())
    return false;

  if (status_result.current_operation() == Operation::REPORTING_ERROR_EVENT) {
    LOG(ERROR) << "Signal from update_engine indicates reporting failure.";
    SendFailedSignalAndCleanup();
    return false;
  }

  // This situation is reached if update_engine crashes during an install and
  // dlcservice still believes that it is waiting for an install to complete.
  // TODO(kimjae): Need to handle checking preiodically if an install is started
  // and dlcservice hasn't gotten an install progress in a certain period of
  // time, try to explicity check update_engine's status and act accordingly.
  if (!status_result.is_install()) {
    int last_attempt_error;
    update_engine_proxy_->GetLastAttemptError(&last_attempt_error, nullptr);
    LOG(ERROR) << "Signal from update_engine indicates non-install, so install "
               << " failed and update_engine error code is: "
               << "(" << last_attempt_error << ")";
    SendFailedSignalAndCleanup();
    return false;
  }

  // When update_engine is still installing, we got a progress update here.
  if (status_result.current_operation() != Operation::IDLE) {
    SendOnInstallStatusSignal(utils::CreateInstallStatus(
        Status::RUNNING, kErrorNone, {}, status_result.progress()));
    return false;
  }

  LOG(INFO)
      << "Signal from update_engine, proceeding to complete installation.";
  return true;
}

bool DlcService::CreateDlc(const string& id,
                           FilePath* path,
                           brillo::ErrorPtr* err) {
  path->clear();
  if (supported_dlc_modules_.find(id) == supported_dlc_modules_.end()) {
    LogAndSetError(err, kErrorInvalidDlc,
                   "The DLC ID provided is not supported.");
    return false;
  }

  const string& package = ScanDlcModulePackage(id);
  FilePath module_path = utils::GetDlcModulePath(content_dir_, id);
  FilePath module_package_path =
      utils::GetDlcModulePackagePath(content_dir_, id, package);

  if (base::PathExists(module_path)) {
    LogAndSetError(err, kErrorInternal,
                   "The DLC module is installed or duplicate.");
    return false;
  }
  // Create the DLC ID directory with correct permissions.
  if (!CreateDirWithDlcPermissions(module_path)) {
    LogAndSetError(err, kErrorInternal, "Failed to create DLC ID directory");
    return false;
  }
  // Create the DLC package directory with correct permissions.
  if (!CreateDirWithDlcPermissions(module_package_path)) {
    LogAndSetError(err, kErrorInternal,
                   "Failed to create DLC ID package directory");
    return false;
  }

  // Creates DLC module storage.
  // TODO(xiaochu): Manifest currently returns a signed integer, which means
  // will likely fail for modules >= 2 GiB in size. https://crbug.com/904539
  imageloader::Manifest manifest;
  if (!dlcservice::utils::GetDlcManifest(manifest_dir_, id, package,
                                         &manifest)) {
    LogAndSetError(err, kErrorInternal, "Failed to get DLC module manifest.");
    return false;
  }
  int64_t image_size = manifest.preallocated_size();
  if (image_size <= 0) {
    LogAndSetError(err, kErrorInternal,
                   "Preallocated size in manifest is illegal.");
    return false;
  }

  // Creates image A.
  FilePath image_a_path =
      utils::GetDlcModuleImagePath(content_dir_, id, package, 0);
  if (!CreateImageFile(image_a_path, image_size)) {
    LogAndSetError(err, kErrorInternal,
                   "Failed to create slot A DLC image file");
    return false;
  }

  // Creates image B.
  FilePath image_b_path =
      utils::GetDlcModuleImagePath(content_dir_, id, package, 1);
  if (!CreateImageFile(image_b_path, image_size)) {
    LogAndSetError(err, kErrorInternal, "Failed to create slot B image file");
    return false;
  }

  *path = module_path;
  return true;
}

bool DlcService::DeleteDlc(const std::string& id, brillo::ErrorPtr* err) {
  FilePath dlc_module_path = utils::GetDlcModulePath(content_dir_, id);
  if (!DeleteFile(dlc_module_path, true)) {
    LogAndSetError(err, kErrorInternal,
                   "DLC image folder could not be deleted.");
    return false;
  }
  return true;
}

bool DlcService::MountDlc(const string& id,
                          string* mount_point,
                          brillo::ErrorPtr* err) {
  if (!image_loader_proxy_->LoadDlcImage(id, ScanDlcModulePackage(id),
                                         current_boot_slot_name_, mount_point,
                                         nullptr)) {
    LogAndSetError(err, kErrorInternal, "Imageloader is not available.");
    return false;
  }
  if (mount_point->empty()) {
    LogAndSetError(err, kErrorInternal, "Imageloader LoadDlcImage() failed.");
    return false;
  }
  return true;
}

bool DlcService::UnmountDlc(const string& id, brillo::ErrorPtr* err) {
  bool success = false;
  if (!image_loader_proxy_->UnloadDlcImage(id, ScanDlcModulePackage(id),
                                           &success, nullptr)) {
    LogAndSetError(err, kErrorInternal, "Imageloader is not available.");
    return false;
  }
  if (!success) {
    LogAndSetError(err, kErrorInternal, "Imageloader UnloadDlcImage failed.");
    return false;
  }
  return true;
}

string DlcService::ScanDlcModulePackage(const string& id) {
  return *(utils::ScanDirectory(manifest_dir_.Append(id)).begin());
}

bool DlcService::GetUpdateEngineStatus(Operation* operation) {
  StatusResult status_result;
  if (!update_engine_proxy_->GetStatusAdvanced(&status_result, nullptr)) {
    return false;
  }
  *operation = status_result.current_operation();
  return true;
}

void DlcService::AddObserver(DlcService::Observer* observer) {
  observers_.push_back(observer);
}

void DlcService::SendOnInstallStatusSignal(
    const InstallStatus& install_status) {
  for (const auto& observer : observers_) {
    observer->SendInstallStatus(install_status);
  }
}

void DlcService::OnStatusUpdateAdvancedSignal(
    const StatusResult& status_result) {
  if (!HandleStatusResult(status_result))
    return;

  // At this point, update_engine finished installation of the requested DLC(s).
  DlcModuleList dlc_module_list, dlc_module_list_post_mount;
  dlc_module_list.CopyFrom(dlc_modules_being_installed_);
  dlc_module_list_post_mount.CopyFrom(dlc_modules_being_installed_);
  dlc_modules_being_installed_.clear_dlc_module_infos();

  // Keep track of the cleanups for DLC images.
  utils::ScopedCleanups<Callback<void()>> scoped_cleanups;
  for (const DlcModuleInfo& dlc_module : dlc_module_list.dlc_module_infos()) {
    // Don't cleanup for already mounted.
    if (!dlc_module.dlc_root().empty())
      continue;
    const string& dlc_id = dlc_module.dlc_id();
    auto cleanup = base::Bind(
        [](Callback<bool()> unmounter, Callback<bool()> deleter) {
          unmounter.Run();
          deleter.Run();
        },
        base::Bind(&DlcService::UnmountDlc, base::Unretained(this), dlc_id,
                   nullptr),
        base::Bind(&DlcService::DeleteDlc, base::Unretained(this), dlc_id,
                   nullptr));
    scoped_cleanups.Insert(cleanup);
  }

  // Mount the installed DLC module images not already mounted.
  for (auto& dlc_module :
       *dlc_module_list_post_mount.mutable_dlc_module_infos()) {
    // Don't remount already mounted.
    if (!dlc_module.dlc_root().empty())
      continue;
    const string& dlc_module_id = dlc_module.dlc_id();
    string mount_point;
    if (!MountDlc(dlc_module_id, &mount_point, nullptr)) {
      InstallStatus install_status = utils::CreateInstallStatus(
          Status::FAILED, kErrorInternal, dlc_module_list, 0.);
      SendOnInstallStatusSignal(install_status);
      return;
    }
    dlc_module.set_dlc_root(
        utils::GetDlcRootInModulePath(FilePath(mount_point)).value());
  }

  // Don't unmount+delete the images+directories as all successfully installed.
  scoped_cleanups.Cancel();

  // Install was a success so keep track.
  for (const DlcModuleInfo& installed_dlc_module :
       dlc_module_list_post_mount.dlc_module_infos())
    installed_dlc_modules_.emplace(installed_dlc_module.dlc_id(),
                                   installed_dlc_module.dlc_root());

  InstallStatus install_status = utils::CreateInstallStatus(
      Status::COMPLETED, kErrorNone, dlc_module_list_post_mount, 1.);
  SendOnInstallStatusSignal(install_status);
}

void DlcService::OnStatusUpdateAdvancedSignalConnected(
    const string& interface_name, const string& signal_name, bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to update_engine's StatusUpdate signal.";
  }
}

}  // namespace dlcservice