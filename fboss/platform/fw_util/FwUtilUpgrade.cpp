#include <folly/logging/xlog.h>
#include <chrono>
#include <thread>
#include "fboss/platform/fw_util/FwUtilImpl.h"

namespace facebook::fboss::platform::fw_util {

void FwUtilImpl::doUpgrade(const std::string& fpd) {
  if (dryRun_) {
    XLOG(INFO) << "Dry run mode enabled, skipping upgrade for " << fpd;
    return;
  }
  XLOG(INFO) << "Running Upgrade operation for " << fpd;
  const auto& fwConfig = fwUtilConfig_.fwConfigs()->at(fpd);
  if (!fwConfig.upgrade().has_value()) {
    throw std::runtime_error("No upgrade operation found for " + fpd);
  }
  for (const auto& operation : *fwConfig.upgrade()) {
    doUpgradeOperation(operation, fpd);
  }
  if (auto delaySec = fwConfig.delayAfterUpgradeSec()) {
    XLOG(INFO) << "Waiting " << *delaySec << " seconds after upgrading " << fpd;
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(std::chrono::seconds(*delaySec));
  }
}

void FwUtilImpl::doUpgradeOperation(
    const UpgradeConfig& upgradeConfig,
    const std::string& fpd) {
  if (*upgradeConfig.commandType() == "flashrom") {
    if (upgradeConfig.flashromArgs().has_value()) {
      performFlashromUpgrade(upgradeConfig.flashromArgs().value(), fpd);
    }
  } else if (*upgradeConfig.commandType() == "jam") {
    if (upgradeConfig.jamArgs().has_value()) {
      performJamUpgrade(upgradeConfig.jamArgs().value());
    }
  } else if (*upgradeConfig.commandType() == "xapp") {
    if (upgradeConfig.xappArgs().has_value()) {
      performXappUpgrade(upgradeConfig.xappArgs().value());
    }
  } else if (*upgradeConfig.commandType() == "psu_util") {
    if (upgradeConfig.psuUtilArgs().has_value()) {
      performPsuUtilUpgrade(upgradeConfig.psuUtilArgs().value());
    }
  } else {
    throw std::runtime_error(
        "Unsupported command type: " + *upgradeConfig.commandType());
  }
}

} // namespace facebook::fboss::platform::fw_util
