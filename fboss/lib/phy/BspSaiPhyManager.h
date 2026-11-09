/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/lib/bsp/BspPlatformMapping.h"
#include "fboss/lib/bsp/BspSystemContainer.h"
#include "fboss/lib/phy/SaiPhyManager.h"

namespace facebook::fboss {

/**
 * Generic BSP-based PHY Manager that extends SaiPhyManager to support XPHY
 * retimers that communicate through BSP MDIO interfaces.
 * No platform-specific subclasses needed - everything is driven by
 * configuration.
 */
class BspSaiPhyManager : public SaiPhyManager {
 public:
  explicit BspSaiPhyManager(
      const PlatformMapping* platformMapping,
      const BspSystemContainer* systemContainer);

  ~BspSaiPhyManager() override;

  phy::PhyIDInfo getPhyIDInfo(GlobalXphyID xphyID) const override;
  GlobalXphyID getGlobalXphyID(const phy::PhyIDInfo& phyIDInfo) const override;

  bool initExternalPhyMap(bool warmboot) override;

  void initializeXphy(GlobalXphyID xphyID, bool warmboot) override;

  void initializeSlotPhys(PimID pimID, bool warmboot) override;

  MultiPimPlatformSystemContainer* getSystemContainer() override;

  int getPimStartNum() override;

  cfg::AsicType getPhyAsicType() const override {
    return cfg::AsicType::ASIC_TYPE_AGERA3;
  }

  // Override to return the event base for a specific XPHY from BspPimContainer
  folly::EventBase* getXphyEventBase(const GlobalXphyID& xphyID) const override;

 private:
  // We won't be using this MultiPimPlatformPimContainer
  void createExternalPhy(
      const phy::PhyIDInfo& phyIDInfo,
      MultiPimPlatformPimContainer* pimContainer) override;

  // Agera3 publishes real XPHY FEC/lane counters to fb303. The SaiPhyManager
  // base keeps NullPortStats so Credo/Elbert is unaffected.
  std::unique_ptr<ExternalPhyPortStatsUtils> createExternalPhyPortStats(
      PortID portID) override;

  // Store BSP platform mapping for quick lookups
  std::unique_ptr<BspPlatformMapping> bspMapping_;

  // Store system container to access BspPimContainers
  const BspSystemContainer* systemContainer_;
};

} // namespace facebook::fboss
