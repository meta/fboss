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

#include "fboss/agent/hw/HwSwitchWarmBootHelper.h"
#include "fboss/agent/hw/sai/switch/SaiMacsecManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/lib/phy/PhyManager.h"
#include "fboss/mka_service/if/gen-cpp2/mka_structs_types.h"

#include "fboss/agent/types.h"

#include <chrono>
#include <map>
#include <mutex>

namespace facebook::fboss {

class SaiPlatform;
class StateUpdate;
class Port;

// Port ID range constants for Agera3 (Ladakh platform)
// Ladakh has 2561 ports (port IDs 0-2560), so we need a larger range
// than the default (0-2047). Using 0-8191 to provide sufficient headroom
constexpr int64_t kAgera3PortIdRangeMin = 0;
constexpr int64_t kAgera3PortIdRangeMax = 8191;

class SaiPhyManager : public PhyManager {
 public:
  explicit SaiPhyManager(const PlatformMapping* platformMapping);
  ~SaiPhyManager() override;

  SaiPlatform* getSaiPlatform(GlobalXphyID xphyID);
  const SaiPlatform* getSaiPlatform(GlobalXphyID xphyID) const {
    return const_cast<SaiPhyManager*>(this)->getSaiPlatform(xphyID);
  }
  SaiPlatform* getSaiPlatform(PortID portID);
  const SaiPlatform* getSaiPlatform(PortID portID) const {
    return const_cast<SaiPhyManager*>(this)->getSaiPlatform(portID);
  }
  SaiSwitch* getSaiSwitch(GlobalXphyID xphyID);
  const SaiSwitch* getSaiSwitch(GlobalXphyID xphyID) const {
    return const_cast<SaiPhyManager*>(this)->getSaiSwitch(xphyID);
  }
  SaiSwitch* getSaiSwitch(PortID portID);
  const SaiSwitch* getSaiSwitch(PortID portID) const {
    return const_cast<SaiPhyManager*>(this)->getSaiSwitch(portID);
  }

  void sakInstallTx(const mka::MKASak& sak) override;
  void sakInstallRx(const mka::MKASak& sak, const mka::MKASci& sci) override;
  void sakDeleteRx(const mka::MKASak& sak, const mka::MKASci& sci) override;
  void sakDelete(const mka::MKASak& sak) override;
  mka::MKASakHealthResponse sakHealthCheck(
      const mka::MKASak sak) const override;

  mka::MacsecPortStats getMacsecPortStats(
      std::string portName,
      mka::MacsecDirection direction,
      bool readFromHw) override;
  mka::MacsecFlowStats getMacsecFlowStats(
      std::string portName,
      mka::MacsecDirection direction,
      bool readFromHw) override;
  mka::MacsecSaStats getMacsecSecureAssocStats(
      std::string portName,
      mka::MacsecDirection direction,
      bool readFromHw) override;

  std::map<std::string, MacsecStats> getAllMacsecPortStats(
      bool readFromHw) override;

  std::map<std::string, MacsecStats> getMacsecPortStats(
      const std::vector<std::string>& portName,
      bool readFromHw) override;

  std::optional<HwPortStats> getHwPortStats(
      const std::string& /* portName */) const override;

  std::string listHwObjects(std::vector<HwObjectType>& hwObjects, bool cached)
      override;

  void programOnePort(
      PortID portId,
      cfg::PortProfileID portProfileId,
      const std::optional<TransceiverInfo>& transceiverInfo,
      bool needResetDataPath) override;

  /*
   * removeOnePort
   *
   * Remove a programmed port from the PHY. This applies a SwitchState delta
   * that removes the port, triggering SaiPortManager::removePort() which
   * tears down the port connector, serdes, line port, and system port SAI
   * objects. The PhyManager cache is also cleared so the port can be
   * re-programmed from scratch.
   */
  void removeOnePort(PortID portId);

  template <typename platformT, typename xphychipT>
  void initializeSlotPhysImpl(PimID pimID, bool warmBoot);

  template <typename platformT, typename xphychipT>
  void initializeXphyImpl(PimID pimID, GlobalXphyID xphyID, bool warmBoot);

  PortOperState macsecGetPhyLinkInfo(PortID swPort);
  phy::PhyInfo getPhyInfo(PortID swPort) override;
  void updateAllXphyPortsStats() override;

  bool isXphyStatsCollectionDone(PortID portID) const override;

  bool getSdkState(const std::string& fileName) override;

  bool setupMacsecState(
      const std::vector<std::string>& portList,
      bool macsecDesired,
      bool dropUnencrypted) override;

  void setPortPrbs(
      PortID portID,
      phy::Side side,
      const phy::PortPrbsState& prbs) override;

  phy::PortPrbsState getPortPrbs(PortID portID, phy::Side side) override;

  std::vector<phy::PrbsLaneStats> getPortPrbsStats(
      PortID portID,
      phy::Side side) override;

  void setPortLoopbackState(
      PortID swPort,
      phy::PortComponent component,
      bool setLoopback) override {
    setSaiPortLoopbackState(swPort, component, setLoopback);
  }

  void setPortAdminState(
      PortID swPort,
      phy::PortComponent component,
      bool setAdminUp) override {
    setSaiPortAdminState(swPort, component, setAdminUp);
  }

  std::string getPortInfoStr(PortID swPort) override {
    return getSaiPortInfo(swPort);
  }

  void xphyPortStateToggle(PortID swPort, phy::Side side);

  void gracefulExit() override;

  virtual cfg::AsicType getPhyAsicType() const = 0;

 protected:
  void addSaiPlatform(
      GlobalXphyID xphyID,
      std::unique_ptr<SaiPlatform> platform);

  folly::MacAddress getLocalMac() const {
    return localMac_;
  }

 private:
  std::shared_ptr<SwitchState> portUpdateHelper(
      std::shared_ptr<SwitchState> in,
      PortID port,
      const SaiPlatform* platform,
      const std::function<void(std::shared_ptr<Port>&)>& modify) const;
  class PlatformInfo {
   public:
    explicit PlatformInfo(std::unique_ptr<SaiPlatform> platform);
    ~PlatformInfo();

    using StateUpdateFn = std::function<std::shared_ptr<SwitchState>(
        const std::shared_ptr<SwitchState>&)>;

    SaiSwitch* getHwSwitch();
    SaiPlatform* getPlatform() {
      return saiPlatform_.get();
    }
    void applyUpdate(folly::StringPiece name, StateUpdateFn fn);

    std::shared_ptr<SwitchState> getState() const {
      return *appliedStateDontUseDirectly_.rlock();
    }

    void seedStateFromHwSwitch();

   private:
    PlatformInfo(PlatformInfo&&) = delete;
    PlatformInfo& operator=(PlatformInfo&&) = delete;

    void setState(const std::shared_ptr<SwitchState>& newState);
    std::unique_ptr<SaiPlatform> saiPlatform_;
    // Don't hold locked access to SwitchState for long periods. Instead
    // Just access via set/getState apis, to allow for many readers just
    // accessing the COW SwitchState object w/o holding a lock.
    folly::Synchronized<std::shared_ptr<SwitchState>>
        appliedStateDontUseDirectly_;
    std::mutex updateMutex_;
  };
  PlatformInfo* getPlatformInfo(GlobalXphyID xphyID);
  void seedPlatformStateAfterWarmBoot(GlobalXphyID xphyID);
  const PlatformInfo* getPlatformInfo(GlobalXphyID xphyID) const {
    return const_cast<SaiPhyManager*>(this)->getPlatformInfo(xphyID);
  }
  PlatformInfo* getPlatformInfo(PortID portID);
  const PlatformInfo* getPlatformInfo(PortID portId) const {
    return const_cast<SaiPhyManager*>(this)->getPlatformInfo(portId);
  }
  MacsecStats getMacsecStats(const std::string& portName, bool readFromHw) {
    return readFromHw ? getMacsecStatsFromHw(portName)
                      : getMacsecStats(portName);
  }
  MacsecStats getMacsecStatsFromHw(const std::string& portName);
  MacsecStats getMacsecStats(const std::string& portName) const;
  // Forbidden copy constructor and assignment operator
  SaiPhyManager(SaiPhyManager const&) = delete;
  SaiPhyManager& operator=(SaiPhyManager const&) = delete;

  PortID getPortId(std::string portName) const;

  std::unique_ptr<ExternalPhyPortStatsUtils> createExternalPhyPortStats(
      PortID portID) override;

  virtual bool isPortCreateAllowed(
      GlobalXphyID /* globalXphyId */,
      const phy::PhyPortConfig& /* portCfg */) {
    return true;
  }

  void setSaiPortLoopbackState(
      PortID swPort,
      phy::PortComponent component,
      bool setLoopback);

  void setSaiPortAdminState(
      PortID swPort,
      phy::PortComponent component,
      bool setAdminUp);

  std::string getSaiPortInfo(PortID swPort);

  void collectXphyStats(GlobalXphyID xphyID, PlatformInfo* platformInfo);

  // Due to SaiPhyManager usually has more than one phy, and each phy has its
  // own SaiPlatform, which needs a local mac address. As local mac address
  // will be the same mac address for the running system, all these phys and
  // their Platforms will share the same local mac.
  // Therefore, to avoid calling the getLocalMacAddress() too frequently,
  // use a const private member to store the local mac once, and then pass this
  // mac address when creating each single SaiPlatform
  const folly::MacAddress localMac_;
  std::map<PimID, std::map<GlobalXphyID, std::unique_ptr<PlatformInfo>>>
      saiPlatforms_;

  // pim2OngoingStatsCollection_ needs to be synchronized because it's updated
  // in the updateAllXphyPortsStats loop and can be read from in the main
  // thread through isXphyStatsCollectionDone
  folly::Synchronized<
      std::map<PimID, std::optional<folly::Future<folly::Unit>>>>
      pim2OngoingStatsCollection_;
};

using namespace std::chrono;
template <typename platformT, typename xphychipT>
void SaiPhyManager::initializeSlotPhysImpl(PimID pimID, bool warmboot) {
  // Simply loop through all xphys in this PIM and initialize each one
  if (const auto pimPhyMap = xphyMap_.find(pimID);
      pimPhyMap != xphyMap_.end()) {
    for (const auto& phy : pimPhyMap->second) {
      initializeXphyImpl<platformT, xphychipT>(pimID, phy.first, warmboot);
    }
  }
}

template <typename platformT, typename xphychipT>
void SaiPhyManager::initializeXphyImpl(
    PimID pimID,
    GlobalXphyID xphyID,
    bool warmboot) {
  auto saiPlatform = static_cast<platformT*>(getSaiPlatform(xphyID));

  XLOG(DBG2) << "About to initialize phy of global phyId:" << xphyID;
  steady_clock::time_point begin = steady_clock::now();

  // Create xphy sai switch
  auto xphy = static_cast<xphychipT*>(getExternalPhy(xphyID));
  // Set xphy's customized switch attributes before calling init
  saiPlatform->setSwitchAttributes(xphy->getSwitchAttributes(warmboot));
  cfg::AgentConfig config;
  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = cfg::SwitchType::PHY;
  switchInfo.asicType() = getPhyAsicType();

  // For Agera3 (Ladakh platform), set larger portIdRange to support 2561 ports
  // (port IDs up to 2560). Other XPHY platforms (eg: Elbert) will use the
  // default range (0-2047).
  if (getPhyAsicType() == cfg::AsicType::ASIC_TYPE_AGERA3) {
    cfg::Range64 portIdRange;
    portIdRange.minimum() = kAgera3PortIdRangeMin;
    portIdRange.maximum() = kAgera3PortIdRangeMax;
    switchInfo.portIdRange() = portIdRange;

    XLOG(DBG2) << "Setting portIdRange=[" << kAgera3PortIdRangeMin << "-"
               << kAgera3PortIdRangeMax << "] for ASIC_TYPE_AGERA3";
  }

  config.sw()->switchSettings()->switchIdToSwitchInfo() = {
      std::make_pair(0, switchInfo)};
  saiPlatform->init(
      std::make_unique<AgentConfig>(config, ""),
      0 /* No switch featured needed */,
      0 /* switchIndex */);

  // Now call HwSwitch to create the switch object in hardware
  auto saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());

  auto initSaiSwitch =
      [pimID, saiSwitch, xphy](std::shared_ptr<SwitchState> statePrev) {
        try {
          saiSwitch->init(xphy, statePrev, true /* failHwCallsOnWarmboot */);
        } catch (const std::exception& ex) {
          XLOG(ERR) << "Failed to initialize SaiSwitch: pimID=" << pimID << ":"
                    << ex.what();
          throw;
        }
      };

  if (warmboot) {
    auto wbState = saiPlatform->getWarmBootHelper()->getWarmBootThriftState();
    auto statePrev =
        saiPlatform->getWarmBootHelper()->reconstructWarmBootThriftState(
            wbState);
    initSaiSwitch(statePrev);
    // TODO: seed Elbert too once Credo is validated.
    if (getPhyAsicType() != cfg::AsicType::ASIC_TYPE_ELBERT_8DD) {
      seedPlatformStateAfterWarmBoot(xphyID);
    }
  } else {
    initSaiSwitch(nullptr);
  }

  xphy->setSwitchId(saiSwitch->getSaiSwitchId());
  xphy->dump();
  XLOG(DBG2) << "Finished initializing phy of global phyId:" << xphyID
             << ", switchId:" << saiSwitch->getSaiSwitchId() << " took "
             << duration_cast<milliseconds>(steady_clock::now() - begin).count()
             << "ms";

  // After initializing the external PHY, set its hwswitch's runstate to
  // CONFIGURED
  saiPlatform->getHwSwitch()->switchRunStateChanged(SwitchRunState::CONFIGURED);
}
} // namespace facebook::fboss
