/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/lib/phy/BspSaiPhyManager.h"

#include <folly/logging/xlog.h>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/platforms/sai/SaiPhyPlatform.h"
#include "fboss/lib/bsp/BspPimContainer.h"
#include "fboss/lib/phy/SaiExternalPhyPortStats.h"
#include "fboss/lib/phy/SaiPhyRetimer.h"
#include "fboss/lib/platforms/PlatformProductInfo.h"

namespace facebook::fboss {

BspSaiPhyManager::BspSaiPhyManager(
    const PlatformMapping* platformMapping,
    const BspSystemContainer* systemContainer)
    : SaiPhyManager(platformMapping), systemContainer_(systemContainer) {
  // Store BSP mapping for later use
  bspMapping_ = std::make_unique<BspPlatformMapping>(
      *systemContainer->getBspPlatformMapping());

  // Set threading model to XPHY_LEVEL for per-xphy threading
  setXphyThreadingModel(XphyThreadingModel::XPHY_LEVEL);

  // Set number of slots from BSP mapping
  numOfSlot_ = systemContainer->getNumPims();

  XLOG(INFO) << "BspSaiPhyManager initialized with " << numOfSlot_
             << " PIMs using XPHY_LEVEL threading model";
}

BspSaiPhyManager::~BspSaiPhyManager() {}

phy::PhyIDInfo BspSaiPhyManager::getPhyIDInfo(GlobalXphyID xphyID) const {
  // For BSP based systems, we are defining GlobalXphyID as the phyID directly
  // Search through mapping to find which PIM this phy belongs to
  for (const auto& [pimID, pimMapping] : bspMapping_->getPimMappings()) {
    if (pimMapping.phyMapping()->find(xphyID) !=
        pimMapping.phyMapping()->end()) {
      const auto& phyMapping = pimMapping.phyMapping()->at(xphyID);
      phy::PhyIDInfo info;
      info.pimID = pimID;
      info.controllerID = *phyMapping.phyIOControllerId();
      info.phyAddr = *phyMapping.phyAddr();
      XLOG(DBG5) << __func__ << " Found xphyID=" << xphyID
                 << " in pimID=" << info.pimID
                 << " controllerID=" << info.controllerID
                 << " phyAddr=" << info.phyAddr;
      return info;
    }
  }
  throw FbossError("Cannot find PhyIDInfo for GlobalXphyID=", xphyID);
}

GlobalXphyID BspSaiPhyManager::getGlobalXphyID(
    const phy::PhyIDInfo& phyIDInfo) const {
  // Search through mapping to find the phyID
  const auto& pimMappings = bspMapping_->getPimMappings();
  auto pimIt = pimMappings.find(phyIDInfo.pimID);
  if (pimIt == pimMappings.end()) {
    throw FbossError("Cannot find PIM=", phyIDInfo.pimID, " in BSP mapping");
  }

  // Find the phyID that matches the controllerID and phyAddr
  for (const auto& [phyID, phyMapping] : *pimIt->second.phyMapping()) {
    if (MdioControllerID(*phyMapping.phyIOControllerId()) ==
            phyIDInfo.controllerID &&
        PhyAddr(*phyMapping.phyAddr()) == phyIDInfo.phyAddr) {
      XLOG(DBG5) << __func__ << ": Found matching phyID=" << phyID;
      return GlobalXphyID(phyID);
    }
  }

  throw FbossError(
      "Cannot find GlobalXphyID for PIM=",
      phyIDInfo.pimID,
      " controllerID=",
      phyIDInfo.controllerID,
      " phyAddr=",
      phyIDInfo.phyAddr);
}

bool BspSaiPhyManager::initExternalPhyMap(bool warmboot) {
  XLOG(DBG5) << __func__ << ": Starting with warmboot=" << warmboot;
  std::optional<GlobalXphyID> firstXphy;

  XLOG(INFO)
      << (warmboot ? "Warmboot: Initializing PHY IO controllers without reset"
                   : "Coldboot: Resetting all PHY IO controllers and PHYs");
  for (const auto& [pimID, pimMapping] : bspMapping_->getPimMappings()) {
    auto pimContainer = systemContainer_->getPimContainerFromPimID(pimID);

    // 1. Init MDIO bus controllers first
    pimContainer->initAllPhyIOControllers(/*forceReset=*/!warmboot);

    // 2. Then reset individual PHY retimers
    if (!warmboot) {
      pimContainer->initAllPhys();
    }
  }
  if (!warmboot) {
    XLOG(INFO) << "Coldboot: All PHY resets complete";
  }

  // Iterate through all PIMs in the BSP mapping
  for (const auto& [pimID, pimMapping] : bspMapping_->getPimMappings()) {
    XLOG(INFO) << "Initializing XPHYs for PIM " << pimID;

    auto bspPimContainer = systemContainer_->getPimContainerFromPimID(pimID);

    for (const auto& [phyID, phyMapping] : *pimMapping.phyMapping()) {
      GlobalXphyID xphyID = GlobalXphyID(phyID);

      if (!firstXphy) {
        firstXphy = xphyID;
      }

      phy::PhyIDInfo phyIDInfo;
      phyIDInfo.pimID = pimID;
      phyIDInfo.controllerID = *phyMapping.phyIOControllerId();
      phyIDInfo.phyAddr = *phyMapping.phyAddr();
      XLOG(DBG5) << __func__ << ": PhyIDInfo created with"
                 << " pimID=" << phyIDInfo.pimID
                 << " controllerID=" << phyIDInfo.controllerID
                 << " phyAddr=" << phyIDInfo.phyAddr;

      // Create ExternalPhy (SaiPhyRetimer) object
      createExternalPhy(
          phyIDInfo, const_cast<BspPimContainer*>(bspPimContainer));
      XLOG(DBG5) << __func__
                 << ": Completed createExternalPhy for xphyID=" << xphyID;

      XLOG(INFO) << "Created SaiPhyRetimer and setup threading for xphy "
                 << xphyID << " in PIM " << pimID;
    }
  }

  if (firstXphy) {
    XLOG(DBG5) << __func__
               << ": Calling preHwInitialized for firstXphy=" << *firstXphy;
    // Initialize SAI APIs once
    getSaiPlatform(*firstXphy)->preHwInitialized(warmboot);

    // Mark the SAI adaptor as thread-safe to enable parallel XPHY
    // initialization. Gated to PAI 4.1+ (which installs the PAI lock-sync
    // callbacks); on PAI 4.0 PAI access is unprotected, so keep serial init.
#if defined(BRCM_PAI_SDK) && SAI_API_VERSION >= SAI_VERSION(1, 18, 1)
    SaiApiLock::getInstance()->setAdaptorIsThreadSafe(true);
#endif
  }

  return true;
}

void BspSaiPhyManager::initializeXphy(GlobalXphyID xphyID, bool warmboot) {
  auto phyIDInfo = getPhyIDInfo(xphyID);
  auto pimID = phyIDInfo.pimID;

  XLOG(DBG2) << __func__ << ": Initializing xphy " << xphyID << " in PIM "
             << pimID << " warmboot=" << warmboot;

  initializeXphyImpl<SaiPhyPlatform, phy::SaiPhyRetimer>(
      pimID, xphyID, warmboot);

  XLOG(DBG2) << "Finished initializing xphy " << xphyID;
}

void BspSaiPhyManager::initializeSlotPhys(PimID pimID, bool /* warmboot */) {
  // Hard fail - BspSaiPhyManager uses XPHY_LEVEL threading only
  throw FbossError(
      "initializeSlotPhys() should not be called for BspSaiPhyManager. "
      "This platform uses XPHY_LEVEL threading model. "
      "Use initializeXphy() instead. PimID=",
      pimID);
}

void BspSaiPhyManager::createExternalPhy(
    const phy::PhyIDInfo& phyIDInfo,
    MultiPimPlatformPimContainer* pimContainer) {
  XLOG(DBG5) << __func__ << ": Starting for phyIDInfo"
             << " pimID=" << phyIDInfo.pimID
             << " controllerID=" << phyIDInfo.controllerID
             << " phyAddr=" << phyIDInfo.phyAddr;

  auto xphyID = getGlobalXphyID(phyIDInfo);

  // Create SaiPhyPlatform for this xphy
  auto productInfo =
      std::make_unique<PlatformProductInfo>(FLAGS_fruid_filepath);
  productInfo->initialize();
  addSaiPlatform(
      xphyID,
      std::make_unique<SaiPhyPlatform>(
          std::move(productInfo),
          std::make_unique<PlatformMapping>(getPlatformMapping()->toThrift()),
          getLocalMac(),
          phyIDInfo.pimID,
          xphyID));

  // Get BspPhyIO from the BSP container
  auto bspPimContainer = static_cast<const BspPimContainer*>(pimContainer);
  auto bspPhyContainer = bspPimContainer->getPhyContainerFromPhyID(xphyID);
  auto bspPhyIO = bspPhyContainer->getPhyIO();

  // Create SaiPhyRetimer using the BspPhyIO
  auto saiPlatform = static_cast<SaiPhyPlatform*>(getSaiPlatform(xphyID));
  xphyMap_[phyIDInfo.pimID][xphyID] = std::make_unique<phy::SaiPhyRetimer>(
      xphyID,
      phy::PhyAddress(phyIDInfo.phyAddr),
      bspPhyIO,
      getPlatformMapping(),
      saiPlatform);
  XLOG(DBG5) << __func__ << ": Created SaiPhyRetimer";

  XLOG(INFO) << "Created SaiPhyRetimer for xphy " << xphyID << " in PIM "
             << phyIDInfo.pimID;
}

std::unique_ptr<ExternalPhyPortStatsUtils>
BspSaiPhyManager::createExternalPhyPortStats(PortID portID) {
  // Real stats object so collectXphyStats() publishes the XPHY FEC/lane
  // counters to fb303 via ExternalPhyPortStatsUtils::updateXphyStats(). PRBS
  // stats are served through SaiPhyManager's SAI attribute path.
  return std::make_unique<SaiExternalPhyPortStats>(getPortName(portID));
}

MultiPimPlatformSystemContainer* BspSaiPhyManager::getSystemContainer() {
  throw FbossError(
      "BspSaiPhyManager::getSystemContainer() not implemented. "
      "BSP system container should be passed to constructor.");
}

int BspSaiPhyManager::getPimStartNum() {
  return systemContainer_->getPimStartNum();
}

folly::EventBase* BspSaiPhyManager::getXphyEventBase(
    const GlobalXphyID& xphyID) const {
  XLOG(DBG5) << __func__ << ": Getting event base for xphyID=" << xphyID;

  auto phyIDInfo = getPhyIDInfo(xphyID);

  auto bspPimContainer =
      systemContainer_->getPimContainerFromPimID(phyIDInfo.pimID);

  return bspPimContainer->getPhyIOEventBase(xphyID);
}

} // namespace facebook::fboss
