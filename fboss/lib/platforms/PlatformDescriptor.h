/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <folly/CppAttributes.h>

#include "fboss/agent/gen-cpp2/platform_config_types.h"
#include "fboss/lib/platforms/gen-cpp2/platform_descriptor_types.h"

DECLARE_string(platform_descriptor_config_path);
DECLARE_bool(hwrev_state1_substate1_respin0);

namespace facebook::fboss {

class PlatformDescriptorRegistry {
 public:
  // Returns a cached singleton loaded from
  // FLAGS_platform_descriptor_config_path. Descriptor files are discovered
  // recursively once on first access.
  static const PlatformDescriptorRegistry& get();

  const PlatformDescriptor* FOLLY_NULLABLE
  getDescriptor(PlatformType type) const;
  std::optional<PlatformType> findPlatformType(
      std::string_view productName,
      std::string_view mode) const;
  std::optional<std::string> loadPlatformMapping(PlatformType type) const;
  // Returns the SDK yaml shipped beside the selected descriptor's
  // platform_mapping.json (asic_config_idx<switchIndex>.yaml if present,
  // else asic_config.yaml); nullopt when the directory carries no yaml.
  std::optional<std::string> loadAsicConfigYaml(
      PlatformType type,
      std::optional<int16_t> switchIndex = std::nullopt) const;
  cfg::PlatformMapping loadPlatformMappingFromRaw(
      PlatformType type,
      const cfg::PlatformConfig& platformConfig) const;

  static PlatformDescriptor loadPlatformDescriptorFromFile(
      const std::string& path);
  // Recursively loads platform_descriptor.json files and records each sibling
  // platform_mapping.json for lazy mapping load. systemVendor optionally
  // narrows the scan to one vendor subtree.
  static PlatformDescriptorRegistry loadPlatformDescriptorRegistryFromDirectory(
      const std::string& path,
      std::string_view systemVendor = "");

 private:
  struct PlatformDescriptorEntry {
    PlatformDescriptor descriptor;
    std::string platformMappingPath;
    std::string rawPlatformMappingPath;
  };

  explicit PlatformDescriptorRegistry(
      std::vector<PlatformDescriptorEntry> descriptorEntries);

  const PlatformDescriptorEntry* FOLLY_NULLABLE
  getDescriptorEntry(PlatformType type) const;

  static std::vector<PlatformDescriptorEntry>
  loadPlatformDescriptorEntriesFromDirectory(
      const std::string& path,
      std::string_view systemVendor = "");

  std::vector<PlatformDescriptorEntry> descriptorEntries_;
};

} // namespace facebook::fboss
