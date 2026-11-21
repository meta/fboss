// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
#pragma once
#include <string_view>
#include "fboss/platform/fw_util/if/gen-cpp2/fw_util_config_types.h"

namespace facebook::fboss::platform::fw_util {

inline constexpr std::string_view kFwTargetListHint =
    "Use fw_util --fw_action=list to see available firmware targets";

class FwUtilVersionHandler {
 public:
  explicit FwUtilVersionHandler(
      const std::vector<std::pair<std::string, int>>& fwDeviceNamesByPrio,
      const fw_util_config::FwUtilConfig& fwUtilConfig)
      : fwDeviceNamesByPrio_(fwDeviceNamesByPrio),
        fwUtilConfig_(fwUtilConfig) {};
  ~FwUtilVersionHandler() = default;
  void printDarwinVersion(const std::string& fpd);
  std::string getSingleVersion(const std::string& fpd);
  void printAllVersions();

 private:
  std::vector<std::pair<std::string, int>> fwDeviceNamesByPrio_;
  fw_util_config::FwUtilConfig fwUtilConfig_{};
};

} // namespace facebook::fboss::platform::fw_util
