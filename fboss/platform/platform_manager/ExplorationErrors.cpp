// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/platform_manager/ExplorationErrors.h"

#include <re2/re2.h>

#include "fboss/platform/platform_manager/Utils.h"

namespace facebook::fboss::platform::platform_manager {

const re2::RE2 kPsuSlotPath{R"(/PSU_SLOT@\d+$)"};

bool isExpectedError(
    const PlatformConfig& platformConfig,
    ExplorationErrorType errorType,
    const std::string& devicePath) {
  // https://fb.workplace.com/groups/1419427118405392/permalink/2752297575118333
  // https://fb.workplace.com/groups/264616536023347/permalink/907140855104242/
  // for details. To be removed once the issue is fixed.
  if ((*platformConfig.platformName() == "MONTBLANC" ||
       *platformConfig.platformName() == "JANGA800BIC" ||
       *platformConfig.platformName() == "TAHAN800BC") &&
      errorType == ExplorationErrorType::IDPROM_READ &&
      devicePath == "/[IDPROM]") {
    return true;
  }

  // PSU slot is expected to be empty on DC-supplied units
  if ((errorType == ExplorationErrorType::SLOT_PM_UNIT_ABSENCE ||
       errorType == ExplorationErrorType::RUN_DEVMAP_SYMLINK)) {
    auto [slotPath, _] = Utils().parseDevicePath(devicePath);
    if (re2::RE2::PartialMatch(slotPath, kPsuSlotPath)) {
      return true;
    }
  }

  // M5120CSC PSU IDPROMs do not use the Meta EEPROM format
  if (*platformConfig.platformName() == "M5120CSC" &&
      errorType == ExplorationErrorType::IDPROM_READ) {
    auto [slotPath, _] = Utils().parseDevicePath(devicePath);
    if (re2::RE2::PartialMatch(slotPath, kPsuSlotPath)) {
      return true;
    }
  }

  // RTM outlet tsensor may not be present on EVT1A hardware for ladakh800bcls
  if (*platformConfig.platformName() == "LADAKH800BCLS" &&
      (errorType == ExplorationErrorType::I2C_DEVICE_CREATE ||
       errorType == ExplorationErrorType::I2C_DEVICE_REG_INIT ||
       errorType == ExplorationErrorType::RUN_DEVMAP_SYMLINK)) {
    if (devicePath == "/RTM_L_SLOT@0/[RTM_L_OUTLET_TSENSOR]" ||
        devicePath == "/RTM_R_SLOT@0/[RTM_R_OUTLET_TSENSOR]") {
      return true;
    }
  }

  // These idproms do not support Meta EEPROM format, but can still provide
  // useful info through WeutilDarwin
  if (*platformConfig.platformName() == "DARWIN" &&
      errorType == ExplorationErrorType::IDPROM_READ &&
      (devicePath == "/RACKMON_SLOT@0/[IDPROM]" ||
       devicePath == "/PEM_SLOT@0/[IDPROM]")) {
    return true;
  }

  return false;
}

} // namespace facebook::fboss::platform::platform_manager
