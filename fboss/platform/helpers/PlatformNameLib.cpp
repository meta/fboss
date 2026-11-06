// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/helpers/PlatformNameLib.h"

#include <filesystem>

#include <fb303/ServiceData.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>

namespace facebook::fboss::platform::helpers {

std::string PlatformNameLib::sanitizePlatformName(
    const std::string& platformNameFromBios) {
  std::string platformNameUpper(platformNameFromBios);
  std::transform(
      platformNameUpper.begin(),
      platformNameUpper.end(),
      platformNameUpper.begin(),
      ::toupper);

  if (platformNameUpper == "MINIPACK3" ||
      platformNameUpper == "MINIPACK3_MCB") {
    return "MONTBLANC";
  }
  if (platformNameUpper == "MINIPACK3M" ||
      platformNameUpper == "MINIPACK3M_MCB") {
    return "MONTBLANCM";
  }
  if (platformNameUpper == "JANGA") {
    return "JANGA800BIC";
  }
  if (platformNameUpper == "TAHAN") {
    return "TAHAN800BC";
  }
  return platformNameUpper;
}

PlatformNameLib::PlatformNameLib(
    const std::shared_ptr<PlatformUtils> platformUtils,
    const std::shared_ptr<PlatformFsUtils> platformFsUtils)
    : platformUtils_(platformUtils), platformFsUtils_(platformFsUtils) {}

std::string PlatformNameLib::getPlatformNameFromBios(bool writeToCache) const {
  XLOG(INFO) << "Getting platform name from bios using dmidecode ...";
  auto [exitStatus, standardOut] =
      platformUtils_->execCommand(dmidecodeCommand);
  if (exitStatus != 0) {
    XLOG(ERR) << fmt::format(
        "Failed to get platform name from bios: {}", standardOut);
    fb303::fbData->setCounter(kPlatformNameBiosReadFailures, 1);
    throw std::runtime_error("Failed to get platform name from bios");
  }
  standardOut = folly::trimWhitespace(standardOut).str();
  XLOG(INFO) << "Platform name inferred from bios: " << standardOut;
  auto result = sanitizePlatformName(standardOut);
  XLOG(INFO) << "Platform name mapped: " << result;
  if (writeToCache) {
    platformFsUtils_->writeStringToFile(result + '\n', kCachePath, true);
  }
  fb303::fbData->setCounter(kPlatformNameBiosReadFailures, 0);
  return result;
}

std::optional<std::string> PlatformNameLib::getPlatformName() const {
  auto nameFromCache = platformFsUtils_->getStringFileContent(kCachePath);
  if (nameFromCache.has_value()) {
    auto result = nameFromCache.value();
    XLOG(INFO) << "Platform name read from cache: " << result;
    fb303::fbData->setCounter(kPlatformNameBiosReadFailures, 0);
    return result;
  }
  try {
    return getPlatformNameFromBios();
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

} // namespace facebook::fboss::platform::helpers
