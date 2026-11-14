// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/LoadBalancerConfigApplier.h"
#include "fboss/agent/LoadBalancerUtils.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/VoqTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <fmt/format.h>
#include <folly/gen/Base.h>

#include <gtest/gtest.h>

DEFINE_string(
    load_balance_traffic_src,
    "",
    "CSV file with source IP, port and destination IP, port for load balancing test. See P827101297 for example.");

namespace {
std::vector<std::string> kTrafficFields = {"sip", "dip", "sport", "dport"};
} // namespace

namespace facebook::fboss::utility {

namespace {

cfg::Fields getHalfHashFields() {
  cfg::Fields hashFields;
  hashFields.ipv4Fields() = std::set<cfg::IPv4Field>(
      {cfg::IPv4Field::SOURCE_ADDRESS, cfg::IPv4Field::DESTINATION_ADDRESS});
  hashFields.ipv6Fields() = std::set<cfg::IPv6Field>(
      {cfg::IPv6Field::SOURCE_ADDRESS, cfg::IPv6Field::DESTINATION_ADDRESS});

  return hashFields;
}

cfg::Fields getFullHashFields() {
  auto hashFields = getHalfHashFields();
  hashFields.transportFields() = std::set<cfg::TransportField>(
      {cfg::TransportField::SOURCE_PORT,
       cfg::TransportField::DESTINATION_PORT});
  return hashFields;
}

cfg::Fields getFullHashFieldsWithFlowLabel() {
  auto hashFields = getFullHashFields();
  hashFields.ipv6Fields()->insert(cfg::IPv6Field::FLOW_LABEL);
  return hashFields;
}

cfg::Fields getFullHashUdf() {
  auto hashFields = getHalfHashFields();
  hashFields.transportFields() = std::set<cfg::TransportField>(
      {cfg::TransportField::SOURCE_PORT,
       cfg::TransportField::DESTINATION_PORT});
  hashFields.udfGroups() =
      std::vector<std::string>({kUdfHashDstQueuePairGroupName});
  return hashFields;
}

cfg::LoadBalancer getHalfHashConfig(
    const HwAsic& asic,
    cfg::LoadBalancerID id) {
  cfg::LoadBalancer loadBalancer;
  *loadBalancer.id() = id;
  if (asic.isSupported(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    *loadBalancer.fieldSelection() = getHalfHashFields();
  }
  *loadBalancer.algorithm() = cfg::HashingAlgorithm::CRC16_CCITT;
  return loadBalancer;
}
cfg::LoadBalancer getFullHashConfig(
    const HwAsic& asic,
    cfg::LoadBalancerID id) {
  cfg::LoadBalancer loadBalancer;
  *loadBalancer.id() = id;
  if (asic.isSupported(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    *loadBalancer.fieldSelection() = getFullHashFields();
  }
  *loadBalancer.algorithm() = cfg::HashingAlgorithm::CRC16_CCITT;
  return loadBalancer;
}
cfg::LoadBalancer getFullHashUdfConfig(
    const HwAsic& asic,
    cfg::LoadBalancerID id) {
  cfg::LoadBalancer loadBalancer;
  *loadBalancer.id() = id;
  if (asic.isSupported(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    *loadBalancer.fieldSelection() = getFullHashUdf();
  }
  *loadBalancer.algorithm() = cfg::HashingAlgorithm::CRC16_CCITT;
  return loadBalancer;
}
cfg::LoadBalancer getFullHashWithFlowLabelConfig(
    const HwAsic& asic,
    cfg::LoadBalancerID id) {
  cfg::LoadBalancer loadBalancer;
  *loadBalancer.id() = id;
  if (asic.isSupported(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    *loadBalancer.fieldSelection() = getFullHashFieldsWithFlowLabel();
  }
  *loadBalancer.algorithm() = cfg::HashingAlgorithm::CRC16_CCITT;
  return loadBalancer;
}
} // namespace
cfg::LoadBalancer getEcmpFullWithFlowLabelHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return getFullHashWithFlowLabelConfig(
      *checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing),
      cfg::LoadBalancerID::ECMP);
}
cfg::LoadBalancer getTrunkFullWithFlowLabelHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return getFullHashWithFlowLabelConfig(
      *checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing),
      cfg::LoadBalancerID::AGGREGATE_PORT);
}
cfg::LoadBalancer getTrunkHalfHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return getHalfHashConfig(
      *checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing),
      cfg::LoadBalancerID::AGGREGATE_PORT);
}
cfg::LoadBalancer getTrunkFullHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return getFullHashConfig(
      *checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing),
      cfg::LoadBalancerID::AGGREGATE_PORT);
}
cfg::LoadBalancer getEcmpHalfHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return getHalfHashConfig(
      *checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing),
      cfg::LoadBalancerID::ECMP);
}
cfg::LoadBalancer getEcmpFullHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return getFullHashConfig(
      *checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing),
      cfg::LoadBalancerID::ECMP);
}
cfg::LoadBalancer getEcmpFullUdfHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return getFullHashUdfConfig(
      *checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing),
      cfg::LoadBalancerID::ECMP);
}
std::vector<cfg::LoadBalancer> getEcmpFullTrunkHalfHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return {getEcmpFullHashConfig(asics), getTrunkHalfHashConfig(asics)};
}
std::vector<cfg::LoadBalancer> getEcmpHalfTrunkFullHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return {getEcmpHalfHashConfig(asics), getTrunkFullHashConfig(asics)};
}
std::vector<cfg::LoadBalancer> getEcmpFullTrunkFullHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return {getEcmpFullHashConfig(asics), getTrunkFullHashConfig(asics)};
}
std::vector<cfg::LoadBalancer>
getEcmpFullWithFlowLabelTrunkFullWithFlowLabelHashConfig(
    const std::vector<const HwAsic*>& asics) {
  return {
      getEcmpFullWithFlowLabelHashConfig(asics),
      getTrunkFullWithFlowLabelHashConfig(asics)};
}

cfg::FlowletSwitchingConfig getDefaultFlowletSwitchingConfig(
    bool isSai,
    cfg::SwitchingMode switchingMode,
    cfg::SwitchingMode backupSwitchingMode,
    bool supportsFuturePortLoad) {
  cfg::FlowletSwitchingConfig flowletCfg;
  flowletCfg.inactivityIntervalUsecs() = 16;
  flowletCfg.flowletTableSize() = 2048;
  if (switchingMode == cfg::SwitchingMode::PER_PACKET_QUALITY) {
    flowletCfg.flowletTableSize() = 256;
  } else if (switchingMode == cfg::SwitchingMode::PER_PACKET_RANDOM) {
    flowletCfg.inactivityIntervalUsecs() = 256;
    flowletCfg.flowletTableSize() = 512;
  }
  // set the egress load and queue exponent to zero for DLB engine
  // to do load balancing across all the links better with single stream
  // SAI has exponents 1 based while native BCM is 0 based
  // SAI has sample rate in msec while native BCM is number of ticks
  if (isSai) {
    flowletCfg.dynamicEgressLoadExponent() = 1;
    if (supportsFuturePortLoad) {
      flowletCfg.dynamicQueueExponent() = 1;
    }
    flowletCfg.dynamicPhysicalQueueExponent() = 5;
    flowletCfg.dynamicSampleRate() = 1000;
  } else {
    flowletCfg.dynamicEgressLoadExponent() = 0;
    if (supportsFuturePortLoad) {
      flowletCfg.dynamicQueueExponent() = 0;
    }
    flowletCfg.dynamicPhysicalQueueExponent() = 4;
    flowletCfg.dynamicSampleRate() = 1000000;
  }
  flowletCfg.dynamicQueueMinThresholdBytes() = 1000;
  flowletCfg.dynamicQueueMaxThresholdBytes() = 10000;
  flowletCfg.dynamicEgressMinThresholdBytes() = 1000;
  flowletCfg.dynamicEgressMaxThresholdBytes() = 10000;
  flowletCfg.switchingMode() = switchingMode;
  flowletCfg.backupSwitchingMode() = backupSwitchingMode;
  return flowletCfg;
}

void addFlowletAcl(
    cfg::SwitchConfig& cfg,
    bool isSai,
    const std::string& aclName,
    const std::string& aclCounterName,
    bool udfFlowlet) {
  cfg::AclEntry acl;
  acl.name() = aclName;
  acl.actionType() = cfg::AclActionType::PERMIT;
  acl.proto() = 17;
  acl.l4DstPort() = 4791;
  acl.dstIp() = "2001::/16";
  if (checkSameAndGetAsicType(cfg) == cfg::AsicType::ASIC_TYPE_CHENAB ||
      checkSameAndGetAsicType(cfg) == cfg::AsicType::ASIC_TYPE_CHENAB2) {
    acl.etherType() = cfg::EtherType::IPv6;
  }
  if (udfFlowlet) {
    if (isSai) {
      utility::addUdfTableToAcl(
          &acl,
          utility::kRoceUdfFlowletGroupName,
          {utility::kRoceReserved},
          {utility::kRoceReserved});
    } else {
      acl.udfGroups() = {utility::kRoceUdfFlowletGroupName};
      acl.roceBytes() = {utility::kRoceReserved};
      acl.roceMask() = {utility::kRoceReserved};
    }
  }
  utility::addAcl(&cfg, acl, cfg::AclStage::INGRESS);

  cfg::MatchAction matchAction = cfg::MatchAction();
  matchAction.flowletAction() = cfg::FlowletAction::FORWARD;
  matchAction.counter() = aclCounterName;
  std::vector<cfg::CounterType> counterTypes{
      cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
  auto counter = cfg::TrafficCounter();
  *counter.name() = aclCounterName;
  *counter.types() = counterTypes;
  cfg.trafficCounters()->push_back(counter);
  utility::addMatcher(&cfg, aclName, matchAction);
}

void addFlowletConfigs(
    cfg::SwitchConfig& cfg,
    const std::vector<PortID>& ports,
    bool isSai,
    cfg::SwitchingMode switchingMode,
    cfg::SwitchingMode backupSwitchingMode,
    bool supportsFuturePortLoad) {
  cfg::FlowletSwitchingConfig flowletCfg =
      utility::getDefaultFlowletSwitchingConfig(
          isSai, switchingMode, backupSwitchingMode, supportsFuturePortLoad);
  cfg.flowletSwitchingConfig() = flowletCfg;

  std::map<std::string, cfg::PortFlowletConfig> portFlowletCfgMap;
  cfg::PortFlowletConfig portFlowletConfig;
  if (isSai) {
    portFlowletConfig.scalingFactor() = kScalingFactorSai;
  } else {
    portFlowletConfig.scalingFactor() = kScalingFactor;
  }
  portFlowletConfig.loadWeight() = kLoadWeight;
  if (supportsFuturePortLoad) {
    portFlowletConfig.queueWeight() = kQueueWeight;
  }
  portFlowletCfgMap.insert(std::make_pair("default", portFlowletConfig));
  cfg.portFlowletConfigs() = portFlowletCfgMap;

  std::vector<PortID> portIds(ports.begin(), ports.begin() + ports.size());
  for (const auto& portId : portIds) {
    auto portCfg = utility::findCfgPort(cfg, portId);
    portCfg->flowletConfigName() = "default";
  }
}

std::shared_ptr<SwitchState> setLoadBalancer(
    TestEnsembleIf* ensemble,
    const std::shared_ptr<SwitchState>& inputState,
    const cfg::LoadBalancer& loadBalancerCfg,
    const SwitchIdScopeResolver& resolver) {
  return addLoadBalancers(ensemble, inputState, {loadBalancerCfg}, resolver);
}

std::shared_ptr<SwitchState> addLoadBalancers(
    TestEnsembleIf* ensemble,
    const std::shared_ptr<SwitchState>& inputState,
    const std::vector<cfg::LoadBalancer>& loadBalancerCfgs,
    const SwitchIdScopeResolver& resolver) {
  bool sai = ensemble->isSai();
  auto mac = ensemble->getLocalMac(SwitchID(0));
  if (!ensemble->getHwAsicTable()->isFeatureSupportedOnAnyAsic(
          HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    // configuring hash is not supported.
    XLOG(WARNING) << "load balancer configuration is not supported.";
    return inputState;
  }
  auto newState{inputState->clone()};
  auto lbMap = newState->getLoadBalancers()->modify(&newState);
  for (const auto& loadBalancerCfg : loadBalancerCfgs) {
    auto id = LoadBalancerConfigParser::parseLoadBalancerID(loadBalancerCfg);
    auto loadBalancer = LoadBalancerConfigParser(
                            utility::generateDeterministicSeed(id, mac, sai))
                            .parse(loadBalancerCfg);
    if (lbMap->getNodeIf(loadBalancer->getID())) {
      lbMap->updateNode(loadBalancer, resolver.scope(loadBalancer));
    } else {
      lbMap->addNode(loadBalancer, resolver.scope(loadBalancer));
    }
  }
  return newState;
}

template <typename PortIdT, typename PortStatsT>
std::set<uint64_t> getSortedPortBytes(
    const std::map<PortIdT, PortStatsT>& portIdToStats) {
  auto portBytes =
      folly::gen::from(portIdToStats) |
      folly::gen::map([&](const auto& portIdAndStats) {
        if constexpr (std::is_same_v<PortStatsT, HwPortStats>) {
          return *portIdAndStats.second.outBytes_();
        } else if constexpr (std::is_same_v<PortStatsT, HwSysPortStats>) {
          const auto& stats = portIdAndStats.second;
          return stats.queueOutBytes_()->at(getDefaultQueue()) +
              stats.queueOutDiscardBytes_()->at(getDefaultQueue());
        }
        throw FbossError("Unsupported port stats type in isLoadBalancedImpl");
      }) |
      folly::gen::as<std::set<uint64_t>>();
  return portBytes;
}

template <typename PortIdT, typename PortStatsT>
std::set<uint64_t> getSortedPortBytesIncrement(
    const std::map<PortIdT, PortStatsT>& beforePortIdToStats,
    const std::map<PortIdT, PortStatsT>& afterPortIdToStats) {
  auto portBytesIncrement =
      folly::gen::from(beforePortIdToStats) |
      folly::gen::map([&](const auto& beforePortIdToStats) {
        CHECK(
            afterPortIdToStats.find(beforePortIdToStats.first) !=
            afterPortIdToStats.end());
        if constexpr (std::is_same_v<PortStatsT, HwPortStats>) {
          return *afterPortIdToStats.at(beforePortIdToStats.first).outBytes_() -
              *beforePortIdToStats.second.outBytes_();
        } else if constexpr (std::is_same_v<PortStatsT, HwSysPortStats>) {
          const auto& beforeStats = beforePortIdToStats.second;
          const auto& afterStats =
              afterPortIdToStats.at(beforePortIdToStats.first);
          const auto kDefaultQueue = getDefaultQueue();
          return (afterStats.queueOutBytes_()->at(kDefaultQueue) +
                  afterStats.queueOutDiscardBytes_()->at(kDefaultQueue)) -
              (beforeStats.queueOutBytes_()->at(kDefaultQueue) +
               beforeStats.queueOutDiscardBytes_()->at(kDefaultQueue));
        }
        throw FbossError("Unsupported port stats type in isLoadBalancedImpl");
      }) |
      folly::gen::as<std::set<uint64_t>>();
  return portBytesIncrement;
}

template <typename PortStatsT>
uint64_t getPortOutBytes(const PortStatsT& stats) {
  if constexpr (std::is_same_v<PortStatsT, HwPortStats>) {
    return *stats.outBytes_();
  } else if constexpr (std::is_same_v<PortStatsT, HwSysPortStats>) {
    return stats.queueOutBytes_()->at(getDefaultQueue()) +
        stats.queueOutDiscardBytes_()->at(getDefaultQueue());
  }
  throw FbossError("Unsupported port stats type in getPortOutBytes");
}

/*
 * Every member must carry between floorPct and ceilPct of the fair share
 * (total / numMembers). See MemberShareBounds for why this is the right
 * assertion for per-packet quality spray. Kept in integer arithmetic:
 *   floorPct * total <= 100 * numMembers * memberBytes <= ceilPct * total
 */
template <typename PortIdT, typename PortStatsT>
bool isTrafficSprayedImpl(
    const std::map<PortIdT, PortStatsT>& portIdToStats,
    const MemberShareBounds& bounds) {
  const uint64_t numMembers = portIdToStats.size();
  uint64_t total = 0;
  for (const auto& [portId, stats] : portIdToStats) {
    total += getPortOutBytes(stats);
  }
  if (!numMembers || !total) {
    // No members, or no traffic at all, is a failure and not a vacuous pass.
    XLOG(INFO) << "Traffic not sprayed, members: " << numMembers
               << " total bytes: " << total;
    return false;
  }
  bool sprayed = true;
  for (const auto& [portId, stats] : portIdToStats) {
    auto memberBytes = getPortOutBytes(stats);
    auto scaled = 100 * numMembers * memberBytes;
    if (scaled < uint64_t(bounds.floorPct) * total ||
        scaled > uint64_t(bounds.ceilPct) * total) {
      XLOG(INFO) << "Member " << fmt::format("{}", portId)
                 << " outBytes: " << memberBytes << " is outside "
                 << bounds.floorPct << "%-" << bounds.ceilPct
                 << "% of the fair share " << (total / numMembers);
      sprayed = false;
    }
  }
  return sprayed;
}

bool isWithinMemberShareBounds(
    const std::map<std::string, uint64_t>& memberCounters,
    const MemberShareBounds& bounds) {
  const uint64_t numMembers = memberCounters.size();
  uint64_t total = 0;
  for (const auto& [member, counter] : memberCounters) {
    total += counter;
  }
  if (!numMembers || !total) {
    XLOG(INFO) << fmt::format(
        "No traffic to check the share of, members: {} total: {}",
        numMembers,
        total);
    return false;
  }
  bool withinBounds = true;
  for (const auto& [member, counter] : memberCounters) {
    const auto scaled = 100 * numMembers * counter;
    if (scaled < uint64_t(bounds.floorPct) * total ||
        scaled > uint64_t(bounds.ceilPct) * total) {
      XLOG(INFO) << fmt::format(
          "Member {} carried {}, outside {}%-{}% of the fair share {}",
          member,
          counter,
          bounds.floorPct,
          bounds.ceilPct,
          total / numMembers);
      withinBounds = false;
    }
  }
  return withinBounds;
}

template <typename PortIdT, typename PortStatsT>
std::pair<uint64_t, uint64_t> getHighestAndLowestBytes(
    const std::map<PortIdT, PortStatsT>& portIdToStats) {
  auto portBytes = getSortedPortBytes(portIdToStats);
  auto lowest = portBytes.empty() ? 0 : *portBytes.begin();
  auto highest = portBytes.empty() ? 0 : *portBytes.rbegin();
  XLOG(DBG0) << " Highest bytes: " << highest << " lowest bytes: " << lowest;
  return std::make_pair(highest, lowest);
}

template <typename PortIdT, typename PortStatsT>
std::pair<uint64_t, uint64_t> getHighestAndLowestBytesIncrement(
    const std::map<PortIdT, PortStatsT>& beforePortIdToStats,
    const std::map<PortIdT, PortStatsT>& afterPortIdToStats) {
  auto portBytes =
      getSortedPortBytesIncrement(beforePortIdToStats, afterPortIdToStats);
  auto lowest = portBytes.empty() ? 0 : *portBytes.begin();
  auto highest = portBytes.empty() ? 0 : *portBytes.rbegin();
  XLOG(DBG0) << " Highest bytes increment: " << highest
             << " lowest bytes increment: " << lowest;
  return std::make_pair(highest, lowest);
}

bool isDeviationWithinThreshold(
    int64_t lowest,
    int64_t highest,
    int maxDeviationPct,
    bool noTrafficOk) {
  if (!lowest) {
    return !highest && noTrafficOk;
  }
  auto percentDev = (static_cast<float>(highest - lowest) / lowest) * 100.0;
  // Don't tolerate a deviation of more than maxDeviationPct
  XLOG(DBG2) << "Percent Deviation: " << percentDev
             << ", Maximum Deviation: " << maxDeviationPct;

  return percentDev <= maxDeviationPct;
}

template <typename PortIdT, typename PortStatsT>
bool isLoadBalancedImpl(
    const std::map<PortIdT, PortStatsT>& portIdToStats,
    const std::vector<NextHopWeight>& weights,
    int maxDeviationPct,
    bool noTrafficOk) {
  auto ecmpPorts = folly::gen::from(portIdToStats) |
      folly::gen::map([](const auto& portIdAndStats) {
                     return portIdAndStats.first;
                   }) |
      folly::gen::as<std::vector<PortIdT>>();

  auto [highest, lowest] = getHighestAndLowestBytes(portIdToStats);
  if (!lowest) {
    return !highest && noTrafficOk;
  }
  if (!weights.empty()) {
    assert(ecmpPorts.size() == weights.size());
    auto maxWeight = *(std::max_element(weights.begin(), weights.end()));
    for (auto i = 0; i < portIdToStats.size(); ++i) {
      uint64_t portOutBytes;
      if constexpr (std::is_same_v<PortStatsT, HwPortStats>) {
        portOutBytes = *portIdToStats.find(ecmpPorts[i])->second.outBytes_();
      } else if constexpr (std::is_same_v<PortStatsT, HwSysPortStats>) {
        const auto& stats = portIdToStats.find(ecmpPorts[i])->second;
        portOutBytes = stats.queueOutBytes_()->at(getDefaultQueue()) +
            stats.queueOutDiscardBytes_()->at(getDefaultQueue());
      } else {
        throw FbossError("Unsupported port stats type in isLoadBalancedImpl");
      }
      auto weightPercent = (static_cast<float>(weights[i]) / maxWeight) * 100.0;
      auto portOutBytesPercent =
          (static_cast<float>(portOutBytes) / highest) * 100.0;
      auto percentDev = std::abs(weightPercent - portOutBytesPercent);
      // Don't tolerate a deviation of more than maxDeviationPct
      XLOG(DBG2) << "Percent Deviation: " << percentDev
                 << ", Maximum Deviation: " << maxDeviationPct;
      if (percentDev > maxDeviationPct) {
        return false;
      }
    }
  } else {
    if (!isDeviationWithinThreshold(lowest, highest, maxDeviationPct)) {
      return false;
    }
  }
  return true;
}

/*
 *  RoCEv2 header lookss as below
 *  Dst Queue Pair field is in the middle
 *  of the header. (below fields in bits)
 *   ----------------------------------
 *  | Opcode(8)  | Flags(8) | Key (16) |
 *  --------------- -------------------
 *  | Resvd (8)  | Dst Queue Pair (24) |
 *  -----------------------------------
 *  | Ack Req(8) | Packet Seq num (24) |
 *  ------------------------------------
 */
size_t pumpRoCETraffic(
    bool isV6,
    const AllocatePktFunc& allocateFn,
    SendPktFunc sendFn,
    folly::MacAddress dstMac,
    const std::optional<VlanID>& vlan,
    const std::optional<PortID>& frontPanelPortToLoopTraffic,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    int destPort,
    int hopLimit,
    std::optional<folly::MacAddress> srcMacAddr,
    int packetCount,
    uint8_t roceOpcode,
    uint8_t reserved,
    const std::optional<std::vector<uint8_t>>& nxtHdr,
    bool sameDstQueue) {
  folly::MacAddress srcMac(
      srcMacAddr.has_value() ? *srcMacAddr
                             : MacAddressGenerator().get(dstMac.u64HBO() + 1));

  size_t txPacketSize = 0;
  XLOG(INFO) << "Send traffic with RoCE payload .. Packet Count = "
             << packetCount;
  for (auto i = 0; i < packetCount; ++i) {
    std::vector<uint8_t> rocePayload = {roceOpcode, 0x40, 0xff, 0xff, 0x00};
    // ack req is 0x40 for packet which can be re-ordered
    // 0x0 means the packet cannot be re-ordered
    // default is 0x40
    std::vector<uint8_t> roceEndPayload = {reserved, 0x00, 0x00, 0x03};

    // vary dst queues pair ids ONLY in the RoCE pkt
    // to verify that we can hash on it
    int dstQueueIds = sameDstQueue ? 0 : i;
    // since dst queue pair id is in the middle of the packet
    // we need to keep front/end payload which doesn't vary
    rocePayload.push_back((dstQueueIds & 0x00ff0000) >> 16);
    rocePayload.push_back((dstQueueIds & 0x0000ff00) >> 8);
    rocePayload.push_back((dstQueueIds & 0x000000ff));
    // 12 byte payload
    std::move(
        roceEndPayload.begin(),
        roceEndPayload.end(),
        std::back_inserter(rocePayload));
    if (nxtHdr.has_value()) {
      std::copy(
          nxtHdr.value().begin(),
          nxtHdr.value().end(),
          std::back_inserter(rocePayload));
    }
    // Filler to take pkt size >64. IPv4 is at 58 which typically below SDK
    // supported value
    std::vector<uint8_t> filler = {0, 0, 0, 0, 0, 0, 0};
    std::move(filler.begin(), filler.end(), std::back_inserter(rocePayload));
    auto pkt = makeUDPTxPacket(
        allocateFn,
        vlan,
        srcMac,
        dstMac,
        srcIp,
        dstIp,
        kRandomUdfL4SrcPort, /* arbit src port, fixed */
        destPort,
        0,
        hopLimit,
        rocePayload);
    txPacketSize = pkt->buf()->length();
    if (frontPanelPortToLoopTraffic) {
      sendFn(
          std::move(pkt), PortDescriptor(frontPanelPortToLoopTraffic.value()));
    } else {
      sendFn(std::move(pkt));
    }
  }
  return txPacketSize;
}

size_t pumpRoCETraffic(
    bool isV6,
    const AllocatePktFunc& allocateFn,
    SendPktFunc sendFn,
    folly::MacAddress dstMac,
    const std::optional<VlanID>& vlan,
    const std::optional<PortID>& frontPanelPortToLoopTraffic,
    int destPort,
    int hopLimit,
    std::optional<folly::MacAddress> srcMacAddr,
    int packetCount,
    uint8_t roceOpcode,
    uint8_t reserved,
    const std::optional<std::vector<uint8_t>>& nxtHdr,
    bool sameDstQueue) {
  auto srcIp = folly::IPAddress(isV6 ? "1001::1" : "100.10.0.1");
  auto dstIp = folly::IPAddress(isV6 ? "2001::1" : "200.10.0.1");

  return pumpRoCETraffic(
      isV6,
      allocateFn,
      std::move(sendFn),
      dstMac,
      vlan,
      frontPanelPortToLoopTraffic,
      srcIp,
      dstIp,
      destPort,
      hopLimit,
      srcMacAddr,
      packetCount,
      roceOpcode,
      reserved,
      nxtHdr,
      sameDstQueue);
}
/*
 * The helper expects source file FLAGS_load_balance_traffic_src to be in CSV
 * format, where it should contain the following columns:
 *
 *   sip - source IP
 *   dip - destination IP
 *   sport - source port
 *   dport - destination port
 *
 *   Please see P827101297 for an example of how this file should be formatted.
 */
size_t pumpTrafficWithSourceFile(
    const AllocatePktFunc& allocateFn,
    SendPktFunc sendFn,
    folly::MacAddress dstMac,
    const std::optional<VlanID>& vlan,
    std::optional<PortID> frontPanelPortToLoopTraffic,
    int hopLimit,
    std::optional<folly::MacAddress> srcMacAddr) {
  XLOG(DBG2) << "Using " << FLAGS_load_balance_traffic_src
             << " as source file for traffic generation";
  folly::MacAddress srcMac(
      srcMacAddr.has_value() ? *srcMacAddr
                             : MacAddressGenerator().get(dstMac.u64HBO() + 1));
  size_t pktSize = 0;
  // Use source file to generate traffic
  if (!FLAGS_load_balance_traffic_src.empty()) {
    std::ifstream srcFile(FLAGS_load_balance_traffic_src);
    std::string line;
    // Find index of SIP, DIP, SPort and DPort
    std::vector<int> indices;
    if (std::getline(srcFile, line)) {
      std::vector<std::string> parsedLine;
      folly::split(',', line, parsedLine);
      for (const auto& field : kTrafficFields) {
        for (int i = 0; i < parsedLine.size(); i++) {
          std::string column = parsedLine[i];
          column.erase(
              std::remove_if(
                  column.begin(),
                  column.end(),
                  [](auto const& c) -> bool { return !std::isalnum(c); }),
              column.end());
          if (field == column) {
            indices.push_back(i);
            break;
          }
        }
      }
      CHECK_EQ(kTrafficFields.size(), indices.size());
    } else {
      throw FbossError("Empty source file ", FLAGS_load_balance_traffic_src);
    }
    while (std::getline(srcFile, line)) {
      std::vector<std::string> parsedLine;
      folly::split(',', line, parsedLine);
      auto pkt = makeUDPTxPacket(
          allocateFn,
          vlan,
          srcMac,
          dstMac,
          folly::IPAddress(parsedLine[indices[0]]),
          folly::IPAddress(parsedLine[indices[1]]),
          folly::to<uint16_t>(parsedLine[indices[2]]),
          folly::to<uint16_t>(parsedLine[indices[3]]),
          0,
          hopLimit);
      pktSize = pkt->buf()->length();
      if (frontPanelPortToLoopTraffic) {
        sendFn(
            std::move(pkt),
            PortDescriptor(frontPanelPortToLoopTraffic.value()));
      } else {
        sendFn(std::move(pkt));
      }
    }
  } else {
    throw FbossError(
        "Using pumpTrafficWithSourceFile without source file specified");
  }
  return pktSize;
}

size_t pumpTraffic(
    bool isV6,
    const AllocatePktFunc& allocateFn,
    SendPktFunc sendFn,
    folly::MacAddress dstMac,
    const std::optional<VlanID>& vlan,
    std::optional<PortID> frontPanelPortToLoopTraffic,
    int hopLimit,
    int numPackets,
    std::optional<folly::MacAddress> srcMacAddr) {
  if (!FLAGS_load_balance_traffic_src.empty()) {
    return pumpTrafficWithSourceFile(
        allocateFn,
        sendFn,
        dstMac,
        vlan,
        frontPanelPortToLoopTraffic,
        hopLimit,
        srcMacAddr);
  }
  size_t pktSize = 0;
  folly::MacAddress srcMac(
      srcMacAddr.has_value() ? *srcMacAddr
                             : MacAddressGenerator().get(dstMac.u64HBO() + 1));
  for (auto i = 0; i < int(std::sqrt(numPackets)); ++i) {
    auto srcIp = folly::IPAddress(
        isV6 ? fmt::format("1001::{}:{}", (i + 1) / 256, (i + 1) % 256)
             : fmt::format("100.10.{}.{}", (i + 1) / 256, (i + 1) % 256));
    for (auto j = 0; j < int(numPackets / std::sqrt(numPackets)); ++j) {
      auto dstIp = folly::IPAddress(
          isV6 ? fmt::format("2001::{}:{}", (j + 1) / 256, (j + 1) % 256)
               : fmt::format("201.10.{}.{}", (j + 1) / 256, (j + 1) % 256));
      auto pkt = makeUDPTxPacket(
          allocateFn,
          vlan,
          srcMac,
          dstMac,
          srcIp,
          dstIp,
          10000 + i,
          20000 + j,
          0,
          hopLimit);
      pktSize = pkt->buf()->length();
      if (frontPanelPortToLoopTraffic) {
        sendFn(
            std::move(pkt),
            PortDescriptor(frontPanelPortToLoopTraffic.value()));
      } else {
        sendFn(std::move(pkt));
      }
    }
  }
  return pktSize;
}

size_t pumpTrafficWithFlowLabel(
    const AllocatePktFunc& allocateFn,
    SendPktFunc sendFn,
    folly::MacAddress dstMac,
    const std::optional<VlanID>& vlan,
    std::optional<PortID> frontPanelPortToLoopTraffic,
    int hopLimit,
    int numPackets,
    std::optional<folly::MacAddress> srcMacAddr) {
  size_t pktSize = 0;
  folly::MacAddress srcMac(
      srcMacAddr.has_value() ? *srcMacAddr
                             : MacAddressGenerator().get(dstMac.u64HBO() + 1));
  auto srcIp = folly::IPAddressV6("1001::1");
  auto dstIp = folly::IPAddressV6("2001::1");
  uint16_t srcPort = 10000;
  uint16_t dstPort = 20000;
  for (int i = 0; i < numPackets; ++i) {
    uint32_t flowLabel = (i + 1) & 0xFFFFF;
    auto pkt = makeUDPTxPacket(
        allocateFn,
        vlan,
        srcMac,
        dstMac,
        srcIp,
        dstIp,
        srcPort,
        dstPort,
        0,
        hopLimit,
        std::nullopt,
        flowLabel);
    pktSize = pkt->buf()->length();
    if (frontPanelPortToLoopTraffic) {
      sendFn(
          std::move(pkt), PortDescriptor(frontPanelPortToLoopTraffic.value()));
    } else {
      sendFn(std::move(pkt));
    }
  }
  return pktSize;
}

void pumpTraffic(
    const AllocatePktFunc& allocateFn,
    SendPktFunc sendFn,
    folly::MacAddress dstMac,
    const std::vector<folly::IPAddress>& srcIps,
    const std::vector<folly::IPAddress>& dstIps,
    uint16_t srcPort,
    uint16_t dstPort,
    uint8_t streams,
    const std::optional<VlanID>& vlan,
    std::optional<PortID> frontPanelPortToLoopTraffic,
    int hopLimit,
    std::optional<folly::MacAddress> srcMacAddr,
    int numPkts) {
  folly::MacAddress srcMac(
      srcMacAddr.has_value() ? *srcMacAddr
                             : MacAddressGenerator().get(dstMac.u64HBO() + 1));
  for (auto srcIp : srcIps) {
    for (auto dstIp : dstIps) {
      CHECK_EQ(srcIp.isV4(), dstIp.isV4());
      for (auto i = 0; i < streams; i++) {
        while (numPkts--) {
          auto pkt = makeUDPTxPacket(
              allocateFn,
              vlan,
              srcMac,
              dstMac,
              srcIp,
              dstIp,
              srcPort + i,
              dstPort + i,
              0,
              hopLimit);
          if (frontPanelPortToLoopTraffic) {
            sendFn(
                std::move(pkt),
                PortDescriptor(frontPanelPortToLoopTraffic.value()));
          } else {
            sendFn(std::move(pkt));
          }
        }
      }
    }
  }
}
/*
 * Generate traffic with random source ip, destination ip, source port and
 * destination port. every run will pump same random traffic as random number
 * generator is seeded with constant value. in an attempt to unify hash
 * configurations across switches in network, full hash is considered to be
 * present on all switches. this causes polarization in tests and vendor
 * recommends not to use traffic where source and destination fields (ip and
 * port) are only incremented by 1 but to use somewhat random traffic. however
 * random traffic should be deterministic. this function attempts to provide the
 * deterministic random traffic for experimentation and use in the load balancer
 * tests.
 */
void pumpDeterministicRandomTraffic(
    bool isV6,
    const AllocatePktFunc& allocateFn,
    SendPktFunc sendFn,
    folly::MacAddress intfMac,
    VlanID vlan,
    std::optional<PortID> frontPanelPortToLoopTraffic,
    int hopLimit) {
  static uint32_t count = 0;
  uint32_t counter = 1;

  RandomNumberGenerator srcV4(0, 0, 0xFF);
  RandomNumberGenerator srcV6(0, 0, 0xFFFF);
  RandomNumberGenerator dstV4(1, 0, 0xFF);
  RandomNumberGenerator dstV6(1, 0, 0xFFFF);
  RandomNumberGenerator srcPort(2, 10001, 10100);
  RandomNumberGenerator dstPort(2, 20001, 20100);

  auto intToHex = [](auto i) {
    std::stringstream stream;
    stream << std::hex << i;
    return stream.str();
  };

  auto srcMac = MacAddressGenerator().get(intfMac.u64HBO() + 1);
  for (auto i = 0; i < 1000; ++i) {
    auto srcIp = isV6
        ? folly::IPAddress(fmt::format("1001::{}", intToHex(srcV6())))
        : folly::IPAddress(fmt::format("100.10.0.{}", srcV4()));
    for (auto j = 0; j < 100; ++j) {
      auto dstIp = isV6
          ? folly::IPAddress(fmt::format("2001::{}", intToHex(dstV6())))
          : folly::IPAddress(fmt::format("200.10.0.{}", dstV4()));

      auto pkt = makeUDPTxPacket(
          allocateFn,
          vlan,
          srcMac,
          intfMac,
          srcIp,
          dstIp,
          srcPort(),
          dstPort(),
          0,
          hopLimit);
      if (frontPanelPortToLoopTraffic) {
        sendFn(
            std::move(pkt),
            PortDescriptor(frontPanelPortToLoopTraffic.value()));
      } else {
        sendFn(std::move(pkt));
      }

      count++;
      if (count % 1000 == 0) {
        XLOG(DBG2) << counter << " . sent " << count << " packets";
        counter++;
      }
    }
  }
  XLOG(DBG2) << "Sent total of " << count << " packets";
}

void pumpMplsTraffic(
    bool isV6,
    const AllocatePktFunc& allocateFn,
    SendPktFunc sendFn,
    uint32_t label,
    folly::MacAddress intfMac,
    const VlanID& vlanId,
    std::optional<PortID> frontPanelPortToLoopTraffic) {
  MPLSHdr::Label mplsLabel{label, 0, true, 128};
  std::unique_ptr<TxPacket> pkt;
  for (auto i = 0; i < 100; ++i) {
    auto srcIp = folly::IPAddress(
        isV6 ? fmt::format("1001::{}", i + 1)
             : fmt::format("100.10.0.{}", i + 1));
    for (auto j = 0; j < 100; ++j) {
      auto dstIp = folly::IPAddress(
          isV6 ? fmt::format("2001::{}", j + 1)
               : fmt::format("200.10.0.{}", j + 1));

      auto frame = isV6 ? utility::getEthFrame(
                              intfMac,
                              intfMac,
                              {mplsLabel},
                              srcIp.asV6(),
                              dstIp.asV6(),
                              10000 + i,
                              20000 + j,
                              vlanId)
                        : utility::getEthFrame(
                              intfMac,
                              intfMac,
                              {mplsLabel},
                              srcIp.asV4(),
                              dstIp.asV4(),
                              10000 + i,
                              20000 + j,
                              vlanId);

      if (isV6) {
        pkt = frame.getTxPacket(allocateFn);
      } else {
        pkt = frame.getTxPacket(allocateFn);
      }
      if (frontPanelPortToLoopTraffic) {
        sendFn(
            std::move(pkt),
            PortDescriptor(frontPanelPortToLoopTraffic.value()));
      } else {
        sendFn(std::move(pkt));
      }
    }
  }
}

void SendPktFunc::operator()(
    std::unique_ptr<TxPacket> pkt,
    std::optional<PortDescriptor> port,
    std::optional<uint8_t> queue) {
  func3_(std::move(pkt), std::move(port), queue);
}

AllocatePktFunc getAllocatePktFn(TestEnsembleIf* ensemble) {
  return [ensemble](uint32_t size) { return ensemble->allocatePacket(size); };
}

AllocatePktFunc getAllocatePktFn(SwSwitch* sw) {
  return [sw](uint32_t size) { return sw->allocatePacket(size); };
}

SendPktFunc getSendPktFunc(TestEnsembleIf* ensemble) {
  return SendPktFunc([ensemble](
                         std::unique_ptr<TxPacket> pkt,
                         std::optional<PortDescriptor> port,
                         std::optional<uint8_t> queue) {
    ensemble->sendPacketAsync(std::move(pkt), std::move(port), queue);
  });
}

SendPktFunc getSendPktFunc(
    SwSwitch* sw,
    const std::optional<SwitchID>& switchId) {
  return SendPktFunc([sw, switchId](
                         std::unique_ptr<TxPacket> pkt,
                         std::optional<PortDescriptor> port,
                         std::optional<uint8_t> queue) {
    if (switchId.has_value() && !port.has_value()) {
      CHECK(!queue.has_value());
      sw->sendPacketSwitchedAsync(std::move(pkt), {*switchId});
      return;
    }
    sw->sendPacketAsync(std::move(pkt), std::move(port), queue);
  });
}

void pumpTrafficAndVerifyLoadBalanced(
    const std::function<void()>& pumpTraffic,
    const std::function<void()>& clearPortStats,
    const std::function<bool()>& isLoadBalanced,
    bool loadBalanceExpected) {
  clearPortStats();
  pumpTraffic();
  if (loadBalanceExpected) {
    WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(isLoadBalanced()));
  } else {
    EXPECT_FALSE(isLoadBalanced());
  }
}

void pumpTrafficAndVerifyLoadBalanced(
    const std::function<void()>& pumpTraffic,
    const std::function<void()>& clearPortStats,
    const std::function<std::optional<uint64_t>()>& getPortOutPackets,
    uint64_t numPacketsSent,
    const std::function<bool()>& isLoadBalanced,
    bool loadBalanceExpected) {
  clearPortStats();
  auto portOutPacketsBefore = uint64_t{0};
  WITH_RETRIES({
    auto portOutPackets = getPortOutPackets();
    ASSERT_EVENTUALLY_TRUE(portOutPackets.has_value());
    portOutPacketsBefore = *portOutPackets;
    ASSERT_EVENTUALLY_EQ(0, portOutPacketsBefore);
  });

  pumpTraffic();
  auto portOutPacketsAfter = uint64_t{0};
  WITH_RETRIES({
    auto portOutPackets = getPortOutPackets();
    ASSERT_EVENTUALLY_TRUE(portOutPackets.has_value());
    portOutPacketsAfter = *portOutPackets;
    XLOG(DBG2) << "Port out packets before: " << portOutPacketsBefore
               << ", after: " << portOutPacketsAfter
               << ", packets sent: " << numPacketsSent;
    ASSERT_EVENTUALLY_EQ(numPacketsSent, portOutPacketsAfter);
  });

  if (loadBalanceExpected) {
    WITH_RETRIES(EXPECT_EVENTUALLY_TRUE(isLoadBalanced()));
  } else {
    EXPECT_FALSE(isLoadBalanced());
  }
}

/*
 * Enable load balancing on provided config. Uses an enum, declared in
 * ConfigFactory.h, to decide whether to apply full-hash or half-hash config.
 * These are the only two current hashing algorithms at the time of writing; if
 * any more are added, this function and the enum can be modified accordingly.
 */
void addLoadBalancerToConfig(
    cfg::SwitchConfig& config,
    const HwAsic* hwAsic,
    LBHash hashType) {
  if (!hwAsic->isSupported(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    return;
  }
  switch (hashType) {
    case LBHash::FULL_HASH:
      config.loadBalancers()->push_back(getEcmpFullHashConfig({hwAsic}));
      break;
    case LBHash::HALF_HASH:
      config.loadBalancers()->push_back(getEcmpHalfHashConfig({hwAsic}));
      break;
    case LBHash::FULL_HASH_UDF:
      config.loadBalancers()->push_back(getEcmpFullUdfHashConfig({hwAsic}));
      break;
    default:
      throw FbossError("invalid hashing option ", hashType);
  }
}

template bool isLoadBalancedImpl<SystemPortID, HwSysPortStats>(
    const std::map<SystemPortID, HwSysPortStats>& portIdToStats,
    const std::vector<NextHopWeight>& weights,
    int maxDeviationPct,
    bool noTrafficOk);

template bool isLoadBalancedImpl<PortID, HwPortStats>(
    const std::map<PortID, HwPortStats>& portIdToStats,
    const std::vector<NextHopWeight>& weights,
    int maxDeviationPct,
    bool noTrafficOk);

template bool isLoadBalancedImpl<std::string, HwPortStats>(
    const std::map<std::string, HwPortStats>& portIdToStats,
    const std::vector<NextHopWeight>& weights,
    int maxDeviationPct,
    bool noTrafficOk);

template bool isLoadBalancedImpl<std::string, HwSysPortStats>(
    const std::map<std::string, HwSysPortStats>& portIdToStats,
    const std::vector<NextHopWeight>& weights,
    int maxDeviationPct,
    bool noTrafficOk);

template std::set<uint64_t> getSortedPortBytes(
    const std::map<SystemPortID, HwSysPortStats>& portIdToStats);

template std::set<uint64_t> getSortedPortBytes(
    const std::map<PortID, HwPortStats>& portIdToStats);

template std::set<uint64_t> getSortedPortBytesIncrement(
    const std::map<SystemPortID, HwSysPortStats>& beforePortIdToStats,
    const std::map<SystemPortID, HwSysPortStats>& afterPortIdToStats);

template std::set<uint64_t> getSortedPortBytesIncrement(
    const std::map<PortID, HwPortStats>& beforePortIdToStats,
    const std::map<PortID, HwPortStats>& afterPortIdToStats);

template std::pair<uint64_t, uint64_t> getHighestAndLowestBytes(
    const std::map<SystemPortID, HwSysPortStats>& portIdToStats);

template std::pair<uint64_t, uint64_t> getHighestAndLowestBytes(
    const std::map<PortID, HwPortStats>& portIdToStats);

template std::pair<uint64_t, uint64_t> getHighestAndLowestBytesIncrement(
    const std::map<SystemPortID, HwSysPortStats>& beforePortIdToStats,
    const std::map<SystemPortID, HwSysPortStats>& afterPortIdToStats);

template std::pair<uint64_t, uint64_t> getHighestAndLowestBytesIncrement(
    const std::map<PortID, HwPortStats>& beforePortIdToStats,
    const std::map<PortID, HwPortStats>& afterPortIdToStats);

} // namespace facebook::fboss::utility
