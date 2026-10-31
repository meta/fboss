/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/ThriftHandler.h"

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/ArpHandler.h"
#include "fboss/agent/DsfStateUpdaterUtil.h"
#include "fboss/agent/DsfSubscriber.h"
#include "fboss/agent/FabricLinkMonitoringManager.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/HwSwitchThriftClientTable.h"
#include "fboss/agent/IPv6Handler.h"
#include "fboss/agent/LabelFibUtils.h"
#include "fboss/agent/LinkAggregationManager.h"
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/RouteUpdateLogger.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwSwitchMySidUpdater.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/ThriftHandlerUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/capture/PktCapture.h"
#include "fboss/agent/capture/PktCaptureManager.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/hw/mock/MockRxPacket.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/rib/SwitchStateNextHopIdUpdater.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/AggregatePortMap.h"
#include "fboss/agent/state/ArpEntry.h"
#include "fboss/agent/state/ArpTable.h"
#include "fboss/agent/state/ClassBasedPolicyMap.h"
#include "fboss/agent/state/ClassBasedPolicyNode.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/LabelForwardingEntry.h"
#include "fboss/agent/state/NdpEntry.h"
#include "fboss/agent/state/NdpTable.h"
#include "fboss/agent/state/PbrUtils.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortQueue.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Transceiver.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"
#include "fboss/agent/state/Vrf.h"
#include "fboss/agent/state/VrfMap.h"
#include "fboss/lib/LogThriftCall.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"
#include "fboss/lib/phy/gen-cpp2/prbs_types.h"

#include <fb303/ServiceData.h>
#include <fmt/format.h>
#include <folly/ExceptionString.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/Range.h>
#include <folly/container/F14Map.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <limits>
#include <unordered_map>

using apache::thrift::ClientReceiveState;
using apache::thrift::server::TConnectionContext;
using facebook::fb303::cpp2::fb_status;
using folly::fbstring;
using folly::IOBuf;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using folly::StringPiece;
using folly::io::RWPrivateCursor;
using std::make_unique;
using std::map;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;
using std::chrono::duration_cast;
using std::chrono::seconds;
using std::chrono::steady_clock;

using facebook::network::toAddress;
using facebook::network::toBinaryAddress;
using facebook::network::toIPAddress;

using namespace facebook::fboss;

DEFINE_bool(
    allow_running_switch_state_mutations,
    false, // false => Prevents such mutations in prod
    "Allow mutations of running switch state by external thrift calls");

DECLARE_bool(enable_acl_table_group);

namespace facebook::fboss {

namespace util {

/**
 * Utility function to convert `Nexthops` (resolved ones) to list<BinaryAddress>
 */
std::vector<network::thrift::BinaryAddress> fromFwdNextHops(
    RouteNextHopSet const& nexthops) {
  std::vector<network::thrift::BinaryAddress> nhs;
  nhs.reserve(nexthops.size());
  for (auto const& nexthop : nexthops) {
    auto addr = network::toBinaryAddress(nexthop.addr());
    addr.ifName() = utility::createTunIntfName(nexthop.intf());
    nhs.emplace_back(std::move(addr));
  }
  return nhs;
}

} // namespace util

} // namespace facebook::fboss

namespace {

template <typename RouteT>
UnicastRoute toUnicastRoute(
    const std::shared_ptr<RouteT>& route,
    const RouteNextHopEntry& entry,
    std::vector<NextHopThrift> nextHops,
    std::vector<facebook::network::thrift::BinaryAddress> nextHopAddrs,
    std::optional<std::vector<NextHopThrift>> overrideNextHops = std::nullopt) {
  UnicastRoute tempRoute;
  tempRoute.dest()->ip() =
      facebook::network::toBinaryAddress(route->prefix().network());
  tempRoute.dest()->prefixLength() = route->prefix().mask();
  tempRoute.nextHops() = std::move(nextHops);
  tempRoute.nextHopAddrs() = std::move(nextHopAddrs);
  if (entry.getCounterID()) {
    tempRoute.counterID() = *entry.getCounterID();
  }
  if (auto classID = entry.getClassID()) {
    tempRoute.classID() = *classID;
  }
  if (auto overrideEcmpSwitchingMode = entry.getOverrideEcmpSwitchingMode()) {
    tempRoute.overrideEcmpSwitchingMode() = *overrideEcmpSwitchingMode;
  }
  if (auto namedDest = entry.getNamedRouteDestination()) {
    tempRoute.namedRouteDestination() = *namedDest;
  }
  if (overrideNextHops) {
    tempRoute.overrideNextHops() = std::move(*overrideNextHops);
  }
  return tempRoute;
}

template <typename RouteT>
RouteDetails toRouteDetails(
    const std::shared_ptr<SwitchState>& state,
    const std::shared_ptr<RouteT>& route,
    const ClientNextHopsResolver& resolveClient) {
  auto details = route->toRouteDetails(
      getNonOverrideNormalizedNextHops(state, route->getForwardInfo()),
      getNormalizedNextHops(state, route->getForwardInfo()),
      resolveClient);
  details.resolvedNextHops() = std::vector<NextHopThrift>();
  auto preNormalizationNextHops = getNextHops(state, route->getForwardInfo());
  const auto& bestEntry = route->getBestEntry().second;
  if (bestEntry) {
    auto clientNextHops = getClientNextHops(state, *bestEntry);
    if (!clientNextHops.empty() &&
        std::all_of(
            clientNextHops.begin(), clientNextHops.end(), [](const auto& nhop) {
              return nhop.isResolved();
            })) {
      preNormalizationNextHops = std::move(clientNextHops);
    }
  }
  for (const auto& nhop : preNormalizationNextHops) {
    details.resolvedNextHops()->push_back(nhop.toThrift());
  }
  return details;
}

void fillPortStats(
    const SwSwitch& sw,
    PortInfoThrift& portInfo,
    int numPortQs) {
  auto portId = *portInfo.portId();
  auto hwPortsStats = sw.getHwPortStats({PortID(portId)});
  if (hwPortsStats.empty()) {
    return;
  }
  const auto& hwPortStats = hwPortsStats.at(PortID(portId));
  portInfo.input()->bytes() = *hwPortStats.inBytes_();
  portInfo.input()->ucastPkts() = *hwPortStats.inUnicastPkts_();
  portInfo.input()->multicastPkts() = *hwPortStats.inMulticastPkts_();
  portInfo.input()->broadcastPkts() = *hwPortStats.inBroadcastPkts_();
  portInfo.input()->errors()->errors() = *hwPortStats.inErrors_();
  portInfo.input()->errors()->discards() = *hwPortStats.inDiscards_();

  portInfo.output()->bytes() = *hwPortStats.outBytes_();
  portInfo.output()->ucastPkts() = *hwPortStats.outUnicastPkts_();
  portInfo.output()->multicastPkts() = *hwPortStats.outMulticastPkts_();
  portInfo.output()->broadcastPkts() = *hwPortStats.outBroadcastPkts_();
  portInfo.output()->errors()->errors() = *hwPortStats.outErrors_();
  portInfo.output()->errors()->discards() = *hwPortStats.outDiscards_();
  if (auto cableLen = hwPortStats.cableLengthMeters()) {
    portInfo.cableLengthMeters() = *cableLen;
  }
  if (auto llrTxStatus = hwPortStats.llrTxStatus_()) {
    portInfo.llrTxStatus() = *llrTxStatus;
  }
  if (auto llrRxStatus = hwPortStats.llrRxStatus_()) {
    portInfo.llrRxStatus() = *llrRxStatus;
  }

  for (int16_t i = 0; i < numPortQs; i++) {
    QueueStats stats;
    if (hwPortStats.queueOutDiscardBytes_()->find(i) !=
        hwPortStats.queueOutDiscardBytes_()->end()) {
      *stats.congestionDiscards() = hwPortStats.queueOutDiscardBytes_()->at(i);
    } else {
      *stats.congestionDiscards() = 0;
    }

    if (hwPortStats.queueOutBytes_()->find(i) !=
        hwPortStats.queueOutBytes_()->end()) {
      *stats.outBytes() = hwPortStats.queueOutBytes_()->at(i);
    } else {
      *stats.outBytes() = 0;
    }
    portInfo.output()->unicast()->push_back(stats);
  }
}

void getPortInfoHelper(
    const SwSwitch& sw,
    PortInfoThrift& portInfo,
    const std::shared_ptr<Port> port) {
  *portInfo.portId() = port->getID();
  *portInfo.name() = port->getName();
  *portInfo.description() = port->getDescription();
  *portInfo.speedMbps() = static_cast<int>(port->getSpeed());
  for (auto entry : port->getVlans()) {
    portInfo.vlans()->push_back(entry.first);
  }
  if (auto id = sw.getHwLogicalPortId(port->getID())) {
    portInfo.hwLogicalPortId() = *id;
  }
  *portInfo.portType() = port->getPortType();
  *portInfo.scope() = port->getScope();

  std::shared_ptr<QosPolicy> qosPolicy;
  auto state = sw.getState();
  if (port->getQosPolicy().has_value()) {
    auto appliedPolicyName = port->getQosPolicy();
    qosPolicy =
        (state->getDefaultDataPlaneQosPolicy() &&
         *appliedPolicyName == state->getDefaultDataPlaneQosPolicy()->getName())
        ? state->getDefaultDataPlaneQosPolicy()
        : state->getQosPolicies()->getNodeIf(*appliedPolicyName);
    if (!qosPolicy) {
      throw std::runtime_error("qosPolicy state is null");
    }
  }

  auto switchIds = sw.getScopeResolver()->scope(port->getID()).switchIds();
  CHECK_EQ(switchIds.size(), 1);
  auto asic = sw.getHwAsicTable()->getHwAsicIf(*switchIds.begin());
  for (const auto& queue : std::as_const(*port->getPortQueues())) {
    PortQueueThrift pq;
    *pq.id() = queue->getID();
    *pq.mode() = apache::thrift::TEnumTraits<cfg::QueueScheduling>::findName(
        queue->getScheduling());
    if (queue->getScheduling() ==
        facebook::fboss::cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN) {
      pq.weight() = queue->getWeight();
    }
    if (queue->getReservedBytes()) {
      pq.reservedBytes() = queue->getReservedBytes().value();
    } else if (asic->isSupported(HwAsic::Feature::BUFFER_POOL)) {
      if (auto defaultReservedBytes = asic->getDefaultReservedBytes(
              queue->getStreamType(), port->getPortType())) {
        pq.reservedBytes() = *defaultReservedBytes;
      }
    }
    if (queue->getScalingFactor()) {
      pq.scalingFactor() =
          apache::thrift::TEnumTraits<cfg::MMUScalingFactor>::findName(
              queue->getScalingFactor().value());
    } else if (asic->isSupported(HwAsic::Feature::BUFFER_POOL)) {
      if (auto defaultScalingFactor =
              asic->getDefaultScalingFactor(queue->getStreamType(), false)) {
        pq.scalingFactor() =
            apache::thrift::TEnumTraits<cfg::MMUScalingFactor>::findName(
                defaultScalingFactor.value());
      }
    }
    if (const auto& aqms = queue->getAqms()) {
      std::vector<ActiveQueueManagement> aqmsThrift;
      for (const auto& aqm : std::as_const(*aqms)) {
        ActiveQueueManagement aqmThrift{};
        switch (aqm->cref<switch_config_tags::detection>()->type()) {
          case facebook::fboss::cfg::QueueCongestionDetection::Type::linear:
            aqmThrift.detection()->linear() = LinearQueueCongestionDetection();
            *aqmThrift.detection()->linear()->minimumLength() =
                aqm->cref<switch_config_tags::detection>()
                    ->cref<switch_config_tags::linear>()
                    ->cref<switch_config_tags::minimumLength>()
                    ->toThrift();
            *aqmThrift.detection()->linear()->maximumLength() =
                aqm->cref<switch_config_tags::detection>()
                    ->cref<switch_config_tags::linear>()
                    ->cref<switch_config_tags::maximumLength>()
                    ->toThrift();
            aqmThrift.detection()->linear()->probability() =
                aqm->cref<switch_config_tags::detection>()
                    ->cref<switch_config_tags::linear>()
                    ->cref<switch_config_tags::probability>()
                    ->toThrift();
            break;
          case facebook::fboss::cfg::QueueCongestionDetection::Type::__EMPTY__:
            XLOG(WARNING) << "Invalid queue congestion detection config";
            break;
        }
        *aqmThrift.behavior() = QueueCongestionBehavior(
            aqm->cref<switch_config_tags::behavior>()->cref());
        aqmsThrift.push_back(aqmThrift);
      }
      pq.aqms() = {};
      pq.aqms()->swap(aqmsThrift);
    }
    if (queue->getName()) {
      *pq.name() = queue->getName().value();
    }

    if (const auto& portQueueRate = queue->getPortQueueRate()) {
      if (portQueueRate->type() == cfg::PortQueueRate::Type::pktsPerSec) {
        Range range;
        range.minimum() = portQueueRate->cref<switch_config_tags::pktsPerSec>()
                              ->cref<switch_config_tags::minimum>()
                              ->cref();
        range.maximum() = portQueueRate->cref<switch_config_tags::pktsPerSec>()
                              ->cref<switch_config_tags::maximum>()
                              ->cref();
        PortQueueRate pqRate;
        pqRate.pktsPerSec() = range;

        pq.portQueueRate() = pqRate;
      } else if (
          portQueueRate->type() == cfg::PortQueueRate::Type::kbitsPerSec) {
        Range range;
        range.minimum() = portQueueRate->cref<switch_config_tags::kbitsPerSec>()
                              ->cref<switch_config_tags::minimum>()
                              ->cref();
        range.maximum() = portQueueRate->cref<switch_config_tags::kbitsPerSec>()
                              ->cref<switch_config_tags::maximum>()
                              ->cref();
        PortQueueRate pqRate;
        pqRate.kbitsPerSec() = range;

        pq.portQueueRate() = pqRate;
      }
    }

    if (queue->getBandwidthBurstMinKbits()) {
      pq.bandwidthBurstMinKbits() = queue->getBandwidthBurstMinKbits().value();
    }

    if (queue->getBandwidthBurstMaxKbits()) {
      pq.bandwidthBurstMaxKbits() = queue->getBandwidthBurstMaxKbits().value();
    }

    if (!port->getLookupClassesToDistributeTrafficOn().empty()) {
      // On MH-NIC setup, RSW downlinks implement queue-pe-host.
      // For such configurations traffic goes to queue corresponding
      // to host regardless of DSCP value
      auto kMaxDscp = 64;
      std::vector<signed char> dscps(kMaxDscp);
      std::iota(dscps.begin(), dscps.end(), 0);
      pq.dscps() = dscps;
    } else if (qosPolicy) {
      std::vector<signed char> dscps;
      auto& tcToDscp = qosPolicy->getDscpMap()->cref<switch_state_tags::from>();
      auto& tcToQueueId = qosPolicy->getTrafficClassToQueueId();
      FOLLY_PUSH_WARNING
      FOLLY_GCC_DISABLE_WARNING("-Wdangling-pointer")
      for (const auto& entry : std::as_const(*tcToDscp)) {
        auto& tc = entry->get<switch_state_tags::trafficClass>()->cref();
        auto& dscp = entry->get<switch_state_tags::attr>()->cref();
        if (tcToQueueId->at(tc)->cref() == queue->getID()) {
          dscps.push_back(dscp);
        }
      }
      FOLLY_POP_WARNING
      pq.dscps() = dscps;
    }

    portInfo.portQueues()->push_back(pq);
  }

  *portInfo.adminState() = PortAdminState(
      port->getAdminState() == facebook::fboss::cfg::PortState::ENABLED);
  *portInfo.operState() =
      PortOperState(port->getOperState() == Port::OperState::UP);

  if (port->getActiveState().has_value()) {
    portInfo.activeState() = PortActiveState(
        port->getActiveState().value() == Port::ActiveState::ACTIVE);
  }
  portInfo.activeErrors() = port->getActiveErrors();

  *portInfo.profileID() = apache::thrift::util::enumName(port->getProfileID());

  const auto pPort = sw.getPlatformMapping()->getPlatformPort(port->getID());
  if (pPort.mapping()->attachedCoreId().has_value()) {
    portInfo.coreId() = *pPort.mapping()->attachedCoreId();
  }
  if (pPort.mapping()->virtualDeviceId().has_value()) {
    portInfo.virtualDeviceId() = *pPort.mapping()->virtualDeviceId();
  }
  if (port->isEnabled()) {
    PortHardwareDetails hw;
    hw.profile() = port->getProfileID();
    auto matcher =
        PlatformPortProfileConfigMatcher(port->getProfileID(), port->getID());
    if (auto portProfileCfg =
            sw.getPlatformMapping()->getPortProfileConfig(matcher)) {
      hw.profileConfig() = *portProfileCfg;
    }
    hw.pinConfig() = sw.getPlatformMapping()->getPortXphyPinConfig(matcher);
    // Use SW Port pinConfig directly
    hw.pinConfig()->iphy() = port->getPinConfigs();
    hw.chips() = sw.getPlatformMapping()->getPortDataplaneChips(matcher);
    portInfo.hw() = hw;

    auto fec = hw.profileConfig()->iphy()->fec().value();
    portInfo.fecEnabled() = fec != phy::FecMode::NONE;
    portInfo.fecMode() = apache::thrift::util::enumName(fec);

    // Fill expected LLDP info
    auto lldpmap = port->getLLDPValidations();
    auto peerName = lldpmap.find(cfg::LLDPTag::SYSTEM_NAME);
    if (peerName != lldpmap.end()) {
      portInfo.expectedLLDPeerName() = peerName->second;
    }
    auto peerPort = lldpmap.find(cfg::LLDPTag::PORT);
    if (peerPort != lldpmap.end()) {
      portInfo.expectedLLDPPeerPort() = peerPort->second;
    }
  }

  auto pause = port->getPause();
  *portInfo.txPause() = *pause.tx();
  *portInfo.rxPause() = *pause.rx();

  if (port->getPfc().has_value()) {
    PfcConfig pc;
    auto pfc = port->getPfc();
    pc.tx() = *pfc->tx();
    pc.rx() = *pfc->rx();
    pc.watchdog() = pfc->watchdog().has_value();
    portInfo.pfc() = pc;
  }
  if (auto cbfcConfigName = port->getCbfcConfigName()) {
    CbfcInfo cbfc;
    cbfc.configName() = *cbfcConfigName;
    if (const auto& virtualChannels = port->getVirtualChannels()) {
      for (const auto& virtualChannel : *virtualChannels) {
        cbfc.virtualChannels()->push_back(
            virtualChannel->cref<switch_state_tags::id>()->cref());
      }
    }
    cbfc.senderCreditSize().from_optional(port->getCbfcSenderCreditSize());
    cbfc.senderPacketOverhead().from_optional(
        port->getCbfcSenderPacketOverhead());
    cbfc.senderCreditLimit().from_optional(port->getCbfcSenderCreditLimit());
    portInfo.cbfc() = std::move(cbfc);
  }
  try {
    portInfo.transceiverIdx() = sw.getTransceiverIdxThrift(port->getID());
  } catch (const facebook::fboss::FbossError&) {
    // No problem, we just don't set the other info
  }

  fillPortStats(sw, portInfo, portInfo.portQueues()->size());
  *portInfo.isDrained() =
      (port->getPortDrainState() ==
       facebook::fboss::cfg::PortDrainState::DRAINED);
  portInfo.expectedNeighborReachability() =
      port->getExpectedNeighborValues()->toThrift();
  if (auto userMetaData = port->getUserMetaData()) {
    portInfo.userMetaData() = *userMetaData;
  }
  if (auto ingressAclTableName = port->getIngressAclTableName()) {
    portInfo.ingressAclTableName() = *ingressAclTableName;
  }
}

LacpPortRateThrift fromLacpPortRate(facebook::fboss::cfg::LacpPortRate rate) {
  switch (rate) {
    case facebook::fboss::cfg::LacpPortRate::SLOW:
      return LacpPortRateThrift::SLOW;
    case facebook::fboss::cfg::LacpPortRate::FAST:
      return LacpPortRateThrift::FAST;
  }
  throw FbossError("Unknown LACP port rate: ", rate);
}

LacpPortActivityThrift fromLacpPortActivity(
    facebook::fboss::cfg::LacpPortActivity activity) {
  switch (activity) {
    case facebook::fboss::cfg::LacpPortActivity::ACTIVE:
      return LacpPortActivityThrift::ACTIVE;
    case facebook::fboss::cfg::LacpPortActivity::PASSIVE:
      return LacpPortActivityThrift::PASSIVE;
  }
  throw FbossError("Unknown LACP port activity: ", activity);
}

void populateAggregatePortThrift(
    const std::shared_ptr<AggregatePort>& aggregatePort,
    AggregatePortThrift& aggregatePortThrift) {
  *aggregatePortThrift.key() = static_cast<uint32_t>(aggregatePort->getID());
  *aggregatePortThrift.name() = aggregatePort->getName();
  *aggregatePortThrift.description() = aggregatePort->getDescription();
  *aggregatePortThrift.systemPriority() = aggregatePort->getSystemPriority();
  *aggregatePortThrift.systemID() = aggregatePort->getSystemID().toString();
  *aggregatePortThrift.minimumLinkCount() =
      aggregatePort->getMinimumLinkCount();
  *aggregatePortThrift.isUp() = aggregatePort->isUp();
  if (aggregatePort->getMinimumLinkCountToUp().has_value()) {
    aggregatePortThrift.minimumLinkCountToUp() =
        aggregatePort->getMinimumLinkCountToUp().value();
  }

  // Since aggregatePortThrift.memberPorts is being push_back'ed to, but is an
  // out parameter, make sure it's clear() first
  aggregatePortThrift.memberPorts()->clear();

  aggregatePortThrift.memberPorts()->reserve(aggregatePort->subportsCount());

  for (const auto& subport : aggregatePort->sortedSubports()) {
    bool isEnabled = aggregatePort->getForwardingState(subport.portID) ==
        AggregatePort::Forwarding::ENABLED;
    AggregatePortMemberThrift aggPortMember;
    *aggPortMember.memberPortID() = static_cast<int32_t>(subport.portID),
    *aggPortMember.isForwarding() = isEnabled,
    *aggPortMember.priority() = static_cast<int32_t>(subport.priority),
    *aggPortMember.rate() = fromLacpPortRate(subport.rate),
    *aggPortMember.activity() = fromLacpPortActivity(subport.activity);
    aggregatePortThrift.memberPorts()->push_back(aggPortMember);
  }
}

AclEntryThrift populateAclEntryThrift(const AclEntry& aclEntry) {
  AclEntryThrift aclEntryThrift;
  *aclEntryThrift.priority() = aclEntry.getPriority();
  *aclEntryThrift.name() = aclEntry.getID();
  *aclEntryThrift.srcIp() = toBinaryAddress(aclEntry.getSrcIp().first);
  *aclEntryThrift.srcIpPrefixLength() = aclEntry.getSrcIp().second;
  *aclEntryThrift.dstIp() = toBinaryAddress(aclEntry.getDstIp().first);
  *aclEntryThrift.dstIpPrefixLength() = aclEntry.getDstIp().second;
  switch (aclEntry.getActionType()) {
    case facebook::fboss::cfg::AclActionType::DENY:
      *aclEntryThrift.actionType() = "deny";
      break;
    case facebook::fboss::cfg::AclActionType::DENY_DATA_AND_CONTROL_PLANE:
      *aclEntryThrift.actionType() = "deny_data_and_control_plane";
      break;
    case facebook::fboss::cfg::AclActionType::PERMIT:
      *aclEntryThrift.actionType() = "permit";
      break;
  }
  if (aclEntry.getProto()) {
    aclEntryThrift.proto() = aclEntry.getProto().value();
  }
  if (aclEntry.getSrcPort()) {
    aclEntryThrift.srcPort() = aclEntry.getSrcPort().value();
  }
  if (aclEntry.getDstPort()) {
    aclEntryThrift.dstPort() = aclEntry.getDstPort().value();
  }
  if (aclEntry.getIcmpCode()) {
    aclEntryThrift.icmpCode() = aclEntry.getIcmpCode().value();
  }
  if (aclEntry.getIcmpType()) {
    aclEntryThrift.icmpType() = aclEntry.getIcmpType().value();
  }
  if (aclEntry.getDscp()) {
    aclEntryThrift.dscp() = aclEntry.getDscp().value();
  }
  if (aclEntry.getTtl()) {
    aclEntryThrift.ttl() = aclEntry.getTtl().value().getValue();
  }
  if (aclEntry.getL4SrcPort()) {
    aclEntryThrift.l4SrcPort() = aclEntry.getL4SrcPort().value();
  }
  if (aclEntry.getL4DstPort()) {
    aclEntryThrift.l4DstPort() = aclEntry.getL4DstPort().value();
  }
  if (aclEntry.getDstMac()) {
    aclEntryThrift.dstMac() = aclEntry.getDstMac().value().toString();
  }
  if (aclEntry.getUdfGroups()) {
    aclEntryThrift.udfGroups() = aclEntry.getUdfGroups().value();
  }
  if (aclEntry.isEnabled()) {
    aclEntryThrift.enabled() = aclEntry.isEnabled().value();
  }
  if (aclEntry.getLookupClassPort()) {
    aclEntryThrift.lookupClassPort() = aclEntry.getLookupClassPort().value();
  }
  return aclEntryThrift;
}

LinkNeighborThrift thriftLinkNeighbor(
    const SwSwitch& sw,
    const std::shared_ptr<LinkNeighbor>& n,
    steady_clock::time_point now) {
  LinkNeighborThrift tn;
  *tn.localPort() = n->getLocalPort();
  *tn.localVlan() = n->getLocalVlan();
  *tn.srcMac() = n->getMac().toString();
  *tn.chassisIdType() = static_cast<int32_t>(n->getChassisIdType());
  *tn.chassisId() = n->getChassisId();
  *tn.printableChassisId() = n->humanReadableChassisId();
  *tn.portIdType() = static_cast<int32_t>(n->getPortIdType());
  *tn.portId() = n->getPortId();
  *tn.printablePortId() = n->humanReadablePortId();
  *tn.originalTTL() = duration_cast<seconds>(n->getTTL()).count();
  *tn.ttlSecondsLeft() =
      duration_cast<seconds>(n->getExpirationTime() - now).count();
  if (!n->getSystemName().empty()) {
    tn.systemName() = n->getSystemName();
  }
  if (!n->getSystemDescription().empty()) {
    tn.systemDescription() = n->getSystemDescription();
  }
  if (!n->getPortDescription().empty()) {
    tn.portDescription() = n->getPortDescription();
  }
  const auto port = sw.getState()->getPorts()->getNodeIf(n->getLocalPort());
  if (port) {
    tn.localPortName() = port->getName();
  }
  return tn;
}
template <typename AddrT>
IpPrefix getIpPrefix(const Route<AddrT>& route) {
  IpPrefix pfx;
  pfx.ip() = toBinaryAddress(route.prefix().network());
  pfx.prefixLength() = route.prefix().mask();
  return pfx;
}

void translateToFibError(const FbossHwUpdateError& updError) {
  StateDelta delta(updError.appliedState, updError.desiredState);
  FbossFibUpdateError fibError;
  forEachChangedRoute(
      delta,
      [&](RouterID rid, const auto& removed, const auto& added) {
        if (!removed->isSame(added.get())) {
          fibError.vrf2failedAddUpdatePrefixes_ref()[rid].push_back(
              getIpPrefix(*added));
        }
      },
      [&](RouterID rid, const auto& added) {
        fibError.vrf2failedAddUpdatePrefixes_ref()[rid].push_back(
            getIpPrefix(*added));
      },
      [&](RouterID rid, const auto& removed) {
        fibError.vrf2failedDeletePrefixes_ref()[rid].push_back(
            getIpPrefix(*removed));
      });

  DeltaFunctions::forEachChanged(
      delta.getLabelForwardingInformationBaseDelta(),
      [&](const auto& removed, const auto& added) {
        if (!(added->isSame(removed.get()))) {
          fibError.failedAddUpdateMplsLabels_ref()->push_back(added->getID());
        }
      },
      [&](const auto& added) {
        fibError.failedAddUpdateMplsLabels_ref()->push_back(added->getID());
      },
      [&](const auto& removed) {
        fibError.failedDeleteMplsLabels_ref()->push_back(removed->getID());
      });
  throw fibError;
}

cfg::PortLoopbackMode toLoopbackMode(PortLoopbackMode mode) {
  switch (mode) {
    case PortLoopbackMode::NONE:
      return cfg::PortLoopbackMode::NONE;
    case PortLoopbackMode::MAC:
      return cfg::PortLoopbackMode::MAC;
    case PortLoopbackMode::PHY:
      return cfg::PortLoopbackMode::PHY;
    case PortLoopbackMode::NIF:
      return cfg::PortLoopbackMode::NIF;
  }
  throw FbossError("Bogus loopback mode: ", mode);
}
PortLoopbackMode toThriftLoopbackMode(cfg::PortLoopbackMode mode) {
  switch (mode) {
    case cfg::PortLoopbackMode::NONE:
      return PortLoopbackMode::NONE;
    case cfg::PortLoopbackMode::MAC:
      return PortLoopbackMode::MAC;
    case cfg::PortLoopbackMode::PHY:
      return PortLoopbackMode::PHY;
    case cfg::PortLoopbackMode::NIF:
      return PortLoopbackMode::NIF;
  }
  throw FbossError("Bogus loopback mode: ", mode);
}
template <typename AddressT, typename NeighborThriftT>
void addRecylePortRifNeighbors(
    const std::shared_ptr<SwitchState> state,
    std::vector<NeighborThriftT>& nbrs) {
  for (const auto& switchIdAndInfo :
       utility::getFirstNodeIf(state->getSwitchSettings())
           ->getSwitchIdToSwitchInfo()) {
    if (switchIdAndInfo.second.switchType() != cfg::SwitchType::VOQ) {
      continue;
    }
    auto switchId = SwitchID(switchIdAndInfo.first);
    auto inbandSysPortId = getInbandSystemPortID(state, switchId);
    auto dsfNode = state->getDsfNodes()->getNodeIf(switchId);
    if (!dsfNode || !dsfNode->getInbandPortId().has_value()) {
      throw FbossError("Unable to lookup inband port for : ", switchId);
    }
    auto localRecycleRifId =
        InterfaceID(static_cast<uint32_t>(inbandSysPortId));
    const auto& localRecycleRif =
        state->getInterfaces()->getNode(localRecycleRifId);
    const auto& nbrTable =
        std::as_const(*localRecycleRif->getNeighborEntryTable<AddressT>());
    for (const auto& ipAndEntry : nbrTable) {
      const auto& entry = ipAndEntry.second;
      NeighborThriftT nbrThrift;
      nbrThrift.ip() = facebook::network::toBinaryAddress(entry->getIP());
      nbrThrift.mac() = entry->getMac().toString();
      nbrThrift.port() = dsfNode->getInbandPortId().value();
      nbrThrift.portDescriptor()->portId() = dsfNode->getInbandPortId().value();
      nbrThrift.portDescriptor()->portType() =
          cfg::PortDescriptorType::Physical;

      nbrThrift.vlanName() = "--";
      nbrThrift.interfaceID() = static_cast<uint32_t>(localRecycleRifId);
      // Local recycle port for RIF, should always be STATIC
      CHECK(entry->getType() == state::NeighborEntryType::STATIC_ENTRY);
      nbrThrift.state() = "STATIC";
      nbrThrift.isLocal() = true;
      nbrThrift.switchId() = static_cast<int64_t>(switchId);
      nbrs.push_back(nbrThrift);
    }
  }
}

void validateAndDefaultSrv6NextHops(
    std::vector<NextHopThrift>& nextHops,
    const std::optional<std::string>& defaultSrv6TunnelId) {
  for (auto& nhop : nextHops) {
    if (nhop.mplsAction().has_value() && !nhop.srv6SegmentList()->empty()) {
      throw facebook::fboss::FbossError(
          "Next hop cannot have both mplsAction (label stack) and srv6SegmentList");
    }
    if (!nhop.srv6SegmentList()->empty()) {
      if (!nhop.tunnelType().has_value()) {
        nhop.tunnelType() = facebook::fboss::TunnelType::SRV6_ENCAP;
      } else if (
          *nhop.tunnelType() != facebook::fboss::TunnelType::SRV6_ENCAP) {
        throw facebook::fboss::FbossError(
            "Next hop with srv6SegmentList must have tunnelType SRV6_ENCAP");
      }
      if (!nhop.tunnelId().has_value()) {
        if (!defaultSrv6TunnelId.has_value()) {
          throw facebook::fboss::FbossError(
              "Next hop with srv6SegmentList requires a tunnelId, but no SRV6_ENCAP tunnel found in config");
        }
        nhop.tunnelId() = defaultSrv6TunnelId.value();
      }
    }
  }
}

void validateLinkLocalNextHopInterfaces(
    const std::vector<NextHopThrift>& nextHops,
    const std::shared_ptr<SwitchState>& state) {
  for (const auto& nhop : nextHops) {
    const auto& address = toIPAddress(*nhop.address());
    auto ifName = apache::thrift::get_pointer(nhop.address()->ifName());
    if (!ifName) {
      continue;
    }
    if (!(address.isV6() && address.isLinkLocal())) {
      throw FbossError(
          "Interface ",
          *ifName,
          " associated with a non link-local next hop ",
          address.str());
    }
    auto intfID = utility::getIDFromTunIntfName(*ifName);
    if (!state->getInterfaces()->getNodeIf(intfID)) {
      throw FbossError(
          "Interface ",
          intfID,
          " does not exist for link-local next hop ",
          address.str());
    }
  }
}

std::optional<std::string> getDefaultSrv6TunnelId(
    const facebook::fboss::cfg::SwitchConfig& config) {
  if (config.srv6Tunnels().has_value()) {
    for (const auto& tunnel : config.srv6Tunnels().value()) {
      if (*tunnel.tunnelType() == facebook::fboss::TunnelType::SRV6_ENCAP) {
        return *tunnel.srv6TunnelId();
      }
    }
  }
  return std::nullopt;
}

// Adjacency FRR is implemented only for uA MySIDs. Every other arm of the
// union must be rejected here: an unset union would otherwise reach the RIB
// as an empty update, which mutates nothing and reports success even though
// no protection was installed.
void ensureFrrProtectedObjectIsMySid(
    const facebook::fboss::FrrProtectedObject& protectedObject) {
  switch (protectedObject.getType()) {
    case facebook::fboss::FrrProtectedObject::Type::mySid:
      return;
    case facebook::fboss::FrrProtectedObject::Type::mplsLabel:
      throw facebook::fboss::FbossError(
          "Adj FRR protection not implemented for MPLS labels");
    case facebook::fboss::FrrProtectedObject::Type::__EMPTY__:
      throw facebook::fboss::FbossError(
          "FrrProtectedObject must specify the protected uA mySID");
  }
  throw facebook::fboss::FbossError(
      "Unknown FrrProtectedObject type ",
      static_cast<int>(protectedObject.getType()));
}

Policy toThriftPolicy(const std::shared_ptr<ClassBasedPolicyNode>& node) {
  ClassBasedPolicy cbf;
  cbf.name() = node->getID();
  cbf.defaultNexthopGroup() = *node->getDefaultNextHopGroup().name();
  for (const auto& [fc, named] : node->getClass2NextHopGroup()) {
    NextHopGroup nhg;
    nhg.name() = *named.name();
    cbf.class2NextHopGroup()[fc] = std::move(nhg);
  }
  Policy policy;
  policy.cbfPolicy() = std::move(cbf);
  return policy;
}

// NHGs referenced by a live PBR policy. Not counting the policy's default NHG,
// since that is counted through its route when the PBR policy is live.
std::unordered_set<NextHopSetId> nhgIdsReferencedByPbr(
    const std::shared_ptr<SwitchState>& state,
    const std::map<std::string, NextHopSetId>& nameToSetId) {
  std::unordered_set<NextHopSetId> ids;
  for (const auto& [_matcher, policyMap] :
       std::as_const(*state->getClassBasedPolicies())) {
    for (const auto& [_name, policy] : std::as_const(*policyMap)) {
      if (!policy->isReferenced()) {
        continue;
      }
      const auto defaultName = *policy->getDefaultNextHopGroup().name();
      for (const auto& [_fc, nhg] : policy->getClass2NextHopGroup()) {
        if (*nhg.name() == defaultName) {
          continue;
        }
        if (auto it = nameToSetId.find(*nhg.name()); it != nameToSetId.end()) {
          ids.insert(it->second);
        }
      }
    }
  }
  return ids;
}

void validateClassBasedPolicies(
    const SwSwitch* sw,
    const std::vector<ClassBasedPolicy>& policies) {
  if (sw->getScopeResolver()->hasMultipleSwitches()) {
    throw FbossError(
        "Class-based policies are not supported on a multi-ASIC switch");
  }
  if (!sw->getHwAsicTable()->isFeatureSupportedOnAllAsic(
          HwAsic::Feature::PBR_ACL)) {
    throw FbossError("Class-based policies are not supported on this platform");
  }

  for (const auto& policy : policies) {
    if (policy.name()->starts_with(kPbrTempAclEntryPrefix)) {
      throw FbossError(
          "Class-based policy name '",
          *policy.name(),
          "' cannot start with '",
          kPbrTempAclEntryPrefix,
          "'");
    }
    for (const auto& [forwardingClass, nhg] : *policy.class2NextHopGroup()) {
      if (!nhg.nexthops()->empty()) {
        throw FbossError(
            "Class-based policy '",
            *policy.name(),
            "' supplies inline next hops for traffic class ",
            apache::thrift::util::enumNameSafe(forwardingClass),
            "; a policy references a named next-hop group by name only");
      }
    }
  }
}
} // namespace

namespace facebook::fboss {

struct ThriftHandler::L3TrafficLoopStateStore {
  struct Loop {
    L3TrafficLoopStatus status;
    PortID portId;
    std::optional<AggregatePortID> aggregatePortId;
    InterfaceID interfaceId;
    IPAddressV6 qualificationAddress;
    IPAddressV6 peerNextHop;
    std::optional<MacAddress> peerMac;
    std::chrono::steady_clock::time_point expiresAt;
    bool removeNeighborOnCleanup{false};
    bool portTtlChanged{false};
    bool aggregateTtlChanged{false};
    bool neighborTtlChanged{false};
  };

  std::mutex operationMutex;
  folly::Synchronized<std::map<std::string, Loop>> loops;
};

namespace {

constexpr int32_t kL3TrafficLoopMinTimeoutSeconds{1};
constexpr int32_t kL3TrafficLoopMaxTimeoutSeconds{3600};
constexpr int16_t kL3TrafficLoopClientId =
    static_cast<int16_t>(ClientID::TE_AGENT);
constexpr auto kL3TrafficLoopNeighborUpdateTimeout = std::chrono::seconds(5);

void validateL3TrafficLoopTimeout(int32_t timeoutSeconds) {
  if (timeoutSeconds < kL3TrafficLoopMinTimeoutSeconds ||
      timeoutSeconds > kL3TrafficLoopMaxTimeoutSeconds) {
    throw FbossError(
        "L3 traffic loop timeout must be between ",
        kL3TrafficLoopMinTimeoutSeconds,
        " and ",
        kL3TrafficLoopMaxTimeoutSeconds,
        " seconds");
  }
}

std::shared_ptr<Port> findL3TrafficLoopPort(
    const std::shared_ptr<SwitchState>& state,
    const std::string& portName) {
  return state->getPorts()->getPortIf(portName);
}

std::shared_ptr<AggregatePort> findL3TrafficLoopAggregate(
    const std::shared_ptr<SwitchState>& state,
    PortID portId) {
  for (const auto& switchAndAggregatePorts :
       std::as_const(*state->getAggregatePorts())) {
    for (const auto& aggregatePortEntry :
         std::as_const(*switchAndAggregatePorts.second)) {
      if (aggregatePortEntry.second->isMemberPort(portId)) {
        return aggregatePortEntry.second;
      }
    }
  }
  return nullptr;
}

std::optional<HwAsic::Feature> getL3TrafficLoopTtlFeature(
    SwSwitch* sw,
    const std::shared_ptr<AggregatePort>& aggregatePort) {
  const auto asicTable = sw->getHwAsicTable();
  const auto feature = aggregatePort
      ? HwAsic::Feature::LAG_TTL_DECREMENT_DISABLE
      : HwAsic::Feature::PORT_TTL_DECREMENT_DISABLE;
  if (asicTable->isFeatureSupportedOnAnyAsic(feature)) {
    return feature;
  }
  if (asicTable->isFeatureSupportedOnAnyAsic(
          HwAsic::Feature::NEXTHOP_TTL_DECREMENT_DISABLE)) {
    return HwAsic::Feature::NEXTHOP_TTL_DECREMENT_DISABLE;
  }
  return std::nullopt;
}

std::optional<std::string> getL3TrafficLoopUnsupportedReason(
    const std::shared_ptr<Port>& port,
    const std::shared_ptr<AggregatePort>& aggregatePort,
    std::optional<HwAsic::Feature> ttlFeature) {
  if (port->getPortType() != cfg::PortType::INTERFACE_PORT) {
    return "L3 traffic loops require an interface port";
  }
  if (ttlFeature) {
    return std::nullopt;
  }
  return aggregatePort
      ? "Aggregate member loops require LAG or next-hop TTL control"
      : "Port loops require port or next-hop TTL control";
}

IpPrefix makeL3TrafficLoopPrefix(const IPAddressV6& address) {
  IpPrefix prefix;
  prefix.ip() = toBinaryAddress(address);
  prefix.prefixLength() = 128;
  return prefix;
}

UnicastRoute makeL3TrafficLoopRoute(
    const IPAddressV6& qualificationAddress,
    const std::optional<IPAddressV6>& peerNextHop,
    bool disableTtlDecrement) {
  UnicastRoute route;
  route.dest() = makeL3TrafficLoopPrefix(qualificationAddress);
  route.adminDistance() = AdminDistance::TE_AGENT;
  if (!peerNextHop) {
    // Teardown uses a DROP route to stop traffic before restoring TTL.
    route.action() = RouteForwardAction::DROP;
    return route;
  }

  route.action() = RouteForwardAction::NEXTHOPS;
  NextHopThrift nextHop;
  nextHop.address() = toBinaryAddress(*peerNextHop);
  if (disableTtlDecrement) {
    nextHop.disableTTLDecrement() = true;
  }
  route.nextHops()->push_back(std::move(nextHop));
  return route;
}

std::shared_ptr<NdpEntry> getL3TrafficLoopNeighbor(
    SwSwitch* sw,
    InterfaceID interfaceId,
    const IPAddressV6& peerNextHop) {
  const auto interface =
      sw->getState()->getInterfaces()->getNodeIf(interfaceId);
  if (!interface) {
    return nullptr;
  }
  return interface->getNdpTable()->getEntryIf(peerNextHop);
}

void setL3TrafficLoopNeighborTtl(
    SwSwitch* sw,
    InterfaceID interfaceId,
    const IPAddressV6& peerNextHop,
    std::optional<bool> disableTtlDecrement) {
  sw->updateStateBlocking(
      "update L3 traffic loop neighbor TTL decrement",
      [interfaceId, peerNextHop, disableTtlDecrement](
          const std::shared_ptr<SwitchState>& state) {
        auto newState = state;
        auto oldInterface = newState->getInterfaces()->getNodeIf(interfaceId);
        if (!oldInterface) {
          throw FbossError("No interface ", interfaceId, " for neighbor");
        }
        auto interface = oldInterface->modify(&newState);
        auto ndpTable =
            interface->getNdpTable()->modify(interfaceId, &newState);
        auto oldEntry = ndpTable->getEntryIf(peerNextHop);
        if (!oldEntry) {
          throw FbossError(
              "No NDP neighbor ", peerNextHop, " on interface ", interfaceId);
        }
        auto newEntry = oldEntry->clone();
        newEntry->setDisableTTLDecrement(disableTtlDecrement);
        ndpTable->updateNode(newEntry);
        return newState;
      });
}

} // namespace

ThriftHandler::ThriftHandler(SwSwitch* sw)
    : FacebookBase2("FBOSS"),
      sw_(sw),
      l3TrafficLoopState_(std::make_unique<L3TrafficLoopStateStore>()) {}

ThriftHandler::~ThriftHandler() = default;

void ThriftHandler::setupL3TrafficLoopImpl(
    const BinaryAddress& qualificationAddressArg,
    const std::string& portName,
    const BinaryAddress& peerNextHopArg,
    const std::string& expectedPeerMac,
    int32_t timeoutSeconds) {
  const auto qualificationAddress = toIPAddress(qualificationAddressArg);
  if (!qualificationAddress.isV6()) {
    throw FbossError("L3 traffic loop qualification address must be IPv6");
  }
  const auto peerNextHop = toIPAddress(peerNextHopArg);
  if (!peerNextHop.isV6() || peerNextHop == qualificationAddress) {
    throw FbossError(
        "L3 traffic loop peer next hop must be a distinct IPv6 address");
  }
  validateL3TrafficLoopTimeout(timeoutSeconds);

  const auto state = sw_->getState();
  const auto port = findL3TrafficLoopPort(state, portName);
  if (!port) {
    throw FbossError("no such port ", portName);
  }
  const auto portId = port->getID();
  const auto aggregatePort = findL3TrafficLoopAggregate(state, portId);
  const auto ttlFeature = getL3TrafficLoopTtlFeature(sw_, aggregatePort);
  const auto unsupportedReason =
      getL3TrafficLoopUnsupportedReason(port, aggregatePort, ttlFeature);
  if (unsupportedReason) {
    throw FbossError(*unsupportedReason);
  }
  const auto interfaceId = port->getInterfaceID();
  const auto interface = state->getInterfaces()->getNodeIf(interfaceId);
  if (!interface) {
    throw FbossError("No routed interface found for port ", portName);
  }
  // Use LAG control for members, port control for standalone ports, then
  // next-hop control if the selected LAG or port has no TTL control.
  // The LAG strategy is selected only when aggregatePort is present.
  const bool useLagTtlDisable =
      ttlFeature == HwAsic::Feature::LAG_TTL_DECREMENT_DISABLE;
  const bool usePortTtlDisable =
      ttlFeature == HwAsic::Feature::PORT_TTL_DECREMENT_DISABLE;
  const bool useNextHopTtlDisable =
      ttlFeature == HwAsic::Feature::NEXTHOP_TTL_DECREMENT_DISABLE;
  XLOG(INFO) << "L3 traffic loop TTL strategy on " << portName
             << ": aggregateMember=" << static_cast<bool>(aggregatePort)
             << ", lagTtlDisable=" << useLagTtlDisable
             << ", nextHopTtlDisable=" << useNextHopTtlDisable
             << ", portTtlDisable=" << usePortTtlDisable;

  auto peerMac = std::optional<MacAddress>{};
  if (!expectedPeerMac.empty()) {
    auto parsedMac = MacAddress::tryFromString(expectedPeerMac);
    if (!parsedMac.hasValue()) {
      throw FbossError("Invalid expected peer MAC ", expectedPeerMac);
    }
    peerMac = parsedMac.value();
  }
  auto neighbor = interface->getNdpTable()->getEntryIf(peerNextHop.asV6());
  if (neighbor && !neighbor->isPending()) {
    if (peerMac && neighbor->getMac() != *peerMac) {
      throw FbossError(
          "Existing NDP neighbor MAC does not match expected peer MAC for ",
          peerNextHop);
    }
    if (!peerMac) {
      peerMac = neighbor->getMac();
    }
  } else if (!peerMac) {
    throw FbossError(
        "L3 traffic loop requires a resolved peer neighbor; supply expectedPeerMac");
  }

  // A pending neighbor still has a configured TTL setting to preserve.
  if ((useLagTtlDisable &&
       aggregatePort->getTTLDisableDecrement().value_or(false)) ||
      (usePortTtlDisable && port->getTTLDisableDecrement().value_or(false)) ||
      (useNextHopTtlDisable && neighbor &&
       neighbor->getDisableTTLDecrement().value_or(false))) {
    throw FbossError("TTL decrement is already disabled on ", portName);
  }

  L3TrafficLoopStateStore::Loop loop;
  loop.portId = portId;
  if (aggregatePort) {
    loop.aggregatePortId = aggregatePort->getID();
  }
  loop.interfaceId = interfaceId;
  loop.qualificationAddress = qualificationAddress.asV6();
  loop.peerNextHop = peerNextHop.asV6();
  loop.peerMac = peerMac;
  loop.expiresAt =
      std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
  loop.status.state() = L3TrafficLoopState::CONFIGURING;
  loop.status.expirationTimeMs() =
      duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch() +
          std::chrono::seconds(timeoutSeconds))
          .count();

  {
    auto loops = l3TrafficLoopState_->loops.wlock();
    auto existing = loops->find(portName);
    if (existing != loops->end() &&
        existing->second.status.state() != L3TrafficLoopState::NOT_CONFIGURED) {
      throw FbossError("L3 traffic loop already exists on port ", portName);
    }
    for (const auto& [existingPort, existingLoop] : *loops) {
      if (existingLoop.status.state() == L3TrafficLoopState::NOT_CONFIGURED) {
        continue;
      }
      // A port can be renamed while its loop is active, so also compare IDs.
      if (existingLoop.portId == loop.portId) {
        throw FbossError(
            "L3 traffic loop already exists on port ", existingPort);
      }
      if (loop.aggregatePortId &&
          existingLoop.aggregatePortId == loop.aggregatePortId) {
        throw FbossError("L3 traffic loop already exists on this aggregate");
      }
      if (existingLoop.qualificationAddress == loop.qualificationAddress) {
        throw FbossError(
            "Qualification address is already used by L3 traffic loop on ",
            existingPort);
      }
    }
    loops->insert_or_assign(portName, loop);
  }

  auto saveLoop =
      [this, &portName](const L3TrafficLoopStateStore::Loop& updatedLoop) {
        auto loops = l3TrafficLoopState_->loops.wlock();
        loops->insert_or_assign(portName, updatedLoop);
      };

  try {
    if (!neighbor || neighbor->isPending()) {
      auto install = sw_->getNeighborUpdater()->receivedNdpMineForIntf(
          interfaceId,
          peerNextHop.asV6(),
          *peerMac,
          aggregatePort ? PortDescriptor(aggregatePort->getID())
                        : PortDescriptor(portId),
          ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
          0 /* flags */);
      // Keep neighbors that existed before setup, including pending ones.
      loop.removeNeighborOnCleanup = !neighbor;
      saveLoop(loop);
      std::move(install).get(kL3TrafficLoopNeighborUpdateTimeout);
      // The cache update queues a separate asynchronous SwitchState update.
      sw_->updateStateBlocking(
          "complete L3 traffic loop neighbor installation",
          [](const std::shared_ptr<SwitchState>&)
              -> std::shared_ptr<SwitchState> { return nullptr; });
    }

    // Record the change before applying it so rollback covers partial updates.
    if (useLagTtlDisable) {
      loop.aggregateTtlChanged = true;
      saveLoop(loop);
      const auto aggregatePortId = *loop.aggregatePortId;
      sw_->updateStateBlocking(
          "disable LAG TTL decrement for L3 traffic loop",
          [aggregatePortId, portId](const std::shared_ptr<SwitchState>& state) {
            auto newState = state;
            auto aggregate =
                state->getAggregatePorts()->getNodeIf(aggregatePortId);
            if (!aggregate || !aggregate->isMemberPort(portId)) {
              throw FbossError(
                  "L3 traffic loop aggregate changed during setup");
            }
            aggregate->modify(&newState)->setTTLDisableDecrement(true);
            return newState;
          });
    } else if (usePortTtlDisable) {
      loop.portTtlChanged = true;
      saveLoop(loop);
      sw_->updateStateBlocking(
          "disable TTL decrement for L3 traffic loop",
          [portId](const std::shared_ptr<SwitchState>& oldState) {
            auto newState = oldState;
            auto oldPort = newState->getPorts()->getNodeIf(portId);
            if (!oldPort) {
              throw FbossError("No port ", portId, " for L3 traffic loop");
            }
            auto newPort = oldPort->modify(&newState);
            newPort->setTTLDisableDecrement(true);
            return newState;
          });
    } else if (useNextHopTtlDisable) {
      neighbor = getL3TrafficLoopNeighbor(sw_, interfaceId, peerNextHop.asV6());
      if (!neighbor) {
        throw FbossError(
            "Peer NDP neighbor was not installed for ", peerNextHop);
      }
      loop.neighborTtlChanged = true;
      saveLoop(loop);
      setL3TrafficLoopNeighborTtl(sw_, interfaceId, peerNextHop.asV6(), true);
    } else {
      throw FbossError("No supported TTL control for L3 traffic loop");
    }

    auto route = makeL3TrafficLoopRoute(
        loop.qualificationAddress, loop.peerNextHop, useNextHopTtlDisable);
    addUnicastRoute(
        kL3TrafficLoopClientId,
        std::make_unique<UnicastRoute>(std::move(route)));
    loop.status.state() = L3TrafficLoopState::READY;
    saveLoop(loop);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "L3 traffic loop setup failed on " << portName << ": "
              << ex.what();
    teardownL3TrafficLoopImpl(portName);
    throw FbossError(
        "L3 traffic loop setup failed on ", portName, ": ", ex.what());
  }
}

void ThriftHandler::setupL3TrafficLoops(
    std::unique_ptr<std::vector<BinaryAddress>> qualificationAddresses,
    std::unique_ptr<std::vector<std::string>> portNames,
    std::unique_ptr<std::vector<BinaryAddress>> peerNextHops,
    std::unique_ptr<std::vector<std::string>> expectedPeerMacs,
    std::unique_ptr<std::vector<int32_t>> timeoutSeconds) {
  // Part 6 enables setup after rollback cleanup is implemented.
  throw FbossError("L3 traffic loop setup requires cleanup support");

  if (!qualificationAddresses || !portNames || !peerNextHops ||
      !expectedPeerMacs || !timeoutSeconds) {
    throw FbossError("L3 traffic loop setup lists are required");
  }
  const auto numLoops = portNames->size();
  if (qualificationAddresses->size() != numLoops ||
      peerNextHops->size() != numLoops ||
      expectedPeerMacs->size() != numLoops ||
      timeoutSeconds->size() != numLoops) {
    throw FbossError("L3 traffic loop setup lists must have equal lengths");
  }
  const auto portNamesForLog = folly::join(",", *portNames);
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), portNamesForLog);
  ensureConfigured(__func__);

  std::lock_guard operationLock(l3TrafficLoopState_->operationMutex);
  std::vector<std::string> configuredPorts;
  try {
    for (size_t i = 0; i < numLoops; ++i) {
      const auto& portName = portNames->at(i);
      setupL3TrafficLoopImpl(
          qualificationAddresses->at(i),
          portName,
          peerNextHops->at(i),
          expectedPeerMacs->at(i),
          timeoutSeconds->at(i));
      configuredPorts.push_back(portName);
    }
  } catch (const std::exception& ex) {
    for (const auto& portName : configuredPorts) {
      teardownL3TrafficLoopImpl(portName);
    }
    throw FbossError("Bulk L3 traffic loop setup failed: ", ex.what());
  }
}

void ThriftHandler::extendL3TrafficLoops(
    std::unique_ptr<std::vector<std::string>> portNames,
    int32_t timeoutSeconds) {
  if (!portNames) {
    throw FbossError("L3 traffic loop port list is required");
  }
  const auto portNamesForLog = folly::join(",", *portNames);
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), portNamesForLog);
  ensureConfigured(__func__);
  validateL3TrafficLoopTimeout(timeoutSeconds);

  std::lock_guard operationLock(l3TrafficLoopState_->operationMutex);
  auto loops = l3TrafficLoopState_->loops.wlock();
  const auto now = std::chrono::steady_clock::now();
  std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>>
      expirations;
  expirations.reserve(portNames->size());
  for (const auto& portName : *portNames) {
    auto it = loops->find(portName);
    if (it == loops->end() ||
        it->second.status.state() != L3TrafficLoopState::READY) {
      throw FbossError("L3 traffic loop is not ready yet on port ", portName);
    }
    const auto base = std::max(it->second.expiresAt, now);
    const auto extended = base + std::chrono::seconds(timeoutSeconds);
    if (extended - now >
        std::chrono::seconds(kL3TrafficLoopMaxTimeoutSeconds)) {
      throw FbossError(
          "L3 traffic loop extension exceeds maximum remaining lease");
    }
    expirations.emplace_back(portName, extended);
  }

  const auto systemNow = std::chrono::system_clock::now();
  for (const auto& [portName, extended] : expirations) {
    auto& loop = loops->at(portName);
    loop.expiresAt = extended;
    loop.status.expirationTimeMs() =
        duration_cast<std::chrono::milliseconds>(
            systemNow.time_since_epoch() + (extended - now))
            .count();
  }
}

void ThriftHandler::teardownL3TrafficLoopImpl(const std::string&) {
  throw FbossError("L3 traffic loop cleanup is not implemented yet");
}

void ThriftHandler::teardownL3TrafficLoops(
    std::unique_ptr<std::vector<std::string>>) {
  throw FbossError("L3 traffic loop teardown is not implemented yet");
}

void ThriftHandler::getL3TrafficLoopStatus(
    std::map<std::string, L3TrafficLoopStatus>&,
    std::unique_ptr<std::vector<std::string>>) {
  throw FbossError("L3 traffic loop status is not implemented yet");
}

fb_status ThriftHandler::getStatus() {
  if (sw_->isExiting()) {
    return fb_status::STOPPING;
  }

  auto bootType = sw_->getBootType();
  switch (bootType) {
    case BootType::UNINITIALIZED:
      return fb_status::STARTING;
    case BootType::COLD_BOOT:
      return (sw_->isFullyConfigured()) ? fb_status::ALIVE
                                        : fb_status::STARTING;
    case BootType::WARM_BOOT:
      return (sw_->isFullyInitialized()) ? fb_status::ALIVE
                                         : fb_status::STARTING;
  }

  throw FbossError("Unknown bootType", bootType);
}

void ThriftHandler::async_tm_getStatus(ThriftCallback<fb_status> callback) {
  callback->result(getStatus());
}

void ThriftHandler::flushCountersNow() {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  // Currently SwSwitch only contains thread local stats.
  //
  // Depending on how we design the HW-specific stats interface,
  // we may also need to make a separate call to force immediate collection of
  // hardware stats.
  fb303::ThreadCachedServiceData::get()->publishStats();
}

void ThriftHandler::addUnicastRouteInVrf(
    int16_t client,
    std::unique_ptr<UnicastRoute> route,
    int32_t vrf) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  auto routes = std::make_unique<std::vector<UnicastRoute>>();
  routes->emplace_back(std::move(*route));
  addUnicastRoutesInVrf(client, std::move(routes), vrf);
}

void ThriftHandler::addUnicastRoute(
    int16_t client,
    std::unique_ptr<UnicastRoute> route) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  addUnicastRouteInVrf(client, std::move(route), 0);
}

void ThriftHandler::deleteUnicastRouteInVrf(
    int16_t client,
    std::unique_ptr<IpPrefix> prefix,
    int32_t vrf) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  auto prefixes = std::make_unique<std::vector<IpPrefix>>();
  prefixes->emplace_back(std::move(*prefix));
  deleteUnicastRoutesInVrf(client, std::move(prefixes), vrf);
}

void ThriftHandler::deleteUnicastRoute(
    int16_t client,
    std::unique_ptr<IpPrefix> prefix) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  deleteUnicastRouteInVrf(client, std::move(prefix), 0);
}

void ThriftHandler::addUnicastRoutesInVrf(
    int16_t client,
    std::unique_ptr<std::vector<UnicastRoute>> routes,
    int32_t vrf) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  ensureNotFabric(__func__);
  updateUnicastRoutesImpl(vrf, client, routes, "addUnicastRoutesInVrf", false);
}

void ThriftHandler::addUnicastRoutes(
    int16_t client,
    std::unique_ptr<std::vector<UnicastRoute>> routes) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  ensureConfigured(__func__);
  addUnicastRoutesInVrf(client, std::move(routes), 0);
}

void ThriftHandler::getProductInfo(ProductInfo& productInfo) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  sw_->getProductInfo(productInfo);
}

void ThriftHandler::deleteUnicastRoutesInVrf(
    int16_t client,
    std::unique_ptr<std::vector<IpPrefix>> prefixes,
    int32_t vrf) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  ensureNotFabric(__func__);

  auto updater = sw_->getRouteUpdater();
  auto routerID = RouterID(vrf);
  auto clientID = ClientID(client);
  for (const auto& prefix : *prefixes) {
    updater.delRoute(routerID, prefix, clientID);
  }
  // count the number of times we attempt to update the FIB
  sw_->stats()->routeProgrammingUpdateAttempts();

  try {
    updater.program();
  } catch (const FbossHwUpdateError& ex) {
    sw_->stats()->routeProgrammingUpdateFailures();
    translateToFibError(ex);
  }
}

void ThriftHandler::deleteUnicastRoutes(
    int16_t client,
    std::unique_ptr<std::vector<IpPrefix>> prefixes) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  ensureConfigured(__func__);
  deleteUnicastRoutesInVrf(client, std::move(prefixes), 0);
}

void ThriftHandler::syncFibInVrf(
    int16_t client,
    std::unique_ptr<std::vector<UnicastRoute>> routes,
    int32_t vrf) {
  auto clientId = static_cast<ClientID>(client);
  auto clientName = apache::thrift::util::enumNameSafe(clientId);
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  ensureConfigured(__func__);
  if (!sw_->getSwitchInfoTable().haveL3Switches()) {
    if (routes->size()) {
      throw FbossError("No ASIC found with L3 functionality");
    }
    return;
  }
  // Only route updates in first syncFib for each client are logged
  auto firstClientSync =
      syncedFibClients_.find(client) == syncedFibClients_.end();
  auto clientIdentifier = "fboss-agent-warmboot-" + clientName;
  if (firstClientSync && sw_->getBootType() == BootType::WARM_BOOT) {
    sw_->logRouteUpdates("::", 0, clientIdentifier);
    sw_->logRouteUpdates("0.0.0.0", 0, clientIdentifier);
  }
  SCOPE_EXIT {
    if (firstClientSync && sw_->getBootType() == BootType::WARM_BOOT) {
      sw_->stopLoggingRouteUpdates(clientIdentifier);
    }
  };
  updateUnicastRoutesImpl(vrf, client, routes, "syncFibInVrf", true);

  if (firstClientSync) {
    sw_->setFibSyncTimeForClient(clientId);
  }

  syncedFibClients_.emplace(client);
}

void ThriftHandler::syncFib(
    int16_t client,
    std::unique_ptr<std::vector<UnicastRoute>> routes) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(client));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  ensureConfigured(__func__);
  syncFibInVrf(client, std::move(routes), 0);
}

void ThriftHandler::updateUnicastRoutesImpl(
    int32_t vrf,
    int16_t client,
    const std::unique_ptr<std::vector<UnicastRoute>>& routes,
    const std::string& updType,
    bool sync) {
  auto updater = sw_->getRouteUpdater();
  auto routerID = RouterID(vrf);
  auto clientID = ClientID(client);
  auto defaultSrv6TunnelId = getDefaultSrv6TunnelId(sw_->getConfig());
  auto state = sw_->getState();
  for (auto& route : *routes) {
    if (route.overrideEcmpSwitchingMode().has_value() ||
        route.overrideNextHops().has_value()) {
      throw FbossError(
          "Override nhops or switching mode cannot be set by clients");
    }
    validateAndDefaultSrv6NextHops(*route.nextHops(), defaultSrv6TunnelId);
    validateLinkLocalNextHopInterfaces(*route.nextHops(), state);
    if (FLAGS_enable_route_counters_for_named_nhg &&
        route.namedRouteDestination()->getType() ==
            NamedRouteDestination::Type::nextHopGroup) {
      route.counterID() = *route.namedRouteDestination()->nextHopGroup_ref();
    }
    updater.addRoute(routerID, clientID, route);
  }
  RouteUpdateWrapper::SyncFibFor syncFibs;

  if (sync) {
    syncFibs.insert({routerID, clientID});
  }
  // count the number of times we attempt to update the FIB
  sw_->stats()->routeProgrammingUpdateAttempts();

  try {
    updater.program(
        {syncFibs, RouteUpdateWrapper::SyncFibInfo::SyncFibType::IP_ONLY});
  } catch (const FbossHwUpdateError& ex) {
    sw_->stats()->routeProgrammingUpdateFailures();
    translateToFibError(ex);
  }
}

void ThriftHandler::addMySidEntries(
    std::unique_ptr<std::vector<MySidEntry>> mySidEntries) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  for (const auto& entry : *mySidEntries) {
    switch (*entry.type()) {
      case MySidType::BINDING_MICRO_SID:
        break;
      case MySidType::ADJACENCY_MICRO_SID:
      case MySidType::NODE_MICRO_SID:
      case MySidType::DECAPSULATE_AND_LOOKUP:
        throw FbossError(
            "Only BINDING_MICRO_SID MySid type is supported via ThriftHandler");
    }
  }
  auto rib = sw_->getRib();
  if (!rib) {
    throw FbossError("RIB not initialized");
  }
  auto ribMySidToSwitchStateFunc =
      createRibMySidToSwitchStateFunction(std::nullopt);
  rib->update(
      sw_->getScopeResolver(),
      *mySidEntries,
      {} /* toDelete */,
      "addMySidEntries",
      ribMySidToSwitchStateFunc,
      sw_);
}

void ThriftHandler::deleteMySidEntries(
    std::unique_ptr<std::vector<IpPrefix>> prefixes) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto rib = sw_->getRib();
  if (!rib) {
    throw FbossError("RIB not initialized");
  }
  auto ribMySidToSwitchStateFunc =
      createRibMySidToSwitchStateFunction(std::nullopt);
  rib->update(
      sw_->getScopeResolver(),
      std::vector<MySidEntry>{} /* toAdd */,
      *prefixes,
      "deleteMySidEntries",
      ribMySidToSwitchStateFunc,
      sw_);
}

void ThriftHandler::getMySidEntries(std::vector<MySidEntry>& entries) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();

  // MySids are replicated across per-switch maps in MultiSwitchMySidMap.
  // Deduplicate by prefix string key.
  std::set<std::string> seen;
  for (const auto& miter : std::as_const(*state->getMySids())) {
    for (const auto& [key, mySid] : std::as_const(*miter.second)) {
      if (!seen.insert(key).second) {
        continue;
      }
      MySidEntry entry;
      entry.type() = mySid->getType();

      auto [ip, prefixLen] = mySid->getMySid();
      entry.mySid()->prefixAddress() = toBinaryAddress(ip);
      entry.mySid()->prefixLength() = prefixLen;

      // Populate unresolved next hops (what the caller originally provided).
      if (auto unresolvedId = mySid->getUnresolveNextHopsId()) {
        auto nhops = getNextHops(
            state,
            static_cast<NextHopSetId>(static_cast<int64_t>(*unresolvedId)));
        RouteNextHopSet nhopSet;
        nhopSet.insert(nhops.begin(), nhops.end());
        entry.nextHops() = util::fromRouteNextHopSet(nhopSet);
      }

      // Populate resolved next hops (resolved to actual hardware next hops).
      if (auto resolvedId = mySid->getResolvedNextHopsId()) {
        auto nhops = getNextHops(
            state,
            static_cast<NextHopSetId>(static_cast<int64_t>(*resolvedId)));
        RouteNextHopSet nhopSet;
        nhopSet.insert(nhops.begin(), nhops.end());
        entry.resolvedNextHops() = util::fromRouteNextHopSet(nhopSet);
      }

      // Populate backup unresolved next hops (FRR backups as requested via
      // addAdjacencyFrr).
      if (auto backupUnresolvedId = mySid->getBackupUnresolveNextHopsId()) {
        auto nhops = getNextHops(
            state,
            static_cast<NextHopSetId>(
                static_cast<int64_t>(*backupUnresolvedId)));
        RouteNextHopSet nhopSet;
        nhopSet.insert(nhops.begin(), nhops.end());
        entry.backupUnresolvedNextHops() = util::fromRouteNextHopSet(nhopSet);
      }

      // Populate backup resolved next hops (FRR backups post resolution).
      if (auto backupResolvedId = mySid->getBackupResolvedNextHopsId()) {
        auto nhops = getNextHops(
            state,
            static_cast<NextHopSetId>(static_cast<int64_t>(*backupResolvedId)));
        RouteNextHopSet nhopSet;
        nhopSet.insert(nhops.begin(), nhops.end());
        entry.backupResolvedNextHops() = util::fromRouteNextHopSet(nhopSet);
      }

      entries.emplace_back(std::move(entry));
    }
  }
}

static void populateInterfaceDetail(
    InterfaceDetail& interfaceDetail,
    const std::shared_ptr<Interface> intf,
    const std::shared_ptr<SwitchState> state) {
  *interfaceDetail.interfaceName() = intf->getName();
  *interfaceDetail.interfaceId() = intf->getID();
  switch (intf->getType()) {
    case cfg::InterfaceType::PORT: {
      // A port router interface is bound to either a physical port or an
      // aggregate port, in which case it covers all of the members.
      if (auto aggregatePortID = intf->getAggregatePortIDf()) {
        auto aggPort = state->getAggregatePorts()->getNodeIf(*aggregatePortID);
        if (aggPort) {
          for (const auto& subport : aggPort->sortedSubports()) {
            auto port = state->getPorts()->getNodeIf(subport.portID);
            if (port) {
              interfaceDetail.portNames()->emplace_back(port->getName());
            }
          }
        }
      } else {
        auto port = state->getPorts()->getNode(intf->getPortID());
        interfaceDetail.portNames()->emplace_back(port->getName());
      }
    } break;
    case cfg::InterfaceType::VLAN: {
      *interfaceDetail.vlanId() = intf->getVlanID();
      auto vlan = state->getVlans()->getNodeIf(intf->getVlanID());
      if (!intf->isVirtual() && vlan != nullptr) {
        auto members = vlan->getPortsInfo();
        for (auto member : members) {
          auto port = state->getPorts()->getNode(PortID(member.first));
          interfaceDetail.portNames()->emplace_back(port->getName());
        }
      }
    } break;
    case cfg::InterfaceType::SYSTEM_PORT: {
      auto sysPortID = intf->getSystemPortID();
      if (!sysPortID) {
        throw FbossError(
            "System port interface ", intf->getID(), " has no system port");
      }
      if (state->getSystemPorts()->getNodeIf(*sysPortID)) {
        auto portID = getPortID(*sysPortID, state);
        auto port = state->getPorts()->getNode(portID);
        interfaceDetail.portNames()->emplace_back(port->getName());
      }
    } break;
  }
  if (intf->getType() == cfg::InterfaceType::PORT) {
    if (auto aggregatePortID = intf->getAggregatePortIDf()) {
      interfaceDetail.aggregatePortId() = *aggregatePortID;
    } else {
      *interfaceDetail.portId() = intf->getPortID();
    }
  }
  *interfaceDetail.routerId() = intf->getRouterID();
  *interfaceDetail.mtu() = intf->getMtu();
  *interfaceDetail.mac() = intf->getMac().toString();
  interfaceDetail.address()->clear();
  auto addrs = intf->getAddressesCopy();
  interfaceDetail.address()->reserve(addrs.size());
  for (const auto& addrAndMask : addrs) {
    IpPrefix temp;
    *temp.ip() = toBinaryAddress(addrAndMask.first);
    *temp.prefixLength() = addrAndMask.second;
    interfaceDetail.address()->push_back(temp);
  }

  if (intf->getRemoteInterfaceType().has_value()) {
    interfaceDetail.remoteIntfType() = intf->getRemoteInterfaceType().value();
  }

  if (intf->getRemoteLivenessStatus().has_value()) {
    interfaceDetail.remoteIntfLivenessStatus() =
        intf->getRemoteLivenessStatus().value();
  }

  interfaceDetail.scope() = intf->getScope();

  if (intf->getDesiredPeerAddressIPv6().has_value()) {
    interfaceDetail.desiredPeerAddressIPv6() =
        intf->getDesiredPeerAddressIPv6().value();
  }
  interfaceDetail.interfaceType() = intf->getType();
}

void ThriftHandler::getAllInterfaces(
    std::map<int32_t, InterfaceDetail>& interfaces) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  auto state = sw_->getState();
  auto getAllInterfacesHelper = [&interfaces, state](const auto& interfaceMap) {
    for (const auto& [_, intfs] : std::as_const(*interfaceMap)) {
      for (const auto& iter : std::as_const(*intfs)) {
        const auto& intf = iter.second;
        auto& interfaceDetail = interfaces[intf->getID()];
        populateInterfaceDetail(interfaceDetail, intf, state);
      }
    }
  };

  getAllInterfacesHelper(state->getInterfaces());
  getAllInterfacesHelper(state->getRemoteInterfaces());
}

void ThriftHandler::getInterfaceList(std::vector<std::string>& interfaceList) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  const auto interfaceMap = sw_->getState()->getInterfaces();
  for (const auto& [_, intfs] : std::as_const(*interfaceMap)) {
    for (const auto& iter : std::as_const(*intfs)) {
      auto intf = iter.second;
      interfaceList.push_back(intf->getName());
    }
  }
}

void ThriftHandler::getVrfs(std::vector<VrfInfo>& vrfs) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  auto state = sw_->getState();
  std::map<int32_t, VrfInfo> vrfInfos;
  vrfInfos[0].vrfId() = 0;
  for (const auto& [_, vrfMap] : std::as_const(*state->getVrfs())) {
    for (const auto& iter : std::as_const(*vrfMap)) {
      const auto& vrf = iter.second;
      auto& vrfInfo = vrfInfos[vrf->getID()];
      vrfInfo.vrfId() = vrf->getID();
      vrfInfo.name().from_optional(vrf->getName());
    }
  }
  for (const auto& [_, intfs] : std::as_const(*state->getInterfaces())) {
    for (const auto& iter : std::as_const(*intfs)) {
      const auto& intf = iter.second;
      auto& vrfInfo = vrfInfos[intf->getRouterID()];
      vrfInfo.vrfId() = intf->getRouterID();
      vrfInfo.interfaceIds()->push_back(intf->getID());
    }
  }
  for (auto& [_, vrfInfo] : vrfInfos) {
    vrfs.push_back(std::move(vrfInfo));
  }
}

void ThriftHandler::getInterfaceDetail(
    InterfaceDetail& interfaceDetail,
    int32_t interfaceId) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();
  const auto intf = state->getInterfaces()->getNodeIf(InterfaceID(interfaceId));

  if (!intf) {
    throw FbossError("no such interface ", interfaceId);
  }
  populateInterfaceDetail(interfaceDetail, intf, state);
}

// NOTE : pass by value of state is deliberate. We want to bump
// a reference cnt and not have sw switch state deleted from
// underneath us due to parallel updates
template <typename AddressT, typename NeighborThriftT>
void ThriftHandler::addRemoteNeighbors(
    const std::shared_ptr<SwitchState> state,
    std::vector<NeighborThriftT>& nbrs) const {
  if (!sw_->getSwitchInfoTable().haveVoqSwitches()) {
    return;
  }
  for (auto& nbr : nbrs) {
    if (nbr.portDescriptor().has_value()) {
      PortID portID;
      switch (*nbr.portDescriptor().value().portType()) {
        case cfg::PortDescriptorType::Physical:
          portID = *nbr.portDescriptor().value().portId();
          break;
        case cfg::PortDescriptorType::SystemPort:
          portID = getPortID(
              SystemPortID(*nbr.portDescriptor().value().portId()), state);
          break;
        case cfg::PortDescriptorType::Aggregate:
          throw FbossError(
              "Aggregate ports are not yet supported for VOQ switches");
          break;
      }

      nbr.switchId() = state->getAssociatedSwitchID(portID);
    }
  }
  const auto& remoteRifs = state->getRemoteInterfaces();
  const auto& remoteSysPorts = state->getRemoteSystemPorts();
  for (const auto& [_, rifMap] : std::as_const(*remoteRifs)) {
    for (const auto& idAndRif : std::as_const(*rifMap)) {
      const auto& rif = idAndRif.second;
      const auto& nbrTable =
          std::as_const(*rif->template getNeighborEntryTable<AddressT>());
      for (const auto& ipAndEntry : nbrTable) {
        const auto& entry = ipAndEntry.second;
        NeighborThriftT nbrThrift;
        nbrThrift.ip() = facebook::network::toBinaryAddress(entry->getIP());
        nbrThrift.mac() = entry->getMac().toString();
        CHECK(rif->getSystemPortID().has_value());
        nbrThrift.port() = static_cast<int32_t>(*rif->getSystemPortID());
        nbrThrift.portDescriptor()->portId() = *rif->getSystemPortID();
        nbrThrift.portDescriptor()->portType() =
            cfg::PortDescriptorType::SystemPort;
        nbrThrift.vlanName() = "--";
        nbrThrift.interfaceID() = static_cast<int32_t>(*rif->getSystemPortID());

        switch (entry->getType()) {
          case state::NeighborEntryType::STATIC_ENTRY:
            nbrThrift.state() = "STATIC";
            break;
          case state::NeighborEntryType::DYNAMIC_ENTRY:
            nbrThrift.state() = "DYNAMIC";
            break;
        }

        nbrThrift.isLocal() = false;
        const auto& sysPort =
            remoteSysPorts->getNodeIf(*rif->getSystemPortID());
        if (sysPort) {
          nbrThrift.switchId() = static_cast<int64_t>(sysPort->getSwitchId());
        }
        if (entry->getResolvedSince().has_value()) {
          nbrThrift.resolvedSince() = entry->getResolvedSince().value();
        } else {
          XLOG(WARNING) << "Neighbor entry " << ipAndEntry.first
                        << " is missing resolved timestamp";
        }
        nbrs.push_back(nbrThrift);
      }
    }
  }
}

void ThriftHandler::getNdpTable(std::vector<NdpEntryThrift>& ndpTable) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  // Look up neighbor table entries
  std::list<facebook::fboss::NdpEntryThrift> entries;
  entries = sw_->getNeighborUpdater()->getNdpCacheDataForIntf().get();

  ndpTable.reserve(entries.size());
  ndpTable.insert(
      ndpTable.begin(),
      std::make_move_iterator(std::begin(entries)),
      std::make_move_iterator(std::end(entries)));
  addRecylePortRifNeighbors<folly::IPAddressV6>(sw_->getState(), ndpTable);
  addRemoteNeighbors<folly::IPAddressV6>(sw_->getState(), ndpTable);
}

void ThriftHandler::getArpTable(std::vector<ArpEntryThrift>& arpTable) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  // Look up neighbor table entries
  std::list<facebook::fboss::ArpEntryThrift> entries;
  entries = sw_->getNeighborUpdater()->getArpCacheDataForIntf().get();

  arpTable.reserve(entries.size());
  arpTable.insert(
      arpTable.begin(),
      std::make_move_iterator(std::begin(entries)),
      std::make_move_iterator(std::end(entries)));
  addRecylePortRifNeighbors<folly::IPAddressV4>(sw_->getState(), arpTable);
  addRemoteNeighbors<folly::IPAddressV4>(sw_->getState(), arpTable);
}

void ThriftHandler::getL2Table(std::vector<L2EntryThrift>& l2Table) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  ensureNPU(__func__);
  if (sw_->isRunModeMultiSwitch()) {
    std::vector<L2EntryThrift> l2TableForSwitch;
    for (const auto& switchId : sw_->getSwitchInfoTable().getSwitchIDs()) {
      sw_->getHwSwitchThriftClientTable()->getHwL2Table(
          switchId, l2TableForSwitch);
      l2Table.insert(
          l2Table.end(), l2TableForSwitch.begin(), l2TableForSwitch.end());
    }
  } else {
    sw_->getMonolithicHwSwitchHandler()->fetchL2Table(&l2Table);
  }
  XLOG(DBG6) << "L2 Table size:" << l2Table.size();
}

void ThriftHandler::getAclTable(std::vector<AclEntryThrift>& aclTable) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  aclTable.reserve(sw_->getState()->getAcls()->numNodes());
  for (const auto& mIter : std::as_const(*(sw_->getState()->getAcls()))) {
    for (const auto& iter : std::as_const(*mIter.second)) {
      const auto& aclEntry = iter.second;
      aclTable.push_back(populateAclEntryThrift(*aclEntry));
    }
  }
}

void ThriftHandler::getAclTableGroup(AclTableThrift& aclTableEntry) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  if (FLAGS_enable_acl_table_group) {
    for (const auto& mIter :
         std::as_const(*(sw_->getState()->getAclTableGroups()))) {
      for (const auto& iter : std::as_const(*mIter.second)) {
        auto aclStage = iter.first;
        auto aclTableMap = sw_->getState()->getAclTablesForStage(aclStage);
        if (aclTableMap) {
          for (const auto& tableIter : std::as_const(*aclTableMap)) {
            std::vector<AclEntryThrift> aclTable;
            auto aclTableName = tableIter.first;
            auto aclMap = tableIter.second->getAclMap().unwrap();
            for (const auto& aclMapEntry : std::as_const(*aclMap)) {
              const auto& aclEntry = aclMapEntry.second;
              aclTable.push_back(populateAclEntryThrift(*aclEntry));
            }
            aclTableEntry.aclTableEntries()[aclTableName] = std::move(aclTable);
          }
        }
      }
    }
  } else {
    std::vector<AclEntryThrift> aclTable;
    getAclTable(aclTable);
    aclTableEntry.aclTableEntries()
        [cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE()] =
        std::move(aclTable);
  }
}

void ThriftHandler::getAggregatePort(
    AggregatePortThrift& aggregatePortThrift,
    int32_t aggregatePortIDThrift) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  if (aggregatePortIDThrift < 0 ||
      aggregatePortIDThrift > std::numeric_limits<uint16_t>::max()) {
    throw FbossError(
        "AggregatePort ID ", aggregatePortIDThrift, " is out of range");
  }
  auto aggregatePortID = static_cast<AggregatePortID>(aggregatePortIDThrift);

  auto aggregatePort =
      sw_->getState()->getAggregatePorts()->getNodeIf(aggregatePortID);

  if (!aggregatePort) {
    throw FbossError(
        "AggregatePort with ID ", aggregatePortIDThrift, " not found");
  }

  populateAggregatePortThrift(aggregatePort, aggregatePortThrift);
}

void ThriftHandler::getAggregatePortTable(
    std::vector<AggregatePortThrift>& aggregatePortsThrift) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  // Since aggregatePortsThrift is being push_back'ed to, but is an out
  // parameter, make sure it's clear() first
  aggregatePortsThrift.clear();

  const auto& aggregatePortMap = sw_->getState()->getAggregatePorts();
  aggregatePortsThrift.reserve(aggregatePortMap->numNodes());
  for (const auto& [_, entry] : std::as_const(*aggregatePortMap)) {
    for (const auto& aggregatePortAndID : std::as_const(*entry)) {
      aggregatePortsThrift.emplace_back();

      populateAggregatePortThrift(
          aggregatePortAndID.second, aggregatePortsThrift.back());
    }
  }
}

void ThriftHandler::getPortInfo(PortInfoThrift& portInfo, int32_t portId) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  const auto port = sw_->getState()->getPorts()->getNodeIf(PortID(portId));
  if (!port) {
    throw FbossError("no such port ", portId);
  }

  getPortInfoHelper(*sw_, portInfo, port);
}

void ThriftHandler::getAllPortInfo(map<int32_t, PortInfoThrift>& portInfoMap) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  // NOTE: important to take pointer to switch state before iterating over
  // list of ports
  std::shared_ptr<SwitchState> swState = sw_->getState();
  for (const auto& portMap : std::as_const(*(swState->getPorts()))) {
    for (const auto& port : std::as_const(*portMap.second)) {
      auto portId = port.second->getID();
      auto& portInfo = portInfoMap[portId];
      getPortInfoHelper(*sw_, portInfo, port.second);
    }
  }
}

void ThriftHandler::clearPortStats(unique_ptr<vector<int32_t>> ports) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), *ports);
  ensureConfigured(__func__);
  utility::clearSwPortStats(*ports, sw_->getState());
  if (sw_->isRunModeMultiSwitch()) {
    for (const auto& switchId : sw_->getSwitchInfoTable().getSwitchIDs()) {
      std::vector<int32_t> portList;
      for (const auto& port : *ports) {
        if (sw_->getScopeResolver()->scope(PortID(port)).has(switchId)) {
          portList.push_back(port);
        }
      }
      if (!portList.empty()) {
        sw_->getHwSwitchThriftClientTable()->clearHwPortStats(
            switchId, portList);
      }
    }
  } else {
    utility::clearPortStats(sw_, std::move(ports), sw_->getState());
  }
}

void ThriftHandler::clearAllPortStats() {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  if (sw_->isRunModeMultiSwitch()) {
    for (const auto& switchId : sw_->getSwitchInfoTable().getSwitchIDs()) {
      sw_->getHwSwitchThriftClientTable()->clearAllHwPortStats(switchId);
    }
  } else {
    auto allPorts = std::make_unique<std::vector<int32_t>>();
    std::shared_ptr<SwitchState> swState = sw_->getState();
    for (const auto& portMap : std::as_const(*(swState->getPorts()))) {
      for (const auto& port : std::as_const(*portMap.second)) {
        allPorts->push_back(port.second->getID());
      }
    }
    clearPortStats(std::move(allPorts));
  }
}

void ThriftHandler::getPortStats(PortInfoThrift& portInfo, int32_t portId) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  getPortInfo(portInfo, portId);
}

void ThriftHandler::getAllPortStats(map<int32_t, PortInfoThrift>& portInfoMap) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  getAllPortInfo(portInfoMap);
}

void ThriftHandler::getRunningConfig(std::string& configStr) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  configStr = sw_->getConfigStr();
}

void ThriftHandler::getCurrentStateJSON(
    std::string& ret,
    std::unique_ptr<std::string> path) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  if (path) {
    ret = utility::getCurrentStateJSONForPathHelper(*path, sw_->getState());
  }
}

void ThriftHandler::getCurrentStateJSONForPaths(
    std::map<std::string, std::string>& pathToState,
    std::unique_ptr<std::vector<std::string>> paths) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  for (auto& path : *paths) {
    pathToState[path] =
        utility::getCurrentStateJSONForPathHelper(path, sw_->getState());
  }
}

void ThriftHandler::patchCurrentStateJSONForPaths(
    std::unique_ptr<std::map<std::string, std::string>> pathToJsonPatch) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  if (!FLAGS_allow_running_switch_state_mutations) {
    throw FbossError("Running switch state mutations are not allowed");
  }

  /*
   * The caller (thrift client) must get localSystPorts/localIntfs from every
   * other RDSW in the DSF cluster, set isLocal = false for every neighbor,
   * merge localSysPorts/localRifs in one flat list and then call this API by
   * passing these lists as remoteSysPorts/remoteRifs.
   *
   * The implementation works as follows:
   *  - Break the flat list of remoteSysPorts/remoteRifs into per switchID map.
   *  - Build a new desired state by iterating over the map and invoking
   *    DsfStateUpdaterUtil::getUpdatedState
   *  - This is the same util that is invoked when FSDB is running and we
   *    receive state update from a peer DSF node.
   *  - Apply the state.
   */

  std::map<SwitchID, std::shared_ptr<SystemPortMap>> switchId2SystemPorts;
  std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Rifs;

  for (const auto& [path, jsonPatch] : *pathToJsonPatch) {
    if (path == "remoteSystemPortMaps") {
      MultiSwitchSystemPortMap mswitchSysPorts;
      mswitchSysPorts.fromThrift(
          thrift_cow::deserialize<
              MultiSwitchSystemPortMapTypeClass,
              MultiSwitchSystemPortMapThriftType>(
              fsdb::OperProtocol::SIMPLE_JSON, jsonPatch));
      for (const auto& systemPortMap : mswitchSysPorts) {
        // A given port belongs to exactly one switch
        auto matcher = HwSwitchMatcher(systemPortMap.first);
        switchId2SystemPorts[matcher.switchId()] = systemPortMap.second;
      }
    } else if (path == "remoteInterfaceMaps") {
      MultiSwitchInterfaceMap mswitchIntfs;
      mswitchIntfs.fromThrift(
          thrift_cow::deserialize<
              MultiSwitchInterfaceMapTypeClass,
              MultiSwitchInterfaceMapThriftType>(
              fsdb::OperProtocol::SIMPLE_JSON, jsonPatch));
      for (const auto& remoteIntfMap : mswitchIntfs) {
        auto matcher = HwSwitchMatcher(remoteIntfMap.first);
        // Pick first switchId for now, may need to revise when we support
        // Multi NPU VOQ switches.
        auto switchId = *matcher.switchIds().cbegin();
        switchId2Rifs[switchId] = remoteIntfMap.second;
      }
    } else {
      throw FbossError(
          "Running switch state mutation not supported for: ", path);
    }
  }

  if (switchId2SystemPorts.size() != switchId2Rifs.size()) {
    // This size check + check in updateDsfStateFn guarantee this
    throw FbossError(
        "Both remoteSystemPorts and remoteRifs must be provided together for every switchID");
  }

  auto updateDsfStateFn = [this, switchId2SystemPorts, switchId2Rifs](
                              const std::shared_ptr<SwitchState>& in) {
    auto newState = DsfStateUpdaterUtil::getUpdatedState(
        in,
        sw_->getScopeResolver(),
        sw_->getRib(),
        switchId2SystemPorts,
        switchId2Rifs);
    return newState;
  };

  sw_->updateState(
      fmt::format("Update state by patchCurrentStateJSONForPaths: "),
      std::move(updateDsfStateFn));
}

void ThriftHandler::getPortStatusImpl(
    std::map<int32_t, PortStatus>& statusMap,
    const std::unique_ptr<std::vector<int32_t>>& ports) const {
  ensureConfigured(__func__);
  if (ports->empty()) {
    statusMap = sw_->getPortStatus();
  } else {
    for (auto port : *ports) {
      statusMap[port] = sw_->getPortStatus(PortID(port));
    }
  }
}

void ThriftHandler::getPortStatus(
    map<int32_t, PortStatus>& statusMap,
    unique_ptr<vector<int32_t>> ports) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  getPortStatusImpl(statusMap, ports);
}

void ThriftHandler::getSupportedPrbsPolynomials(
    std::vector<prbs::PrbsPolynomial>& prbsCapabilities,
    std::unique_ptr<std::string> portName,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  auto portID = sw_->getPlatformMapping()->getPortID(*portName);
  prbsCapabilities = sw_->getPortPrbsPolynomials(PortID(portID));
}

void ThriftHandler::getInterfacePrbsState(
    prbs::InterfacePrbsState& prbsState,
    std::unique_ptr<std::string> portName,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  auto portID = sw_->getPlatformMapping()->getPortID(*portName);
  prbsState = sw_->getPortPrbsState(portID);
}

void ThriftHandler::clearInterfacePrbsStats(
    std::unique_ptr<std::string> portName,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  auto portID = sw_->getPlatformMapping()->getPortID(*portName);
  clearPortPrbsStats(portID, component);
}

void ThriftHandler::getInterfacePrbsStats(
    phy::PrbsStats& response,
    std::unique_ptr<std::string> portName,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  auto portID = sw_->getPlatformMapping()->getPortID(*portName);
  getPortPrbsStats(response, portID, component);
}

void ThriftHandler::setInterfacePrbs(
    std::unique_ptr<std::string> portName,
    phy::PortComponent component,
    std::unique_ptr<prbs::InterfacePrbsState> state) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), *portName);
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  if ((state->generatorEnabled().has_value() &&
       state->checkerEnabled().has_value()) &&
      (state->generatorEnabled().value() == state->checkerEnabled().value())) {
    auto portID = sw_->getPlatformMapping()->getPortID(*portName);
    bool enabled =
        (state->generatorEnabled().value() && state->checkerEnabled().value());
    setPortPrbs(
        portID, component, enabled, static_cast<int>(*state->polynomial()));
  } else if (
      !state->generatorEnabled().has_value() ||
      !state->checkerEnabled().has_value()) {
    throw FbossError(
        "Both generator and checker must be specified for PRBS setting");
  } else {
    throw FbossError(
        "ASIC only supports bidirectional PRBS. Generator and checker must be both enabled or disabled.");
  }
}

void ThriftHandler::setInterfacesPrbs(
    std::unique_ptr<std::vector<std::string>> portNames,
    phy::PortComponent component,
    std::unique_ptr<prbs::InterfacePrbsState> state) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  for (const auto& portName : *portNames) {
    auto portNamePtr = std::make_unique<std::string>(portName);
    auto stateCopy = std::make_unique<prbs::InterfacePrbsState>(*state);
    setInterfacePrbs(std::move(portNamePtr), component, std::move(stateCopy));
  }
}

void ThriftHandler::addAdjacencyFrr(
    std::unique_ptr<FrrProtectedObject> protectedObject,
    std::unique_ptr<std::vector<NextHopThrift>> backupNextHops) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  ensureFrrProtectedObjectIsMySid(*protectedObject);
  if (backupNextHops->empty()) {
    throw FbossError(
        "Adjacency FRR requires at least one backup next hop; use "
        "deleteAdjacencyFrr to remove protection");
  }

  auto rib = sw_->getRib();
  if (!rib) {
    throw FbossError("RIB not initialized");
  }
  auto defaultSrv6TunnelId = getDefaultSrv6TunnelId(sw_->getConfig());
  auto state = sw_->getState();
  for (auto& nextHop : *backupNextHops) {
    nextHop.role() = NextHopRole::BACKUP;
  }
  validateAndDefaultSrv6NextHops(*backupNextHops, defaultSrv6TunnelId);
  validateLinkLocalNextHopInterfaces(*backupNextHops, state);
  std::vector<MySidFrrProtectionUpdate> toAddOrUpdate{MySidFrrProtectionUpdate{
      .mySidPrefix =
          facebook::network::toCIDRNetwork(*protectedObject->mySid_ref()),
      .nextHops = util::toRouteNextHopSet(
          *backupNextHops, true /* allowV6NonLinkLocal */),
  }};
  auto ribMySidToSwitchStateFunc =
      createRibMySidToSwitchStateFunction(std::nullopt);
  rib->updateMySidFrrProtection(
      sw_->getScopeResolver(),
      toAddOrUpdate,
      {},
      ribMySidToSwitchStateFunc,
      sw_);
}

void ThriftHandler::deleteAdjacencyFrr(
    std::unique_ptr<FrrProtectedObject> protectedObject) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  ensureFrrProtectedObjectIsMySid(*protectedObject);

  auto rib = sw_->getRib();
  if (!rib) {
    throw FbossError("RIB not initialized");
  }
  std::vector<folly::CIDRNetwork> toDelete{
      facebook::network::toCIDRNetwork(*protectedObject->mySid_ref())};
  auto ribMySidToSwitchStateFunc =
      createRibMySidToSwitchStateFunction(std::nullopt);
  rib->updateMySidFrrProtection(
      sw_->getScopeResolver(), {}, toDelete, ribMySidToSwitchStateFunc, sw_);
}

void ThriftHandler::clearPortPrbsStats(
    int32_t portId,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  if (component == phy::PortComponent::ASIC) {
    sw_->clearPortAsicPrbsStats(PortID(portId));
  } else if (
      component == phy::PortComponent::GB_SYSTEM ||
      component == phy::PortComponent::GB_LINE) {
    throw FbossError("Clear gearbox prbs stats is not supported");
  } else {
    XLOG(DBG2) << "Unrecognized component to ClearPortPrbsStats: "
               << apache::thrift::util::enumNameSafe(component);
  }
}

void ThriftHandler::getPortPrbsStats(
    phy::PrbsStats& prbsStats,
    int32_t portId,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  if (component == phy::PortComponent::ASIC) {
    auto asicPrbsStats = sw_->getPortAsicPrbsStats(PortID(portId));
    prbsStats.portId() = portId;
    prbsStats.component() = phy::PortComponent::ASIC;
    for (const auto& lane : asicPrbsStats) {
      prbsStats.laneStats()->push_back(lane);
      auto timeCollected = lane.timeCollected().value();
      // Store most recent timeCollected across all lane stats
      if (timeCollected > prbsStats.timeCollected()) {
        prbsStats.timeCollected() = timeCollected;
      }
    }
  } else if (
      component == phy::PortComponent::GB_SYSTEM ||
      component == phy::PortComponent::GB_LINE) {
    throw FbossError("Get gearbox prbs stats is not supported");
  } else {
    XLOG(DBG2) << "Unrecognized component to GetPortPrbsStats: "
               << apache::thrift::util::enumNameSafe(component);
  }
}

void ThriftHandler::setPortPrbs(
    int32_t portNum,
    phy::PortComponent component,
    bool enable,
    int32_t polynominal) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), portNum, enable);
  ensureConfigured(__func__);
  PortID portId = PortID(portNum);
  const auto port = sw_->getState()->getPorts()->getNodeIf(portId);
  if (!port) {
    throw FbossError("no such port ", portNum);
  }
  auto capabilities = sw_->getPortPrbsPolynomials(PortID(portNum));
  if (enable &&
      std::find(
          capabilities.begin(),
          capabilities.end(),
          static_cast<prbs::PrbsPolynomial>(polynominal)) ==
          capabilities.end()) {
    throw FbossError("Polynomial not supported");
  }
  phy::PortPrbsState newPrbsState;
  *newPrbsState.enabled() = enable;
  *newPrbsState.polynominal() = polynominal;

  if (component == phy::PortComponent::ASIC) {
    auto updateFn = [=](const shared_ptr<SwitchState>& state) {
      shared_ptr<SwitchState> newState{state};
      auto newPort = newState->getPorts()->getNodeIf(portId).get();
      newPort = newPort->modify(&newState);
      if (newPort) {
        newPort = newPort->modify(&newState);
        newPort->setAsicPrbs(newPrbsState);
      }
      return newState;
    };
    sw_->updateStateBlocking("set port asic prbs", updateFn);
  } else if (component == phy::PortComponent::GB_SYSTEM) {
    auto updateFn = [=](const shared_ptr<SwitchState>& state) {
      shared_ptr<SwitchState> newState{state};
      auto newPort = newState->getPorts()->getNodeIf(portId).get();
      newPort = newPort->modify(&newState);
      if (newPort) {
        newPort->setGbSystemPrbs(newPrbsState);
      }
      return newState;
    };
    sw_->updateStateBlocking("set port gearbox system side prbs", updateFn);
  } else if (component == phy::PortComponent::GB_LINE) {
    auto updateFn = [=](const shared_ptr<SwitchState>& state) {
      shared_ptr<SwitchState> newState{state};
      auto newPort = newState->getPorts()->getNodeIf(portId).get();
      newPort = newPort->modify(&newState);
      if (newPort) {
        newPort->setGbLinePrbs(newPrbsState);
      }
      return newState;
    };
    sw_->updateStateBlocking("set port gearbox line side prbs", updateFn);
  } else {
    XLOG(DBG2) << "Unrecognized component to setPortPrbs: "
               << apache::thrift::util::enumNameSafe(component);
  }
}

void ThriftHandler::setPortState(int32_t portNum, bool enable) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), portNum, enable);
  ensureConfigured(__func__);
  PortID portId = PortID(portNum);
  const auto port = sw_->getState()->getPorts()->getNodeIf(portId);
  if (!port) {
    throw FbossError("no such port ", portNum);
  }

  cfg::PortState newPortState =
      enable ? cfg::PortState::ENABLED : cfg::PortState::DISABLED;

  if (port->getAdminState() == newPortState) {
    XLOG(DBG2) << "setPortState: port already in state "
               << (enable ? "ENABLED" : "DISABLED");
    return;
  }

  auto updateFn = [portId, newPortState](const shared_ptr<SwitchState>& state) {
    const auto oldPort = state->getPorts()->getNodeIf(portId);
    shared_ptr<SwitchState> newState{state};
    auto newPort = oldPort->modify(&newState);
    newPort->setAdminState(newPortState);
    return newState;
  };
  sw_->updateStateBlocking("set port state", updateFn);
}

void ThriftHandler::setPortDrainState(int32_t portNum, bool drain) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), portNum, drain);
  ensureConfigured(__func__);
  PortID portId = PortID(portNum);
  const auto port = sw_->getState()->getPorts()->getNodeIf(portId);
  if (!port) {
    throw FbossError("no such port ", portNum);
  }

  if (port->getPortType() != cfg::PortType::FABRIC_PORT) {
    throw FbossError("Cannot drain/undrain non-fabric port ", portNum);
  }

  cfg::PortDrainState newPortDrainState =
      drain ? cfg::PortDrainState::DRAINED : cfg::PortDrainState::UNDRAINED;

  auto updateFn =
      [portId, newPortDrainState, drain](
          const shared_ptr<SwitchState>& state) -> shared_ptr<SwitchState> {
    const auto oldPort = state->getPorts()->getNodeIf(portId);
    if (oldPort->getPortDrainState() == newPortDrainState) {
      XLOG(DBG2) << "setPortDrainState: port already in state "
                 << (drain ? "DRAINED" : "UNDRAINED");
      return nullptr;
    }
    shared_ptr<SwitchState> newState{state};
    auto newPort = oldPort->modify(&newState);
    newPort->setPortDrainState(newPortDrainState);
    return newState;
  };
  sw_->updateStateBlocking("set port drain state", updateFn);
}

void ThriftHandler::setPortLoopbackMode(
    int32_t portNum,
    PortLoopbackMode mode) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), portNum, mode);
  ensureConfigured(__func__);
  PortID portId = PortID(portNum);
  const auto port = sw_->getState()->getPorts()->getNodeIf(portId);
  if (!port) {
    throw FbossError("no such port ", portNum);
  }

  auto newLoopbackMode = toLoopbackMode(mode);

  if (port->getLoopbackMode() == newLoopbackMode) {
    XLOG(DBG2) << "setPortLoopbackMode: port already set to lb mode : "
               << static_cast<int>(newLoopbackMode);
    return;
  }

  auto updateFn = [portId,
                   newLoopbackMode](const shared_ptr<SwitchState>& state) {
    const auto oldPort = state->getPorts()->getNodeIf(portId);
    shared_ptr<SwitchState> newState{state};
    auto newPort = oldPort->modify(&newState);
    newPort->setLoopbackMode(newLoopbackMode);
    return newState;
  };
  sw_->updateStateBlocking("set port loopback mode", updateFn);
}

void ThriftHandler::getAllPortLoopbackMode(
    std::map<int32_t, PortLoopbackMode>& port2LbMode) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  for (auto& portMap : std::as_const(*sw_->getState()->getPorts())) {
    for (auto& port : std::as_const(*portMap.second)) {
      port2LbMode[port.second->getID()] =
          toThriftLoopbackMode(port.second->getLoopbackMode());
    }
  }
}

void ThriftHandler::setInterfaceTxRx(
    std::vector<phy::TxRxEnableResponse>& /* txRxEnableResponse */,
    std::unique_ptr<
        std::vector<phy::TxRxEnableRequest>> /* txRxEnableRequests */) {
  throw FbossError("setInterfaceTxRx not implemented for this service");
}

void ThriftHandler::programInternalPhyPorts(
    std::map<int32_t, cfg::PortProfileID>& programmedPorts,
    std::unique_ptr<TransceiverInfo> transceiver,
    bool force) {
  const int32_t id = *transceiver->tcvrState()->port();
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), id, force);
  ensureConfigured(__func__);

  // Check whether the transceiver has valid id
  std::optional<phy::DataPlanePhyChip> tcvrChip;
  for (const auto& chip : sw_->getPlatformMapping()->getChips()) {
    if (*chip.second.type() == phy::DataPlanePhyChipType::TRANSCEIVER &&
        *chip.second.physicalID() == id) {
      tcvrChip = chip.second;
      break;
    }
  }
  if (!tcvrChip) {
    throw FbossError("Can't find transceiver:", id, " from PlatformMapping");
  }

  TransceiverID tcvrID = TransceiverID(id);
  auto newTransceiver = TransceiverSpec::createPresentTransceiver(*transceiver);

  const auto tcvr = sw_->getState()->getTransceivers()->getNodeIf(tcvrID);
  const auto& platformPorts = utility::getPlatformPortsByChip(
      sw_->getPlatformMapping()->getPlatformPorts(), *tcvrChip);
  // Check whether the current Transceiver in the SwitchState matches the
  // input TransceiverInfo
  if (!tcvr && !newTransceiver) {
    XLOG(DBG2) << "programInternalPhyPorts for not present Transceiver:"
               << tcvrID
               << " which doesn't exist in SwitchState. Skip re-programming";
  } else if (!force && tcvr && newTransceiver && *tcvr == *newTransceiver) {
    XLOG(DBG2) << "programInternalPhyPorts for present Transceiver:" << tcvrID
               << " matches current SwitchState. Skip re-programming";
  } else {
    auto updateFn = [&, tcvrID](const shared_ptr<SwitchState>& state) {
      auto newState = state->clone();
      auto newTransceiverMap = newState->getTransceivers()->modify(&newState);
      std::vector<PortID> portIds;
      for (const auto& platformPort : platformPorts) {
        const auto port =
            state->getPorts()->getNodeIf(PortID(*platformPort.mapping()->id()));
        if (port) {
          portIds.emplace_back(port->getID());
        }
      }

      if (portIds.size() == 0) {
        XLOG(INFO) << "No ports found for transceiver " << tcvrID
                   << ". Skip programming internal phy.";
        return newState;
      }

      if (!newTransceiver) {
        newTransceiverMap->removeNode(tcvrID);
      } else if (newTransceiverMap->getNodeIf(tcvrID)) {
        newTransceiverMap->updateNode(
            newTransceiver, sw_->getScopeResolver()->scope(portIds));
      } else {
        newTransceiverMap->addNode(
            newTransceiver, sw_->getScopeResolver()->scope(portIds));
      }

      // Now we also need to update the port profile config and pin configs
      // using the newTransceiver
      std::optional<cfg::PlatformPortConfigOverrideFactor> factor;
      if (newTransceiver != nullptr) {
        factor = newTransceiver->toPlatformPortConfigOverrideFactor();
      }
      sw_->getPlatformMapping()->customizePlatformPortConfigOverrideFactor(
          factor);
      for (const auto& platformPort : platformPorts) {
        const auto oldPort =
            state->getPorts()->getNodeIf(PortID(*platformPort.mapping()->id()));
        if (!oldPort) {
          continue;
        }
        PlatformPortProfileConfigMatcher matcher{
            oldPort->getProfileID(), oldPort->getID(), factor};
        auto portProfileCfg =
            sw_->getPlatformMapping()->getPortProfileConfig(matcher);
        if (!portProfileCfg) {
          throw FbossError(
              "No port profile config found with matcher:", matcher.toString());
        }
        if (oldPort->isEnabled() &&
            *portProfileCfg->speed() != oldPort->getSpeed()) {
          throw FbossError(
              oldPort->getName(),
              " has mismatched speed on profile:",
              apache::thrift::util::enumNameSafe(oldPort->getProfileID()),
              " and config:",
              apache::thrift::util::enumNameSafe(oldPort->getSpeed()));
        }
        auto newProfileConfigRef = portProfileCfg->iphy();
        const auto& newPinConfigs =
            sw_->getPlatformMapping()->getPortIphyPinConfigs(matcher);

        auto newPort = oldPort->modify(&newState);
        newPort->setProfileConfig(*newProfileConfigRef);
        newPort->resetPinConfigs(newPinConfigs);
        newPort->setSerdesCustomCollection(
            sw_->getPlatformMapping()->getPortSerdesCustomCollection(matcher));
      }

      return newState;
    };
    sw_->updateStateBlocking(
        fmt::format("program iphy ports for transceiver: {}", id), updateFn);
  }

  // fetch the programmed profiles
  for (const auto& platformPort : platformPorts) {
    const auto port = sw_->getState()->getPorts()->getNodeIf(
        PortID(*platformPort.mapping()->id()));
    if (port && port->isEnabled()) {
      // Only return ports actually exist and are enabled
      programmedPorts.emplace(port->getID(), port->getProfileID());
    }
  }
}

void ThriftHandler::getRouteTable(std::vector<UnicastRoute>& routes) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();
  forAllRoutes(
      state, [&routes, &state](const RouterID& /*rid*/, const auto& route) {
        if (!route->isResolved()) {
          XLOG(DBG2) << "Skipping unresolved route: "
                     << route->toFollyDynamic();
          return;
        }
        const auto& fwdInfo = route->getForwardInfo();
        std::optional<std::vector<NextHopThrift>> overrideNextHops;
        if (fwdInfo.getOverrideNextHops().has_value()) {
          overrideNextHops =
              util::fromRouteNextHopSet(getNormalizedNextHops(state, fwdInfo));
        }
        routes.emplace_back(toUnicastRoute(
            route,
            fwdInfo,
            util::fromRouteNextHopSet(
                getNonOverrideNormalizedNextHops(state, fwdInfo)),
            util::fromFwdNextHops(getNextHops(state, fwdInfo)),
            std::move(overrideNextHops)));
      });
}

void ThriftHandler::getRouteTableByClient(
    std::vector<UnicastRoute>& routes,
    int16_t client) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();
  forAllRoutes(
      state,
      [&routes, &state, client](const RouterID& /*rid*/, const auto& route) {
        auto entry = route->getEntryForClient(ClientID(client));
        if (!entry) {
          return;
        }
        auto nextHops =
            util::fromRouteNextHopSet(getClientNextHops(state, *entry));
        std::vector<network::thrift::BinaryAddress> nextHopAddrs;
        for (const auto& nh : nextHops) {
          nextHopAddrs.emplace_back(*nh.address());
        }
        routes.emplace_back(toUnicastRoute(
            route, *entry, std::move(nextHops), std::move(nextHopAddrs)));
      });
}

void ThriftHandler::getRouteTableDetails(std::vector<RouteDetails>& routes) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();
  ClientNextHopsResolver resolveClient =
      [&state](const RouteNextHopEntry& entry) {
        return getClientNextHops(state, entry);
      };
  forAllRoutes(
      state,
      [&routes, &state, &resolveClient](
          const RouterID& /*rid*/, const auto& route) {
        routes.emplace_back(toRouteDetails(state, route, resolveClient));
      });
}

void ThriftHandler::getRouteTableSize(RouteCount& routeCount) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();
  auto [v4Count, v6Count] = state->getFibsInfoMap()->getRouteCount();
  *routeCount.v4Count() = v4Count;
  *routeCount.v6Count() = v6Count;
}

void ThriftHandler::getIpRoute(
    UnicastRoute& route,
    std::unique_ptr<Address> addr,
    int32_t vrfId) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  folly::IPAddress ipAddr = toIPAddress(*addr);

  auto state = sw_->getState();
  if (ipAddr.isV4()) {
    auto match = sw_->longestMatch(state, ipAddr.asV4(), RouterID(vrfId));
    if (!match || !match->isResolved()) {
      *route.dest()->ip() = toBinaryAddress(IPAddressV4("0.0.0.0"));
      *route.dest()->prefixLength() = 0;
      return;
    }
    const auto& fwdInfo = match->getForwardInfo();
    route = toUnicastRoute(
        match,
        fwdInfo,
        util::fromRouteNextHopSet(
            getNonOverrideNormalizedNextHops(state, fwdInfo)),
        util::fromFwdNextHops(getNextHops(state, fwdInfo)));
  } else {
    auto match = sw_->longestMatch(state, ipAddr.asV6(), RouterID(vrfId));
    if (!match || !match->isResolved()) {
      *route.dest()->ip() = toBinaryAddress(IPAddressV6("::0"));
      *route.dest()->prefixLength() = 0;
      return;
    }
    const auto& fwdInfo = match->getForwardInfo();
    route = toUnicastRoute(
        match,
        fwdInfo,
        util::fromRouteNextHopSet(
            getNonOverrideNormalizedNextHops(state, fwdInfo)),
        util::fromFwdNextHops(getNextHops(state, fwdInfo)));
  }
}

void ThriftHandler::getIpRouteDetails(
    RouteDetails& route,
    std::unique_ptr<Address> addr,
    int32_t vrfId) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  folly::IPAddress ipAddr = toIPAddress(*addr);
  auto state = sw_->getState();
  ClientNextHopsResolver resolveClient =
      [&state](const RouteNextHopEntry& entry) {
        return getClientNextHops(state, entry);
      };

  if (ipAddr.isV4()) {
    auto match = sw_->longestMatch(state, ipAddr.asV4(), RouterID(vrfId));
    if (match && match->isResolved()) {
      route = toRouteDetails(state, match, resolveClient);
    }
  } else {
    auto match = sw_->longestMatch(state, ipAddr.asV6(), RouterID(vrfId));
    if (match && match->isResolved()) {
      route = toRouteDetails(state, match, resolveClient);
    }
  }
}

void ThriftHandler::getHwAgentConnectionStatus(
    std::map<int16_t, HwAgentEventSyncStatus>& hwAgentSyncStatusMap) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  sw_->stats()->getHwAgentStatus(hwAgentSyncStatusMap);
}

void ThriftHandler::getRouteCounterBytes(
    std::map<std::string, std::int64_t>& routeCounters,
    std::unique_ptr<std::vector<std::string>> counters) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto statMap = facebook::fb303::fbData->getStatMap();
  for (const auto& statName : *counters) {
    // returns default stat if statName does not exists
    auto statPtr = statMap->getStatPtrNoExport(statName);
    auto lockedStatPtr = statPtr->wlock();
    lockedStatPtr->update(seconds(facebook::fb303::get_legacy_stats_time()));
    auto numLevels = lockedStatPtr->numLevels();
    // Cumulative (ALLTIME) counters are at (numLevels - 1)
    auto value = lockedStatPtr->sum(numLevels - 1);
    routeCounters.try_emplace(statName, value);
  }
}

void ThriftHandler::getAllRouteCounterBytes(
    std::map<std::string, std::int64_t>& routeCounters) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();
  std::unordered_set<std::string> countersUsed;
  forAllRoutes(
      state, [&countersUsed](const RouterID& /*rid*/, const auto& route) {
        if (route->isResolved()) {
          auto counterID = route->getForwardInfo().getCounterID();
          if (counterID.has_value()) {
            std::string statName = counterID.value();
            countersUsed.emplace(statName);
          }
        }
      });
  auto counters = std::make_unique<std::vector<std::string>>();
  for (const auto& counter : countersUsed) {
    counters->emplace_back(counter);
  }
  return getRouteCounterBytes(routeCounters, std::move(counters));
}

void ThriftHandler::getLldpNeighbors(vector<LinkNeighborThrift>& results) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto lldpMgr = sw_->getLldpMgr();
  if (lldpMgr == nullptr) {
    throw std::runtime_error("lldpMgr is not configured");
  }

  auto* db = lldpMgr->getDB();
  // Do an immediate check for expired neighbors
  db->pruneExpiredNeighbors();
  auto neighbors = db->getNeighbors();
  results.reserve(neighbors.size());
  auto now = steady_clock::now();
  for (const auto& entry : neighbors) {
    results.push_back(thriftLinkNeighbor(*sw_, entry, now));
  }
}

void ThriftHandler::startPktCapture(unique_ptr<CaptureInfo> info) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* mgr = sw_->getCaptureMgr();
  auto capture = make_unique<PktCapture>(
      *info->name(), *info->maxPackets(), *info->direction(), *info->filter());
  mgr->startCapture(std::move(capture));
}

void ThriftHandler::stopPktCapture(unique_ptr<std::string> name) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* mgr = sw_->getCaptureMgr();
  mgr->forgetCapture(*name);
}

void ThriftHandler::stopAllPktCaptures() {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* mgr = sw_->getCaptureMgr();
  mgr->forgetAllCaptures();
}

void ThriftHandler::startLoggingRouteUpdates(
    std::unique_ptr<RouteUpdateLoggingInfo> info) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  folly::IPAddress addr = toIPAddress(*info->prefix()->ip());
  uint8_t mask = static_cast<uint8_t>(*info->prefix()->prefixLength());
  RouteUpdateLoggingInstance loggingInstance{
      RoutePrefix<folly::IPAddress>{addr, mask},
      *info->identifier(),
      *info->exact()};
  routeUpdateLogger->startLoggingForPrefix(loggingInstance);
}

void ThriftHandler::startLoggingMplsRouteUpdates(
    std::unique_ptr<MplsRouteUpdateLoggingInfo> info) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  routeUpdateLogger->startLoggingForLabel(*info->label(), *info->identifier());
}

void ThriftHandler::stopLoggingRouteUpdates(
    std::unique_ptr<IpPrefix> prefix,
    std::unique_ptr<std::string> identifier) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  folly::IPAddress addr = toIPAddress(*prefix->ip());
  uint8_t mask = static_cast<uint8_t>(*prefix->prefixLength());
  routeUpdateLogger->stopLoggingForPrefix(addr, mask, *identifier);
}

void ThriftHandler::stopLoggingAnyRouteUpdates(
    std::unique_ptr<std::string> identifier) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  routeUpdateLogger->stopLoggingForIdentifier(*identifier);
}

void ThriftHandler::stopLoggingAnyMplsRouteUpdates(
    std::unique_ptr<std::string> identifier) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  routeUpdateLogger->stopLabelLoggingForIdentifier(*identifier);
}

void ThriftHandler::stopLoggingMplsRouteUpdates(
    std::unique_ptr<MplsRouteUpdateLoggingInfo> info) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  routeUpdateLogger->stopLoggingForLabel(*info->label(), *info->identifier());
}

void ThriftHandler::getRouteUpdateLoggingTrackedPrefixes(
    std::vector<RouteUpdateLoggingInfo>& infos) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  for (const auto& tracked : routeUpdateLogger->getTrackedPrefixes()) {
    RouteUpdateLoggingInfo info;
    IpPrefix prefix;
    *prefix.ip() = toBinaryAddress(tracked.prefix.network());
    *prefix.prefixLength() = tracked.prefix.mask();
    *info.prefix() = prefix;
    *info.identifier() = tracked.identifier;
    *info.exact() = tracked.exact;
    infos.push_back(info);
  }
}

void ThriftHandler::getMplsRouteUpdateLoggingTrackedLabels(
    std::vector<MplsRouteUpdateLoggingInfo>& infos) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto* routeUpdateLogger = sw_->getRouteUpdateLogger();
  for (const auto& tracked : routeUpdateLogger->gettTrackedLabels()) {
    MplsRouteUpdateLoggingInfo info;
    *info.identifier() = tracked.first;
    info.label() = tracked.second.value();
    infos.push_back(info);
  }
}

void ThriftHandler::sendPkt(
    int32_t port,
    int32_t vlan,
    unique_ptr<fbstring> data,
    int32_t numOfPkts,
    int32_t intervalInMs) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureNotFabric(__func__);
  for (int32_t i = 0; i < numOfPkts; ++i) {
    size_t txPacketCount = TxPacket::getPacketCounter()->load();
    if (txPacketCount >= FLAGS_max_tx_packets) {
      XLOG_EVERY_MS(ERR, 5000) << "Reached max tx packets threshold "
                               << txPacketCount << ". Dropping packets.";
      sw_->stats()->updateTxBufferLimitExceededDrops();
      continue;
    }
    auto buf = IOBuf::copyBuffer(
        reinterpret_cast<const uint8_t*>(data->data()), data->size());
    auto pkt = make_unique<MockRxPacket>(std::move(buf));
    pkt->setSrcPort(PortID(port));
    pkt->setSrcVlan(VlanID(vlan));
    sw_->packetReceived(std::move(pkt));
    if (i < numOfPkts - 1) {
      // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
      std::this_thread::sleep_for(std::chrono::milliseconds(intervalInMs));
    }
  }
}

void ThriftHandler::sendPktHex(
    int32_t port,
    int32_t vlan,
    unique_ptr<fbstring> hex,
    int32_t numOfPkts,
    int32_t intervalInMs) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureNotFabric(__func__);
  for (int32_t i = 0; i < numOfPkts; ++i) {
    size_t txPacketCount = TxPacket::getPacketCounter()->load();
    if (txPacketCount >= FLAGS_max_tx_packets) {
      XLOG_EVERY_MS(ERR, 5000) << "Reached max tx packets threshold "
                               << txPacketCount << ". Dropping packets.";
      sw_->stats()->updateTxBufferLimitExceededDrops();
      continue;
    }
    auto pkt = MockRxPacket::fromHex(StringPiece(*hex));
    pkt->setSrcPort(PortID(port));
    pkt->setSrcVlan(VlanID(vlan));
    sw_->packetReceived(std::move(pkt));
    if (i < numOfPkts - 1) {
      // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
      std::this_thread::sleep_for(std::chrono::milliseconds(intervalInMs));
    }
  }
}

void ThriftHandler::txPkt(
    int32_t port,
    unique_ptr<fbstring> data,
    int32_t numOfPkts,
    int32_t intervalInMs) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureNotFabric(__func__);

  for (int32_t i = 0; i < numOfPkts; ++i) {
    size_t txPacketCount = TxPacket::getPacketCounter()->load();
    if (txPacketCount >= FLAGS_max_tx_packets) {
      XLOG_EVERY_MS(ERR, 5000) << "Reached max tx packets threshold "
                               << txPacketCount << ". Dropping packets.";
      sw_->stats()->updateTxBufferLimitExceededDrops();
      continue;
    }
    unique_ptr<TxPacket> pkt =
        sw_->allocatePacket(static_cast<uint32_t>(data->size()));
    RWPrivateCursor cursor(pkt->buf());
    cursor.push(StringPiece(*data));
    sw_->sendPacketOutOfPortAsync(std::move(pkt), PortID(port));
    if (i < numOfPkts - 1) {
      // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
      std::this_thread::sleep_for(std::chrono::milliseconds(intervalInMs));
    }
  }
}

void ThriftHandler::txPktL2(
    unique_ptr<fbstring> data,
    int32_t numOfPkts,
    int32_t intervalInMs) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureNotFabric(__func__);

  for (int32_t i = 0; i < numOfPkts; ++i) {
    size_t txPacketCount = TxPacket::getPacketCounter()->load();
    if (txPacketCount >= FLAGS_max_tx_packets) {
      XLOG_EVERY_MS(ERR, 5000) << "Reached max tx packets threshold "
                               << txPacketCount << ". Dropping packets.";
      sw_->stats()->updateTxBufferLimitExceededDrops();
      continue;
    }
    unique_ptr<TxPacket> pkt =
        sw_->allocatePacket(static_cast<uint32_t>(data->size()));
    RWPrivateCursor cursor(pkt->buf());
    cursor.push(StringPiece(*data));
    sw_->sendPacketSwitchedAsync(std::move(pkt));
    if (i < numOfPkts - 1) {
      // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
      std::this_thread::sleep_for(std::chrono::milliseconds(intervalInMs));
    }
  }
}

void ThriftHandler::txPktL3(
    unique_ptr<fbstring> payload,
    int32_t numOfPkts,
    int32_t intervalInMs) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureNotFabric(__func__);

  const auto interfaceMap = sw_->getState()->getInterfaces();
  if (interfaceMap->numNodes() == 0) {
    throw FbossError("No interface configured");
  }
  std::optional<InterfaceID> intfID;
  cfg::InterfaceType type{cfg::InterfaceType::VLAN};
  for (const auto& [_, intfs] : std::as_const(*interfaceMap)) {
    if (!intfs->empty()) {
      intfID = intfs->at(0)->getID();
      type = intfs->at(0)->getType();
      break;
    }
  }
  CHECK(intfID.has_value());

  for (int32_t i = 0; i < numOfPkts; ++i) {
    size_t txPacketCount = TxPacket::getPacketCounter()->load();
    if (txPacketCount >= FLAGS_max_tx_packets) {
      XLOG_EVERY_MS(ERR, 5000) << "Reached max tx packets threshold "
                               << txPacketCount << ". Dropping packets.";
      sw_->stats()->updateTxBufferLimitExceededDrops();
      continue;
    }
    unique_ptr<TxPacket> pkt = sw_->allocateL3TxPacket(
        static_cast<uint32_t>(payload->size()),
        (type == cfg::InterfaceType::VLAN));
    RWPrivateCursor cursor(pkt->buf());
    cursor.push(StringPiece(*payload));
    sw_->sendL3Packet(std::move(pkt), *intfID);
    if (i < numOfPkts - 1) {
      // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
      std::this_thread::sleep_for(std::chrono::milliseconds(intervalInMs));
    }
  }
}

Vlan* ThriftHandler::getVlan(int32_t vlanId) {
  ensureConfigured(__func__);
  return sw_->getState()->getVlans()->getNode(VlanID(vlanId)).get();
}

Vlan* ThriftHandler::getVlan(const std::string& vlanName) {
  ensureConfigured(__func__);
  return sw_->getState()->getVlans()->getVlanSlow(vlanName).get();
}

int32_t ThriftHandler::flushNeighborEntry(
    unique_ptr<BinaryAddress> ip,
    int32_t vlan) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  auto parsedIP = toIPAddress(*ip);

  try {
    int32_t result;
    // VOQ switches don't support VLANs. The thrift client will pass
    // interfaceID instead of VLAN. NPU switches support VLANs, but vlanID is
    // identical to interfaceID.
    InterfaceID intfID = InterfaceID(vlan);
    result =
        sw_->getNeighborUpdater()->flushEntryForIntf(intfID, parsedIP).get();

    // Check if NDP static neighbor is enabled
    if (FLAGS_ndp_static_neighbor) {
      // After clearing NDP entry, send neighbor solicitation for configured
      // interfaces
      XLOG(DBG4) << "ThriftHandler::flushNeighborEntry - Entry flushed for "
                 << parsedIP.str()
                 << ", checking for interfaces that need neighbor solicitation";

      if (parsedIP.isV6()) {
        sw_->sendNeighborSolicitationForConfiguredInterfaces(
            "NDP entry clear", parsedIP.asV6());
      }
    }

    return result;
  } catch (...) {
    throw FbossError(
        "Entry : ",
        parsedIP,
        " could not be deleted. Entry is either of type STATIC, DYNAMIC or does not exist");
  }
}

int32_t ThriftHandler::flushNeighborEntries(
    unique_ptr<std::vector<IfAndIP>> entries) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  int32_t totalFlushed = 0;

  for (const auto& entry : *entries) {
    try {
      totalFlushed += flushNeighborEntry(
          std::make_unique<BinaryAddress>(*entry.ip()), *entry.interfaceID());
    } catch (...) {
      XLOG(WARNING) << "Failed to flush neighbor entry for "
                    << toIPAddress(*entry.ip()).str() << " on interface "
                    << *entry.interfaceID();
    }
  }

  return totalFlushed;
}

void ThriftHandler::getVlanAddresses(Addresses& addrs, int32_t vlan) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureNPU(__func__);
  getVlanAddresses(getVlan(vlan), addrs, toAddress);
}

void ThriftHandler::getVlanAddressesByName(
    Addresses& addrs,
    unique_ptr<string> vlan) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureNPU(__func__);
  getVlanAddresses(getVlan(*vlan), addrs, toAddress);
}

void ThriftHandler::getVlanBinaryAddresses(
    BinaryAddresses& addrs,
    int32_t vlan) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureNPU(__func__);
  getVlanAddresses(getVlan(vlan), addrs, toBinaryAddress);
}

void ThriftHandler::getVlanBinaryAddressesByName(
    BinaryAddresses& addrs,
    const std::unique_ptr<std::string> vlan) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureNPU(__func__);
  getVlanAddresses(getVlan(*vlan), addrs, toBinaryAddress);
}

template <typename ADDR_TYPE, typename ADDR_CONVERTER>
void ThriftHandler::getVlanAddresses(
    const Vlan* vlan,
    std::vector<ADDR_TYPE>& addrs,
    ADDR_CONVERTER& converter) {
  CHECK(vlan);
  // Explicitly take ownership of interface map
  auto interfaces = sw_->getState()->getInterfaces();
  for (const auto& [_, intfMap] : std::as_const(*interfaces)) {
    for (const auto& iter : std::as_const(*intfMap)) {
      auto intf = iter.second;
      if (intf->getVlanID() == vlan->getID()) {
        for (auto addrAndMask : intf->getAddressesCopy()) {
          addrs.push_back(converter(addrAndMask.first));
        }
      }
    }
  }
}

BootType ThriftHandler::getBootType() {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  return sw_->getBootType();
}

void ThriftHandler::ensureConfigured(StringPiece function) const {
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

void ThriftHandler::ensureNPU(StringPiece function) const {
  ensureConfigured(function);
  if (sw_->getSwitchInfoTable().haveNpuSwitches()) {
    return;
  }

  if (!function.empty()) {
    XLOG(DBG1) << function << " only supported on NPU Switch type: ";
  }
  throw FbossError(function, " is only supported on NPU switch type");
}

void ThriftHandler::ensureNotFabric(StringPiece function) const {
  ensureConfigured(function);
  if (!sw_->getSwitchInfoTable().haveL3Switches()) {
    if (!function.empty()) {
      XLOG(DBG1) << function << " not supported on Fabric Switch type: ";
    }
    throw FbossError(function, " not supported on Fabric switch type");
  }
}

void ThriftHandler::ensureVoqOrFabric(StringPiece function) const {
  ensureConfigured(function);
  if (!sw_->getSwitchInfoTable().haveVoqSwitches() &&
      !sw_->getSwitchInfoTable().haveFabricSwitches()) {
    if (!function.empty()) {
      XLOG(DBG1) << function
                 << " only supported on Voq or Fabric Switch type: ";
    }
    throw FbossError(function, " only supported on Voq or Fabric Switch type");
  }
}

int32_t ThriftHandler::getIdleTimeout() {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  if (thriftIdleTimeout_ < 0) {
    throw FbossError("Idle timeout has not been set");
  }
  return thriftIdleTimeout_;
}

void ThriftHandler::reloadConfig() {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  return sw_->applyConfig("reload config initiated by thrift call", true);
}

void ThriftHandler::validateConfig(
    thrift::ConfigValidationResult& result,
    std::unique_ptr<std::string> config,
    thrift::ConfigApplyMethod applyMethod) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  // Not being able to validate yet is an error, not a verdict on the config.
  ensureConfigured(__func__);
  auto reject = [&result](
                    std::string message,
                    std::optional<thrift::ConfigApplyMethod> required =
                        std::nullopt) {
    thrift::ConfigValidationError error;
    error.message() = std::move(message);
    if (required) {
      error.requiredApplyMethod() = *required;
    }
    result.errors()->push_back(std::move(error));
  };
  std::unique_ptr<AgentConfig> agentConfig;
  try {
    agentConfig = AgentConfig::fromRawConfig(*config);
  } catch (const std::exception& ex) {
    reject(fmt::format("Failed to parse config: {}", folly::exceptionStr(ex)));
    return;
  }
  // Anything the dry run throws is caused by the candidate config.
  try {
    sw_->validateConfig(*agentConfig->thrift.sw(), applyMethod);
  } catch (const RestartRequiredError& ex) {
    reject(ex.what(), ex.requiredApplyMethod());
  } catch (const FbossError& ex) {
    reject(ex.what());
  } catch (const std::exception& ex) {
    reject(folly::exceptionStr(ex).toStdString());
  }
}

int64_t ThriftHandler::getLastConfigAppliedInMs() {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  return *sw_->getConfigAppliedInfo().lastAppliedInMs();
}

void ThriftHandler::getConfigAppliedInfo(ConfigAppliedInfo& configAppliedInfo) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  configAppliedInfo = sw_->getConfigAppliedInfo();
}

void ThriftHandler::getLacpPartnerPair(
    LacpPartnerPair& lacpPartnerPair,
    int32_t portID) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  auto lagManager = sw_->getLagManager();
  if (!lagManager) {
    throw FbossError("LACP not enabled");
  }

  lagManager->populatePartnerPair(static_cast<PortID>(portID), lacpPartnerPair);
}

void ThriftHandler::getAllLacpPartnerPairs(
    std::vector<LacpPartnerPair>& lacpPartnerPairs) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  auto lagManager = sw_->getLagManager();
  if (!lagManager) {
    throw FbossError("LACP not enabled");
  }

  lagManager->populatePartnerPairs(lacpPartnerPairs);
}

SwitchRunState ThriftHandler::getSwitchRunState() {
  auto log = LOG_THRIFT_CALL(DBG2);
  ensureConfigured(__func__);
  return sw_->getSwitchRunState();
}

SSLType ThriftHandler::getSSLPolicy() {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  SSLType sslType = SSLType::PERMITTED;

  if (sslPolicy_ == apache::thrift::SSLPolicy::DISABLED) {
    sslType = SSLType::DISABLED;
  } else if (sslPolicy_ == apache::thrift::SSLPolicy::PERMITTED) {
    sslType = SSLType::PERMITTED;
  } else if (sslPolicy_ == apache::thrift::SSLPolicy::REQUIRED) {
    sslType = SSLType::REQUIRED;
  } else {
    throw FbossError("Invalid SSL Policy");
  }

  return sslType;
}

void ThriftHandler::setExternalLedState(
    int32_t portNum,
    PortLedExternalState ledState) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  PortID portId = PortID(portNum);

  auto updateFn = [&portId, ledState](const shared_ptr<SwitchState>& state) {
    auto newState = state->clone();
    auto port = state->getPorts()->getNode(portId);
    auto newPort = port->modify(&newState);
    newPort->setLedPortExternalState(ledState);
    return newState;
  };
  sw_->updateStateBlocking("set port LED state from thrift handler", updateFn);
}

void ThriftHandler::addMplsRoutes(
    int16_t clientId,
    std::unique_ptr<std::vector<MplsRoute>> mplsRoutes) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(clientId));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  ensureConfigured(__func__);
  if (FLAGS_mpls_rib) {
    return addMplsRibRoutes(clientId, std::move(mplsRoutes), false /* sync */);
  }
  auto updateFn = [=, routes = std::move(*mplsRoutes), this](
                      const std::shared_ptr<SwitchState>& state) {
    auto newState = state->clone();

    addMplsRoutesImpl(&newState, ClientID(clientId), routes);
    if (!sw_->isValidStateUpdateDryRun(
            StateDelta(state, newState), sw_->stats())) {
      throw FbossError("Invalid MPLS routes");
    }
    return newState;
  };
  try {
    sw_->updateStateWithHwFailureProtection("addMplsRoutes", updateFn);
  } catch (const FbossHwUpdateError& ex) {
    translateToFibError(ex);
  }
}

void ThriftHandler::addMplsRoutesImpl(
    std::shared_ptr<SwitchState>* state,
    ClientID clientId,
    const std::vector<MplsRoute>& mplsRoutes) const {
  /* cache to return interface for non-link local but directly connected next
   * hop address for label fib entry  */
  folly::F14FastMap<std::pair<RouterID, folly::IPAddress>, InterfaceID>
      labelFibEntryNextHopAddress2Interface;

  for (const auto& mplsRoute : mplsRoutes) {
    auto topLabel = *mplsRoute.topLabel();
    if (topLabel > mpls_constants::MAX_MPLS_LABEL_) {
      throw FbossError("invalid value for label ", topLabel);
    }
    for (const auto& nhop : *mplsRoute.nextHops()) {
      if (!nhop.srv6SegmentList()->empty()) {
        throw FbossError("MPLS route next hop cannot have srv6SegmentList");
      }
    }
    auto adminDistance = mplsRoute.adminDistance().has_value()
        ? mplsRoute.adminDistance().value()
        : sw_->clientIdToAdminDistance(static_cast<int>(clientId));
    // check for each next hop if these are resolved, if not resolve them
    // unresolved next hop must always be directly connected for MPLS
    // so unresolved next hops must be directly reachable via one of the
    // interface
    LabelNextHopSet nexthops;
    for (auto& nexthop : util::toRouteNextHopSet(*mplsRoute.nextHops())) {
      if (nexthop.isResolved() || nexthop.isPopAndLookup()) {
        nexthops.emplace(nexthop);
        continue;
      }
      if (nexthop.addr().isV6() && nexthop.addr().isLinkLocal()) {
        throw FbossError(
            "v6 link-local nexthop: ",
            nexthop.addr().str(),
            " must have interface id");
      }
      // BGP leaks MPLS routes to OpenR which then sends routes to agent
      // In such routes, interface id information is absent, because neither
      // BGP nor OpenR has enough information (in different scenarios) to
      // resolve this interface ID. Consequently doing this in agent. Each
      // such unresolved next hop will always be in the subnet of one of the
      // interface routes. look for all interfaces of a router to find an
      // interface which can reach this next hop. searching interfaces of a
      // default router, in future if multiple routers are to be supported,
      // router id must either be part of MPLS route or some configured router
      // id for unresolved MPLS next hops.
      // router id of MPLS route is to be used ONLY for resolving unresolved
      // next hop addresses. MPLS has no notion of multiple switching domains
      // within the same switch, all the labels must be unique.
      // So router ID has no relevance to label switching.
      auto iter = labelFibEntryNextHopAddress2Interface.find(
          std::make_pair(RouterID(0), nexthop.addr()));
      if (iter == labelFibEntryNextHopAddress2Interface.end()) {
        auto result = (*state)->getInterfaces()->getIntfToReach(
            RouterID(0), nexthop.addr());
        if (!result) {
          throw FbossError(
              "nexthop : ", nexthop.addr().str(), " is not connected");
        }
        if (result->hasAddress(nexthop.addr())) {
          // attempt to program local interface address as next hop
          throw FbossError(
              "invalid next hop, nexthop : ",
              nexthop.addr().str(),
              " is same as interface address");
        }

        std::tie(iter, std::ignore) =
            labelFibEntryNextHopAddress2Interface.emplace(
                std::make_pair<RouterID, folly::IPAddress>(
                    RouterID(0), nexthop.addr()),
                result->getID());
      }

      nexthops.emplace(ResolvedNextHop(
          nexthop.addr(),
          iter->second,
          nexthop.weight(),
          nexthop.labelForwardingAction()));
    }

    auto newState = programLabel(
        *(sw_->getScopeResolver()),
        *state,
        topLabel,
        ClientID(clientId),
        adminDistance,
        std::move(nexthops));
    *state = newState;
  }
}

void ThriftHandler::addMplsRibRoutes(
    int16_t clientId,
    std::unique_ptr<std::vector<MplsRoute>> mplsRoutes,
    bool sync) const {
  auto updater = sw_->getRouteUpdater();
  auto clientID = ClientID(clientId);
  for (const auto& route : *mplsRoutes) {
    int topLabel = *route.topLabel();
    if (topLabel > mpls_constants::MAX_MPLS_LABEL_) {
      throw FbossError("invalid value for label ", topLabel);
    }
    for (const auto& nhop : *route.nextHops()) {
      if (!nhop.srv6SegmentList()->empty()) {
        throw FbossError("MPLS route next hop cannot have srv6SegmentList");
      }
    }
    updater.addRoute(clientID, route);
  }
  RouteUpdateWrapper::SyncFibFor syncFibs;
  if (sync) {
    syncFibs.insert({RouterID(0), clientID});
  }
  // count the number of times we attempt to update the FIB
  sw_->stats()->routeProgrammingUpdateAttempts();

  try {
    updater.program(
        {syncFibs, RouteUpdateWrapper::SyncFibInfo::SyncFibType::MPLS_ONLY});
  } catch (const FbossHwUpdateError& ex) {
    sw_->stats()->routeProgrammingUpdateFailures();
    translateToFibError(ex);
  }
}

void ThriftHandler::deleteMplsRoutes(
    int16_t clientId,
    std::unique_ptr<std::vector<MplsLabel>> topLabels) {
  auto clientName = apache::thrift::util::enumNameSafe(ClientID(clientId));
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), clientName);
  ensureConfigured(__func__);
  if (FLAGS_mpls_rib) {
    return deleteMplsRibRoutes(clientId, std::move(topLabels));
  }
  auto updateFn = [=, topLabels = std::move(*topLabels), this](
                      const std::shared_ptr<SwitchState>& state) {
    auto newState = state->clone();
    for (const auto topLabel : topLabels) {
      if (topLabel > mpls_constants::MAX_MPLS_LABEL_) {
        throw FbossError("invalid value for label ", topLabel);
      }
      newState = unprogramLabel(
          *(sw_->getScopeResolver()), newState, topLabel, ClientID(clientId));
    }
    return newState;
  };
  sw_->updateStateBlocking("deleteMplsRoutes", updateFn);
}

void ThriftHandler::deleteMplsRibRoutes(
    int16_t clientId,
    std::unique_ptr<std::vector<MplsLabel>> topLabels) const {
  auto updater = sw_->getRouteUpdater();
  auto clientID = ClientID(clientId);
  for (const auto& label : *topLabels) {
    if (label > mpls_constants::MAX_MPLS_LABEL_) {
      throw FbossError("invalid value for label ", label);
    }
    updater.delRoute(MplsLabel(label), clientID);
  }
  // count the number of times we attempt to update the FIB
  sw_->stats()->routeProgrammingUpdateAttempts();

  try {
    updater.program();
  } catch (const FbossHwUpdateError& ex) {
    sw_->stats()->routeProgrammingUpdateFailures();
    translateToFibError(ex);
  }
  return;
}

void ThriftHandler::syncMplsFib(
    int16_t clientId,
    std::unique_ptr<std::vector<MplsRoute>> mplsRoutes) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  if (FLAGS_mpls_rib) {
    return addMplsRibRoutes(clientId, std::move(mplsRoutes), true /* sync */);
  }
  auto updateFn = [=, routes = std::move(*mplsRoutes), this](
                      const std::shared_ptr<SwitchState>& state) {
    auto newState = purgeEntriesForClient(
        *(sw_->getScopeResolver()), state, ClientID(clientId));
    addMplsRoutesImpl(&newState, ClientID(clientId), routes);
    if (!sw_->isValidStateUpdateDryRun(
            StateDelta(state, newState), sw_->stats())) {
      throw FbossError("Invalid MPLS routes");
    }
    return newState;
  };
  try {
    sw_->updateStateWithHwFailureProtection("syncMplsFib", updateFn);
  } catch (const FbossHwUpdateError& ex) {
    translateToFibError(ex);
  }
}

void ThriftHandler::getMplsRouteTableByClient(
    std::vector<MplsRoute>& mplsRoutes,
    int16_t clientId) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();
  auto labelFib = state->getLabelForwardingInformationBase();
  for (const auto& iter : std::as_const(*labelFib)) {
    for (const auto& [_, entry] : std::as_const(*iter.second)) {
      auto labelNextHopEntry = entry->getEntryForClient(ClientID(clientId));
      if (!labelNextHopEntry) {
        continue;
      }
      MplsRoute mplsRoute;
      mplsRoute.topLabel() = entry->getID();
      mplsRoute.adminDistance() = labelNextHopEntry->getAdminDistance();
      mplsRoute.nextHops() = util::fromRouteNextHopSet(
          getMplsClientNextHops(state, *labelNextHopEntry));
      mplsRoutes.emplace_back(std::move(mplsRoute));
    }
  }
}

void ThriftHandler::getAllMplsRouteDetails(
    std::vector<MplsRouteDetails>& mplsRouteDetails) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  const auto labelFib = sw_->getState()->getLabelForwardingInformationBase();
  for (const auto& iter : std::as_const(*labelFib)) {
    for (const auto& [_, entry] : std::as_const(*iter.second)) {
      MplsRouteDetails details;
      getMplsRouteDetails(details, entry->getID());
      mplsRouteDetails.push_back(details);
    }
  }
}

void ThriftHandler::getMplsRouteDetails(
    MplsRouteDetails& mplsRouteDetail,
    MplsLabel topLabel) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();
  const auto entry =
      state->getLabelForwardingInformationBase()->getNode(topLabel);
  ClientNextHopsResolver resolveClient =
      [&state](const RouteNextHopEntry& entry) {
        return getMplsClientNextHops(state, entry);
      };
  mplsRouteDetail.topLabel() = entry->getID();
  mplsRouteDetail.nextHopMulti() =
      entry->getEntryForClients().toThriftLegacy(std::nullopt, resolveClient);
  const auto& fwd = entry->getForwardInfo();
  for (const auto& nh : getMplsNextHops(state, fwd)) {
    mplsRouteDetail.nextHops()->push_back(nh.toThrift());
  }
  *mplsRouteDetail.adminDistance() = fwd.getAdminDistance();
  *mplsRouteDetail.action() = forwardActionStr(fwd.getAction());
}

void ThriftHandler::getHwDebugDump(std::string& out) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  if (sw_->isRunModeMonolithic()) {
    out = sw_->getMonolithicHwSwitchHandler()->getDebugDump();
  } else {
    folly::dynamic json = folly::dynamic::object;
    for (const auto& switchId : sw_->getSwitchInfoTable().getSwitchIDs()) {
      json[folly::to<std::string>(switchId)] =
          sw_->getHwSwitchThriftClientTable()->getHwDebugDump(switchId);
    }
    out = folly::toPrettyJson(json);
  }
}

void ThriftHandler::setSdkRegDumpEnabled(bool enabled) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  if (sw_->isRunModeMonolithic()) {
    sw_->getMonolithicHwSwitchHandler()->setSdkRegDumpEnabled(enabled);
    return;
  }
  // Multi-switch: apply to every switch best-effort so that one unsupported or
  // unreachable switch does not prevent updating the others. Collect failures
  // and surface them together.
  std::string failures;
  for (const auto& switchId : sw_->getSwitchInfoTable().getSwitchIDs()) {
    try {
      sw_->getHwSwitchThriftClientTable()->setSdkRegDumpEnabled(
          switchId, enabled);
    } catch (const std::exception& ex) {
      failures += folly::to<std::string>(
          failures.empty() ? "" : "; ", switchId, ": ", ex.what());
    }
  }
  if (!failures.empty()) {
    throw FbossError(
        "Failed to set SDK register dump on switch(es): ", failures);
  }
}

void ThriftHandler::getPlatformMapping(cfg::PlatformMapping& ret) {
  ret = sw_->getPlatformMapping()->toThrift();
}

void ThriftHandler::listHwObjects(
    std::string& out,
    std::unique_ptr<std::vector<HwObjectType>> hwObjects,
    bool cached) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  if (sw_->isRunModeMonolithic()) {
    out = sw_->getMonolithicHwSwitchHandler()->listObjects(*hwObjects, cached);
  } else {
    throw FbossError(
        "listHwObjects() is not supported for fboss_sw_agent. Clients should query hw agent instead");
  }
}

void ThriftHandler::getBlockedNeighbors(
    std::vector<cfg::Neighbor>& blockedNeighbors) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  throw FbossError(
      "Deprecated thrift API. Please use setMacAddrsToBlock/getMacAddrsToBlock.");
}

void ThriftHandler::setNeighborsToBlock(
    std::unique_ptr<std::vector<cfg::Neighbor>> neighborsToBlock) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureNPU(__func__);
  ensureConfigured(__func__);
  throw FbossError("Deprecated thrift API. Please use setMacAddrsToBlock");
}

void ThriftHandler::getMacAddrsToBlock(
    std::vector<cfg::MacAndVlan>& blockedMacAddrs) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  for (const auto& iter :
       *(utility::getFirstNodeIf(sw_->getState()->getSwitchSettings())
             ->getMacAddrsToBlock())) {
    auto vlanID = VlanID(
        iter->cref<switch_state_tags::macAddrToBlockVlanID>()->toThrift());
    auto macAddress = folly::MacAddress(
        iter->cref<switch_state_tags::macAddrToBlockAddr>()->toThrift());
    cfg::MacAndVlan blockedMacAddr;
    blockedMacAddr.vlanID() = vlanID;
    blockedMacAddr.macAddress() = macAddress.toString();
    blockedMacAddrs.emplace_back(std::move(blockedMacAddr));
  }
}

void ThriftHandler::setMacAddrsToBlock(
    std::unique_ptr<std::vector<cfg::MacAndVlan>> macAddrsToBlock) {
  ensureNPU(__func__);
  std::string macAddrsToBlockStr;
  std::vector<std::pair<VlanID, folly::MacAddress>> blockMacAddrs;

  if (macAddrsToBlock) {
    if (macAddrsToBlock->size() > FLAGS_max_mac_address_to_block) {
      throw FbossError(
          "MAC addresses to block list size ",
          macAddrsToBlock->size(),
          " exceeds limit ",
          FLAGS_max_mac_address_to_block);
    }

    for (const auto& macAddrToBlock : *macAddrsToBlock) {
      auto macAddr =
          folly::MacAddress::tryFromString(*macAddrToBlock.macAddress());
      if (!macAddr.hasValue()) {
        throw FbossError("Invalid MAC address: ", *macAddrToBlock.macAddress());
      }

      auto macAddrToBlockStr = folly::to<std::string>(
          "[vlan: ",
          *macAddrToBlock.vlanID(),
          " ip: ",
          *macAddrToBlock.macAddress(),
          "], ");
      macAddrsToBlockStr.append(macAddrToBlockStr);

      blockMacAddrs.emplace_back(VlanID(*macAddrToBlock.vlanID()), *macAddr);
    }
  }

  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), macAddrsToBlockStr);

  sw_->updateStateBlocking(
      "Update MAC addrs to block ",
      [blockMacAddrs](const std::shared_ptr<SwitchState>& state) {
        std::shared_ptr<SwitchState> newState{state};
        auto newSwitchSettings =
            utility::getFirstNodeIf(state->getSwitchSettings())
                ->modify(&newState);
        newSwitchSettings->setMacAddrsToBlock(blockMacAddrs);
        return newState;
      });
}

void ThriftHandler::getMyMacs(std::vector<cfg::MacAndVlan>& myMacs) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  if (auto switchSettings =
          utility::getFirstNodeIf(sw_->getState()->getSwitchSettings())) {
    myMacs = switchSettings->getMyMacs();
  }
}

void ThriftHandler::publishLinkSnapshots(
    std::unique_ptr<std::vector<std::string>> portNames) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats(), portNames);
  ensureConfigured(__func__);
  for (const auto& portName : *portNames) {
    auto portID = sw_->getPlatformMapping()->getPortID(portName);
    sw_->publishPhyInfoSnapshots(portID);
  }
}

void ThriftHandler::getAllInterfacePhyInfo(
    std::map<std::string, phy::PhyInfo>& phyInfos) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto portNames = std::make_unique<std::vector<std::string>>();
  std::shared_ptr<SwitchState> swState = sw_->getState();
  for (const auto& portMap : std::as_const(*swState->getPorts())) {
    for (const auto& port : std::as_const(*portMap.second)) {
      portNames->push_back(port.second->getName());
    }
  }
  getInterfacePhyInfo(phyInfos, std::move(portNames));
}

void ThriftHandler::getInterfacePhyInfo(
    std::map<std::string, phy::PhyInfo>& phyInfos,
    std::unique_ptr<std::vector<std::string>> portNames) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  std::vector<PortID> portIDs;
  for (const auto& portName : *portNames) {
    portIDs.push_back(sw_->getPlatformMapping()->getPortID(portName));
  }
  auto portPhyInfo = sw_->getIPhyInfo(portIDs);
  for (const auto& portName : *portNames) {
    phyInfos[portName] =
        portPhyInfo[sw_->getPlatformMapping()->getPortID(portName)];
  }
}

bool ThriftHandler::isSwitchDrained() {
  ensureConfigured(__func__);
  auto switchSettings = sw_->getState()->getSwitchSettings()->cbegin()->second;
  return switchSettings->getSwitchDrainState() ==
      cfg::SwitchDrainState::DRAINED;
}

void ThriftHandler::getActualSwitchDrainState(
    std::map<int64_t, cfg::SwitchDrainState>& switchId2ActualSwitchDrainState) {
  ensureConfigured(__func__);

  for (auto& [matcherString, switchSettings] :
       std::as_const(*sw_->getState()->getSwitchSettings())) {
    auto matcher = HwSwitchMatcher(matcherString);
    switchId2ActualSwitchDrainState.insert(
        {static_cast<int64_t>(matcher.switchId()),
         switchSettings->getActualSwitchDrainState()});
  }
}

void ThriftHandler::getTeFlowTableDetails(
    std::vector<TeFlowDetails>& /*flowTable*/) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  throw FbossError("getTeFlowTableDetails is deprecated");
}

void ThriftHandler::addNamedNextHopGroups(
    std::unique_ptr<std::vector<NextHopGroup>> nextHopGroups,
    bool combineDuplicatedNextHops) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  addNamedNextHopGroupsImpl(
      __func__, std::move(nextHopGroups), combineDuplicatedNextHops);
}

void ThriftHandler::addOrUpdateNamedNextHopGroups(
    std::unique_ptr<std::vector<NextHopGroup>> nextHopGroups,
    bool combineDuplicatedNextHops) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  addNamedNextHopGroupsImpl(
      __func__, std::move(nextHopGroups), combineDuplicatedNextHops);
}

void ThriftHandler::addNamedNextHopGroupsImpl(
    folly::StringPiece function,
    std::unique_ptr<std::vector<NextHopGroup>> nextHopGroups,
    bool combineDuplicatedNextHops) {
  ensureConfigured(function);

  auto* rib = sw_->getRib();
  if (!rib) {
    throw FbossError("RIB not initialized");
  }

  /*
   * Named NHG are associated with counters in HW. The base SAI counter label
   * holds 31 characters; with FLAGS_srv6 the extended label attribute allows up
   * to 255. Restrict the group name to the corresponding limit here.
   */
  static constexpr size_t kMaxGroupNameLen = 31;
  static constexpr size_t kMaxExtendedGroupNameLen = 255;
  auto maxGroupNameLen =
      FLAGS_srv6 ? kMaxExtendedGroupNameLen : kMaxGroupNameLen;
  auto defaultSrv6TunnelId = getDefaultSrv6TunnelId(sw_->getConfig());
  auto state = sw_->getState();
  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  for (auto& group : *nextHopGroups) {
    if (!group.name().has_value() || group.name()->empty()) {
      throw FbossError("Named next-hop group must have a name");
    }
    if (group.name()->size() > maxGroupNameLen) {
      throw FbossError(
          "Named next-hop group name exceeds max length of ",
          maxGroupNameLen,
          " characters: ",
          *group.name());
    }
    validateAndDefaultSrv6NextHops(*group.nexthops(), defaultSrv6TunnelId);
    validateLinkLocalNextHopInterfaces(*group.nexthops(), state);
    auto nextHops = util::toRouteNextHopSet(
        *group.nexthops(),
        true /* allowV6NonLinkLocal */,
        combineDuplicatedNextHops);
    if (hasSrv6AndNonSrv6NextHops(nextHops)) {
      throw FbossError(
          "Named next-hop group ",
          *group.name(),
          " contains mixed SRv6 and non-SRv6 next hops, which are unsupported");
    }
    groups.emplace_back(*group.name(), std::move(nextHops));
  }

  // RIB handles allocation + route reprogramming on the RIB thread.
  // RibToSwitchStateUpdater syncs FIB routes, mySids, and NHG ID maps
  // to switch state in a single pass.
  rib->addOrUpdateNamedNextHopGroups(
      sw_->getScopeResolver(), groups, createRibToSwitchStateFunction(), sw_);
}

void ThriftHandler::deleteNamedNextHopGroups(
    std::unique_ptr<std::vector<std::string>> names) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  auto* rib = sw_->getRib();
  if (!rib) {
    throw FbossError("RIB not initialized");
  }

  // RIB handles deallocation + state update atomically on the RIB thread
  rib->deleteNamedNextHopGroups(*names, [this](const NextHopIDManager* mgr) {
    SwitchStateNextHopIdUpdater updater(mgr);
    sw_->updateStateBlocking(
        "deleteNamedNextHopGroups",
        [&updater](const std::shared_ptr<SwitchState>& state) {
          return updater(state);
        });
  });
}

void ThriftHandler::addPolicies(std::unique_ptr<std::vector<Policy>> policies) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  auto* rib = sw_->getRib();
  if (!rib) {
    throw FbossError("RIB not initialized");
  }

  std::vector<ClassBasedPolicy> classBasedPolicies;
  classBasedPolicies.reserve(policies->size());
  for (auto& policy : *policies) {
    if (policy.getType() != Policy::Type::cbfPolicy) {
      throw FbossError("Only class-based (cbf) policies are supported");
    }
    classBasedPolicies.push_back(std::move(*policy.cbfPolicy()));
  }
  validateClassBasedPolicies(sw_, classBasedPolicies);

  rib->addOrUpdatePolicies(
      classBasedPolicies, createNextHopIdStateUpdate(sw_, "addPolicies"));
}

void ThriftHandler::deletePolicies(
    std::unique_ptr<std::vector<std::string>> policyNames) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  auto* rib = sw_->getRib();
  if (!rib) {
    throw FbossError("RIB not initialized");
  }

  rib->removePolicies(
      *policyNames, createNextHopIdStateUpdate(sw_, "deletePolicies"));
}

void ThriftHandler::getAllPolicies(std::vector<Policy>& result) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();
  std::set<std::string> seen;
  for (const auto& [_, policyMap] :
       std::as_const(*state->getClassBasedPolicies())) {
    for (const auto& [name, node] : std::as_const(*policyMap)) {
      if (seen.insert(name).second) {
        result.push_back(toThriftPolicy(node));
      }
    }
  }
}

void ThriftHandler::getPolicies(
    std::vector<Policy>& result,
    std::unique_ptr<std::vector<std::string>> policyNames) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto state = sw_->getState();
  std::set<std::string> requested(policyNames->begin(), policyNames->end());
  std::vector<Policy> found;
  found.reserve(requested.size());
  for (const auto& name : requested) {
    std::shared_ptr<ClassBasedPolicyNode> node;
    for (const auto& [_matcher, policyMap] :
         std::as_const(*state->getClassBasedPolicies())) {
      node = policyMap->getPolicyIf(name);
      if (node) {
        break;
      }
    }
    if (!node) {
      throw FbossError("Class-based policy not found: ", name);
    }
    found.push_back(toThriftPolicy(node));
  }
  result = std::move(found);
}

namespace {

struct NhgFibContext {
  std::shared_ptr<FibInfo> fibInfo;
  std::map<std::string, NextHopSetId> nameToSetId;
  std::unordered_map<NextHopSetId, std::string> nhgSetIdToName;
};

std::optional<NhgFibContext> getNhgFibContext(
    const std::shared_ptr<SwitchState>& state) {
  auto fibInfoMap = state->getFibsInfoMap();
  auto fibInfoIt = fibInfoMap->cbegin();
  if (fibInfoIt == fibInfoMap->cend()) {
    return std::nullopt;
  }
  NhgFibContext ctx;
  ctx.fibInfo = fibInfoIt->second;
  ctx.nameToSetId = ctx.fibInfo->getNameToNextHopSetId();
  for (const auto& [name, setId] : ctx.nameToSetId) {
    ctx.nhgSetIdToName.emplace(setId, name);
  }
  return ctx;
}

} // namespace

void ThriftHandler::getNextHopGroups(
    std::vector<NextHopGroup>& result,
    bool replicateWeightedNexthops) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  auto state = sw_->getState();
  auto ctx = getNhgFibContext(state);
  if (!ctx) {
    return;
  }

  auto idToNextHopIdSetMap = ctx->fibInfo->getIdToNextHopIdSetMap();
  if (!idToNextHopIdSetMap) {
    return;
  }
  auto refCounts = ctx->fibInfo->getNextHopSetIdRefCountsFromRoutes();
  FibInfo::getNextHopSetIdRefCountsFromMySid(state, refCounts);
  auto pbrHeldIds = nhgIdsReferencedByPbr(state, ctx->nameToSetId);

  auto idSetMapThrift = idToNextHopIdSetMap->toThrift();
  for (const auto& [setId, nhIds] : idSetMapThrift) {
    auto nameIter = ctx->nhgSetIdToName.find(setId);
    auto isNamed = nameIter != ctx->nhgSetIdToName.end();
    if (!isNamed && nhIds.size() < 2) {
      continue;
    }

    NextHopGroup thriftGroup;
    thriftGroup.id() = setId;
    if (isNamed) {
      thriftGroup.name() = nameIter->second;
      auto refCountIter = refCounts.find(NextHopSetID(setId));
      thriftGroup.isProgrammed() =
          (refCountIter != refCounts.end() && refCountIter->second > 0) ||
          pbrHeldIds.count(setId) > 0;
    } else {
      thriftGroup.isProgrammed() = true;
    }

    try {
      auto nextHops = ctx->fibInfo->resolveNextHopSetFromId(setId);
      thriftGroup.nexthops() =
          util::fromNextHops(nextHops, replicateWeightedNexthops);
      result.push_back(std::move(thriftGroup));
    } catch (const FbossError& e) {
      XLOG(ERR) << "Failed to resolve nexthops for NextHopSetId " << setId
                << ": " << e.what();
    }
  }
}

void ThriftHandler::getNamedNextHopGroups(
    std::vector<NextHopGroup>& result,
    std::unique_ptr<std::vector<std::string>> names,
    bool replicateWeightedNexthops) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  auto state = sw_->getState();
  auto ctx = getNhgFibContext(state);
  if (!ctx) {
    return;
  }

  std::unordered_set<std::string> nameFilter;
  if (names && !names->empty()) {
    nameFilter.insert(names->begin(), names->end());
  }

  auto refCounts = ctx->fibInfo->getNextHopSetIdRefCountsFromRoutes();
  FibInfo::getNextHopSetIdRefCountsFromMySid(state, refCounts);
  auto pbrHeldIds = nhgIdsReferencedByPbr(state, ctx->nameToSetId);

  std::unordered_set<std::string> foundNames;
  for (const auto& [name, nextHopSetId] : ctx->nameToSetId) {
    if (!nameFilter.empty() && !nameFilter.count(name)) {
      continue;
    }
    foundNames.insert(name);
    NextHopGroup thriftGroup;
    thriftGroup.name() = name;
    thriftGroup.id() = nextHopSetId;
    thriftGroup.isProgrammed() =
        refCounts.count(NextHopSetID(nextHopSetId)) > 0 ||
        pbrHeldIds.count(nextHopSetId) > 0;
    try {
      auto nextHops = ctx->fibInfo->resolveNextHopSetFromId(nextHopSetId);
      thriftGroup.nexthops() =
          util::fromNextHops(nextHops, replicateWeightedNexthops);
      result.push_back(std::move(thriftGroup));
    } catch (const FbossError& e) {
      XLOG(ERR) << "Failed to resolve nexthops for named group '" << name
                << "' with NextHopSetId " << nextHopSetId << ": " << e.what();
    }
  }

  for (const auto& name : nameFilter) {
    if (!foundNames.count(name)) {
      XLOG(WARNING) << "Named next hop group '" << name << "' not found";
    }
  }
}

void ThriftHandler::getFabricReachability(
    std::map<std::string, FabricEndpoint>& connectivity) {
  return getFabricConnectivity(connectivity);
}

void ThriftHandler::getFabricConnectivity(
    std::map<std::string, FabricEndpoint>& connectivity) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureVoqOrFabric(__func__);
  // get cached data as stored in the fabric manager
  for (const auto& switchId : sw_->getSwitchInfoTable().getSwitchIDs()) {
    auto portId2FabricEndpoint =
        sw_->getHwSwitchThriftClientTable()->getFabricConnectivity(switchId);
    for (const auto& [portName, fabricEndpoint] : portId2FabricEndpoint) {
      connectivity.insert({portName, fabricEndpoint});
    }
  }
}

void ThriftHandler::getSwitchReachability(
    std::map<std::string, std::vector<string>>& reachabilityMatrix,
    std::unique_ptr<std::vector<std::string>> switchNames) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureVoqOrFabric(__func__);
  const auto hwSwitchReachabilityInfo = sw_->getSwitchReachability();
  std::unordered_set<std::string> switchNameSet;
  if (switchNames->size()) {
    std::copy(
        switchNames->begin(),
        switchNames->end(),
        std::inserter(switchNameSet, switchNameSet.begin()));
  }
  for (const auto& [_, dsfNodes] :
       std::as_const(*sw_->getState()->getDsfNodes())) {
    for (const auto& [_, node] : std::as_const(*dsfNodes)) {
      if (switchNameSet.size() &&
          (switchNameSet.find(node->getName()) == switchNameSet.end())) {
        // Switch is not of interest!
        continue;
      }
      if (node->getType() != cfg::DsfNodeType::INTERFACE_NODE) {
        // Reachability information available only for RDSW/EDSW
        continue;
      }
      const auto& switchId = node->getSwitchId();
      std::vector<std::string> reachablePorts;
      // Reachability to a remote switch ID is via fabric links
      // over all the local switches.
      for (const auto& [_, reachabilityInfo] : hwSwitchReachabilityInfo) {
        auto portGroupInfo =
            reachabilityInfo.switchIdToFabricPortGroupMap()->find(switchId);
        if (portGroupInfo !=
            reachabilityInfo.switchIdToFabricPortGroupMap()->end()) {
          int portGroupId = portGroupInfo->second;
          auto portNames =
              reachabilityInfo.fabricPortGroupMap()->find(portGroupId);
          reachablePorts.insert(
              reachablePorts.end(),
              portNames->second.begin(),
              portNames->second.end());
        }
      }
      // Update the switch to port mapping
      reachabilityMatrix.insert({node->getName(), std::move(reachablePorts)});
    }
  }
}

void ThriftHandler::getDsfNodes(std::map<int64_t, cfg::DsfNode>& dsfNodes) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureVoqOrFabric(__func__);
  for (const auto& matcherAndNodes :
       std::as_const(*sw_->getState()->getDsfNodes())) {
    for (const auto& idAndNode : std::as_const(*matcherAndNodes.second)) {
      dsfNodes.insert(
          {static_cast<int64_t>(idAndNode.first),
           idAndNode.second->toThrift()});
    }
  }
}

void ThriftHandler::getDsfSubscriptions(
    std::vector<FsdbSubscriptionThrift>& subscriptions) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureVoqOrFabric(__func__);
  // Build a map of <loopbackIp, switchName> from DsfNodes
  std::unordered_map<IPAddress, std::string> loopbackIpToName;
  for (const auto& [_, dsfNodes] :
       std::as_const(*sw_->getState()->getDsfNodes())) {
    for (const auto& [__, node] : std::as_const(*dsfNodes)) {
      if (node->getType() == cfg::DsfNodeType::INTERFACE_NODE &&
          node->getLoopbackIps()->size()) {
        auto loopbackIps = node->getLoopbackIps()->toThrift();
        std::for_each(
            loopbackIps.begin(),
            loopbackIps.end(),
            [&loopbackIpToName,
             currentNode = node](const auto& loopbackSubnet) {
              loopbackIpToName.emplace(
                  IPAddress(loopbackSubnet.substr(0, loopbackSubnet.find("/"))),
                  currentNode->getName());
            });
      }
    }
  }

  for (const auto& subscriptionInfo :
       sw_->getDsfSubscriber()->getSubscriptionInfo()) {
    FsdbSubscriptionThrift subscriptionThrift;
    subscriptionThrift.paths() = subscriptionInfo.paths;
    subscriptionThrift.state() =
        fsdb::FsdbPubSubManager::subscriptionStateToString(
            subscriptionInfo.state);
    auto serverIp = IPAddress(subscriptionInfo.server);
    if (loopbackIpToName.find(serverIp) != loopbackIpToName.end()) {
      subscriptionThrift.name() = loopbackIpToName[serverIp];
      subscriptionThrift.ip() = serverIp.str();
      subscriptionThrift.subscriptionId() = DsfSubscription::makeRemoteEndpoint(
          *subscriptionThrift.name(), serverIp);
      subscriptions.push_back(subscriptionThrift);
    } else {
      XLOG(ERR) << "Unable to find loopback ip " << subscriptionInfo.server
                << " from DsfSubscription in Dsf nodes";
    }
  }
}

void ThriftHandler::getDsfSubscriptionClientId(std::string& ret) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureVoqOrFabric(__func__);
  ret = sw_->getDsfSubscriber()->getClientId();
}

void ThriftHandler::getDsfSessions(std::vector<DsfSessionThrift>& dsfSessions) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureVoqOrFabric(__func__);
  dsfSessions = this->sw_->getDsfSubscriber()->getDsfSessionsThrift();
}

void ThriftHandler::getSystemPorts(
    std::map<int64_t, SystemPortThrift>& sysPortsThrift) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  auto fillSysPorts = [&sysPortsThrift](const auto& mSwitchSysPorts) {
    for (const auto& [_, sysPorts] : std::as_const(*mSwitchSysPorts)) {
      sysPortsThrift.merge(sysPorts->toThrift());
    }
  };
  auto state = sw_->getState();
  fillSysPorts(state->getSystemPorts());
  fillSysPorts(state->getRemoteSystemPorts());
}

void ThriftHandler::getSysPortStats(
    std::map<std::string, HwSysPortStats>& hwSysPortStats) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  sw_->getAllHwSysPortStats(hwSysPortStats);
}

// TODO - delete this api after callers migrate to getAllCpuPortStats
void ThriftHandler::getCpuPortStats(CpuPortStats& cpuPortStats) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  std::map<int, CpuPortStats> hwCpuPortStats;
  sw_->getAllCpuPortStats(hwCpuPortStats);
  if (hwCpuPortStats.size() > 0) {
    cpuPortStats = hwCpuPortStats.begin()->second;
  }
}

void ThriftHandler::getAllCpuPortStats(
    std::map<int, CpuPortStats>& hwCpuPortStats) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  sw_->getAllCpuPortStats(hwCpuPortStats);
}

void ThriftHandler::getHwPortStats(
    std::map<std::string, HwPortStats>& hwPortStats) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  sw_->getAllHwPortStats(hwPortStats);
}

void ThriftHandler::getRouteCounters(
    std::map<std::string, HwSwitchCounter>& routeCounters) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  routeCounters = sw_->getRouteCounters();
}

void ThriftHandler::getHwRouterInterfaceStats(
    std::map<std::string, HwRouterInterfaceStats>& hwPortStats) {
  auto log = LOG_THRIFT_CALL(DBG1);
  ensureConfigured(__func__);
  // TODO(pshaikh) : implement this
}

void ThriftHandler::getFabricReachabilityStats(
    FabricReachabilityStats& fabricReachabilityStats) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  fabricReachabilityStats = sw_->getFabricReachabilityStats();
}

void ThriftHandler::getMultiSwitchRunState(MultiSwitchRunState& runState) {
  runState.swSwitchRunState() = sw_->getSwitchRunState();
  runState.hwIndexToRunState() =
      sw_->getHwSwitchHandler()->getHwSwitchRunStates();
  runState.multiSwitchEnabled() = sw_->isRunModeMultiSwitch();
}

void ThriftHandler::getAllEcmpDetails(std::vector<EcmpDetails>& ecmpDetails) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  if (sw_->isRunModeMonolithic()) {
    ecmpDetails = sw_->getMonolithicHwSwitchHandler()->getAllEcmpDetails();
  } else {
    throw FbossError("getAllEcmpDetails is not supported in multi-switch mode");
  }
}

void ThriftHandler::getSwitchIndicesForInterfaces(
    std::map<int16_t, std::vector<std::string>>& switchIndicesForInterfaces,
    std::unique_ptr<std::vector<std::string>> interfaces) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);
  for (const auto& interface : *interfaces) {
    auto switchIndex = sw_->getSwitchIndexForInterface(interface);
    auto switchIndicesForInterfacesItr =
        switchIndicesForInterfaces.find(switchIndex);
    if (switchIndicesForInterfacesItr == switchIndicesForInterfaces.end()) {
      std::vector<std::string> switchIndexInterfaces{interface};
      switchIndicesForInterfaces[switchIndex] = switchIndexInterfaces;
    } else {
      switchIndicesForInterfaces[switchIndex].push_back(interface);
    }
  }
}

void ThriftHandler::getSwitchIdToSwitchInfo(
    std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo) {
  ensureConfigured(__func__);

  // SwitchSettings are per switchId(s), and should hold switchInfo for
  // corresponding SwitchId(s).
  // However, today SwitchSettingsFields carries
  //  - switchIdToSwitchInfo: switchInfo for every SwitchId,
  //  - switchInfo i.e. switchInfo for corresponding SwitchId(s):
  //
  //  However, switchInfo is not always popualted correctly.
  //
  // TODO:
  //   - Populate switchInfo correctly.
  //   - Change every switchIdToSwitchInfo callsite to consume switchInfo
  //   - At that time, switch to below implementation:
  // for (const auto& [matcherString, switchSettings] :
  //      std::as_const(*sw_->getState()->getSwitchSettings())) {
  //      auto matcher = HwSwitchMatcher(matcherString);
  //      switchIdToSwitchInfo[matcher.switchId()] =
  //      switchSettings->getSwitchInfo();
  // }
  //
  //   - Remove switchIdToSwitchInfo from SwitchSettingsFields.

  const auto& switchSettings =
      utility::getFirstNodeIf(sw_->getState()->getSwitchSettings());
  for (const auto& [switchId, switchInfo] :
       switchSettings->getSwitchIdToSwitchInfo()) {
    switchIdToSwitchInfo[switchId] = switchInfo;
  }
}

void ThriftHandler::getAllFabricLinkMonitoringStats(
    std::map<int32_t, FabricLinkMonPortStats>& stats) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  // Get FabricLinkMonitoringManager from SwSwitch
  auto fabricLinkMonitoringMgr = sw_->getFabricLinkMonitoringManager();
  if (!fabricLinkMonitoringMgr) {
    XLOG(WARNING) << "FabricLinkMonitoringManager not available";
    return;
  }

  // Get all fabric ports from the current state
  std::shared_ptr<SwitchState> swState = sw_->getState();
  for (const auto& portMap : std::as_const(*(swState->getPorts()))) {
    for (const auto& port : std::as_const(*portMap.second)) {
      // Only process fabric ports
      if (port.second->getPortType() != cfg::PortType::FABRIC_PORT) {
        continue;
      }

      auto portId = port.second->getID();
      // Get stats from FabricLinkMonitoringManager (returns thrift struct
      // directly)
      stats[portId] =
          fabricLinkMonitoringMgr->getFabricLinkMonPortStats(portId);
    }
  }
}

void ThriftHandler::buildFabricMonitoringLookupMaps(
    const cfg::SwitchConfig& config,
    const std::shared_ptr<SwitchState>& swState,
    cfg::SwitchType switchType,
    std::map<std::string, SwitchID>& switchNameToSwitchIds,
    std::map<SwitchID, std::string>& switchIdToSystemPort) {
  // Collect switch name to switch ID mapping
  for (const auto& dsfNodeEntry : *config.dsfNodes()) {
    switchNameToSwitchIds[*dsfNodeEntry.second.name()] =
        SwitchID(dsfNodeEntry.first);
  }

  // Build a cache of switch ID to system port ID for VOQ switches
  if (switchType == cfg::SwitchType::VOQ) {
    for (const auto& sysPortMap :
         std::as_const(*(swState->getFabricLinkMonitoringSystemPorts()))) {
      for (const auto& sysPort : std::as_const(*sysPortMap.second)) {
        switchIdToSystemPort[sysPort.second->getSwitchId()] =
            std::to_string(sysPort.second->getID());
      }
    }
  }
}

int ThriftHandler::determineVirtualDevice(
    const std::shared_ptr<Port>& swPort,
    const cfg::SwitchConfig& config,
    cfg::SwitchType switchType,
    const std::map<std::string, SwitchID>& switchNameToSwitchIds,
    const cfg::PortNeighbor& neighbor) {
  if (switchType == cfg::SwitchType::FABRIC) {
    auto localPlatformMapping = sw_->getPlatformMapping();
    if (localPlatformMapping) {
      auto vdId = localPlatformMapping->getVirtualDeviceID(swPort->getName());
      return vdId.value_or(-1);
    }
  } else if (switchType == cfg::SwitchType::VOQ) {
    const auto& neighborSwitchName = *neighbor.remoteSystem();
    auto neighborSwitchIdIter = switchNameToSwitchIds.find(neighborSwitchName);
    if (neighborSwitchIdIter != switchNameToSwitchIds.end()) {
      auto baseSwitchId = neighborSwitchIdIter->second;
      auto dsfNodeItr = config.dsfNodes()->find(baseSwitchId);
      if (dsfNodeItr != config.dsfNodes()->end()) {
        const auto platformMapping = getPlatformMappingForPlatformType(
            *dsfNodeItr->second.platformType());
        if (platformMapping) {
          auto vdId =
              platformMapping->getVirtualDeviceID(*neighbor.remotePort());
          return vdId.value_or(-1);
        }
      }
    }
  }
  return -1;
}

void ThriftHandler::populateFabricPortDetail(
    const std::shared_ptr<Port>& swPort,
    const cfg::SwitchConfig& config,
    cfg::SwitchType switchType,
    const std::map<std::string, SwitchID>& switchNameToSwitchIds,
    const std::map<SwitchID, std::string>& switchIdToSystemPort,
    FabricMonitoringDetail& detail) {
  detail.portName() = swPort->getName();
  detail.portId() = swPort->getID();

  // Get expected neighbor values
  const auto& expectedNeighbors =
      swPort->getExpectedNeighborValues()->toThrift();
  if (expectedNeighbors.empty()) {
    detail.neighborSwitch() = "--";
    detail.neighborPortName() = "--";
    detail.virtualDevice() = -1;
    detail.linkSwitchId() = -1;
    detail.linkSystemPort() = "--";
    return;
  }

  const auto& neighbor = expectedNeighbors.front();
  detail.neighborSwitch() = *neighbor.remoteSystem();
  detail.neighborPortName() = *neighbor.remotePort();

  // Determine virtual device
  detail.virtualDevice() = determineVirtualDevice(
      swPort, config, switchType, switchNameToSwitchIds, neighbor);

  // Set link switch ID and system port
  auto portSwitchId = swPort->getPortSwitchId();
  detail.linkSwitchId() = portSwitchId.value_or(-1);

  if (switchType == cfg::SwitchType::VOQ && portSwitchId.has_value()) {
    auto it = switchIdToSystemPort.find(SwitchID(*portSwitchId));
    if (it != switchIdToSystemPort.end()) {
      detail.linkSystemPort() = it->second;
    } else {
      detail.linkSystemPort() = "--";
    }
  } else {
    detail.linkSystemPort() = "--";
  }
}

void ThriftHandler::getFabricMonitoringDetails(
    std::vector<FabricMonitoringDetail>& details) {
  auto log = LOG_THRIFT_CALL_WITH_STATS(DBG1, sw_->stats());
  ensureConfigured(__func__);

  if (!FLAGS_enable_fabric_link_monitoring) {
    XLOG(ERR) << "Fabric Link Monitoring is not enabled!";
    return;
  }

  std::shared_ptr<SwitchState> swState = sw_->getState();
  auto config = sw_->getConfig();
  if (config.ports()->empty()) {
    XLOG(ERR) << "Port config is not available!";
    return;
  }

  // Get switch type
  const auto& switchSettings = swState->getSwitchSettings()->cbegin()->second;
  auto switchIds = switchSettings->getSwitchIds();
  if (switchIds.empty()) {
    XLOG(ERR) << "No switch IDs available in switch settings!";
    return;
  }
  auto switchId = static_cast<int64_t>(*switchIds.begin());
  auto switchType = switchSettings->getSwitchType(switchId);

  // Build lookup maps for efficient fabric port processing
  std::map<std::string, SwitchID> switchNameToSwitchIds;
  std::map<SwitchID, std::string> switchIdToSystemPort;
  buildFabricMonitoringLookupMaps(
      config, swState, switchType, switchNameToSwitchIds, switchIdToSystemPort);

  // Process all fabric ports
  for (const auto& portMap : std::as_const(*(swState->getPorts()))) {
    for (const auto& port : std::as_const(*portMap.second)) {
      if (port.second->getPortType() != cfg::PortType::FABRIC_PORT) {
        continue;
      }

      FabricMonitoringDetail detail;
      populateFabricPortDetail(
          port.second,
          config,
          switchType,
          switchNameToSwitchIds,
          switchIdToSystemPort,
          detail);
      details.push_back(detail);
    }
  }
}

} // namespace facebook::fboss
