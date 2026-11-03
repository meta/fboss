// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/FbossInit.h"
#include "fboss/agent/HwSwitchHandler.h"

namespace facebook::fboss {

class HwSwitch;
class Platform;
class TxPacket;
class SwSwitch;

class MonolithicHwSwitchHandler : public HwSwitchHandler {
 public:
  MonolithicHwSwitchHandler(
      Platform* platform,
      const SwitchID& switchId,
      const cfg::SwitchInfo& info,
      SwSwitch* sw);

  virtual ~MonolithicHwSwitchHandler() override = default;

  void exitFatal() const;

  std::unique_ptr<TxPacket> allocatePacket(uint32_t size) const override;

  bool sendPacketOutOfPortAsync(
      std::unique_ptr<TxPacket> pkt,
      PortID portID,
      std::optional<uint8_t> queue = std::nullopt) noexcept override;

  bool sendPacketSwitchedSync(std::unique_ptr<TxPacket> pkt) noexcept override;

  bool sendPacketSwitchedAsync(std::unique_ptr<TxPacket> pkt) noexcept override;

  bool sendPacketOutOfPortSyncForPktType(
      std::unique_ptr<TxPacket> pkt,
      const PortID& portID,
      PacketType packetType) noexcept override;

  bool isValidStateUpdate(const StateDelta& delta) const override;

  void unregisterCallbacks();

  void gracefulExit();

  folly::dynamic toFollyDynamic() const;

  folly::F14FastMap<std::string, HwPortStats> getPortStats() const;

  std::map<std::string, HwSysPortStats> getSysPortStats() const;
  HwSwitchDropStats getSwitchDropStats() const;

  void updateStats();

  void updateAllPhyInfo();
  std::map<PortID, phy::PhyInfo> getAllPhyInfo() const;

  uint64_t getDeviceWatermarkBytes() const;

  HwFlowletStats getHwFlowletStats() const;

  HwSwitchFb303Stats* getSwitchStats() const;

  void clearPortStats(const std::unique_ptr<std::vector<int32_t>>& ports);

  std::vector<phy::PrbsLaneStats> getPortAsicPrbsStats(PortID portId);

  void clearPortAsicPrbsStats(PortID portId);

  std::vector<prbs::PrbsPolynomial> getPortPrbsPolynomials(int32_t portId);

  prbs::InterfacePrbsState getPortPrbsState(PortID portId);

  void switchRunStateChanged(SwitchRunState newState);

  std::vector<EcmpDetails> getAllEcmpDetails() const;

  cfg::SwitchingMode getFwdSwitchingMode(const RouteNextHopEntry&);

  // platform access apis
  void onHwInitialized(HwSwitchCallback* callback);

  std::pair<fsdb::OperDelta, HwSwitchStateUpdateStatus> stateChanged(
      const std::vector<fsdb::OperDelta>& deltas,
      bool transaction,
      const std::shared_ptr<SwitchState>& oldState,
      const std::shared_ptr<SwitchState>& newState,
      const HwWriteBehavior& hwWriteBehavior = HwWriteBehavior::WRITE,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior =
          std::nullopt) override;

  bool transactionsSupported(
      std::optional<cfg::SdkVersion> sdkVersion) const override;

  /* TODO: remove this method */
  HwSwitch* getHwSwitch() const {
    return hw_;
  }

  /* TODO: remove this method */
  Platform* getPlatform() const {
    return platform_;
  }

  CpuPortStats getCpuPortStats() const;

  std::map<PortID, FabricEndpoint> getFabricConnectivity() const override;

  FabricReachabilityStats getFabricReachabilityStats() const override;

  std::vector<PortID> getSwitchReachability(SwitchID switchId) const override;

  std::string getDebugDump() const;

  void setSdkRegDumpEnabled(bool enabled);

  void fetchL2Table(std::vector<L2EntryThrift>* l2Table, bool sdk = false)
      const;

  std::string listObjects(const std::vector<HwObjectType>& types, bool cached)
      const;

  bool needL2EntryForNeighbor(const cfg::SwitchConfig* config) const override;

  multiswitch::StateOperDelta getNextStateOperDelta(
      std::unique_ptr<multiswitch::StateOperDelta> prevOperResult,
      int64_t lastUpdateSeqNum) override;

  void notifyHwSwitchDisconnected() override;

  HwSwitchOperDeltaSyncState getHwSwitchOperDeltaSyncState() override {
    return HwSwitchOperDeltaSyncState::SYNC_CONNECTED;
  }

  SwitchRunState getHwSwitchRunState() override;

  void cancelOperDeltaSync() override {}

  AclStats getAclStats() const;

  HwSwitchWatermarkStats getSwitchWatermarkStats() const;

  HwSwitchPipelineStats getSwitchPipelineStats() const;

  HwSwitchTemperatureStats getSwitchTemperatureStats() const;

  HwSwitchHardResetStats getHwSwitchHardResetStats() const;

  void getHwStats(multiswitch::HwSwitchStats& hwStats) const;

  state::SwitchState reconstructSwitchState() override {
    throw FbossError(
        "reconstructSwitchState Not implemented in MultiSwitchHwSwitchHandler");
  }

  void initialStateApplied();

 private:
  Platform* platform_;
  HwSwitch* hw_;
};

} // namespace facebook::fboss
