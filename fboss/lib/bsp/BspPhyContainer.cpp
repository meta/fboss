// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/BspPhyContainer.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>
#include "fboss/lib/bsp/BspResetUtils.h"
#include "fboss/lib/bsp/gen-cpp2/bsp_platform_mapping_types.h"

namespace facebook::fboss {

BspPhyContainer::BspPhyContainer(
    int pimID,
    BspPhyMapping& phyMapping,
    BspPhyIO* phyIO)
    : phyMapping_(phyMapping), phyIO_(phyIO) {
  pimID_ = pimID;
  phyID_ = *phyMapping.phyId();

  if (phyMapping.phyResetPath().has_value()) {
    phyResetPath_ = *phyMapping.phyResetPath();
    XLOG(DBG5) << fmt::format(
        "BspPhyContainerTrace: PHY {:d} has reset path: {}",
        phyID_,
        *phyResetPath_);
  }
}

void BspPhyContainer::initPhy(bool forceReset) const {
  XLOG(INFO) << fmt::format(
      "BspPhyContainerTrace: {} initializing PHY {:d}, forceReset={}",
      __func__,
      phyID_,
      forceReset);

  auto componentName = fmt::format("PHY {:d}", phyID_);

  if (forceReset && hasPhyResetPath()) {
    holdResetViaSysfs(phyResetPath_, componentName);
  }
  if (hasPhyResetPath()) {
    releaseResetViaSysfs(phyResetPath_, componentName);
  }

  XLOG(INFO) << fmt::format(
      "BspPhyContainerTrace: {} PHY {:d} initialized", __func__, phyID_);
}

bool BspPhyContainer::hasPhyResetPath() const {
  return phyResetPath_.has_value();
}

std::optional<std::string> BspPhyContainer::getPhyResetPath() const {
  return phyResetPath_;
}

} // namespace facebook::fboss
