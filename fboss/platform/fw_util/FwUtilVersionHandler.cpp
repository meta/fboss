#include "fboss/platform/fw_util/FwUtilVersionHandler.h"
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <glob.h>
#include <iostream>
#include "fboss/platform/helpers/PlatformFsUtils.h"
#include "fboss/platform/helpers/PlatformUtils.h"

namespace facebook::fboss::platform::fw_util {

void FwUtilVersionHandler::printDarwinVersion(const std::string& fpd) {
  if (fpd == "all") {
    for (const auto& orderedfpd : fwDeviceNamesByPrio_) {
      auto iter = fwUtilConfig_.fwConfigs()->find(orderedfpd.first);
      std::string versionCmd = *iter->second.version()->getVersionCmd();
      auto [exitStatus, version] = PlatformUtils().execCommand(versionCmd);
      if (exitStatus != 0) {
        throw std::runtime_error("Run " + versionCmd + " failed!");
      }
      std::cout << orderedfpd.first << " : " << version;
    }
  } else {
    auto iter = fwUtilConfig_.fwConfigs()->find(fpd);
    if (iter != fwUtilConfig_.fwConfigs()->end()) {
      std::string versionCmd = *iter->second.version()->getVersionCmd();
      auto [exitStatus, version] = PlatformUtils().execCommand(versionCmd);
      if (exitStatus != 0) {
        throw std::runtime_error("Run " + versionCmd + " failed!");
      }
      std::cout << fmt::format("{} : {}", fpd, version);
    } else {
      XLOG(INFO)
          << fpd
          << " is not part of the firmware target_name list. Please use fw_util --help for the right usage";
    }
  }
}

std::string FwUtilVersionHandler::getSingleVersion(const std::string& fpd) {
  std::string version;
  auto iter = fwUtilConfig_.fwConfigs()->find(fpd);
  if (iter == fwUtilConfig_.fwConfigs()->end()) {
    throw std::runtime_error(
        "Firmware target name not found in config: " + fpd +
        " Please use fw_util --help for the right usage");
  }

  const auto& versionConfig = *iter->second.version();
  // Check if the path is set
  if (*versionConfig.versionType() == "Not Applicable") {
    return "Not Applicable";
  } else if (
      *versionConfig.versionType() == "sysfs" &&
      versionConfig.path().has_value()) {
    // using the glob function to expand the path to the file
    // because there are cases where we might have a wild card
    // such as  /run/devmap/cplds/FAN_CPLD/hwmon/hwmon*/fw_ver
    // For those cases, it's guarantee that there will always be
    // one match. if there is no wildcard, the glob function will
    // return a single match, which is the original path itself
    glob_t globbuf;
    int ret = glob(
        versionConfig.path()->c_str(),
        GLOB_ERR | GLOB_NOCHECK,
        nullptr,
        &globbuf);
    if (ret != 0) {
      throw std::runtime_error("Failed to glob file: " + *versionConfig.path());
    }
    // Rewrote to use library function instead of executing cat
    const std::string matchedPath = globbuf.gl_pathv[0];
    auto content = PlatformFsUtils().getStringFileContent(matchedPath);
    if (!content.has_value()) {
      globfree(&globbuf);
      throw std::runtime_error("Failed to read file: " + matchedPath);
    }
    version = content.value();
    globfree(&globbuf);
  } else if (
      *versionConfig.versionType() == "full_command" &&
      versionConfig.getVersionCmd().has_value()) {
    auto [exitStatus, cmdOutput] =
        PlatformUtils().execCommand(*versionConfig.getVersionCmd());
    if (exitStatus != 0) {
      throw std::runtime_error(
          "Running version command failed for " + fpd + ": " +
          *versionConfig.getVersionCmd());
    }
    version = folly::trimWhitespace(cmdOutput).str();
  } else {
    XLOG(ERR) << "Unsupported versionType or missing required fields for "
              << fpd;
    return "";
  }
  return version;
}

void FwUtilVersionHandler::printAllVersions() {
  std::vector<std::string> failedDevices;
  for (const auto& orderedfpd : fwDeviceNamesByPrio_) {
    try {
      std::string version = getSingleVersion(orderedfpd.first);
      std::cout << orderedfpd.first << " : " << version << std::endl;
    } catch (const std::exception& e) {
      XLOG(WARNING) << "Failed to read version for " << orderedfpd.first << ": "
                    << e.what();
      std::cout << orderedfpd.first << " : ERROR" << std::endl;
      failedDevices.push_back(orderedfpd.first);
    }
  }
  if (!failedDevices.empty()) {
    throw std::runtime_error(
        "Failed to read versions for: " + folly::join(", ", failedDevices));
  }
}

} // namespace facebook::fboss::platform::fw_util
