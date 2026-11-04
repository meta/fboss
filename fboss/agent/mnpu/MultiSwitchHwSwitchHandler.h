#pragma once

#include "fboss/agent/HwSwitchHandler.h"
#include "fboss/agent/if/gen-cpp2/MultiSwitchCtrl.h"

DECLARE_int32(oper_delta_ack_timeout);

namespace facebook::fboss {

class SwSwitch;

class MultiSwitchHwSwitchHandler : public HwSwitchHandler {
 public:
  MultiSwitchHwSwitchHandler(
      const SwitchID& switchId,
      const cfg::SwitchInfo& info,
      SwSwitch* sw);

  virtual ~MultiSwitchHwSwitchHandler() override;

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

  bool transactionsSupported(
      std::optional<cfg::SdkVersion> sdkVersion) const override;

  std::pair<fsdb::OperDelta, HwSwitchStateUpdateStatus> stateChanged(
      std::vector<fsdb::OperDelta> deltas,
      bool transaction,
      const std::shared_ptr<SwitchState>& oldState,
      const std::shared_ptr<SwitchState>& newState,
      const HwWriteBehavior& hwWriteBehavior = HwWriteBehavior::WRITE,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior =
          std::nullopt) override;

  std::map<PortID, FabricEndpoint> getFabricConnectivity() const override;

  FabricReachabilityStats getFabricReachabilityStats() const override;

  std::vector<PortID> getSwitchReachability(SwitchID switchId) const override;

  bool needL2EntryForNeighbor(const cfg::SwitchConfig* config) const override;

  multiswitch::StateOperDelta getNextStateOperDelta(
      std::unique_ptr<multiswitch::StateOperDelta> prevOperResult,
      int64_t lastUpdateSeqNum) override;

  void notifyHwSwitchDisconnected() override;
  void cancelOperDeltaSync() override;
  HwSwitchOperDeltaSyncState getHwSwitchOperDeltaSyncState() override;

  bool sendPacketOutViaThriftStream(
      std::unique_ptr<TxPacket> pkt,
      std::optional<PortID> portID = std::nullopt,
      std::optional<uint8_t> queue = std::nullopt,
      std::optional<PacketType> packetType = std::nullopt);

  SwitchRunState getHwSwitchRunState() override;

  state::SwitchState reconstructSwitchState() override;

  bool isValidStateUpdate(const StateDelta& delta) const override;

 private:
  bool checkOperSyncStateLocked(
      HwSwitchOperDeltaSyncState state,
      const std::unique_lock<std::mutex>& /*lock*/) const;
  void setOperSyncStateLocked(
      HwSwitchOperDeltaSyncState state,
      const std::unique_lock<std::mutex>& /*lock*/) {
    operDeltaSyncState_ = state;
  }
  HwSwitchOperDeltaSyncState getOperSyncState() {
    return operDeltaSyncState_;
  }
  /* wait for ack. returns false if cancelled */
  bool waitForOperSyncAck(
      std::unique_lock<std::mutex>& lk,
      uint64_t timeoutInSec);
  /*
   * wait for oper delta to be ready from swswitch.
   * returns false if cancelled or timedout
   */
  bool waitForOperDeltaReady(
      std::unique_lock<std::mutex>& lk,
      uint64_t timeoutInSec);
  void fillMultiswitchOperDelta(
      multiswitch::StateOperDelta& stateDelta,
      const std::shared_ptr<SwitchState>& state,
      std::vector<fsdb::OperDelta> deltas,
      bool transaction,
      int64_t lastSeqNum,
      const HwWriteBehavior& hwWriteBehavior = HwWriteBehavior::WRITE,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior =
          std::nullopt);
  void operDeltaAckTimeout();

  SwSwitch* sw_;
  std::condition_variable stateUpdateCV_;
  std::mutex stateUpdateMutex_;
  multiswitch::StateOperDelta* nextOperDelta_{nullptr};
  multiswitch::StateOperDelta* prevOperDeltaResult_{nullptr};
  std::shared_ptr<SwitchState> prevUpdateSwitchState_{nullptr};
  HwSwitchOperDeltaSyncState operDeltaSyncState_{DISCONNECTED};
  int64_t currOperDeltaSeqNum_{0};
  int64_t lastAckedOperDeltaSeqNum_{-1};
  bool operRequestInProgress_{false};
  // pause state update till any waiting client request times out. This is
  // needed in cases where thrift abandons client connections due to high cpu or
  // memory load
  bool pauseStateUpdates_{false};
};

} // namespace facebook::fboss
