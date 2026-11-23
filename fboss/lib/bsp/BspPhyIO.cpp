// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/BspPhyIO.h"

#include <folly/Range.h>
#include <folly/logging/xlog.h>
#include "fboss/lib/bsp/BspResetUtils.h"
#include "fboss/lib/bsp/gen-cpp2/bsp_platform_mapping_types.h"
#include "fboss/mdio/BspDeviceMdio.h"

namespace facebook::fboss {

BspPhyIO::BspPhyIO(int pimID, BspPhyIOControllerInfo& controllerInfo)
    : pimID_(pimID) {
  auto mdioDevName = *controllerInfo.devicePath();
  controllerId_ = *controllerInfo.controllerId();
  XLOG(DBG5) << __func__ << ": mdioDevName=" << mdioDevName
             << " controllerId=" << controllerId_;

  if (controllerInfo.resetPath().has_value()) {
    controllerResetPath_ = *controllerInfo.resetPath();
    XLOG(DBG5) << __func__ << ": controllerResetPath=" << *controllerResetPath_;
  }

  mdioController_ = std::make_unique<BspDeviceMdioController>(
      controllerId_, pimID, controllerId_, mdioDevName);
  controllerInfo_ = controllerInfo;
  XLOG(DBG5) << fmt::format(
      "BspPhyIOTrace: {} successfully created mdio controller {:d}, {}",
      __func__,
      controllerId_,
      mdioDevName);
}

void BspPhyIO::init(bool forceReset) const {
  XLOG(INFO) << fmt::format(
      "BspPhyIOTrace: {} initializing MDIO controller {:d}, forceReset={}",
      __func__,
      controllerId_,
      forceReset);

  auto componentName = fmt::format("MDIO controller {:d}", controllerId_);

  if (forceReset && hasResetPath()) {
    holdResetViaSysfs(controllerResetPath_, componentName);
  }
  if (hasResetPath()) {
    releaseResetViaSysfs(controllerResetPath_, componentName);
  }

  mdioController_->init();
  XLOG(INFO) << fmt::format(
      "BspPhyIOTrace: {} MDIO controller {:d} initialized",
      __func__,
      controllerId_);
}

bool BspPhyIO::hasResetPath() const {
  return controllerResetPath_.has_value();
}

phy::Cl45Data BspPhyIO::readRegister(
    phy::PhyAddress phyAddr,
    phy::Cl45DeviceAddress deviceAddr,
    phy::Cl45RegisterAddress reg) {
  XLOG(DBG5) << fmt::format(
      "{}: Starting read - phyAddr={:#x}, deviceAddr={:#x}, reg={:#x}",
      __func__,
      phyAddr,
      deviceAddr,
      reg);
  try {
    return mdioController_->readCl45(phyAddr, deviceAddr, reg);
  } catch (const std::exception& ex) {
    throw BspPhyIOError(
        fmt::format(
            "{}() failed for phyAddr={:#x}, deviceAddr={:#x}, reg={:#x}: {}",
            __func__,
            phyAddr,
            deviceAddr,
            reg,
            ex.what()));
  }
}

void BspPhyIO::writeRegister(
    phy::PhyAddress phyAddr,
    phy::Cl45DeviceAddress deviceAddr,
    phy::Cl45RegisterAddress reg,
    phy::Cl45Data data) {
  XLOG(DBG5) << fmt::format(
      "{}: Starting write - phyAddr={:#x}, deviceAddr={:#x}, reg={:#x}, data={:#x}",
      __func__,
      phyAddr,
      deviceAddr,
      reg,
      data);

  try {
    mdioController_->writeCl45(phyAddr, deviceAddr, reg, data);
  } catch (const std::exception& ex) {
    throw BspPhyIOError(
        fmt::format(
            "{}() failed for phyAddr={:#x}, deviceAddr={:#x}, reg={:#x}, data={:#x}: {}",
            __func__,
            phyAddr,
            deviceAddr,
            reg,
            data,
            ex.what()));
  }
}

} // namespace facebook::fboss
