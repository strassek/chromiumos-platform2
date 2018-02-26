// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Library to provide access to the Chrome OS master configuration

#ifndef CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_H_
#define CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_H_

#include "chromeos-config/libcros_config/cros_config_fdt.h"
#include "chromeos-config/libcros_config/cros_config_interface.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/values.h>
#include <brillo/brillo_export.h>

namespace base {
class CommandLine;
class FilePath;
}  // namespace base

namespace brillo {

struct SmbiosTable;
class BRILLO_EXPORT CrosConfig : public CrosConfigInterface {
 public:
  CrosConfig();
  ~CrosConfig() override;

  // Prepare the configuration system for for access to the configuration for
  // the model this is running on. This reads the configuration file into
  // memory.
  // @return true if OK, false on error.
  bool InitModel();

  // Alias for the above, since this is used by several clients.
  bool Init();

  // Prepare the configuration system for testing.
  // This reads in the given configuration file and selects the supplied
  // model name.
  // @filepath: Path to configuration .dtb file.
  // @name: Platform name as returned by 'mosys platform id'.
  // @sku_id: SKU ID as returned by 'mosys platform sku'.
  // @customization_id: VPD customization ID from 'mosys platform customization'
  // @return true if OK, false on error.
  bool InitForTest(const base::FilePath& filepath,
                   const std::string& name,
                   int sku_id,
                   const std::string& customization_id);

  // Internal function to obtain a property value and return a list of log
  // messages on failure. Public for tests.
  // @path: Path to locate. Must start with "/".
  // @prop: Property name to look up
  // @val_out: returns the string value found, if any
  // @log_msgs_out: returns a list of error messages if this function fails
  // @return true if found, false if not found
  bool GetString(const std::string& path,
                 const std::string& prop,
                 std::string* val_out,
                 std::vector<std::string>* log_msgs_out);

  // CrosConfigInterface:
  bool GetString(const std::string& path,
                 const std::string& prop,
                 std::string* val_out) override;

  // CrosConfigInterface:
  bool GetAbsPath(const std::string& path,
                  const std::string& prop,
                  std::string* val_out) override;

 private:
  // Init for a particular config file
  // This calls InitCommon() with the given file after reading the identity
  // information for this device.
  // @filepath: Path to configuration file to use
  bool InitForConfig(const base::FilePath& filepath);

  // Common init function for both production and test code.
  // @filepath: path to configuration .dtb file.
  // @smbios_file: File containing memory to scan (typically this is /dev/mem)
  // @vpd_file: File containing the customization_id from VPD. Typically this
  //     is '/sys/firmware/vpd/ro/customization_id'.
  bool InitCommon(const base::FilePath& filepath,
                  const base::FilePath& smbios_file,
                  const base::FilePath& vpd_file);

  // Runs a quick init check and prints an error to stderr if it fails.
  // @return true if OK, false on error.
  bool InitCheck() const;

  // Internal function to obtain a property value based on a node
  // This looks up a property for a path, relative to a given base node.
  // @base_node: base node for the search.
  // @path: Path to locate (relative to @base). Must start with "/".
  // @prop: Property name to look up
  // @val_out: returns the string value found, if any
  // @log_msgs_out: returns a list of error messages if this function fails
  // @return true if found, false if not found
  bool GetString(ConfigNode base_node,
                 const std::string& path,
                 const std::string& prop,
                 std::string* val_out,
                 std::vector<std::string>* log_msgs_out);

  bool inited_ = false;  // true if the class is ready for use (Init*ed)
  CrosConfigFdt cros_config_fdt_;

  DISALLOW_COPY_AND_ASSIGN(CrosConfig);
};

}  // namespace brillo

#endif  // CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_H_
