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

#include "fboss/agent/HwSwitchCallback.h"
#include "fboss/agent/L2Entry.h"
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/hw/sai/api/SwitchApi.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/if/gen-cpp2/multiswitch_ctrl_types.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/types.h"
#include "fboss/lib/bsp/BspPhyIO.h"
#include "fboss/lib/phy/ExternalPhy.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/mdio/Phy.h"

#include <folly/Conv.h>
#include <memory>

#include <mutex>

namespace facebook::fboss {
class SaiPlatform;
class StateObserver;
} // namespace facebook::fboss

namespace facebook::fboss::phy {

/**
 * A generic SAI-based retimer class that communicates with XPHY retimers
 */
class SaiPhyRetimer : public ExternalPhy, public HwSwitchCallback {
 public:
  SaiPhyRetimer(
      const GlobalXphyID& xphyID,
      phy::PhyAddress phyAddr,
      BspPhyIO* xphyIO,
      const PlatformMapping* platformMapping,
      SaiPlatform* platform)
      : xphyID_(xphyID),
        phyAddr_(phyAddr),
        name_(folly::to<std::string>("SaiPhyRetimer[", xphyID, "]")),
        platformMapping_(platformMapping),
        platform_(platform),
        xphyIO_(xphyIO) {}

  ~SaiPhyRetimer() override = default;

  void reset() override {
    // TODO: Implement reset via PhyResetPath
  }

  PhyFwVersion fwVersion() override {
    // TODO: Implement reset via PhyResetPath
    return fwVersionImpl();
  }

  Loopback getLoopback(Side /*side*/) override {
    return Loopback::OFF;
  }

  void setLoopback(Side /*side*/, Loopback /*loopback*/) override {}

  void setPortPrbs(
      Side side,
      const std::vector<LaneID>& lanes,
      const PortPrbsState& prbs) override {
    // Support prbs later on
  }

  PortPrbsState getPortPrbs(Side side, const std::vector<LaneID>& lanes)
      override {
    // Support prbs later on
    PortPrbsState state;
    return state;
  }

  ExternalPhyPortStats getPortStats(
      const std::vector<LaneID>& /* sysLanes */,
      const std::vector<LaneID>& /* lineLanes */) override {
    // Support prbs stats later on
    return ExternalPhyPortStats();
  }

  ExternalPhyPortStats getPortPrbsStats(
      const std::vector<LaneID>& /* sysLanes */,
      const std::vector<LaneID>& /* lineLanes */) override {
    // Support prbs stats later on
    return ExternalPhyPortStats();
  }

  PhyPortConfig getConfigOnePort(
      const std::vector<LaneID>& sysLanes,
      const std::vector<LaneID>& lineLanes,
      cfg::PortProfileID profileID,
      bool readFromHw = false) override;

  PhyInfo getPortInfo(
      const std::vector<LaneID>& sysLanes,
      const std::vector<LaneID>& lineLanes,
      PhyInfo& lastPhyInfo) override;

  void dump() override {
    dumpImpl();
  }

  bool isSupported(Feature feature) const override;

  SwitchSaiId getSwitchId() const {
    CHECK(switchId_);
    return *switchId_;
  }
  void setSwitchId(const SwitchSaiId& switchId) {
    switchId_ = switchId;
  }

  void programOnePort(PhyPortConfig /* config */, bool /* needResetDataPath */)
      override {}

  /*
   * HwSwitchCallback functions.
   * Right now, no-op since we won't have any callbacks from SaiPhyRetimer asic.
   */
  void packetReceived(std::unique_ptr<RxPacket> /* pkt */) noexcept override {}
  void linkStateChanged(
      PortID /* port */,
      bool /* up */,
      cfg::PortType /* portType */,
      std::optional<phy::LinkFaultStatus> /* iPhyFaultStatus */,
      std::optional<AggregatePortID> /*aggPortId*/) override {}
  void linkActiveStateChangedOrFwIsolated(
      const std::map<PortID, bool>& /*port2IsActive */,
      bool /* fwIsolated */,
      const std::optional<uint32_t>& /* numActiveFabricPortsAtFwIsolate */)
      override {}
  void linkAdminStateChangedByFw(
      const std::vector<int32_t>& /*fwDisabledPortIds*/) override {}
  void l2LearningUpdateReceived(
      L2Entry /* l2Entry */,
      L2EntryUpdateType /* l2EntryUpdateType */) override {}
  void switchReachabilityChanged(
      const SwitchID /*switchId*/,
      const std::map<SwitchID, std::set<PortID>>& /*switchReachabilityInfo*/)
      override {}
  void linkConnectivityChanged(
      const std::map<PortID, multiswitch::FabricConnectivityDelta>&
      /*port2OldAndNewConnectivity*/) override {}
  void pfcWatchdogStateChanged(
      const PortID& /* port */,
      const bool /* deadlock */) override {}
  void exitFatal() const noexcept override {}
  void registerStateObserver(
      StateObserver* /*observer*/,
      const std::string& /*name*/) override {}
  void unregisterStateObserver(StateObserver* /*observer*/) override {}

  BspPhyIO* getXphyIO() {
    return xphyIO_;
  }

  // Surface the MDIO IO counters the MdioController already records on each
  // register read/write.
  IOStats getIOStats() override {
    return getXphyIO()->getMdioController()->getIOStats();
  }

  phy::PhyAddress getPhyAddr() const {
    return phyAddr_;
  }

  static constexpr phy::Cl45DeviceAddress getDeviceAddress() {
    return kDeviceAddress;
  }

  void* getRegisterReadFuncPtr();
  void* getRegisterWriteFuncPtr();
  SaiSwitchTraits::CreateAttributes getSwitchAttributes(bool warmboot);

  std::mutex& getPaiMutex() {
    return paiMutex_;
  }

 protected:
  void dumpImpl() const;

  PhyFwVersion fwVersionImpl() const;

 private:
  static constexpr phy::Cl45DeviceAddress kDeviceAddress = 0x1;

  const GlobalXphyID xphyID_;
  const phy::PhyAddress phyAddr_;
  const std::string name_;
  const PlatformMapping* platformMapping_;
  SaiPlatform* platform_;
  BspPhyIO* xphyIO_;
  std::optional<SwitchSaiId> switchId_;
  std::mutex paiMutex_;
};

} // namespace facebook::fboss::phy
