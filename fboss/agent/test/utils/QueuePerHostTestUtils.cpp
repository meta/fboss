/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/test/utils/QueuePerHostTestUtils.h"

#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/test/HwSwitchEnsemble.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/TrafficPolicyTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <string>

namespace facebook::fboss::utility {

using UpdateStatFunc = std::function<void()>;
using GetPortStatsFunc =
    std::function<std::map<PortID, HwPortStats>(const std::vector<PortID>&)>;
using SendPacketFunc = std::function<void(
    std::unique_ptr<TxPacket>,
    std::vector<PortID>,
    GetPortStatsFunc,
    bool)>;

SendPacketFunc getSendPacketFunc(HwSwitch* hwSwitch) {
  return [hwSwitch](
             std::unique_ptr<TxPacket> pkt,
             std::vector<PortID> portIds,
             GetPortStatsFunc /*portStatsFn*/,
             bool useFrontPanel) {
    if (useFrontPanel) {
      return hwSwitch->sendPacketOutOfPortSync(std::move(pkt), portIds[1]);
    } else {
      return hwSwitch->sendPacketSwitchedSync(std::move(pkt));
    }
  };
}

SendPacketFunc getSendPacketFunc(AgentEnsemble* ensemble) {
  return [ensemble](
             std::unique_ptr<TxPacket> pkt,
             std::vector<PortID> portIds,
             GetPortStatsFunc /*portStatsFn*/,
             bool useFrontPanel) -> bool {
    if (useFrontPanel) {
      return ensemble->getSw()->sendPacketOutOfPortAsync(
          std::move(pkt), portIds[1]);
    } else {
      auto switchId =
          ensemble->getSw()->getScopeResolver()->scope(portIds[0]).switchId();
      return ensemble->getSw()->sendPacketSwitchedAsync(
          std::move(pkt), {switchId});
    }
  };
}

namespace {

void verifyQueuePerHostMappingImpl(
    AllocatePktFunc allocatePktFn,
    SendPacketFunc sendPktFn,
    AclStatGetFunc getAclStatFn,
    UpdateStatFunc updateStatFn,
    std::shared_ptr<SwitchState> swState,
    const std::vector<PortID>& portIds,
    std::optional<VlanID> vlanId,
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    bool useFrontPanel,
    bool blockNeighbor,
    GetPortStatsFunc getHwPortStatsFn,
    std::optional<uint16_t> l4SrcPort,
    std::optional<uint16_t> l4DstPort,
    std::optional<uint8_t> dscp) {
  auto ttlAclName = utility::getQueuePerHostTtlAclName();
  auto ttlCounterName = utility::getQueuePerHostTtlCounterName();

  std::map<int, int64_t> beforeQueueOutPkts;
  WITH_RETRIES({
    beforeQueueOutPkts.clear();
    for (const auto& queueId : utility::kQueuePerhostQueueIds()) {
      auto hwPortStatsMap = getHwPortStatsFn({portIds[0]});
      auto queueStats = hwPortStatsMap[portIds[0]].queueOutPackets_().value();
      EXPECT_EVENTUALLY_NE(queueStats.find(queueId), queueStats.end());
      if (queueStats.find(queueId) != queueStats.end()) {
        beforeQueueOutPkts[queueId] = queueStats.find(queueId)->second;
      }
    }
  });

  auto statBefore = getAclStatFn(
      swState,
      ttlAclName,
      ttlCounterName,
      cfg::AclStage::INGRESS,
      std::nullopt);

  auto txPacket = utility::makeUDPTxPacket(
      allocatePktFn,
      vlanId,
      srcMac,
      dstMac,
      srcIp,
      dstIp,
      l4SrcPort.has_value() ? l4SrcPort.value() : 8000,
      l4DstPort.has_value() ? l4DstPort.value() : 8001,
      (dscp.has_value() ? dscp.value() : 48) << 2,
      64 /* ttl < 128 */);

  auto txPacket2 = utility::makeUDPTxPacket(
      allocatePktFn,
      vlanId,
      srcMac,
      dstMac,
      srcIp,
      dstIp,
      l4SrcPort.has_value() ? l4SrcPort.value() : 8000,
      l4DstPort.has_value() ? l4DstPort.value() : 8001,
      (dscp.has_value() ? dscp.value() : 48) << 2,
      128 /* ttl >= 128 */);

  sendPktFn(std::move(txPacket), portIds, getHwPortStatsFn, useFrontPanel);
  sendPktFn(std::move(txPacket2), portIds, getHwPortStatsFn, useFrontPanel);

  WITH_RETRIES({
    std::map<int, int64_t> afterQueueOutPkts;
    for (const auto& queueId : utility::kQueuePerhostQueueIds()) {
      auto hwPortStatsMap = getHwPortStatsFn({portIds[0]});
      auto queueStats = hwPortStatsMap[portIds[0]].queueOutPackets_().value();
      EXPECT_EVENTUALLY_NE(queueStats.find(queueId), queueStats.end());
      if (queueStats.find(queueId) != queueStats.end()) {
        afterQueueOutPkts[queueId] = hwPortStatsMap[portIds[0]]
                                         .queueOutPackets_()
                                         .value()
                                         .find(queueId)
                                         ->second;
      }
    }

    /*
     *  Consider ACL with action to egress pkts through queue 2.
     *
     *  CPU originated packets:
     *     - Hits ACL (queue2Cnt = 1), egress through queue 2 of port0.
     *     - port0 is in loopback mode, so the packet gets looped back.
     *     - When packet is routed, its dstMAC gets overwritten. Thus, the
     *       looped back packet is not routed, and thus does not hit the ACL.
     *     - On some platforms, looped back packets for unknown MACs are
     *       flooded and counted on queue *before* the split horizon check
     *       (drop when srcPort == dstPort). This flooding always happens on
     *       queue 0, so expect one or more packets on queue 0.
     *
     *  Front panel packets (injected with pipeline bypass):
     *     - Egress out of port1 queue0 (pipeline bypass).
     *     - port1 is in loopback mode, so the packet gets looped back.
     *     - Rest of the workflow is same as above when CPU originated packet
     *       gets injected for switching.
     */
    for (auto [qid, beforePkts] : beforeQueueOutPkts) {
      auto pktsOnQueue = afterQueueOutPkts[qid] - beforePkts;

      XLOG(DBG2) << "queueId: " << qid << " pktsOnQueue: " << pktsOnQueue;

      if (blockNeighbor) {
        // if the neighbor is blocked, all pkts are dropped
        EXPECT_EVENTUALLY_EQ(pktsOnQueue, 0);
      } else {
        if (qid == kQueueId) {
          EXPECT_EVENTUALLY_EQ(pktsOnQueue, 2);
        } else if (qid == 0) {
          EXPECT_EVENTUALLY_GE(pktsOnQueue, 0);
        } else {
          EXPECT_EVENTUALLY_EQ(pktsOnQueue, 0);
        }
      }
    }
  });

  auto aclStatsMatch = [&]() {
    auto statAfter = getAclStatFn(
        swState,
        ttlAclName,
        ttlCounterName,
        cfg::AclStage::INGRESS,
        std::nullopt);

    XLOG(DBG2) << " Acl stats : StatsAfter " << statAfter << " StatsBefore "
               << statBefore;
    if (blockNeighbor) {
      // if the neighbor is blocked, all pkts are dropped
      return statAfter - statBefore == 0;
    }
    /*
     * counts ttl >= 128 packet only
     */
    return statAfter - statBefore == 1;
  };
  WITH_RETRIES({
    updateStatFn();
    EXPECT_EVENTUALLY_TRUE(aclStatsMatch());
  });
}
} // namespace

const std::map<int, cfg::AclLookupClass>& kQueuePerHostQueueToClass() {
  static const std::map<int, cfg::AclLookupClass> queueToClass = {
      {kQueuePerHostQueue0, cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0},
      {kQueuePerHostQueue1, cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1},
      {kQueuePerHostQueue2, cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2},
      {kQueuePerHostQueue3, cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3},
      {kQueuePerHostQueue4, cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4},
  };

  return queueToClass;
}

const std::vector<int>& kQueuePerhostQueueIds() {
  static const std::vector<int> queueIds = {
      kQueuePerHostQueue0,
      kQueuePerHostQueue1,
      kQueuePerHostQueue2,
      kQueuePerHostQueue3,
      kQueuePerHostQueue4};

  return queueIds;
}

const std::vector<cfg::AclLookupClass>& kLookupClasses() {
  static const std::vector<cfg::AclLookupClass> lookupClasses = {
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4};

  return lookupClasses;
}

void addQueuePerHostQueueConfig(cfg::SwitchConfig* config) {
  std::vector<cfg::PortQueue> portQueues;

  // All Queue-per-host queues are identical by design
  for (auto queueId : kQueuePerhostQueueIds()) {
    cfg::PortQueue queue;

    queue.id() = queueId;
    queue.name() = "queue_per_host";
    queue.streamType() = cfg::StreamType::UNICAST;
    queue.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
    queue.weight() = kQueuePerHostWeight;
    portQueues.push_back(queue);
  }

  config->portQueueConfigs()["queue_config"] = portQueues;

  for (auto& port : *config->ports()) {
    port.portQueueConfigName() = "queue_config";
    port.lookupClasses() = kLookupClasses();
  }
}

std::string getQueuePerHostAclTableName() {
  return "acl-table-queue-per-host";
}

std::string getTtlAclTableName() {
  return "acl-table-ttl";
}

std::string getQueuePerHostL2AclNameForQueue(int queueId) {
  return folly::to<std::string>("queue-per-host-queue-l2-", queueId);
}

std::string getQueuePerHostNeighborAclNameForQueue(int queueId) {
  return folly::to<std::string>("queue-per-host-queue-neighbor-", queueId);
}

std::string getQueuePerHostRouteAclNameForQueue(int queueId) {
  return folly::to<std::string>("queue-per-host-queue-route-", queueId);
}

std::string getQueuePerHostTtlAclName() {
  return "ttl";
}

std::string getQueuePerHostTtlCounterName() {
  return "ttlCounter";
}

std::string getL2DropAclName() {
  return "l2-drop-acl";
}

std::string getNeighborDropAclName() {
  return "neighbor-drop-acl";
}

std::string getRouteDropAclName() {
  return "route-drop-acl";
}

void addQueuePerHostAcls(cfg::SwitchConfig* config, bool isSai) {
  cfg::Ttl ttl;
  std::tie(*ttl.value(), *ttl.mask()) = std::make_tuple(0x80, 0xFF);
  auto ttlCounterName = getQueuePerHostTtlCounterName();

  std::vector<cfg::CounterType> counterTypes{
      cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
  utility::addTrafficCounter(config, ttlCounterName, counterTypes);

  // DENY rules
  utility::addL2ClassIDDropAcl(
      config, getL2DropAclName(), cfg::AclLookupClass::CLASS_DROP);
  utility::addNeighborClassIDDropAcl(
      config, getNeighborDropAclName(), cfg::AclLookupClass::CLASS_DROP);
  utility::addRouteClassIDDropAcl(
      config, getRouteDropAclName(), cfg::AclLookupClass::CLASS_DROP);

  // TTL + {L2, neighbor, route}
  for (auto queueId : kQueuePerhostQueueIds()) {
    auto classID = kQueuePerHostQueueToClass().at(queueId);

    auto l2AndTtlAclName = folly::to<std::string>(
        "ttl-", getQueuePerHostL2AclNameForQueue(queueId));
    utility::addL2ClassIDAndTtlAcl(config, l2AndTtlAclName, classID, ttl);
    utility::addQueueMatcher(
        config, l2AndTtlAclName, queueId, isSai, ttlCounterName);

    auto neighborAndTtlAclName = folly::to<std::string>(
        "ttl-", getQueuePerHostNeighborAclNameForQueue(queueId));
    utility::addNeighborClassIDAndTtlAcl(
        config, neighborAndTtlAclName, classID, ttl);
    utility::addQueueMatcher(
        config, neighborAndTtlAclName, queueId, isSai, ttlCounterName);

    auto routeAndTtlAclName = folly::to<std::string>(
        "ttl-", getQueuePerHostRouteAclNameForQueue(queueId));
    utility::addRouteClassIDAndTtlAcl(config, routeAndTtlAclName, classID, ttl);
    utility::addQueueMatcher(
        config, routeAndTtlAclName, queueId, isSai, ttlCounterName);
  }

  // {L2, neighbor, route}-only
  for (auto queueId : kQueuePerhostQueueIds()) {
    auto classID = kQueuePerHostQueueToClass().at(queueId);

    auto l2AclName = getQueuePerHostL2AclNameForQueue(queueId);
    utility::addL2ClassIDAndTtlAcl(config, l2AclName, classID);
    utility::addQueueMatcher(config, l2AclName, queueId, isSai);

    auto neighborAclName = getQueuePerHostNeighborAclNameForQueue(queueId);
    utility::addNeighborClassIDAndTtlAcl(config, neighborAclName, classID);
    utility::addQueueMatcher(config, neighborAclName, queueId, isSai);

    auto routeAclName = getQueuePerHostRouteAclNameForQueue(queueId);
    utility::addRouteClassIDAndTtlAcl(config, routeAclName, classID);
    utility::addQueueMatcher(config, routeAclName, queueId, isSai);
  }

  // TTL only
  cfg::AclEntry ttlAcl;
  ttlAcl.name() = getQueuePerHostTtlAclName();
  ttlAcl.actionType() = cfg::AclActionType::PERMIT;
  ttlAcl.ttl() = ttl;

  utility::addAcl(config, ttlAcl, cfg::AclStage::INGRESS);
  std::vector<cfg::CounterType> setCounterTypes{
      cfg::CounterType::PACKETS, cfg::CounterType::BYTES};

  utility::addAclStat(
      config, getQueuePerHostTtlAclName(), ttlCounterName, setCounterTypes);
}

void deleteTtlCounters(cfg::SwitchConfig* config) {
  auto ttlCounterName = getQueuePerHostTtlCounterName();

  utility::delAclStat(config, getQueuePerHostTtlAclName(), ttlCounterName);
}

void addTtlAclEntry(
    cfg::SwitchConfig* config,
    const std::string& aclTableName,
    bool aclByteCounterSupported) {
  cfg::Ttl ttl;
  std::tie(*ttl.value(), *ttl.mask()) = std::make_tuple(0x80, 0x80);
  auto ttlCounterName = getQueuePerHostTtlCounterName();
  std::vector<cfg::CounterType> counterTypes{cfg::CounterType::PACKETS};
  if (aclByteCounterSupported) {
    counterTypes.push_back(cfg::CounterType::BYTES);
  }
  utility::addTrafficCounter(config, ttlCounterName, counterTypes);

  cfg::AclEntry ttlAcl{};
  ttlAcl.name() = getQueuePerHostTtlAclName();
  ttlAcl.ttl() = ttl;
  ttlAcl.actionType() = cfg::AclActionType::PERMIT;
  utility::addAclEntry(config, ttlAcl, aclTableName);
  utility::addAclStat(
      config, getQueuePerHostTtlAclName(), ttlCounterName, counterTypes);
}

// Utility to add TTL ACL table to a multi acl table group
void addTtlAclTable(
    cfg::SwitchConfig* config,
    int16_t priority,
    bool aclByteCounterSupported,
    bool addExtraQualifier) {
  std::vector<cfg::AclTableQualifier> qualifiers = {
      cfg::AclTableQualifier::TTL, cfg::AclTableQualifier::DSCP};
  if (addExtraQualifier) {
    /*
     * This field is used to modify the properties of the ACL table.
     * This will force a recreate of the acl table during delta processing.
     */
    qualifiers.push_back(cfg::AclTableQualifier::OUT_PORT);
  }
  utility::addAclTable(
      config,
      getTtlAclTableName(),
      priority, // priority
      {cfg::AclTableActionType::PACKET_ACTION,
       cfg::AclTableActionType::COUNTER},
      qualifiers);

  addTtlAclEntry(config, getTtlAclTableName(), aclByteCounterSupported);
}

void deleteQueuePerHostMatchers(cfg::SwitchConfig* config) {
  for (auto queueId : kQueuePerhostQueueIds()) {
    auto l2AclName = getQueuePerHostL2AclNameForQueue(queueId);
    utility::delMatcher(config, l2AclName);

    auto neighborAclName = getQueuePerHostNeighborAclNameForQueue(queueId);
    utility::delMatcher(config, neighborAclName);

    auto routeAclName = getQueuePerHostRouteAclNameForQueue(queueId);
    utility::delMatcher(config, routeAclName);
  }
}

void addQueuePerHostAclEntry(
    cfg::SwitchConfig* config,
    const std::string& aclTableName,
    bool isSai) {
  for (auto queueId : kQueuePerhostQueueIds()) {
    auto classID = kQueuePerHostQueueToClass().at(queueId);
    cfg::AclEntry aclL2;
    aclL2.name() = getQueuePerHostL2AclNameForQueue(queueId);
    aclL2.actionType() = cfg::AclActionType::PERMIT;
    aclL2.lookupClassL2() = classID;
    utility::addAclEntry(config, aclL2, aclTableName);
    utility::addQueueMatcher(config, *aclL2.name(), queueId, isSai);

    cfg::AclEntry aclNeighbor;
    aclNeighbor.name() = getQueuePerHostNeighborAclNameForQueue(queueId);
    aclNeighbor.actionType() = cfg::AclActionType::PERMIT;
    aclNeighbor.lookupClassNeighbor() = classID;
    utility::addAclEntry(config, aclNeighbor, aclTableName);
    utility::addQueueMatcher(config, *aclNeighbor.name(), queueId, isSai);

    cfg::AclEntry aclRoute;
    aclRoute.name() = getQueuePerHostRouteAclNameForQueue(queueId);
    aclRoute.actionType() = cfg::AclActionType::PERMIT;
    aclRoute.lookupClassRoute() = classID;
    utility::addAclEntry(config, aclRoute, aclTableName);
    utility::addQueueMatcher(config, *aclRoute.name(), queueId, isSai);
  }
}

// Utility to add {L2, neighbor, route}-Acl Table to Multi Acl table group
void addQueuePerHostAclTables(
    cfg::SwitchConfig* config,
    int16_t priority,
    bool addAllQualifiers,
    bool isSai) {
  std::vector<cfg::AclTableQualifier> qualifiers = {
      cfg::AclTableQualifier::LOOKUP_CLASS_L2,
      cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR,
      cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE};
  std::vector<cfg::AclTableActionType> actions = {
      cfg::AclTableActionType::PACKET_ACTION, cfg::AclTableActionType::SET_TC};

  if (addAllQualifiers) {
    // Add the extra qualifiers needed for Cisco Key profile
    qualifiers.push_back(cfg::AclTableQualifier::TTL);
    qualifiers.push_back(cfg::AclTableQualifier::DSCP);
    actions.push_back(cfg::AclTableActionType::COUNTER);
  }

  utility::addAclTable(
      config,
      getQueuePerHostAclTableName(),
      priority, // priority
      actions,
      qualifiers);

  addQueuePerHostAclEntry(config, getQueuePerHostAclTableName(), isSai);
}

void updateRoutesClassID(
    const std::map<
        RoutePrefix<folly::IPAddressV4>,
        std::optional<cfg::AclLookupClass>>& routePrefix2ClassID,
    RouteUpdateWrapper* updater) {
  for (const auto& [routePrefix, classID] : routePrefix2ClassID) {
    updater->programClassID(
        RouterID(0),
        {{folly::IPAddress(routePrefix.network()), routePrefix.mask()}},
        classID,
        false /* sync*/);
  }
}

void verifyQueuePerHostMapping(
    HwSwitchEnsemble* ensemble,
    std::shared_ptr<SwitchState> swState,
    const std::vector<PortID>& portIds,
    std::optional<VlanID> vlanId,
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    bool useFrontPanel,
    bool blockNeighbor,
    std::function<std::map<PortID, HwPortStats>(const std::vector<PortID>&)>
        getHwPortStatsFn,
    std::optional<uint16_t> l4SrcPort,
    std::optional<uint16_t> l4DstPort,
    std::optional<uint8_t> dscp) {
  verifyQueuePerHostMappingImpl(
      utility::getAllocatePktFn(ensemble),
      getSendPacketFunc(ensemble->getHwSwitch()),
      getAclStatGetFn(ensemble->getHwSwitch()),
      [ensemble]() { ensemble->getHwSwitch()->updateStats(); },
      std::move(swState),
      portIds,
      vlanId,
      std::move(srcMac),
      std::move(dstMac),
      srcIp,
      dstIp,
      useFrontPanel,
      blockNeighbor,
      std::move(getHwPortStatsFn),
      l4SrcPort,
      l4DstPort,
      dscp);
}

void verifyQueuePerHostMapping(
    HwSwitch* hwSwitch,
    std::shared_ptr<SwitchState> swState,
    const std::vector<PortID>& portIds,
    std::optional<VlanID> vlanId,
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    bool useFrontPanel,
    bool blockNeighbor,
    std::function<std::map<PortID, HwPortStats>(const std::vector<PortID>&)>
        getHwPortStatsFn,
    std::optional<uint16_t> l4SrcPort,
    std::optional<uint16_t> l4DstPort,
    std::optional<uint8_t> dscp) {
  verifyQueuePerHostMappingImpl(
      [hwSwitch](uint32_t size) { return hwSwitch->allocatePacket(size); },
      getSendPacketFunc(hwSwitch),
      getAclStatGetFn(hwSwitch),
      [hwSwitch]() { hwSwitch->updateStats(); },
      std::move(swState),
      portIds,
      vlanId,
      std::move(srcMac),
      std::move(dstMac),
      srcIp,
      dstIp,
      useFrontPanel,
      blockNeighbor,
      std::move(getHwPortStatsFn),
      l4SrcPort,
      l4DstPort,
      dscp);
}

void verifyQueuePerHostMapping(
    HwSwitchEnsemble* ensemble,
    std::optional<VlanID> vlanId,
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    bool useFrontPanel,
    bool blockNeighbor,
    std::optional<uint16_t> l4SrcPort,
    std::optional<uint16_t> l4DstPort,
    std::optional<uint8_t> dscp) {
  // lambda that returns HwPortStats for the given port
  auto getPortStats =
      [&](const std::vector<PortID>& portIds) -> std::map<PortID, HwPortStats> {
    return ensemble->getLatestPortStats(portIds);
  };

  verifyQueuePerHostMapping(
      ensemble,
      ensemble->getProgrammedState(),
      ensemble->masterLogicalPortIds(),
      vlanId,
      srcMac,
      dstMac,
      srcIp,
      dstIp,
      useFrontPanel,
      blockNeighbor,
      getPortStats,
      l4SrcPort,
      l4DstPort,
      dscp);
}

void verifyQueuePerHostMapping(
    AgentEnsemble* ensemble,
    std::optional<VlanID> vlanId,
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    bool useFrontPanel,
    bool blockNeighbor,
    std::optional<uint16_t> l4SrcPort,
    std::optional<uint16_t> l4DstPort,
    std::optional<uint8_t> dscp) {
  // lambda that returns HwPortStats for the given port
  auto getPortStats =
      [&](const std::vector<PortID>& portIds) -> std::map<PortID, HwPortStats> {
    return ensemble->getLatestPortStats(portIds);
  };

  verifyQueuePerHostMappingImpl(
      utility::getAllocatePktFn(ensemble),
      getSendPacketFunc(ensemble),
      getAclStatGetFn(ensemble->getSw()),
      []() {},
      ensemble->getProgrammedState(),
      ensemble->masterLogicalPortIds(),
      vlanId,
      std::move(srcMac),
      std::move(dstMac),
      srcIp,
      dstIp,
      useFrontPanel,
      blockNeighbor,
      getPortStats,
      l4SrcPort,
      l4DstPort,
      dscp);
}

} // namespace facebook::fboss::utility
