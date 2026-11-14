// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <chrono>
#include <memory>

#include <folly/ScopeGuard.h>

#include "fboss/agent/MultiSwitchPacketStreamMap.h"
#include "fboss/agent/MultiSwitchThriftHandler.h"
#include "fboss/agent/SwRxPacket.h"
#include "fboss/agent/SwitchStats.h"

namespace facebook::fboss {

DEFINE_uint64(link_event_buffer_size, 1000, "Link eßvent buffer size");
DEFINE_uint64(stats_event_buffer_size, 10, "Stats event buffer size");
DEFINE_uint64(fdb_event_buffer_size, 100, "Fdb event buffer size");
DEFINE_uint64(rx_pkt_buffer_size, 1000, "Rx pkt buffer size");
DEFINE_uint64(
    switch_reachability_change_event_buffer_size,
    2,
    "Switch reachability change event buffer size");

void MultiSwitchThriftHandler::ensureConfigured(
    folly::StringPiece function) const {
  if (sw_->isFullyConfigured()) {
    return;
  }

  if (!function.empty()) {
    XLOG(DBG1) << "failing thrift prior to switch configuration: " << function;
  }
  throw FbossError(
      "switch is still initializing or is exiting and is not "
      "fully configured yet");
}

L2Entry MultiSwitchThriftHandler::getL2Entry(L2EntryThrift thriftEntry) {
  PortDescriptor port = thriftEntry.trunk()
      ? PortDescriptor(AggregatePortID(*thriftEntry.port()))
      : PortDescriptor(PortID(*thriftEntry.port()));
  std::optional<cfg::AclLookupClass> classID;
  if (thriftEntry.classID()) {
    classID = cfg::AclLookupClass(*thriftEntry.classID());
  }
  L2Entry::L2EntryType type =
      *thriftEntry.l2EntryType() == L2EntryType::L2_ENTRY_TYPE_PENDING
      ? L2Entry::L2EntryType::L2_ENTRY_TYPE_PENDING
      : L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED;
  return L2Entry(
      folly::MacAddress(*thriftEntry.mac()),
      VlanID(*thriftEntry.vlanID()),
      port,
      type,
      classID);
}

void MultiSwitchThriftHandler::processLinkState(
    SwitchID switchId,
    const multiswitch::LinkChangeEvent& linkChangeEvent) {
  if (!linkChangeEvent.linkStateEvent().has_value()) {
    return;
  }
  const auto& linkStateEvent = *linkChangeEvent.linkStateEvent();
  XLOG(DBG2) << "Got link event from switch " << switchId << " for port "
             << *linkStateEvent.port()
             << " up :" << (*linkStateEvent.up() ? "UP" : "DOWN");
  PortID portId = PortID(*linkStateEvent.port());
  cfg::PortType portType = *linkStateEvent.portType();
  std::optional<phy::LinkFaultStatus> faultStatus;
  std::optional<AggregatePortID> aggPortId;
  if (linkStateEvent.iPhyLinkFaultStatus()) {
    faultStatus = *linkStateEvent.iPhyLinkFaultStatus();
  }
  if (linkStateEvent.aggPortId()) {
    aggPortId = AggregatePortID(*linkStateEvent.aggPortId());
    XLOG(DBG2) << "link event change for agg port " << aggPortId.value();
  }
  sw_->linkStateChanged(
      portId, *linkStateEvent.up(), portType, faultStatus, aggPortId);
}

void MultiSwitchThriftHandler::processLinkActiveState(
    SwitchID switchId,
    const multiswitch::LinkChangeEvent& linkChangeEvent) {
  if (linkChangeEvent.linkActiveEvents()->port2IsActive()->size() == 0) {
    return;
  }
  XLOG(DBG3) << "Got link active event from switch " << switchId << " for : "
             << linkChangeEvent.linkActiveEvents()->port2IsActive()->size()
             << " ports";

  std::map<PortID, bool> port2IsActive;
  for (const auto& [portID, isActive] :
       *linkChangeEvent.linkActiveEvents()->port2IsActive()) {
    port2IsActive[PortID(portID)] = isActive;
  }

  auto fwIsolated = *linkChangeEvent.linkActiveEvents()->fwIsolated();

  std::optional<uint32_t> numActiveFabricPortsAtFwIsolate;
  if (linkChangeEvent.linkActiveEvents()->numActiveFabricPortsAtFwIsolate()) {
    numActiveFabricPortsAtFwIsolate =
        *linkChangeEvent.linkActiveEvents()->numActiveFabricPortsAtFwIsolate();
  }

  sw_->linkActiveStateChangedOrFwIsolated(
      port2IsActive, fwIsolated, numActiveFabricPortsAtFwIsolate);
}

void MultiSwitchThriftHandler::processLinkConnectivity(
    SwitchID switchId,
    const multiswitch::LinkChangeEvent& linkChangeEvent) {
  if (linkChangeEvent.linkConnectivityEvents()
          ->port2ConnectivityDelta()
          ->size() == 0) {
    return;
  }
  XLOG(DBG3) << "Got link connectivity change event from switch " << switchId
             << " for : "
             << linkChangeEvent.linkConnectivityEvents()
                    ->port2ConnectivityDelta()
                    ->size()
             << " ports";

  std::map<PortID, multiswitch::FabricConnectivityDelta> port2ConnectivityDelta;
  for (const auto& [portID, connectivityDelta] :
       *linkChangeEvent.linkConnectivityEvents()->port2ConnectivityDelta()) {
    port2ConnectivityDelta[PortID(portID)] = connectivityDelta;
  }
  sw_->linkConnectivityChanged(port2ConnectivityDelta);
}

void MultiSwitchThriftHandler::processLinkDisabledByFirmware(
    SwitchID switchId,
    const multiswitch::LinkChangeEvent& linkChangeEvent) {
  if (linkChangeEvent.linkFwDisableEvent()->portIds()->size() == 0) {
    return;
  }
  XLOG(DBG3) << "Got link disabled by firmware event from switch " << switchId
             << " for : "
             << linkChangeEvent.linkFwDisableEvent()->portIds()->size()
             << " ports";

  std::vector<int32_t> portIds(
      linkChangeEvent.linkFwDisableEvent()->portIds()->begin(),
      linkChangeEvent.linkFwDisableEvent()->portIds()->end());
  sw_->linkAdminStateChangedByFw(portIds);
}

void MultiSwitchThriftHandler::processSwitchReachabilityChangeEvent(
    SwitchID switchId,
    const multiswitch::SwitchReachabilityChangeEvent&
        switchReachabilityChangeEvent) {
  if (switchReachabilityChangeEvent.switchId2FabricPorts()->size() == 0) {
    return;
  }
  XLOG(DBG3) << "Got switch reachability change event from switch " << switchId;

  std::map<SwitchID, std::set<PortID>> switchId2FabricPortIds;
  for (const auto& [currentSwitchId, fabricPortIds] :
       *switchReachabilityChangeEvent.switchId2FabricPorts()) {
    switchId2FabricPortIds[SwitchID(currentSwitchId)] =
        std::set<PortID>(fabricPortIds.begin(), fabricPortIds.end());
  }
  sw_->switchReachabilityChanged(switchId, switchId2FabricPortIds);
}

#if FOLLY_HAS_COROUTINES
bool MultiSwitchThriftHandler::startEventSyncer() {
  std::lock_guard lock(eventSyncerMutex_);
  if (eventSyncersStopping_) {
    return false;
  }
  ++activeEventSyncers_;
  return true;
}

void MultiSwitchThriftHandler::finishEventSyncer() {
  std::lock_guard lock(eventSyncerMutex_);
  DCHECK_GT(activeEventSyncers_, 0);
  --activeEventSyncers_;
  if (activeEventSyncers_ == 0) {
    eventSyncerCv_.notify_all();
  }
}

folly::coro::Task<
    apache::thrift::SinkConsumer<multiswitch::LinkChangeEvent, bool>>
MultiSwitchThriftHandler::co_notifyLinkChangeEvent(int64_t switchId) {
  ensureConfigured(__func__);
  co_return apache::thrift::SinkConsumer<multiswitch::LinkChangeEvent, bool>{
      [this, switchId](
          folly::coro::AsyncGenerator<multiswitch::LinkChangeEvent&&> gen)
          -> folly::coro::Task<bool> {
        if (!startEventSyncer()) {
          co_return false;
        }
        SCOPE_EXIT {
          finishEventSyncer();
        };
        auto switchIndex = sw_->getSwitchInfoTable().getSwitchIndexFromSwitchId(
            SwitchID(switchId));
        sw_->stats()->hwAgentLinkEventSinkConnectionStatus(switchIndex, true);
        try {
          while (auto item = co_await folly::coro::co_withCancellation(
                     linkCancellationSource_.getToken(), gen.next())) {
            XLOG(DBG2) << "Got link change event from switch " << switchId;
            sw_->stats()->hwAgentLinkStatusReceived(switchIndex);
            switch (*item->eventType()) {
              case multiswitch::LinkChangeEventType::LINK_STATE:
                processLinkState(SwitchID(switchId), *item);
                break;
              case multiswitch::LinkChangeEventType::LINK_ACTIVE:
                processLinkActiveState(SwitchID(switchId), *item);
                break;
              case multiswitch::LinkChangeEventType::LINK_CONNECTIVITY:
                processLinkConnectivity(SwitchID(switchId), *item);
                break;
              case multiswitch::LinkChangeEventType::LINK_DISABLED_BY_FIRMWARE:
                processLinkDisabledByFirmware(SwitchID(switchId), *item);
                break;
              case multiswitch::LinkChangeEventType::INVALID:
                XLOG(ERR) << "Invalid link event type";
                break;
            }
          }
        } catch (const std::exception& e) {
          XLOG(DBG2) << "link change event sink cancelled for switch "
                     << switchId << " with exception " << e.what();
          sw_->stats()->hwAgentLinkEventSinkConnectionStatus(
              switchIndex, false);
          co_return false;
        }
        co_return true;
      },
      FLAGS_link_event_buffer_size}
      .setChunkTimeout(std::chrono::milliseconds(0));
}

folly::coro::Task<apache::thrift::SinkConsumer<
    multiswitch::SwitchReachabilityChangeEvent,
    bool>>
MultiSwitchThriftHandler::co_notifySwitchReachabilityChangeEvent(
    int64_t switchId) {
  ensureConfigured(__func__);
  co_return apache::thrift::SinkConsumer<
      multiswitch::SwitchReachabilityChangeEvent,
      bool>{
      [this, switchId](
          folly::coro::AsyncGenerator<
              multiswitch::SwitchReachabilityChangeEvent&&> gen)
          -> folly::coro::Task<bool> {
        if (!startEventSyncer()) {
          co_return false;
        }
        SCOPE_EXIT {
          finishEventSyncer();
        };
        auto switchIndex = sw_->getSwitchInfoTable().getSwitchIndexFromSwitchId(
            SwitchID(switchId));
        sw_->stats()->hwAgentSwitchReachabilityChangeEventSinkConnectionStatus(
            switchIndex, true);
        try {
          while (auto item = co_await folly::coro::co_withCancellation(
                     switchReachabilityCancellationSource_.getToken(),
                     gen.next())) {
            XLOG(DBG2) << "Got switch reachability change event from switch "
                       << switchId;
            sw_->stats()->hwAgentSwitchReachabilityChangeEventReceived(
                switchIndex);
            processSwitchReachabilityChangeEvent(SwitchID(switchId), *item);
          }
        } catch (const std::exception& e) {
          XLOG(DBG2)
              << "Switch reachability change event sink cancelled for switch "
              << switchId << " with exception " << e.what();
          sw_->stats()
              ->hwAgentSwitchReachabilityChangeEventSinkConnectionStatus(
                  switchIndex, false);
          co_return false;
        }
        co_return true;
      },
      FLAGS_switch_reachability_change_event_buffer_size}
      .setChunkTimeout(std::chrono::milliseconds(0));
}

folly::coro::Task<apache::thrift::SinkConsumer<multiswitch::FdbEvent, bool>>
MultiSwitchThriftHandler::co_notifyFdbEvent(int64_t switchId) {
  ensureConfigured(__func__);
  co_return apache::thrift::SinkConsumer<multiswitch::FdbEvent, bool>{
      [this, switchId](folly::coro::AsyncGenerator<multiswitch::FdbEvent&&> gen)
          -> folly::coro::Task<bool> {
        if (!startEventSyncer()) {
          co_return false;
        }
        SCOPE_EXIT {
          finishEventSyncer();
        };
        auto switchIndex = sw_->getSwitchInfoTable().getSwitchIndexFromSwitchId(
            SwitchID(switchId));
        sw_->stats()->hwAgentFdbEventSinkConnectionStatus(switchIndex, true);
        try {
          while (auto item = co_await folly::coro::co_withCancellation(
                     fdbCancellationSource_.getToken(), gen.next())) {
            XLOG(DBG3) << "Got fdb event from switch " << switchId
                       << " for port " << *item->entry()->port()
                       << " mac :" << *item->entry()->mac();
            sw_->stats()->hwAgentFdbEventReceived(switchIndex);
            auto l2Entry = getL2Entry(*item->entry());
            sw_->l2LearningUpdateReceived(l2Entry, *item->updateType());
          }
        } catch (const std::exception& e) {
          XLOG(DBG2) << "Fdb event sink cancelled for switch " << switchId
                     << " with exception " << e.what();
          sw_->stats()->hwAgentFdbEventSinkConnectionStatus(switchIndex, false);
          co_return false;
        }
        co_return true;
      },
      FLAGS_fdb_event_buffer_size}
      .setChunkTimeout(std::chrono::milliseconds(0));
}

folly::coro::Task<apache::thrift::SinkConsumer<multiswitch::RxPacket, bool>>
MultiSwitchThriftHandler::co_notifyRxPacket(int64_t switchId) {
  ensureConfigured(__func__);
  co_return apache::thrift::SinkConsumer<multiswitch::RxPacket, bool>{
      [this, switchId](folly::coro::AsyncGenerator<multiswitch::RxPacket&&> gen)
          -> folly::coro::Task<bool> {
        if (!startEventSyncer()) {
          co_return false;
        }
        SCOPE_EXIT {
          finishEventSyncer();
        };
        auto switchIndex = sw_->getSwitchInfoTable().getSwitchIndexFromSwitchId(
            SwitchID(switchId));
        sw_->stats()->hwAgentRxPktEventSinkConnectionStatus(switchIndex, true);
        try {
          while (auto item = co_await folly::coro::co_withCancellation(
                     rxPktCancellationSource_.getToken(), gen.next())) {
            XLOG(DBG4) << "Got rx packet from switch " << switchId
                       << " for port " << *item->port();
            sw_->stats()->hwAgentRxPktReceived(switchIndex);
            auto pkt = make_unique<SwRxPacket>(std::move(*item->data()));
            pkt->setSrcPort(PortID(*item->port()));
            if (item->vlan()) {
              pkt->setSrcVlan(VlanID(*item->vlan()));
            } else {
              // clear default vlan id(0)
              // TODO - retire this once the default value for vlan id is
              // removed
              pkt->setSrcVlan(std::nullopt);
            }
            if (item->aggPort()) {
              pkt->setSrcAggregatePort(AggregatePortID(*item->aggPort()));
            }
            if (item->cosQueue()) {
              pkt->setCosQueue(static_cast<uint8_t>(*item->cosQueue()));
            }
            if (item->packetType()) {
              pkt->setPacketType(*item->packetType());
            }
            // Agent pkt handling code assumes single buffer, so coalesce
            pkt->buf()->coalesce();
            if (*item->length() != pkt->buf()->length()) {
              XLOG(ERR) << "Rx packet length mismatch for switch " << switchId;
              sw_->stats()->hwAgentRxBadPktReceived(switchIndex);
              continue;
            }
            if (FLAGS_rx_sw_priority) {
              sw_->rxPacketReceived(std::move(pkt));
            } else {
              sw_->packetReceived(std::move(pkt));
            }
          }
        } catch (const std::exception& e) {
          XLOG(DBG2) << "Rx packet event sink cancelled for switch " << switchId
                     << " with exception " << e.what();
          sw_->stats()->hwAgentRxPktEventSinkConnectionStatus(
              switchIndex, false);
          co_return (false);
        }
        co_return true;
      },
      FLAGS_rx_pkt_buffer_size}
      .setChunkTimeout(std::chrono::milliseconds(0));
}

folly::coro::Task<apache::thrift::ServerStream<multiswitch::TxPacket>>
MultiSwitchThriftHandler::co_getTxPackets(int64_t switchId) {
  ensureConfigured(__func__);
  auto switchIndex =
      sw_->getSwitchInfoTable().getSwitchIndexFromSwitchId(SwitchID(switchId));
  auto streamAndPublisher =
      apache::thrift::ServerStream<multiswitch::TxPacket>::createPublisher(
          [this, switchId, switchIndex] {
            sw_->getPacketStreamMap()->removePacketStream(SwitchID(switchId));
            XLOG(DBG2) << "Removed stream for switch " << switchId;
            sw_->stats()->hwAgentTxPktEventStreamConnectionStatus(
                switchIndex, false);
            sw_->getHwSwitchHandler()->notifyHwSwitchDisconnected(
                switchId, false);
          });
  auto streamPublisher = std::make_unique<
      apache::thrift::ServerStreamPublisher<multiswitch::TxPacket>>(
      std::move(streamAndPublisher.second));
  sw_->getPacketStreamMap()->addPacketStream(
      SwitchID(switchId), std::move(streamPublisher));
  sw_->stats()->hwAgentTxPktEventStreamConnectionStatus(switchIndex, true);
  co_return std::move(streamAndPublisher.first);
}

folly::coro::Task<
    apache::thrift::SinkConsumer<multiswitch::HwSwitchStats, bool>>
MultiSwitchThriftHandler::co_syncHwStats(int16_t switchIndex) {
  ensureConfigured(__func__);
  co_return apache::thrift::SinkConsumer<multiswitch::HwSwitchStats, bool>{
      [this, switchIndex](
          folly::coro::AsyncGenerator<multiswitch::HwSwitchStats&&> gen)
          -> folly::coro::Task<bool> {
        if (!startEventSyncer()) {
          co_return false;
        }
        SCOPE_EXIT {
          finishEventSyncer();
        };
        sw_->stats()->hwAgentStatsEventSinkConnectionStatus(switchIndex, true);
        try {
          while (auto item = co_await folly::coro::co_withCancellation(
                     statsCancellationSource_.getToken(), gen.next())) {
            XLOG(DBG3) << "Got stats event from switchIndex " << switchIndex;
            sw_->updateHwSwitchStats(switchIndex, std::move(*item));
            sw_->stats()->hwAgentStatsReceived(switchIndex);
          }
        } catch (const std::exception& e) {
          XLOG(DBG2) << "Stats event sink cancelled for switchIndex "
                     << switchIndex << " with exception " << e.what();
          sw_->stats()->hwAgentStatsEventSinkConnectionStatus(
              switchIndex, false);
          co_return false;
        }
        co_return true;
      },
      FLAGS_stats_event_buffer_size};
}

#endif

void MultiSwitchThriftHandler::getNextStateOperDelta(
    multiswitch::StateOperDelta& operDelta,
    int64_t switchId,
    std::unique_ptr<multiswitch::StateOperDelta> prevOperResult,
    int64_t lastUpdateSeqNum) {
  operDelta = sw_->getHwSwitchHandler()->getNextStateOperDelta(
      switchId, std::move(prevOperResult), lastUpdateSeqNum);
}

void MultiSwitchThriftHandler::gracefulExit(int64_t switchId) {
  sw_->getHwSwitchHandler()->notifyHwSwitchGracefulExit(switchId);
}

#if FOLLY_HAS_COROUTINES
void MultiSwitchThriftHandler::cancelEventSyncers() {
  {
    std::lock_guard lock(eventSyncerMutex_);
    eventSyncersStopping_ = true;
  }
  XLOG(DBG2) << "Cancelling all the cancellation sources";
  linkCancellationSource_.requestCancellation();
  fdbCancellationSource_.requestCancellation();
  rxPktCancellationSource_.requestCancellation();
  statsCancellationSource_.requestCancellation();
  switchReachabilityCancellationSource_.requestCancellation();
}

void MultiSwitchThriftHandler::waitForEventSyncers() {
  std::unique_lock lock(eventSyncerMutex_);
  if (eventSyncerCv_.wait_for(lock, std::chrono::seconds(5), [this] {
        return activeEventSyncers_ == 0;
      })) {
    XLOG(DBG2) << "All event syncers drained";
    return;
  }
  XLOG(ERR) << "Timed out waiting for " << activeEventSyncers_
            << " event syncer(s) to finish; proceeding with shutdown anyway";
}
#endif

} // namespace facebook::fboss
