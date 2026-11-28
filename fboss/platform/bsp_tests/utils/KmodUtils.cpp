// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/platform/bsp_tests/utils/KmodUtils.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <re2/re2.h>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fboss/platform/bsp_tests/BspTestEnvironment.h"
#include "fboss/platform/helpers/PlatformUtils.h"
#include "fboss/platform/platform_manager/PciExplorer.h"
#include "fboss/platform/platform_manager/SystemInterface.h"

namespace facebook::fboss::platform::bsp_tests {

void KmodUtils::bindDesiredDrivers(
    const platform_manager::PlatformConfig& platformConfig) {
  for (const auto& [pmUnitName, pmUnitConfig] :
       *platformConfig.pmUnitConfigs()) {
    for (const auto& pciConf : *pmUnitConfig.pciDeviceConfigs()) {
      if (!pciConf.desiredDriver().has_value()) {
        continue;
      }
      try {
        // PciDevice constructor handles device binding
        platform_manager::PciDevice pciDevice(pciConf);
        XLOG(INFO) << fmt::format(
            "Successfully bound {} to driver {}",
            *pciConf.pmUnitScopedName(),
            *pciConf.desiredDriver());
      } catch (const std::exception& e) {
        FAIL() << fmt::format(
            "Failed to bind device {} to driver {}: {}",
            *pciConf.pmUnitScopedName(),
            *pciConf.desiredDriver(),
            e.what());
      }
    }
  }
}

void KmodUtils::loadKmods(const BspKmodsFile& kmods) {
  // Load shared kmods first
  for (const auto& kmod : *kmods.sharedKmods()) {
    auto result = PlatformUtils().execCommand(fmt::format("modprobe {}", kmod));
    EXPECT_EQ(result.first, 0) << "Failed to load shared kmod: " << kmod;
  }

  BspTestEnvironment* env = BspTestEnvironment::GetInstance();
  const platform_manager::PlatformConfig& platformConfig =
      env->getPlatformManagerConfig();
  bindDesiredDrivers(platformConfig);

  // Then load BSP kmods
  for (const auto& kmod : *kmods.bspKmods()) {
    auto result = PlatformUtils().execCommand(fmt::format("modprobe {}", kmod));
    EXPECT_EQ(result.first, 0) << "Failed to load BSP kmod: " << kmod;
  }
}

void KmodUtils::unloadKmods(const BspKmodsFile& kmods) {
  // rmmod like PkgManager: modprobe -r also removes a module's dependencies
  // and fails while another module still uses one of them.
  auto loaded = getLoadedKmods(kmods);
  std::set<std::string> loadedSet(loaded.begin(), loaded.end());
  auto unload = [&](const std::string& kmod, const char* kind) {
    if (!loadedSet.contains(kmod)) {
      return;
    }
    auto result = PlatformUtils().execCommand(fmt::format("rmmod {}", kmod));
    EXPECT_EQ(result.first, 0)
        << "Failed to unload " << kind << " kmod: " << kmod;
  };
  for (const auto& kmod : *kmods.bspKmods()) {
    unload(kmod, "BSP");
  }

  // Then unload shared kmods
  for (const auto& kmod : *kmods.sharedKmods()) {
    unload(kmod, "shared");
  }
}

std::string KmodUtils::getVendorKeyword(
    const platform_manager::PlatformConfig& platformConfig) {
  const re2::RE2 kBspRpmNameRe("(?P<KEYWORD>[a-z]+)_bsp_kmods");
  std::string keyword;
  re2::RE2::FullMatch(
      *platformConfig.bspKmodsRpmName(), kBspRpmNameRe, &keyword);
  return keyword;
}

std::string KmodUtils::getKernelVersion() {
  return platform_manager::package_manager::SystemInterface()
      .getHostKernelVersion();
}

// Run fbsp-remove.sh script
void KmodUtils::fbspRemove(
    const platform_manager::PlatformConfig& platformConfig) {
  // Find fbsp-remove.sh script path
  constexpr auto kremoveScriptPath = "/usr/local/{}_bsp/{}/fbsp-remove.sh";
  std::string fbspPath = fmt::format(
      kremoveScriptPath, getVendorKeyword(platformConfig), getKernelVersion());

  XLOG(DBG) << "Running fbsp-remove: " << fbspPath;
  auto result = PlatformUtils().execCommand(fmt::format("{} -f", fbspPath));
  EXPECT_EQ(result.first, 0) << "Failed to run fbsp-remove.sh";
}

// Get list of loaded kernel modules from `lsmod` that match expected modules
std::vector<std::string> KmodUtils::getLoadedKmods(
    const BspKmodsFile& expectedKmods) {
  auto lsmodResult = PlatformUtils().execCommand("lsmod");
  EXPECT_EQ(lsmodResult.first, 0) << "Failed to run lsmod";
  const auto& lsmodOutput = lsmodResult.second;

  std::set<std::string> expectedKmodSet;
  for (const auto& kmod : *expectedKmods.bspKmods()) {
    expectedKmodSet.insert(kmod);
  }
  for (const auto& kmod : *expectedKmods.sharedKmods()) {
    expectedKmodSet.insert(kmod);
  }

  // Parse lsmod output and find matching kmods
  std::vector<std::string> loadedKmods;
  std::istringstream iss(lsmodOutput);
  std::string line;

  // Skip header line
  std::getline(iss, line);

  while (std::getline(iss, line)) {
    std::istringstream lineStream(line);
    std::string kmod;
    lineStream >> kmod;

    if (!kmod.empty() && expectedKmodSet.find(kmod) != expectedKmodSet.end()) {
      loadedKmods.push_back(kmod);
    }
  }

  return loadedKmods;
}

} // namespace facebook::fboss::platform::bsp_tests
