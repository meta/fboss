/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/HwSwitchEnsemble.h"
#include "fboss/agent/test/utils/PacketSendUtils.h"

#include "fboss/agent/AlpmUtils.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/EncapIndexAllocator.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/FileBasedWarmbootUtils.h"
#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/L2Entry.h"
#include "fboss/agent/Platform.h"
#include "fboss/agent/SwRxPacket.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwSwitchWarmBootHelper.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/TxPacketUtils.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwSwitchEnsembleRouteUpdateWrapper.h"
#include "fboss/agent/hw/test/StaticL2ForNeighborHwSwitchUpdater.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/LinkStateToggler.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"

#include <folly/executors/FunctionScheduler.h>
#include <folly/gen/Base.h>
#include <memory>
#include <utility>

DEFINE_bool(
    setup_thrift,
    false,
    "Setup a thrift handler. Primarily useful for inspecting HW state,"
    "say for debugging things via a shell");

DEFINE_int32(
    thrift_port,
    5909,
    "Port for thrift server to use (use with --setup_thrift");

DEFINE_bool(mmu_lossless_mode, false, "Enable mmu lossless mode");
DEFINE_bool(enable_exact_match, false, "enable init of exact match table");

using namespace std::chrono_literals;

namespace facebook::fboss {

class HwEnsembleMultiSwitchThriftHandler
    : public apache::thrift::ServiceHandler<multiswitch::MultiSwitchCtrl> {
 public:
  explicit HwEnsembleMultiSwitchThriftHandler(HwSwitchEnsemble* ensemble)
      : ensemble_(ensemble) {}

#if FOLLY_HAS_COROUTINES
  folly::coro::Task<
      apache::thrift::SinkConsumer<multiswitch::LinkChangeEvent, bool>>
  co_notifyLinkChangeEvent(int64_t switchId) override {
    co_return apache::thrift::SinkConsumer<multiswitch::LinkChangeEvent, bool>{
        [switchId,
         this](folly::coro::AsyncGenerator<multiswitch::LinkChangeEvent&&> gen)
            -> folly::coro::Task<bool> {
          while (auto item = co_await gen.next()) {
            if (item->linkStateEvent().has_value()) {
              const auto& linkEvent = *item->linkStateEvent();
              XLOG(DBG2) << "Got link state change event from switch "
                         << switchId << " for port " << *linkEvent.port()
                         << " up :" << *linkEvent.up();
              ensemble_->linkStateChanged(
                  PortID(*linkEvent.port()),
                  *linkEvent.up(),
                  *linkEvent.portType());
            }
          }
          co_return true;
        },
        1000 /* buffer size */
    };
  }

  folly::coro::Task<apache::thrift::SinkConsumer<multiswitch::FdbEvent, bool>>
  co_notifyFdbEvent(int64_t switchId) override {
    co_return apache::thrift::SinkConsumer<multiswitch::FdbEvent, bool>{
        [this,
         switchId](folly::coro::AsyncGenerator<multiswitch::FdbEvent&&> gen)
            -> folly::coro::Task<bool> {
          while (auto item = co_await gen.next()) {
            XLOG(DBG3) << "Got fdb event from switch " << switchId
                       << " for port " << *item->entry()->port()
                       << " mac :" << *item->entry()->mac();
            auto l2Entry = MultiSwitchThriftHandler::getL2Entry(*item->entry());
            ensemble_->l2LearningUpdateReceived(l2Entry, *item->updateType());
          }
          co_return true;
        },
        100 /* buffer size */
    };
  }

  folly::coro::Task<apache::thrift::SinkConsumer<multiswitch::RxPacket, bool>>
  co_notifyRxPacket(int64_t switchId) override {
    co_return apache::thrift::SinkConsumer<multiswitch::RxPacket, bool>{
        [this](folly::coro::AsyncGenerator<multiswitch::RxPacket&&> gen)
            -> folly::coro::Task<bool> {
          while (auto item = co_await gen.next()) {
            auto pkt = make_unique<SwRxPacket>(std::move(*item->data()));
            pkt->setSrcPort(PortID(*item->port()));
            if (item->vlan()) {
              pkt->setSrcVlan(VlanID(*item->vlan()));
            }
            if (item->aggPort()) {
              pkt->setSrcAggregatePort(AggregatePortID(*item->aggPort()));
            }
            ensemble_->packetReceived(std::move(pkt));
          }
          co_return true;
        },
        1000 /* buffer size */
    };
  }

  folly::coro::Task<apache::thrift::ServerStream<multiswitch::TxPacket>>
  co_getTxPackets(int64_t switchId) override {
    auto streamAndPublisher =
        apache::thrift::ServerStream<multiswitch::TxPacket>::createPublisher(
            [switchId] {
              XLOG(DBG2) << "Removed stream for switch " << switchId;
            });
    auto streamPublisher = std::make_unique<
        apache::thrift::ServerStreamPublisher<multiswitch::TxPacket>>(
        std::move(streamAndPublisher.second));
    // save publisher for serving the stream
    txStream_ = std::move(streamPublisher);
    // return stream to the client
    co_return std::move(streamAndPublisher.first);
  }

  folly::coro::Task<
      apache::thrift::SinkConsumer<multiswitch::HwSwitchStats, bool>>
  co_syncHwStats(int16_t switchIndex) override {
    co_return apache::thrift::SinkConsumer<multiswitch::HwSwitchStats, bool>{
        [switchIndex](

            folly::coro::AsyncGenerator<multiswitch::HwSwitchStats&&> gen)
            -> folly::coro::Task<bool> {
          while (auto item = co_await gen.next()) {
            XLOG(DBG3) << "Got stats event from switchIndex " << switchIndex;
          }
          co_return true;
        },
        100 /* buffer size */
    };
  }
#endif

  void enqueueTxPacket(multiswitch::TxPacket pkt) {
    txStream_->next(std::move(pkt));
  }

  void getNextStateOperDelta(
      multiswitch::StateOperDelta& operDelta,
      int64_t /*switchId*/,
      std::unique_ptr<multiswitch::StateOperDelta> /*prevOperResult*/,
      int64_t /*lastUpdateSeqNum*/) override {
    std::unique_lock<std::mutex> lk(operDeltaMutex_);
    if (!nextOperReady_) {
      operDeltaCV_.wait(
          lk, [this] { return nextOperReady_ || operDeltaCancelled_; });
    }
    if (nextOperReady_) {
      operDelta = nextOperDelta_;
      nextOperReady_ = false;
      return;
    }
    // return empty delta if cancelled
    operDelta.operDeltas() = {fsdb::OperDelta()};
    return;
  }

  void enqueueOperDelta(multiswitch::StateOperDelta delta) {
    {
      std::unique_lock<std::mutex> lk(operDeltaMutex_);
      if (nextOperReady_) {
        operDeltaCV_.wait(lk, [this] { return !nextOperReady_; });
      }
      nextOperDelta_ = std::move(delta);
      nextOperReady_ = true;
    }
    operDeltaCV_.notify_one();
  }

  void gracefulExit(int64_t /*switchId*/) override {
    std::unique_lock<std::mutex> lk(operDeltaMutex_);
    operDeltaCancelled_ = true;
    operDeltaCV_.notify_one();
  }

 private:
  HwSwitchEnsemble* ensemble_;
  multiswitch::StateOperDelta nextOperDelta_;
  bool nextOperReady_{false};
  bool operDeltaCancelled_{false};
  std::mutex operDeltaMutex_;
  std::condition_variable operDeltaCV_;

  std::unique_ptr<apache::thrift::ServerStreamPublisher<multiswitch::TxPacket>>
      txStream_{nullptr};
};

HwSwitchEnsemble::HwSwitchEnsemble(const Features& featuresDesired)
    : featuresDesired_(featuresDesired) {}

HwSwitchEnsemble::~HwSwitchEnsemble() {
  if (thriftSyncer_) {
    thriftSyncer_->stopOperDeltaSync();
    thriftSyncer_->stop();
  }
  if (swSwitchTestServer_) {
    swSwitchTestServer_.reset();
  }
  if (thriftThread_) {
    thriftThread_->join();
  }
  if (fs_) {
    fs_->shutdown();
  }
  if (hwAgent_ && getPlatform() && getHwSwitch() &&
      getHwSwitch()->getRunState() >= SwitchRunState::INITIALIZED) {
    if (getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::ROUTE_PROGRAMMING)) {
      auto minRouteState = getMinAlpmRouteState(getProgrammedState());
      applyNewState(minRouteState);
    }
    // Unregister callbacks before we start destroying hwSwitch
    getHwSwitch()->unregisterCallbacks();
  }
  if (routingInformationBase_) {
    routingInformationBase_->stop();
  }
  // HwSwitch is about to go away, stop observers to let them finish any
  // in flight events.
  stopObservers();
}

uint32_t HwSwitchEnsemble::getHwSwitchFeatures() const {
  uint32_t features{0};
  for (auto feature : featuresDesired_) {
    switch (feature) {
      case LINKSCAN:
        features |= HwSwitch::LINKSCAN_DESIRED;
        break;
      case PACKET_RX:
        features |= HwSwitch::PACKET_RX_DESIRED;
        break;
      case TAM_NOTIFY:
        features |= HwSwitch::TAM_EVENT_NOTIFY_DESIRED;
        break;
      case STATS_COLLECTION:
      case MULTISWITCH_THRIFT_SERVER:
        // No HwSwitch feture need to turned on.
        // Handled by HwSwitchEnsemble
        break;
    }
  }
  return features;
}

HwSwitch* HwSwitchEnsemble::getHwSwitch() {
  return getPlatform()->getHwSwitch();
}

std::shared_ptr<SwitchState> HwSwitchEnsemble::getProgrammedState() const {
  CHECK(programmedState_->isPublished());
  /*
   * Acquire mutex to guard against picking up a stale programmed
   * state. A state update maybe in progress. A common pattern in
   * tests is to get the current programmedState, make changes to
   * it and then call applyNewState. If a state update is in
   * progress when you query programmedState_, you risk undoing
   * the changes of ongoing state update.
   */
  std::lock_guard<std::mutex> lk(updateStateMutex_);
  return programmedState_;
}

std::shared_ptr<SwitchState> HwSwitchEnsemble::applyNewConfig(
    const cfg::SwitchConfig& config) {
  // Mimic SwSwitch::applyConfig() to modifyTransceiverMap
  auto originalState = getProgrammedState();
  auto overrideTcvrInfos = getPlatform()->getOverrideTransceiverInfos();
  if (overrideTcvrInfos) {
    const auto& currentTcvrs = *overrideTcvrInfos;
    auto tempState = SwSwitch::modifyTransceivers(
        getProgrammedState(),
        currentTcvrs,
        getPlatform()->getPlatformMapping(),
        scopeResolver_.get());
    if (tempState) {
      originalState = tempState;
    }
  } else {
    XLOG(WARN) << "Current platform doesn't have OverrideTransceiverInfos. "
               << "No need to build TransceiverMap";
  }

  if (routingInformationBase_) {
    auto routeUpdater = getRouteUpdater();
    applyNewState(applyThriftConfig(
        originalState,
        &config,
        getPlatform()->supportsAddRemovePort(),
        getPlatform()->getPlatformMapping(),
        hwAsicTable_.get(),
        &routeUpdater));
    routingInformationBase_->setEcmpWidth(getEcmpWidth(getProgrammedState()));
    currentConfig_ = config;
    routeUpdater.program();
    return getProgrammedState();
  }
  auto newState = applyNewState(applyThriftConfig(
      originalState,
      &config,
      getPlatform()->supportsAddRemovePort(),
      getPlatform()->getPlatformMapping(),
      hwAsicTable_.get()));
  currentConfig_ = config;
  return newState;
}

std::shared_ptr<SwitchState> HwSwitchEnsemble::updateEncapIndices(
    const std::shared_ptr<SwitchState>& in) const {
  // For wedge_agent we assign Encap indices on VOQ swtiches at
  // SwSwitch layer and later assert for their presence before
  // programming in HW.
  // For HW tests, since there is no SwSwitch layer we need to
  // explicitly assign and remove encap indices from neighbor entries
  StateDelta delta(programmedState_, in);
  return EncapIndexAllocator::updateEncapIndices(delta, *getAsic());
}

void HwSwitchEnsemble::applyNewState(
    StateUpdateFn fn,
    const std::string& /*name*/,
    bool rollbackOnHwOverflow) {
  applyNewState(fn(getProgrammedState()), rollbackOnHwOverflow);
}

std::shared_ptr<SwitchState> HwSwitchEnsemble::applyNewStateImpl(
    const std::shared_ptr<SwitchState>& newState,
    bool transaction,
    bool disableAppliedStateVerification) {
  if (!newState) {
    return programmedState_;
  }

  newState->publish();
  auto toApply = updateEncapIndices(newState);
  toApply->publish();
  StateDelta delta(programmedState_, toApply);
  auto appliedState = toApply;
  bool applyUpdateSuccess = true;
  {
    std::lock_guard<std::mutex> lk(updateStateMutex_);
    auto resultOperDelta = applyUpdate(delta.getOperDelta(), lk, transaction);
    applyUpdateSuccess = resultOperDelta.changes()->empty();
    // We are about to give up the lock, cache programmedState
    // applied by this function invocation
    if (applyUpdateSuccess) {
      programmedState_ = toApply;
    } else {
      programmedState_ = StateDelta(toApply, resultOperDelta).newState();
    }
    appliedState = programmedState_;
  }
  StaticL2ForNeighborHwSwitchUpdater updater(this);
  updater.stateUpdated(delta);
  if (!disableAppliedStateVerification && !applyUpdateSuccess) {
    throw FbossHwUpdateError(toApply, appliedState);
  }
  return appliedState;
}

fsdb::OperDelta HwSwitchEnsemble::applyUpdate(
    const fsdb::OperDelta& operDelta,
    const std::lock_guard<std::mutex>& /*lock*/,
    bool transaction) {
  std::vector<fsdb::OperDelta> operDeltas = {operDelta};
  auto resultOperDelta = transaction
      ? getHwSwitch()->stateChangedTransaction(operDeltas)
      : getHwSwitch()->stateChanged(operDeltas);
  return resultOperDelta;
}

void HwSwitchEnsemble::applyInitialConfig(const cfg::SwitchConfig& initCfg) {
  CHECK(haveFeature(HwSwitchEnsemble::LINKSCAN))
      << "Link scan feature must be enabled for exercising "
      << "applyInitialConfig";

  linkToggler_->applyInitialConfig(initCfg);
  /* link togglers apply config with ports down and bring ports up differently
   */
  currentConfig_ = initCfg;
}

void HwSwitchEnsemble::linkStateChanged(
    PortID port,
    bool up,
    cfg::PortType portType,
    std::optional<phy::LinkFaultStatus> /* iPhyFaultStatus */,
    std::optional<AggregatePortID> /* aggPortId */) {
  if (getHwSwitch()->getRunState() < SwitchRunState::INITIALIZED) {
    return;
  }
  linkToggler_->linkStateChanged(port, up);
  auto hwEventObservers = hwEventObservers_.rlock();
  std::for_each(
      hwEventObservers->begin(),
      hwEventObservers->end(),
      [port, up](auto observer) { observer->changeLinkState(port, up); });
}

void HwSwitchEnsemble::linkActiveStateChangedOrFwIsolated(
    const std::map<PortID, bool>& /*port2IsActive */,
    bool /* fwIsolated */,
    const std::optional<uint32_t>& /* numActiveFabricPortsAtFwIsolate */) {
  // TODO
}

void HwSwitchEnsemble::switchReachabilityChanged(
    const SwitchID /*switchId*/,
    const std::map<SwitchID, std::set<PortID>>& /*switchReachabilityInfo*/) {
  // TODO
}

void HwSwitchEnsemble::packetReceived(std::unique_ptr<RxPacket> pkt) noexcept {
  auto hwEventObservers = hwEventObservers_.rlock();
  std::for_each(
      hwEventObservers->begin(),
      hwEventObservers->end(),
      [&pkt](auto observer) { observer->receivePacket(pkt.get()); });
}

void HwSwitchEnsemble::l2LearningUpdateReceived(
    L2Entry l2Entry,
    L2EntryUpdateType l2EntryUpdateType) {
  auto hwEventObservers = hwEventObservers_.rlock();
  std::for_each(
      hwEventObservers->begin(),
      hwEventObservers->end(),
      [l2Entry, l2EntryUpdateType](auto observer) {
        observer->updateL2EntryState(l2Entry, l2EntryUpdateType);
      });
}

void HwSwitchEnsemble::addHwEventObserver(HwSwitchEventObserverIf* observer) {
  auto hwEventObservers = hwEventObservers_.wlock();
  if (!hwEventObservers->insert(observer).second) {
    throw FbossError("Observer was already added");
  }
}

void HwSwitchEnsemble::removeHwEventObserver(
    HwSwitchEventObserverIf* observer) {
  auto hwEventObservers = hwEventObservers_.wlock();
  if (!hwEventObservers->erase(observer)) {
    throw FbossError("Observer erase failed");
  }
}

void HwSwitchEnsemble::createEqualDistributedUplinkDownlinks(
    const std::vector<PortID>& /*ports*/,
    std::vector<PortID>& /*uplinks*/,
    std::vector<PortID>& /*downlinks*/,
    std::vector<PortID>& /*disabled*/,
    const int /*totalLinkCount*/) {
  throw FbossError("Needs to be implemented by the derived class");
}

bool HwSwitchEnsemble::ensureSendPacketSwitched(std::unique_ptr<TxPacket> pkt) {
  // lambda that returns HwPortStats for the given port(s)
  auto getPortStats =
      [&](const std::vector<PortID>& portIds) -> std::map<PortID, HwPortStats> {
    return getLatestPortStats(portIds);
  };
  auto getSysPortStats = [&](const std::vector<SystemPortID>& portIds)
      -> std::map<SystemPortID, HwSysPortStats> {
    return getLatestSysPortStats(portIds);
  };

  return utility::ensureSendPacketSwitched(
      this,
      std::move(pkt),
      masterLogicalPortIds({cfg::PortType::INTERFACE_PORT}),
      getPortStats,
      masterLogicalSysPortIds(),
      getSysPortStats);
}

bool HwSwitchEnsemble::ensureSendPacketOutOfPort(
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> queue) {
  // lambda that returns HwPortStats for the given port(s)
  auto getPortStats =
      [&](const std::vector<PortID>& portIds) -> std::map<PortID, HwPortStats> {
    return getLatestPortStats(portIds);
  };
  return utility::ensureSendPacketOutOfPort(
      this,
      std::move(pkt),
      portID,
      masterLogicalPortIds({cfg::PortType::INTERFACE_PORT}),
      getPortStats,
      queue);
}

bool HwSwitchEnsemble::waitPortStatsCondition(
    std::function<bool(const std::map<PortID, HwPortStats>&)> conditionFn,
    uint32_t retries,
    std::chrono::duration<uint32_t, std::milli> msBetweenRetry) {
  // lambda that returns HwPortStats for the given port(s)
  auto getPortStatsFn =
      [&](const std::vector<PortID>& portIds) -> std::map<PortID, HwPortStats> {
    return getLatestPortStats(portIds);
  };
  return utility::waitPortStatsCondition(
      conditionFn,
      masterLogicalPortIds(),
      retries,
      msBetweenRetry,
      getPortStatsFn);
}

bool HwSwitchEnsemble::waitStatsCondition(
    const std::function<bool()>& conditionFn,
    const std::function<void()>& updateStatsFn,
    uint32_t retries,
    const std::chrono::duration<uint32_t, std::milli> msBetweenRetry) {
  return utility::waitStatsCondition(
      conditionFn, updateStatsFn, retries, msBetweenRetry);
}

std::map<PortID, HwPortStats> HwSwitchEnsemble::getLatestPortStats(
    const std::vector<PortID>& ports) {
  std::map<PortID, HwPortStats> portIdStatsMap;
  getHwSwitch()->updateStats();

  auto swState = getProgrammedState();
  auto stats = getHwSwitch()->getPortStats();
  for (auto [portName, portStats] : stats) {
    auto portId = swState->getPorts()->getPort(portName)->getID();
    if (std::find(ports.begin(), ports.end(), (PortID)portId) == ports.end()) {
      continue;
    }
    portIdStatsMap.emplace((PortID)portId, portStats);
  }
  return portIdStatsMap;
}

std::map<InterfaceID, HwRouterInterfaceStats>
HwSwitchEnsemble::getLatestInterfaceStats(
    const std::vector<InterfaceID>& interfaces) {
  std::map<InterfaceID, HwRouterInterfaceStats> interfaceIdStatsMap;
  getHwSwitch()->updateStats();

  auto swState = getProgrammedState();
  auto stats = getHwSwitch()->getRouterInterfaceStats();
  for (auto intfID : interfaces) {
    auto intfName = swState->getInterfaces()->getNodeIf(intfID)->getName();
    auto intfStats = stats.find(intfName);
    if (intfStats == stats.end()) {
      XLOG(ERR) << "Interface " << intfName << " not found in stats";
      continue;
    }
    interfaceIdStatsMap.emplace(intfID, intfStats->second);
  }
  return interfaceIdStatsMap;
}

std::map<SystemPortID, HwSysPortStats> HwSwitchEnsemble::getLatestSysPortStats(
    const std::vector<SystemPortID>& ports) {
  std::map<SystemPortID, HwSysPortStats> portIdStatsMap;
  getHwSwitch()->updateStats();

  auto swState = getProgrammedState();
  auto stats = getHwSwitch()->getSysPortStats();
  for (auto [portStatName, sysPortStats] : stats) {
    // Sysport stats names are suffixed with _switchIndex. Remove that
    // to get at sys port name
    auto portName = portStatName.substr(0, portStatName.find_last_of('_'));
    SystemPortID portId;
    try {
      if (portName.find("cpu") != std::string::npos) {
        portId = 0;
      } else {
        portId = swState->getSystemPorts()->getSystemPort(portName)->getID();
      }
    } catch (const FbossError&) {
      // Look in remote sys ports if we couldn't find in local sys ports
      portId =
          swState->getRemoteSystemPorts()->getSystemPort(portName)->getID();
    }
    if (std::find(ports.begin(), ports.end(), portId) == ports.end()) {
      continue;
    }
    portIdStatsMap.emplace(portId, sysPortStats);
  }
  return portIdStatsMap;
}

HwTrunkStats HwSwitchEnsemble::getLatestAggregatePortStats(
    AggregatePortID aggregatePort) {
  return getLatestAggregatePortStats(
      std::vector<AggregatePortID>{aggregatePort})[aggregatePort];
}

void HwSwitchEnsemble::setupEnsemble(
    std::unique_ptr<HwAgent> hwAgent,
    std::unique_ptr<LinkStateToggler> linkToggler,
    std::unique_ptr<std::thread> thriftThread,
    const TestEnsembleInitInfo& initInfo) {
  hwAgent_ = std::move(hwAgent);
  linkToggler_ = std::move(linkToggler);
  auto asic = getPlatform()->getAsic();
  cfg::SwitchInfo switchInfo;
  switchInfo.switchType() = asic->getSwitchType();
  switchInfo.asicType() = asic->getAsicType();
  cfg::Range64 portIdRange;
  portIdRange.minimum() =
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN();
  portIdRange.maximum() = cfg::switch_config_constants::
      DEFAULT_DUAL_STAGE_3Q_2Q_PORT_ID_RANGE_MAX();
  switchInfo.portIdRange() = portIdRange;
  auto switchIdToSwitchInfo = std::map<int64_t, cfg::SwitchInfo>(
      {{asic->getSwitchId() ? *asic->getSwitchId() : 0, switchInfo}});
  // TODO pass agentConfig to setupEnsemble and populate switchIdToDsfNodes
  const std::map<int64_t, cfg::DsfNode> switchIdToDsfNodes;
  hwAsicTable_ = std::make_unique<HwAsicTable>(
      switchIdToSwitchInfo, std::nullopt, switchIdToDsfNodes);
  swSwitchWarmBootHelper_ = std::make_unique<SwSwitchWarmBootHelper>(
      getPlatform()->getDirectoryUtil(), hwAsicTable_.get());
  scopeResolver_ =
      std::make_unique<SwitchIdScopeResolver>(switchIdToSwitchInfo);
  if (haveFeature(MULTISWITCH_THRIFT_SERVER)) {
    std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>>
        handlers;
    auto multiSwitchHandler =
        std::make_shared<HwEnsembleMultiSwitchThriftHandler>(this);
    multiSwitchThriftHandler_ = multiSwitchHandler.get();
    handlers.emplace_back(multiSwitchHandler);
    swSwitchTestServer_ = std::make_unique<MultiSwitchTestServer>(handlers);
    XLOG(DBG2) << "Started thrift server on port "
               << swSwitchTestServer_->getPort();
    thriftSyncer_ = std::make_unique<SplitAgentThriftSyncer>(
        getPlatform()->getHwSwitch(),
        swSwitchTestServer_->getPort(),
        asic->getSwitchId() ? SwitchID(*asic->getSwitchId()) : SwitchID(0),
        0 /*switchIndex*/,
        std::nullopt /*multiSwitchStatsPrefix*/);
  }

  auto bootType =
      swSwitchWarmBootHelper_->canWarmBoot(
          false /*isRunModeMultiSwitch*/, nullptr /*hwSwitchThriftClientTable*/)
      ? BootType::WARM_BOOT
      : BootType::COLD_BOOT;
  std::optional<state::WarmbootState> wbState;
  if (bootType == BootType::WARM_BOOT) {
    wbState = swSwitchWarmBootHelper_->getWarmBootState();
  }
  auto [initState, rib] =
      reconstructStateAndRib(wbState, scopeResolver_->hasL3());
  routingInformationBase_ = std::move(rib);
  HwSwitchCallback* callback = haveFeature(MULTISWITCH_THRIFT_SERVER)
      ? static_cast<HwSwitchCallback*>(thriftSyncer_.get())
      : this;
  auto hwInitResult =
      getHwSwitch()->init(callback, initState, initInfo.failHwCallsOnWarmboot);
  if (hwInitResult.bootType != bootType) {
    // this is being done for preprod2trunk migration. further until tooling is
    // updated to affect both warm boot flags, HwSwitch will override SwSwitch
    // boot flag (for monolithic agent).
    auto bootStr = [](BootType type) {
      return type == BootType::WARM_BOOT ? "WARM_BOOT" : "COLD_BOOT";
    };
    XLOG(INFO) << "Overriding boot type from " << bootStr(bootType) << " to "
               << bootStr(hwInitResult.bootType);
    bootType = hwInitResult.bootType;
  }
  programmedState_ = initState->clone();
  if (bootType == BootType::WARM_BOOT) {
    auto settings =
        utility::getFirstNodeIf(programmedState_->getSwitchSettings());
    auto newSettings = settings->modify(&programmedState_);
    newSettings->setSwitchIdToSwitchInfo(switchIdToSwitchInfo);
  } else {
    /* setup scope info in switch settings */
    auto multiSwitchSwitchSettings = std::make_shared<MultiSwitchSettings>();
    auto switchSettings = std::make_shared<SwitchSettings>();
    switchSettings->setSwitchIdToSwitchInfo(
        scopeResolver_->switchIdToSwitchInfo());
    // this is supporting single ASIC (or switch only)
    for (auto& switchIdAndSwitchInfo : switchIdToSwitchInfo) {
      auto matcher = HwSwitchMatcher(
          std::unordered_set<SwitchID>(
              {static_cast<SwitchID>(switchIdAndSwitchInfo.first)}));
      multiSwitchSwitchSettings->addNode(
          matcher.matcherString(), switchSettings);
    }
    programmedState_->resetSwitchSettings(multiSwitchSwitchSettings);
  }
  // HwSwitch::init() returns an unpublished programmedState_.  SwSwitch is
  // normally responsible for publishing it.  Go ahead and call publish now.
  // This will catch errors if test cases accidentally try to modify this
  // programmedState_ without first cloning it.
  programmedState_->publish();
  StaticL2ForNeighborHwSwitchUpdater updater(this);
  updater.stateUpdated(
      StateDelta(std::make_shared<SwitchState>(), programmedState_));

  if (getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::ROUTE_PROGRAMMING)) {
    // ALPM requires that default routes be programmed
    // before any other routes. We handle that setup here. Similarly ALPM
    // requires that default routes be deleted last. That aspect is handled
    // in TearDown
    getRouteUpdater().programMinAlpmState();
  }

  thriftThread_ = std::move(thriftThread);
  if (haveFeature(MULTISWITCH_THRIFT_SERVER)) {
    thriftSyncer_->start();
  }
  switchRunStateChanged(SwitchRunState::INITIALIZED);
  if (routingInformationBase_) {
    auto curProgrammedState = programmedState_;
    // Unless there is mismatched state in RIB (i.e.
    // mismatched from FIB). A empty update should not
    // change switchState. Assert that post init. Most
    // interesting case here is of state diverging post WB
    getRouteUpdater().program();
    CHECK_EQ(curProgrammedState, getProgrammedState());
  }
  // Set ConfigFactory port to default profile id map
  utility::setPortToDefaultProfileIDMap(
      getProgrammedState()->getPorts(),
      getPlatform()->getPlatformMapping(),
      getPlatform()->getAsic(),
      getPlatform()->supportsAddRemovePort());
}

void HwSwitchEnsemble::switchRunStateChanged(SwitchRunState switchState) {
  getHwSwitch()->switchRunStateChanged(switchState);
  if (switchState == SwitchRunState::CONFIGURED &&
      haveFeature(STATS_COLLECTION)) {
    fs_ = std::make_unique<folly::FunctionScheduler>();
    fs_->setThreadName("UpdateStatsThread");
    auto statsCollect = [this] { getHwSwitch()->updateStats(); };
    auto timeInterval = std::chrono::seconds(1);
    fs_->addFunction(statsCollect, timeInterval, "updateStats");
    fs_->start();
  }
}

void HwSwitchEnsemble::enqueueTxPacket(multiswitch::TxPacket pkt) {
  multiSwitchThriftHandler_->enqueueTxPacket(std::move(pkt));
}

void HwSwitchEnsemble::enqueueOperDelta(multiswitch::StateOperDelta operDelta) {
  multiSwitchThriftHandler_->enqueueOperDelta(std::move(operDelta));
}

state::WarmbootState HwSwitchEnsemble::gracefulExitState() const {
  state::WarmbootState thriftSwitchState;

  // For RIB we employ a optmization to serialize only unresolved routes
  // and recover others from FIB
  if (routingInformationBase_) {
    // For RIB we employ a optmization to serialize only unresolved routes
    // and recover others from FIB
    thriftSwitchState.routeTables() = routingInformationBase_->warmBootState();
  }
  *thriftSwitchState.swSwitchState() = getProgrammedState()->toThrift();
  return thriftSwitchState;
}

void HwSwitchEnsemble::stopObservers() {
  auto hwEventObservers = hwEventObservers_.rlock();
  std::for_each(
      hwEventObservers->begin(), hwEventObservers->end(), [](auto observer) {
        observer->stopObserving();
      });
}

void HwSwitchEnsemble::gracefulExit() {
  if (thriftThread_) {
    // Join thrift thread. Thrift calls will fail post
    // warm boot exit sequence initiated below
    thriftThread_->join();
  }
  if (fs_) {
    fs_->shutdown();
  }
  // Initiate warm boot
  getHwSwitch()->unregisterCallbacks();
  stopObservers();
  auto thriftSwitchState = gracefulExitState();
  getHwSwitch()->gracefulExit();
  // store or dump sw switch state
  storeWarmBootState(thriftSwitchState);
}

uint64_t HwSwitchEnsemble::getTrafficRate(
    const HwPortStats& prevPortStats,
    const HwPortStats& curPortStats,
    const int secondsBetweenStatsCollection) {
  auto prevPortBytes = *prevPortStats.outBytes_();
  auto prevPortPackets =
      (*prevPortStats.outUnicastPkts_() + *prevPortStats.outMulticastPkts_() +
       *prevPortStats.outBroadcastPkts_());

  auto curPortPackets =
      (*curPortStats.outUnicastPkts_() + *curPortStats.outMulticastPkts_() +
       *curPortStats.outBroadcastPkts_());

  // 20 bytes are consumed by ethernet preamble, start of frame and
  // interpacket gap. Account for that in linerate.
  auto packetPaddingBytes = (curPortPackets - prevPortPackets) * 20;
  auto curPortBytes = *curPortStats.outBytes_() + packetPaddingBytes;
  auto rate = static_cast<uint64_t>((curPortBytes - prevPortBytes) * 8) /
      secondsBetweenStatsCollection;
  XLOG(DBG2) << ": Current rate " << rate << " bps " << " . curPortBytes "
             << curPortBytes << " prevPortBytes " << prevPortBytes
             << " curPortPackets " << curPortPackets << " prevPortPackets "
             << prevPortPackets;
  return rate;
}

/*
 * Wait for traffic on port to reach specified rate. If the
 * specified rate is reached, return true, else false.
 */
bool HwSwitchEnsemble::waitForRateOnPort(
    PortID port,
    const uint64_t desiredBps,
    int secondsToWaitPerIteration) {
  // Need to wait for atleast one second
  if (secondsToWaitPerIteration < 1) {
    secondsToWaitPerIteration = 1;
    XLOG(WARNING) << "Setting wait time to 1 second for tests!";
  }

  const auto portSpeedBps =
      static_cast<uint64_t>(
          programmedState_->getPorts()->getNodeIf(port)->getSpeed()) *
      1000 * 1000;
  if (desiredBps > portSpeedBps) {
    // Cannot achieve higher than line rate
    XLOG(ERR) << "Desired rate " << desiredBps << " bps is > port rate "
              << portSpeedBps << " bps!!";
    return false;
  }

  auto retries = 5;
  while (retries--) {
    const auto prevPortStats = getLatestPortStats(port);
    auto prevPortBytes = *prevPortStats.outBytes_();
    auto prevPortPackets =
        (*prevPortStats.outUnicastPkts_() + *prevPortStats.outMulticastPkts_() +
         *prevPortStats.outBroadcastPkts_());

    sleep(secondsToWaitPerIteration);
    const auto curPortStats = getLatestPortStats(port);
    auto curPortPackets =
        (*curPortStats.outUnicastPkts_() + *curPortStats.outMulticastPkts_() +
         *curPortStats.outBroadcastPkts_());

    // 20 bytes are consumed by ethernet preamble, start of frame and
    // interpacket gap. Account for that in linerate.
    auto packetPaddingBytes = (curPortPackets - prevPortPackets) * 20;
    auto curPortBytes = *curPortStats.outBytes_() + packetPaddingBytes;
    auto rate = static_cast<uint64_t>((curPortBytes - prevPortBytes) * 8) /
        secondsToWaitPerIteration;
    if (desiredBps == 0 && rate == desiredBps) {
      XLOG(DBG0) << "Expect no traffic: Current rate " << rate << " bps!";
      return true;
    } else if (desiredBps > 0 && rate >= desiredBps) {
      XLOG(DBG0) << ": Current rate " << rate << " bps!";
      return true;
    } else {
      XLOG(WARNING) << ": Current rate " << rate << " bps < expected rate "
                    << desiredBps << " bps. curPortBytes " << curPortBytes
                    << " prevPortBytes " << prevPortBytes << " curPortPackets "
                    << curPortPackets << " prevPortPackets " << prevPortPackets;
    }
  }
  return false;
}

void HwSwitchEnsemble::waitForLineRateOnPort(PortID port) {
  const auto portSpeedBps =
      static_cast<uint64_t>(
          programmedState_->getPorts()->getNodeIf(port)->getSpeed()) *
      1000 * 1000;
  if (waitForRateOnPort(port, portSpeedBps)) {
    // Traffic on port reached line rate!
    return;
  }
  throw FbossError("Line rate was never reached");
}

void HwSwitchEnsemble::waitForSpecificRateOnPort(
    PortID port,
    const uint64_t desiredBps,
    int secondsToWaitPerIteration) {
  if (waitForRateOnPort(port, desiredBps, secondsToWaitPerIteration)) {
    // Traffic on port reached desired rate!
    return;
  }

  throw FbossError("Desired rate ", desiredBps, " bps was never reached");
}

HwSwitchEnsemble::Features HwSwitchEnsemble::getAllFeatures() {
  return {
      HwSwitchEnsemble::LINKSCAN,
      HwSwitchEnsemble::PACKET_RX,
      HwSwitchEnsemble::STATS_COLLECTION};
}

void HwSwitchEnsemble::ensureThrift() {
  if (!thriftThread_) {
    thriftThread_ = setupThrift();
  }
}

void HwSwitchEnsemble::addOrUpdateCounter(
    const PortID& port,
    const bool deadlock) {
  auto& watchdogCounterMap =
      deadlock ? watchdogDeadlockCounter_ : watchdogRecoveryCounter_;
  auto iter = watchdogCounterMap.find(port);
  if (iter != watchdogCounterMap.end()) {
    (*iter).second += 1;
  } else {
    watchdogCounterMap.insert({port, 1});
  }
}

void HwSwitchEnsemble::pfcWatchdogStateChanged(
    const PortID& port,
    const bool deadlock) {
  addOrUpdateCounter(port, deadlock);
}

int HwSwitchEnsemble::readPfcDeadlockDetectionCounter(const PortID& port) {
  return readPfcWatchdogCounter(port, true);
}

int HwSwitchEnsemble::readPfcDeadlockRecoveryCounter(const PortID& port) {
  return readPfcWatchdogCounter(port, false);
}

int HwSwitchEnsemble::readPfcWatchdogCounter(
    const PortID& port,
    const bool deadlock) {
  auto& watchdogCounterMap =
      deadlock ? watchdogDeadlockCounter_ : watchdogRecoveryCounter_;
  auto iter = watchdogCounterMap.find(port);
  if (iter != watchdogCounterMap.end()) {
    return (*iter).second;
  }
  return 0;
}

void HwSwitchEnsemble::clearPfcDeadlockRecoveryCounter(const PortID& port) {
  clearPfcWatchdogCounter(port, false);
}

void HwSwitchEnsemble::clearPfcDeadlockDetectionCounter(const PortID& port) {
  clearPfcWatchdogCounter(port, true);
}

void HwSwitchEnsemble::clearPfcWatchdogCounter(
    const PortID& port,
    const bool deadlock) {
  auto& watchdogCounterMap =
      deadlock ? watchdogDeadlockCounter_ : watchdogRecoveryCounter_;
  auto iter = watchdogCounterMap.find(port);
  if (iter != watchdogCounterMap.end()) {
    watchdogCounterMap[port] = 0;
  }
}

const SwitchIdScopeResolver& HwSwitchEnsemble::scopeResolver() const {
  CHECK(scopeResolver_);
  return *scopeResolver_;
}

void HwSwitchEnsemble::storeWarmBootState(const state::WarmbootState& state) {
  swSwitchWarmBootHelper_->storeWarmBootState(state);
}
LinkStateToggler* HwSwitchEnsemble::getLinkToggler() {
  return linkToggler_.get();
}

void HwSwitchEnsemble::sendPacketAsync(
    std::unique_ptr<TxPacket> pkt,
    std::optional<PortDescriptor> portDescriptor,
    std::optional<uint8_t> queueId,
    std::optional<SwitchID> /* switchId */) {
  // switchId is unused on single-HwSwitch ensembles (only one switch).
  if (!portDescriptor.has_value()) {
    getHwSwitch()->sendPacketSwitchedSync(std::move(pkt));
    return;
  }
  if (!portDescriptor->isPhysicalPort()) {
    throw FbossError(
        "sendPacketAsync only supports physical ports, but got ",
        portDescriptor->str());
  }
  getHwSwitch()->sendPacketOutOfPortAsync(
      std::move(pkt), portDescriptor->phyPortID(), queueId);
}

std::unique_ptr<TxPacket> HwSwitchEnsemble::allocatePacket(uint32_t size) {
  return getHwSwitch()->allocatePacket(size);
}

std::optional<VlanID> HwSwitchEnsemble::getVlanIDForTx() const {
  auto intf = firstInterfaceWithPortsForTesting(getProgrammedState());
  return utility::getSwitchVlanIDForTx(getHwSwitch(), intf);
}
} // namespace facebook::fboss
