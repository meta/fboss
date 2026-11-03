// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/FbossEventBase.h"
#include "fboss/agent/HwSwitchCallback.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/types.h"
#include "fboss/lib/HwWriteBehavior.h"

#include <folly/futures/Future.h>
#include "fboss/agent/if/gen-cpp2/MultiSwitchCtrl.h"

namespace facebook::fboss {

class StateDelta;
class TxPacket;
class SwitchStats;
class HwSwitchFb303Stats;

struct HwSwitchStateUpdate {
  HwSwitchStateUpdate(const std::vector<StateDelta>& deltas, bool transaction);
  std::shared_ptr<SwitchState> oldState;
  std::shared_ptr<SwitchState> newState;
  std::vector<fsdb::OperDelta> operDeltas = {};
  bool isTransaction;
};

enum HwSwitchStateUpdateStatus {
  HWSWITCH_STATE_UPDATE_SUCCEEDED,
  HWSWITCH_STATE_UPDATE_FAILED,
  HWSWITCH_STATE_UPDATE_CANCELLED,
};

enum HwSwitchOperDeltaSyncState {
  DISCONNECTED, /* initial state */
  INITIAL_SYNC_SENT, /* initial sync sent, waiting for ack */
  SYNC_CONNECTED, /* ready for incremental updates */
  CANCELLED, /* indicates client disconnecting or server graceful
                shutdown */
};

using HwSwitchStateUpdateResult =
    std::pair<std::shared_ptr<SwitchState>, HwSwitchStateUpdateStatus>;

using HwSwitchStateOperUpdateResult =
    std::pair<fsdb::OperDelta, HwSwitchStateUpdateStatus>;

class HwSwitchHandler {
 public:
  HwSwitchHandler(const SwitchID& switchId, const cfg::SwitchInfo& info);

  void start();

  void stop();

  virtual ~HwSwitchHandler();

  folly::Future<HwSwitchStateUpdateResult> stateChanged(
      HwSwitchStateUpdate update,
      const HwWriteBehavior& hwWriteBehavior = HwWriteBehavior::WRITE,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior =
          std::nullopt);

  virtual std::unique_ptr<TxPacket> allocatePacket(uint32_t size) const = 0;

  virtual bool sendPacketOutOfPortAsync(
      std::unique_ptr<TxPacket> pkt,
      PortID portID,
      std::optional<uint8_t> queue = std::nullopt) noexcept = 0;

  virtual bool sendPacketSwitchedSync(
      std::unique_ptr<TxPacket> pkt) noexcept = 0;

  virtual bool sendPacketSwitchedAsync(
      std::unique_ptr<TxPacket> pkt) noexcept = 0;

  virtual bool sendPacketOutOfPortSyncForPktType(
      std::unique_ptr<TxPacket> pkt,
      const PortID& portID,
      PacketType packetType) noexcept = 0;

  virtual bool transactionsSupported(
      std::optional<cfg::SdkVersion> sdkVersion) const = 0;

  virtual HwSwitchStateOperUpdateResult stateChanged(
      const std::vector<fsdb::OperDelta>& deltas,
      bool transaction,
      const std::shared_ptr<SwitchState>& oldState,
      const std::shared_ptr<SwitchState>& newState,
      const HwWriteBehavior& hwWriteBehavior = HwWriteBehavior::WRITE,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior =
          std::nullopt) = 0;

  virtual std::map<PortID, FabricEndpoint> getFabricConnectivity() const = 0;

  virtual FabricReachabilityStats getFabricReachabilityStats() const = 0;

  virtual std::vector<PortID> getSwitchReachability(
      SwitchID switchId) const = 0;

  virtual bool needL2EntryForNeighbor(
      const cfg::SwitchConfig* config) const = 0;

  virtual multiswitch::StateOperDelta getNextStateOperDelta(
      std::unique_ptr<multiswitch::StateOperDelta> prevOperResult,
      int64_t lastUpdateSeqNum) = 0;

  virtual void notifyHwSwitchDisconnected() = 0;

  SwitchID getSwitchId() const {
    return switchId_;
  }
  virtual HwSwitchOperDeltaSyncState getHwSwitchOperDeltaSyncState() = 0;

  virtual SwitchRunState getHwSwitchRunState() = 0;

  virtual void cancelOperDeltaSync() = 0;

  virtual state::SwitchState reconstructSwitchState() = 0;

  virtual bool isValidStateUpdate(const StateDelta& delta) const = 0;

  const cfg::SwitchInfo& getSwitchInfo() const {
    return info_;
  }

 protected:
  fsdb::OperDelta getFullSyncOperDelta(
      const std::shared_ptr<SwitchState>& state) const;

 private:
  HwSwitchStateUpdateResult stateChangedImpl(
      const HwSwitchStateUpdate& update,
      const HwWriteBehavior& hwWriteBehavior = HwWriteBehavior::WRITE,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior =
          std::nullopt);

  HwSwitchStateOperUpdateResult stateChangedImpl(
      const std::vector<fsdb::OperDelta>& deltas,
      bool transaction,
      const std::shared_ptr<SwitchState>& oldState,
      const std::shared_ptr<SwitchState>& newState,
      const HwWriteBehavior& hwWriteBehavior = HwWriteBehavior::WRITE,
      const std::optional<StateDeltaApplication>& deltaApplicationBehavior =
          std::nullopt);

  void run();

  const SwitchID switchId_;
  const cfg::SwitchInfo info_;
  FbossEventBase hwSwitchManagerEvb_{"HwSwitchManagerEvb"};
  std::unique_ptr<std::thread> hwSwitchManagerThread_;
  const OperDeltaFilter operDeltaFilter_;
};

} // namespace facebook::fboss
