// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/command_line.h>
#include <base/files/file.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/optional.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/strings/string_split.h>
#include <base/synchronization/condition_variable.h>
#include <base/synchronization/lock.h>
#include <base/task/single_thread_task_executor.h>
#include <brillo/errors/error.h>
#include <brillo/flag_helper.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/dbus-proxies.h"
#include "lorgnette/guess_source.h"

using org::chromium::lorgnette::ManagerProxy;

namespace {

base::Optional<std::vector<std::string>> ReadLines(base::File* file) {
  std::string buf(1 << 20, '\0');
  int read = file->ReadAtCurrentPos(&buf[0], buf.size());
  if (read < 0) {
    PLOG(ERROR) << "Reading from file failed";
    return base::nullopt;
  }

  buf.resize(read);
  return base::SplitString(buf, "\n", base::KEEP_WHITESPACE,
                           base::SPLIT_WANT_ALL);
}

std::string EscapeScannerName(const std::string& scanner_name) {
  std::string escaped;
  for (char c : scanner_name) {
    if (isalnum(c)) {
      escaped += c;
    } else {
      escaped += '_';
    }
  }
  return escaped;
}

class ScanHandler {
 public:
  ScanHandler(base::RepeatingClosure quit_closure,
              std::unique_ptr<ManagerProxy> manager,
              std::string scanner_name)
      : cvar_(&lock_),
        quit_closure_(quit_closure),
        manager_(std::move(manager)),
        scanner_name_(scanner_name),
        base_output_path_("/tmp/scan-" + EscapeScannerName(scanner_name) +
                          ".png"),
        current_page_(1),
        connected_callback_called_(false),
        connection_status_(false) {
    manager_->RegisterScanStatusChangedSignalHandler(
        base::BindRepeating(&ScanHandler::HandleScanStatusChangedSignal,
                            weak_factory_.GetWeakPtr()),
        base::BindOnce(&ScanHandler::OnConnectedCallback,
                       weak_factory_.GetWeakPtr()));
  }

  bool WaitUntilConnected();

  bool StartScan(uint32_t resolution,
                 const lorgnette::DocumentSource& scan_source);

 private:
  void HandleScanStatusChangedSignal(
      const std::vector<uint8_t>& signal_serialized);

  void OnConnectedCallback(const std::string& interface_name,
                           const std::string& signal_name,
                           bool signal_connected);

  void RequestNextPage();
  base::Optional<lorgnette::GetNextImageResponse> GetNextImage(
      const base::FilePath& output_path);

  base::Lock lock_;
  base::ConditionVariable cvar_;
  base::RepeatingClosure quit_closure_;

  std::unique_ptr<ManagerProxy> manager_;
  std::string scanner_name_;
  base::FilePath base_output_path_;
  base::Optional<std::string> scan_uuid_;
  int current_page_;

  bool connected_callback_called_;
  bool connection_status_;

  base::WeakPtrFactory<ScanHandler> weak_factory_{this};
};

bool ScanHandler::WaitUntilConnected() {
  base::AutoLock auto_lock(lock_);
  while (!connected_callback_called_) {
    cvar_.Wait();
  }
  return connection_status_;
}

bool ScanHandler::StartScan(uint32_t resolution,
                            const lorgnette::DocumentSource& scan_source) {
  lorgnette::StartScanRequest request;
  request.set_device_name(scanner_name_);
  request.mutable_settings()->set_resolution(resolution);
  *request.mutable_settings()->mutable_source() = scan_source;
  request.mutable_settings()->set_color_mode(lorgnette::MODE_COLOR);
  std::vector<uint8_t> request_in(request.ByteSizeLong());
  request.SerializeToArray(request_in.data(), request_in.size());

  brillo::ErrorPtr error;
  std::vector<uint8_t> response_out;
  if (!manager_->StartScanMultiPage(request_in, &response_out, &error)) {
    LOG(ERROR) << "StartScan failed: " << error->GetMessage();
    return false;
  }

  lorgnette::StartScanResponse response;
  if (!response.ParseFromArray(response_out.data(), response_out.size())) {
    LOG(ERROR) << "Failed to parse StartScanResponse";
    return false;
  }

  if (response.state() == lorgnette::SCAN_STATE_FAILED) {
    LOG(ERROR) << "StartScan failed: " << response.failure_reason();
    return false;
  }

  std::cout << "Scan " << response.scan_uuid() << " started successfully"
            << std::endl;
  scan_uuid_ = response.scan_uuid();

  RequestNextPage();
  return true;
}

void ScanHandler::HandleScanStatusChangedSignal(
    const std::vector<uint8_t>& signal_serialized) {
  if (!scan_uuid_.has_value()) {
    return;
  }

  lorgnette::ScanStatusChangedSignal signal;
  if (!signal.ParseFromArray(signal_serialized.data(),
                             signal_serialized.size())) {
    LOG(ERROR) << "Failed to parse ScanStatusSignal";
    return;
  }

  if (signal.state() == lorgnette::SCAN_STATE_IN_PROGRESS) {
    std::cout << "Page " << signal.page() << " is " << signal.progress()
              << "% finished" << std::endl;
  } else if (signal.state() == lorgnette::SCAN_STATE_FAILED) {
    LOG(ERROR) << "Scan failed: " << signal.failure_reason();
    quit_closure_.Run();
  } else if (signal.state() == lorgnette::SCAN_STATE_PAGE_COMPLETED) {
    std::cout << "Page " << signal.page() << " completed." << std::endl;
    current_page_ += 1;
    if (signal.more_pages())
      RequestNextPage();
  } else if (signal.state() == lorgnette::SCAN_STATE_COMPLETED) {
    std::cout << "Scan completed successfully." << std::endl;
    quit_closure_.Run();
  }
}

void ScanHandler::OnConnectedCallback(const std::string& interface_name,
                                      const std::string& signal_name,
                                      bool signal_connected) {
  base::AutoLock auto_lock(lock_);
  connected_callback_called_ = true;
  connection_status_ = signal_connected;
  if (!signal_connected) {
    LOG(ERROR) << "Failed to connect to ScanStatusChanged signal";
  }
  cvar_.Signal();
}

base::Optional<lorgnette::GetNextImageResponse> ScanHandler::GetNextImage(
    const base::FilePath& output_path) {
  lorgnette::GetNextImageRequest request;
  request.set_scan_uuid(scan_uuid_.value());
  std::vector<uint8_t> request_in(request.ByteSizeLong());
  request.SerializeToArray(request_in.data(), request_in.size());

  base::File output_file(
      output_path, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);

  if (!output_file.IsValid()) {
    PLOG(ERROR) << "Failed to open output file " << output_path;
    return base::nullopt;
  }

  brillo::ErrorPtr error;
  std::vector<uint8_t> response_out;
  if (!manager_->GetNextImage(request_in, output_file.GetPlatformFile(),
                              &response_out, &error)) {
    LOG(ERROR) << "GetNextImage failed: " << error->GetMessage();
    return base::nullopt;
  }

  lorgnette::GetNextImageResponse response;
  if (!response.ParseFromArray(response_out.data(), response_out.size())) {
    LOG(ERROR) << "Failed to parse StartScanResponse";
    return base::nullopt;
  }

  return response;
}

void ScanHandler::RequestNextPage() {
  base::FilePath output_path = base_output_path_.InsertBeforeExtension(
      base::StringPrintf("_page%d", current_page_));

  base::Optional<lorgnette::GetNextImageResponse> response =
      GetNextImage(output_path);
  if (!response.has_value()) {
    quit_closure_.Run();
  }

  if (!response.value().success()) {
    LOG(ERROR) << "Requesting next page failed: "
               << response.value().failure_reason();
    quit_closure_.Run();
  } else {
    std::cout << "Reading page " << current_page_ << " to "
              << output_path.value() << std::endl;
  }
}

base::Optional<std::vector<std::string>> ListScanners(ManagerProxy* manager) {
  brillo::ErrorPtr error;
  std::vector<uint8_t> out_scanner_list;
  if (!manager->ListScanners(&out_scanner_list, &error)) {
    LOG(ERROR) << "ListScanners failed: " << error->GetMessage();
    return base::nullopt;
  }

  lorgnette::ListScannersResponse scanner_list;
  if (!scanner_list.ParseFromArray(out_scanner_list.data(),
                                   out_scanner_list.size())) {
    LOG(ERROR) << "Failed to parse ListScanners response";
    return base::nullopt;
  }

  std::vector<std::string> scanner_names;
  std::cout << "SANE scanners: " << std::endl;
  for (const lorgnette::ScannerInfo& scanner : scanner_list.scanners()) {
    std::cout << scanner.name() << ": " << scanner.manufacturer() << " "
              << scanner.model() << "(" << scanner.type() << ")" << std::endl;
    scanner_names.push_back(scanner.name());
  }
  std::cout << scanner_list.scanners_size() << " SANE scanners found."
            << std::endl;
  return scanner_names;
}

base::Optional<lorgnette::ScannerCapabilities> GetScannerCapabilities(
    ManagerProxy* manager, const std::string& scanner_name) {
  brillo::ErrorPtr error;
  std::vector<uint8_t> serialized;
  if (!manager->GetScannerCapabilities(scanner_name, &serialized, &error)) {
    LOG(ERROR) << "GetScannerCapabilities failed: " << error->GetMessage();
    return base::nullopt;
  }

  lorgnette::ScannerCapabilities capabilities;
  if (!capabilities.ParseFromArray(serialized.data(), serialized.size())) {
    LOG(ERROR) << "Failed to parse ScannerCapabilities response";
    return base::nullopt;
  }
  return capabilities;
}

void PrintScannerCapabilities(
    const lorgnette::ScannerCapabilities& capabilities) {
  std::cout << "--- Capabilities ---" << std::endl;
  std::cout << "Resolutions:" << std::endl;
  for (uint32_t resolution : capabilities.resolutions()) {
    std::cout << "\t" << resolution << std::endl;
  }

  std::cout << "Sources:" << std::endl;
  for (const lorgnette::DocumentSource& source : capabilities.sources()) {
    std::cout << "\t" << source.name() << " ("
              << lorgnette::SourceType_Name(source.type()) << ")" << std::endl;
  }

  std::cout << "Color Modes:" << std::endl;
  for (int color_mode : capabilities.color_modes()) {
    std::cout << "\t" << lorgnette::ColorMode_Name(color_mode) << std::endl;
  }
}

base::Optional<std::vector<std::string>> ReadAirscanOutput(
    brillo::ProcessImpl* discover) {
  base::File discover_output(discover->GetPipe(STDOUT_FILENO));
  if (!discover_output.IsValid()) {
    LOG(ERROR) << "Failed to open airscan-discover output pipe";
    return base::nullopt;
  }

  int ret = discover->Wait();
  if (ret != 0) {
    LOG(ERROR) << "airscan-discover exited with error " << ret;
    return base::nullopt;
  }

  base::Optional<std::vector<std::string>> lines = ReadLines(&discover_output);
  if (!lines.has_value()) {
    LOG(ERROR) << "Failed to read output from airscan-discover";
    return base::nullopt;
  }

  const std::string protocol = ", eSCL";
  std::vector<std::string> scanner_names;
  for (const std::string& line : lines.value()) {
    size_t equals = line.find('=');
    size_t suffix = line.find(protocol, equals);
    if (equals != std::string::npos && suffix != std::string::npos) {
      std::string name = line.substr(0, equals);
      base::TrimWhitespaceASCII(name, base::TrimPositions::TRIM_ALL, &name);

      std::string url = line.substr(equals + 1, suffix - (equals + 1));
      base::TrimWhitespaceASCII(url, base::TrimPositions::TRIM_ALL, &url);

      scanner_names.push_back("airscan:escl:" + name + ":" + url);
    }
  }

  return scanner_names;
}

}  // namespace

int main(int argc, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty |
                  brillo::kLogHeader);

  DEFINE_uint32(scan_resolution, 100,
                "The scan resolution to request from the scanner");
  DEFINE_string(scan_source, "Platen",
                "The scan source to use for the scanner, (e.g. Platen, ADF "
                "Simplex, ADF Duplex)");
  brillo::FlagHelper::Init(argc, argv,
                           "lorgnette_cli, command-line interface to "
                           "Chromium OS Scanning Daemon");

  if (base::CommandLine::ForCurrentProcess()->GetArgs().size() != 0) {
    LOG(ERROR) << "Unexpected command-line argument";
    return 1;
  }

  base::Optional<lorgnette::SourceType> source_type =
      GuessSourceType(FLAGS_scan_source);

  if (!source_type.has_value()) {
    LOG(ERROR) << "Unknown source type: \"" << FLAGS_scan_source
               << "\". Supported values are \"Platen\",\"ADF\", \"ADF Simplex\""
                  ", and \"ADF Duplex\"";
    return 1;
  }

  // Start the airscan-discover process immediately since it can be slightly
  // long-running. We read the output later after we've gotten a scanner list
  // from lorgnette.
  brillo::ProcessImpl discover;
  discover.AddArg("/usr/bin/airscan-discover");
  discover.RedirectUsingPipe(STDOUT_FILENO, false);
  if (!discover.Start()) {
    LOG(ERROR) << "Failed to start airscan-discover process";
    return 1;
  }

  // Create a task executor for this thread. This will automatically be bound
  // to the current thread so that it is usable by other code for posting tasks.
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);

  // Create a FileDescriptorWatcher instance for this thread. The libbase D-Bus
  // bindings use this internally via thread-local storage, but do not properly
  // instantiate it.
  base::FileDescriptorWatcher watcher(executor.task_runner());

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));
  auto manager =
      std::make_unique<ManagerProxy>(bus, lorgnette::kManagerServiceName);

  std::cout << "Getting scanner list." << std::endl;
  base::Optional<std::vector<std::string>> sane_scanners =
      ListScanners(manager.get());
  if (!sane_scanners.has_value())
    return 1;

  base::Optional<std::vector<std::string>> airscan_scanners =
      ReadAirscanOutput(&discover);
  if (!airscan_scanners.has_value())
    return 1;

  std::vector<std::string> scanners = std::move(sane_scanners.value());
  scanners.insert(scanners.end(), airscan_scanners.value().begin(),
                  airscan_scanners.value().end());

  std::cout << "Choose a scanner (blank to quit):" << std::endl;
  for (int i = 0; i < scanners.size(); i++) {
    std::cout << i << ". " << scanners[i] << std::endl;
  }
  int index = -1;
  std::cout << "> ";
  std::cin >> index;
  if (std::cin.fail()) {
    return 0;
  }

  std::string scanner = scanners[index];
  std::cout << "Getting device capabilities for " << scanner << std::endl;
  base::Optional<lorgnette::ScannerCapabilities> capabilities =
      GetScannerCapabilities(manager.get(), scanner);
  if (!capabilities.has_value())
    return 1;
  PrintScannerCapabilities(capabilities.value());

  if (!base::Contains(capabilities->resolutions(), FLAGS_scan_resolution)) {
    // Many scanners will round the requested resolution to the nearest
    // supported resolution. We will attempt to scan with the given resolution
    // since it may still work.
    LOG(WARNING) << "Requested scan resolution " << FLAGS_scan_resolution
                 << " is not supported by the selected scanner. "
                    "Attempting to request it anyways.";
  }

  base::Optional<lorgnette::DocumentSource> scan_source;
  for (const lorgnette::DocumentSource& source : capabilities->sources()) {
    if (source.type() == source_type.value()) {
      scan_source = source;
      break;
    }
  }

  if (!scan_source.has_value()) {
    LOG(ERROR) << "Requested scan source " << FLAGS_scan_source << " ("
               << lorgnette::SourceType_Name(source_type.value())
               << ") is not supported by the selected scanner";
    return 1;
  }

  // Implicitly uses this thread's executor as defined above.
  base::RunLoop run_loop;
  ScanHandler handler(run_loop.QuitClosure(), std::move(manager), scanner);

  if (!handler.WaitUntilConnected()) {
    return 1;
  }

  std::cout << "Scanning from " << scanner << std::endl;

  if (!handler.StartScan(FLAGS_scan_resolution, scan_source.value())) {
    return 1;
  }

  // Will run until the ScanHandler runs this RunLoop's quit_closure.
  run_loop.Run();

  return 0;
}
