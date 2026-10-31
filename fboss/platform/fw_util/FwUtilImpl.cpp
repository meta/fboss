/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <filesystem>
#include <iostream>

#include <fmt/core.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/platform/config_lib/ConfigLib.h"
#include "fboss/platform/fw_util/ConfigValidator.h"
#include "fboss/platform/fw_util/FwUtilImpl.h"
#include "fboss/platform/fw_util/fw_util_helpers.h"
#include "fboss/platform/helpers/PlatformNameLib.h"

using namespace folly::literals::shell_literals;

namespace facebook::fboss::platform::fw_util {
void FwUtilImpl::init() {
  auto platformNameOpt = helpers::PlatformNameLib().getPlatformName();
  if (!platformNameOpt.has_value()) {
    XLOG(WARNING) << "Could not determine platform name from system. "
                  << "Using 'UNKNOWN' as default. This may occur in test "
                  << "environments or systems without dmidecode.";
    platformName_ = "UNKNOWN";
  } else {
    platformName_ = platformNameOpt.value();
  }
  ConfigLib configLib(configFilePath_);
  std::string fwUtilConfJson = configLib.getFwUtilConfig();
  try {
    apache::thrift::SimpleJSONSerializer::deserialize<FwUtilConfig>(
        fwUtilConfJson, fwUtilConfig_);
  } catch (const std::exception& e) {
    XLOG(ERR) << "Error deserializing FwUtilConfig: " << e.what();
  }
  if (!ConfigValidator().isValid(fwUtilConfig_)) {
    XLOG(ERR) << "Invalid fw_util config! Aborting...";
    throw std::runtime_error("Invalid fw_util Config. Aborting...");
  }

  for (const auto& [fpd, fwConfigs] : *fwUtilConfig_.fwConfigs()) {
    fwDeviceNamesByPrio_.emplace_back(fpd, *fwConfigs.priority());
  }
  // Sort firmware devices by priority
  std::sort(
      fwDeviceNamesByPrio_.begin(),
      fwDeviceNamesByPrio_.end(),
      [](const auto& rhsFwDevice, const auto& lhsFwDevice) {
        return rhsFwDevice.second < lhsFwDevice.second;
      });

  fwUtilVersionHandler_ = std::make_unique<FwUtilVersionHandler>(
      fwDeviceNamesByPrio_, fwUtilConfig_);
}

std::vector<std::string> FwUtilImpl::getFpdNameList() {
  std::vector<std::string> fpdNameList;
  fpdNameList.reserve(fwDeviceNamesByPrio_.size());
  for (const auto& fpd : fwDeviceNamesByPrio_) {
    fpdNameList.emplace_back(fpd.first);
  }

  return fpdNameList;
}

std::tuple<std::string, FwConfig> FwUtilImpl::getFpd(
    const std::string& searchFpdName) {
  std::string lowerSearchFpdName = toLower(searchFpdName);
  if (lowerSearchFpdName == "all") {
    return std::tuple("all", FwConfig());
  }

  for (const auto& [fpdName, fpd] : *fwUtilConfig_.fwConfigs()) {
    if (toLower(fpdName) == lowerSearchFpdName) {
      return std::tuple(fpdName, fpd);
    }
  }
  throw std::runtime_error(
      fmt::format(
          "Invalid firmware target name: {}\nUse fw-util --fw_action=list to see available firmware targets",
          searchFpdName));
}

void FwUtilImpl::doFirmwareAction(
    const std::string& fpdArg,
    const std::string& action) {
  XLOG(INFO, " Analyzing the given FW action for execution");

  auto [fpd, fwConfig] = getFpd(fpdArg);
  if (action == "program") {
    // Fw_util is build as part of ramdisk once every 24 hours
    // assuming no test failure. if we force sha1sum check in the fw_util,
    // we will end up blocking provisioning until a new ramdisk is built
    // by their conveyor which can take more than 24 hours if there are test
    // failures. Hence, we are adding a flag to make sha1sum check optional.
    // We will enforce sha1sum check in the run_script of the FIS packages.

    if (verifySha1sum_) {
      if (fwConfig.sha1sum().has_value()) {
        verifySha1sum(fpd, *fwConfig.sha1sum(), fwBinaryFile_);
      } else {
        XLOG(WARN) << "sha1sum is not set in the " << platformName_
                   << " config file. Skipping the action.";
        return;
      }
    }

    if (fwConfig.preUpgrade().has_value()) {
      if (dryRun_) {
        XLOG(INFO) << "Dry run mode enabled, skipping preUpgrade operation for "
                   << fpd;
      } else {
        doPreUpgrade(fpd);
      }
    }

    if (fwConfig.upgrade().has_value()) {
      // Perform the upgrade operation
      doUpgrade(fpd);
    } else {
      XLOG(ERR) << "upgrade entry is not set in the json";
    }

    // do post upgrade operation
    if (fwConfig.postUpgrade().has_value()) {
      if (dryRun_) {
        XLOG(INFO)
            << "Dry run mode enabled, skipping postUpgrade operation for "
            << fpd;
      } else {
        doPostUpgrade(fpd);
      }
    }
  } else if (action == "read" && fwConfig.read().has_value()) {
    // do pre upgrade operation
    if (fwConfig.preUpgrade().has_value()) {
      doPreUpgrade(fpd);
    }

    performRead(fwConfig.read().value(), fpd);

    // do post upgrade operation
    if (fwConfig.postUpgrade().has_value()) {
      doPostUpgrade(fpd);
    }
  } else if (action == "verify" && fwConfig.verify().has_value()) {
    // do pre upgrade operation
    if (fwConfig.preUpgrade().has_value()) {
      doPreUpgrade(fpd);
    }

    performVerify(fwConfig.verify().value(), fpd);

    // do post upgrade operation
    if (fwConfig.postUpgrade().has_value()) {
      doPostUpgrade(fpd);
    }
  } else {
    XLOG(INFO) << "Invalid action: " << action
               << ". Please run ./fw-util --help Flags for the right usage";
    exit(1);
  }
}

void FwUtilImpl::printVersion(const std::string& fpd) {
  // TODO: Remove this check once we have moved all Darwin systems to the latest
  // BSP which provide a single sysfs endpoint for each firmware version
  auto [fpdName, _] = getFpd(fpd);

  if (toLower(platformName_) == "darwin") {
    fwUtilVersionHandler_->printDarwinVersion(fpdName);
  } else {
    if (fpd == "all") {
      fwUtilVersionHandler_->printAllVersions();
    } else {
      std::string version = fwUtilVersionHandler_->getSingleVersion(fpdName);
      std::cout << fmt::format("{} : {}", fpd, version) << std::endl;
    }
  }
}

void FwUtilImpl::doVersionAudit() {
  bool mismatch = false;
  for (const auto& [fpdName, fwConfig] : *fwUtilConfig_.fwConfigs()) {
    std::string desiredVersion = fwConfig.desiredVersion().value_or("");
    if (desiredVersion.empty()) {
      XLOGF(
          INFO,
          "{} does not have a desired version specified in the config.",
          fpdName);
      continue;
    }
    std::string actualVersion;
    try {
      actualVersion = fwUtilVersionHandler_->getSingleVersion(fpdName);
    } catch (const std::exception& e) {
      XLOG(ERR) << "Failed to get version for " << fpdName << ": " << e.what();
      continue;
    }

    if (actualVersion != desiredVersion) {
      XLOGF(
          INFO,
          "{} is at version {} which does not match config's desired version {}.",
          fpdName,
          actualVersion,
          desiredVersion);
      mismatch = true;
    }
  }
  if (mismatch) {
    XLOG(INFO, "Firmware version mismatch found.");
    exit(1);
  } else {
    XLOG(INFO, "All firmware versions match the config.");
  }
}
} // namespace facebook::fboss::platform::fw_util
