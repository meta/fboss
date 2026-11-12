/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/config/agent/CoppConfigUtils.h"

#include <string_view>
#include <utility>
#include <vector>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/lib/config/agent/AclConfigUtils.h"

namespace facebook::fboss::utility {
namespace {

constexpr uint16_t kLowPriorityQueueId = 0;
constexpr uint16_t kDefaultPriorityQueueId = 1;
constexpr uint32_t kLowPriorityWeight = 1;
constexpr uint32_t kDefaultPriorityWeight = 1;
constexpr uint32_t kMidPriorityWeight = 2;
constexpr uint32_t kHighPriorityWeight = 4;

constexpr uint32_t kAveragePacketSize = 300;
constexpr uint32_t kCpuPacketOverheadBytes = 52;
constexpr uint32_t kLegacyLowPriorityPacketsPerSecond = 100;
constexpr uint32_t kLegacyDefaultPriorityPacketsPerSecond = 200;
constexpr uint32_t kLowPriorityPacketsPerSecond = 500;
constexpr uint32_t kDefaultPriorityPacketsPerSecond = 1000;
constexpr uint32_t kLowPriorityKbitsPerSecond = 28216;
constexpr uint32_t kDefaultPriorityKbitsPerSecond = 56263;
constexpr uint32_t kVoqLowPriorityKbitsPerSecond = 100 * 1024;

uint32_t getQueueRate(
    uint16_t queueId,
    uint32_t lowPriorityRate,
    uint32_t defaultPriorityRate) {
  if (queueId == kLowPriorityQueueId) {
    return lowPriorityRate;
  }
  if (queueId == kDefaultPriorityQueueId) {
    return defaultPriorityRate;
  }
  throw FbossError("Unexpected CoPP queue id ", queueId);
}

cfg::PortQueueRate makePpsQueueRate(uint32_t maximum) {
  cfg::PortQueueRate rate;
  rate.pktsPerSec() = getRange(0, maximum);
  return rate;
}

cfg::PortQueueRate makeKbpsQueueRate(uint32_t maximum) {
  cfg::PortQueueRate rate;
  rate.kbitsPerSec() = getRange(0, maximum);
  return rate;
}

uint32_t ppsToKbps(uint32_t pps) {
  // Match COOP's nearest-60-pps rounding before converting packet rate to
  // bandwidth.
  return ((pps + 30) / 60 * 60) *
      (kAveragePacketSize + kCpuPacketOverheadBytes) * 8 / 1000;
}

void setWeight(cfg::PortQueue& queue, uint32_t weight) {
  if (*queue.scheduling() != cfg::QueueScheduling::STRICT_PRIORITY &&
      *queue.scheduling() != cfg::QueueScheduling::INTERNAL) {
    queue.weight() = weight;
  }
}

cfg::PortQueue makeCpuQueue(
    const HwAsic& asic,
    uint16_t queueId,
    std::string_view name,
    uint32_t weight,
    bool rateLimited) {
  cfg::PortQueue queue;
  queue.id() = queueId;
  queue.name() = name;
  queue.streamType() = getCpuDefaultStreamType(&asic);
  queue.scheduling() = getCpuDefaultQueueScheduling(&asic);
  setWeight(queue, weight);
  if (rateLimited) {
    queue.portQueueRate() = getPortQueueRate(&asic, queueId);
  }
  return queue;
}

cfg::PacketRxReasonToQueue makeRxReasonToQueue(
    cfg::PacketRxReason reason,
    uint16_t queueId) {
  cfg::PacketRxReasonToQueue entry;
  entry.rxReason() = reason;
  entry.queueId() = queueId;
  return entry;
}

void setEtherType(
    const HwAsic& asic,
    cfg::AclEntry& acl,
    const folly::CIDRNetwork& network) {
  if (asic.isSupported(HwAsic::Feature::ACL_ENTRY_ETHER_TYPE)) {
    acl.etherType() =
        network.first.isV6() ? cfg::EtherType::IPv6 : cfg::EtherType::IPv4;
  }
}

} // namespace

const folly::CIDRNetwork& kIPv6LinkLocalMcastNetwork() {
  static const auto network = folly::IPAddress::createNetwork("ff02::/16");
  return network;
}

const folly::CIDRNetwork& kIPv6LinkLocalUcastNetwork() {
  static const auto network = folly::IPAddress::createNetwork("fe80::/10");
  return network;
}

cfg::AclEntry genLinkLocalCoppAcl(
    const HwAsic& asic,
    std::string_view name,
    const folly::CIDRNetwork& linkLocalNetwork) {
  cfg::AclEntry acl;
  acl.name() = name;
  acl.dstIp() = folly::IPAddress::networkToString(linkLocalNetwork);
  setEtherType(asic, acl, linkLocalNetwork);
  return acl;
}

cfg::AclEntry genCpuPortLinkLocalCoppAcl(
    const HwAsic& asic,
    std::string_view name,
    const folly::CIDRNetwork& linkLocalNetwork) {
  auto acl = genLinkLocalCoppAcl(asic, name, linkLocalNetwork);
  acl.srcPort() = cfg::switch_config_constants::CPU_PORT_LOGICALID();
  return acl;
}

cfg::AclEntry genNetworkControlLinkLocalCoppAcl(
    const HwAsic& asic,
    std::string_view name,
    const folly::CIDRNetwork& linkLocalNetwork) {
  auto acl = genLinkLocalCoppAcl(asic, name, linkLocalNetwork);
  acl.dscp() = kNetworkControlDscp;
  return acl;
}

cfg::Range getRange(uint32_t minimum, uint32_t maximum) {
  cfg::Range range;
  range.minimum() = minimum;
  range.maximum() = maximum;
  return range;
}

cfg::StreamType getCpuDefaultStreamType(const HwAsic* hwAsic) {
  auto streamTypes = hwAsic->getQueueStreamTypes(cfg::PortType::CPU_PORT);
  return streamTypes.empty() ? cfg::StreamType::MULTICAST
                             : *streamTypes.begin();
}

cfg::QueueScheduling getCpuDefaultQueueScheduling(const HwAsic* hwAsic) {
  // Model the effective defaults selected by COOP platform templates, rather
  // than only the default arguments declared by FBOSS_PLATFORM_CONSTANTS_MAP.
  // For example, its Tomahawk5 lambda defaults cpu_queue_priority_strict to
  // false, but every current Tomahawk5 platform template passes true. Optional
  // role- or feature-selected alternatives are not defaults and are outside
  // this helper's contract.
  if (hwAsic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
    // Spectrum4 defines only strict-priority CPU queues in COOP, without the
    // scheduling-mode option available to most other platform families.
    return cfg::QueueScheduling::STRICT_PRIORITY;
  }

  switch (hwAsic->getAsicType()) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1:
      // The deployed platform templates for these ASICs opt into strict
      // priority even when their constants-map lambda also permits WRR.
      return cfg::QueueScheduling::STRICT_PRIORITY;
    case cfg::AsicType::ASIC_TYPE_JERICHO3:
      // Jericho3's COOP CPU queue definitions are unconditionally strict.
      return cfg::QueueScheduling::STRICT_PRIORITY;
    default:
      // Older XGS, Tajo, and Jericho2 retain COOP's WRR default.
      return cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  }
}

cfg::PortQueueRate getPortQueueRate(const HwAsic* hwAsic, uint16_t queueId) {
  // Resolve the final rate and unit from COOP's effective ASIC defaults. In
  // particular, Tajo and Chenab express their policy directly in Kbps; they do
  // not use an intermediate PPS configuration.
  if (hwAsic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_TAJO ||
      hwAsic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
    return makeKbpsQueueRate(getQueueRate(
        queueId, kLowPriorityKbitsPerSecond, kDefaultPriorityKbitsPerSecond));
  }

  if (hwAsic->getSwitchType() == cfg::SwitchType::VOQ) {
    return makeKbpsQueueRate(getQueueRate(
        queueId, kVoqLowPriorityKbitsPerSecond, kVoqLowPriorityKbitsPerSecond));
  }

  const auto asicType = hwAsic->getAsicType();
  // COOP retains its original 100/200 PPS limits for TD2 and first-generation
  // Tomahawk. TH3 and newer XGS ASICs use the raised 500/1000 PPS policy.
  const auto useLegacyRate = asicType == cfg::AsicType::ASIC_TYPE_TRIDENT2 ||
      asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWK;
  const auto packetsPerSecond = getQueueRate(
      queueId,
      useLegacyRate ? kLegacyLowPriorityPacketsPerSecond
                    : kLowPriorityPacketsPerSecond,
      useLegacyRate ? kLegacyDefaultPriorityPacketsPerSecond
                    : kDefaultPriorityPacketsPerSecond);

  if (hwAsic->isSupported(HwAsic::Feature::SCHEDULER_PPS)) {
    return makePpsQueueRate(packetsPerSecond);
  }

  // TU1's current SAI SDK cannot program packet-meter schedulers. Preserve the
  // modern XGS packet budget by expressing its equivalent as a byte rate.
  return makeKbpsQueueRate(ppsToKbps(packetsPerSecond));
}

cfg::ToCpuAction getCpuActionType(const HwAsic* hwAsic) {
  switch (hwAsic->getAsicType()) {
    case cfg::AsicType::ASIC_TYPE_FAKE:
    case cfg::AsicType::ASIC_TYPE_FAKE_NO_WARMBOOT:
    case cfg::AsicType::ASIC_TYPE_MOCK:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK3:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1:
    case cfg::AsicType::ASIC_TYPE_EBRO:
    case cfg::AsicType::ASIC_TYPE_P200:
    case cfg::AsicType::ASIC_TYPE_GARONNE:
    case cfg::AsicType::ASIC_TYPE_YUBA:
    case cfg::AsicType::ASIC_TYPE_G202X:
      return cfg::ToCpuAction::COPY;
    case cfg::AsicType::ASIC_TYPE_JERICHO2:
    case cfg::AsicType::ASIC_TYPE_JERICHO3:
    case cfg::AsicType::ASIC_TYPE_JERICHO4:
    case cfg::AsicType::ASIC_TYPE_QUMRAN4D:
    case cfg::AsicType::ASIC_TYPE_CHENAB:
    case cfg::AsicType::ASIC_TYPE_CHENAB2:
      return cfg::ToCpuAction::TRAP;
    case cfg::AsicType::ASIC_TYPE_ELBERT_8DD:
    case cfg::AsicType::ASIC_TYPE_TRIDENT2:
    case cfg::AsicType::ASIC_TYPE_AGERA3:
    case cfg::AsicType::ASIC_TYPE_SANDIA_PHY:
    case cfg::AsicType::ASIC_TYPE_RAMON:
    case cfg::AsicType::ASIC_TYPE_RAMON3:
      break;
  }
  throw FbossError(
      "ASIC does not support a CPU action: ", hwAsic->getAsicType());
}

cfg::MatchAction genCoppQueueAction(
    bool enableUserDefinedTrap,
    const HwAsic& asic,
    int queueId,
    std::optional<cfg::ToCpuAction> toCpuAction) {
  cfg::MatchAction action;

  // enableUserDefinedTrap selects a configuration mode; it is not an ASIC
  // capability query. CoppTestUtils passes its gflag directly and intentionally
  // exercises the UDT-mode configuration on every SAI platform. Config
  // generation instead passes the platform-scoped default command argument, so
  // platforms such as Tajo and DNX select the sendToQueue fallback. Keeping
  // that decision at the caller preserves SAI test behavior while allowing
  // generated default and link-test configs to follow their rollout settings.
  //
  // sendToQueue is the portable fallback. For a SAI CoPP ACL,
  // SaiAclTableManager implements it as SET_TC plus the requested COPY/TRAP
  // packet action. Do not also serialize setTc: both fields select the same
  // traffic class and SaiAclTableManager only verifies that they agree.
  //
  // In UDT mode, serialize the explicit SAI actions. On supported ASICs, UDT
  // maps the punted packet to the CPU queue. SaiAclTableManager guards actual
  // UDT programming by ASIC capability, so Tajo and DNX tests ignore the UDT
  // field and retain their existing setTc behavior. ASICs such as Chenab
  // support UDT but cannot combine SET_TC, UDT, and a CPU action, so omit setTc
  // when a CPU action is present. If toCpuAction is absent, the ACL forwards by
  // default and uses only setTc; no packet is copied or trapped.
  if (enableUserDefinedTrap) {
    if (toCpuAction.has_value()) {
      cfg::UserDefinedTrapAction userDefinedTrap;
      userDefinedTrap.queueId() = queueId;
      action.userDefinedTrap() = userDefinedTrap;
    }
    if (!toCpuAction.has_value() ||
        asic.isSupported(
            HwAsic::Feature::SAI_SET_TC_WITH_USER_DEFINED_TRAP_CPU_ACTION)) {
      // Assume TC i maps to queue i for all i on SAI switches.
      cfg::SetTcAction setTc;
      setTc.tcValue() = queueId;
      action.setTc() = setTc;
    }
  } else {
    cfg::QueueMatchAction queueAction;
    queueAction.queueId() = queueId;
    action.sendToQueue() = queueAction;
  }

  if (toCpuAction.has_value()) {
    action.toCpuAction() = *toCpuAction;
  }
  return action;
}

std::vector<std::pair<cfg::AclEntry, cfg::MatchAction>> genLinkLocalCoppAcls(
    const HwAsic& asic,
    const cfg::MatchAction& highPriorityAction,
    const cfg::MatchAction& midPriorityAction) {
  const auto& linkLocalUcastNetwork = kIPv6LinkLocalUcastNetwork();
  const auto& linkLocalMcastNetwork = kIPv6LinkLocalMcastNetwork();
  return {
      {genCpuPortLinkLocalCoppAcl(
           asic, "cpuPolicing-CPU-Port-linkLocal-v6", linkLocalUcastNetwork),
       {}},
      {genCpuPortLinkLocalCoppAcl(
           asic, "cpuPolicing-CPU-Port-Mcast-v6", linkLocalMcastNetwork),
       {}},
      {genNetworkControlLinkLocalCoppAcl(
           asic,
           "cpuPolicing-high-NetworkControl-linkLocal-v6",
           linkLocalUcastNetwork),
       highPriorityAction},
      {genNetworkControlLinkLocalCoppAcl(
           asic,
           "cpuPolicing-high-NetworkControl-ff02::/16",
           linkLocalMcastNetwork),
       highPriorityAction},
      {genLinkLocalCoppAcl(
           asic, "cpuPolicing-mid-linkLocal-v6", linkLocalUcastNetwork),
       midPriorityAction},
      {genLinkLocalCoppAcl(
           asic, "cpuPolicing-mid-ff02::/16", linkLocalMcastNetwork),
       midPriorityAction},
  };
}

void addDefaultCpuQueueConfig(cfg::SwitchConfig& config, const HwAsic& asic) {
  if (asic.getSwitchType() != cfg::SwitchType::NPU ||
      !asic.isSupported(HwAsic::Feature::CPU_QUEUES)) {
    return;
  }
  config.cpuQueues() = {
      makeCpuQueue(
          asic,
          asic.getHiPriCpuQueueId(),
          "cpuQueue-high",
          kHighPriorityWeight,
          false),
      makeCpuQueue(
          asic,
          asic.getMidPriCpuQueueId(),
          "cpuQueue-mid",
          kMidPriorityWeight,
          false),
      makeCpuQueue(
          asic,
          kDefaultPriorityQueueId,
          "cpuQueue-default",
          kDefaultPriorityWeight,
          true),
      makeCpuQueue(
          asic, kLowPriorityQueueId, "cpuQueue-low", kLowPriorityWeight, true),
  };
}

void addDefaultCpuTrafficPolicyConfig(
    cfg::SwitchConfig& config,
    const HwAsic& asic,
    bool enableUserDefinedTrap) {
  if (asic.getSwitchType() != cfg::SwitchType::NPU ||
      !asic.isSupported(HwAsic::Feature::CPU_QUEUES)) {
    return;
  }

  const auto highQueueId = asic.getHiPriCpuQueueId();
  const auto midQueueId = asic.getMidPriCpuQueueId();
  std::vector<cfg::PacketRxReasonToQueue> rxReasons;
  if (asic.isSupported(HwAsic::Feature::NO_RX_REASON_TRAP)) {
    rxReasons = {
        makeRxReasonToQueue(cfg::PacketRxReason::TTL_1, kLowPriorityQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::DHCP, midQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::DHCPV6, midQueueId),
    };
  } else {
    rxReasons = {
        makeRxReasonToQueue(cfg::PacketRxReason::NDP, highQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::ARP, highQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::ARP_RESPONSE, highQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::BGP, highQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::BGPV6, highQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::LACP, highQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::LLDP, midQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::CPU_IS_NHOP, midQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::DHCP, midQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::DHCPV6, midQueueId),
        makeRxReasonToQueue(cfg::PacketRxReason::TTL_1, kLowPriorityQueueId),
    };
  }
  if (asic.isSupported(HwAsic::Feature::L3_MTU_ERROR_TRAP)) {
    rxReasons.push_back(makeRxReasonToQueue(
        cfg::PacketRxReason::L3_MTU_ERROR, kLowPriorityQueueId));
  } else if (asic.isSupported(HwAsic::Feature::PORT_MTU_ERROR_TRAP)) {
    rxReasons.push_back(makeRxReasonToQueue(
        cfg::PacketRxReason::PORT_MTU_ERROR, kLowPriorityQueueId));
  }
  if (!asic.isSupported(HwAsic::Feature::NO_RX_REASON_TRAP)) {
    // UNMATCHED is the fallback reason and must remain last in this ordered
    // list so it does not take priority over a more specific trap reason.
    rxReasons.push_back(makeRxReasonToQueue(
        cfg::PacketRxReason::UNMATCHED, kDefaultPriorityQueueId));
  }

  // Match CoppTestUtils' SAI behavior: when user-defined traps are enabled and
  // supported, setTc selects the CPU queue, so do not also program
  // sendToQueue. Legacy COOP-generated link-test and production configs
  // contain both actions.
  auto cpuAcls = genLinkLocalCoppAcls(
      asic,
      genCoppQueueAction(
          enableUserDefinedTrap, asic, highQueueId, getCpuActionType(&asic)),
      genCoppQueueAction(
          enableUserDefinedTrap, asic, midQueueId, getCpuActionType(&asic)));

  cfg::TrafficPolicyConfig trafficPolicy;
  for (auto& [acl, action] : cpuAcls) {
    cfg::MatchToAction matchToAction;
    matchToAction.matcher() = *acl.name();
    matchToAction.action() = std::move(action);
    trafficPolicy.matchToAction()->push_back(std::move(matchToAction));
    addAclEntryToDefaultAclTable(config, std::move(acl));
  }
  cfg::CPUTrafficPolicyConfig cpuPolicy;
  cpuPolicy.trafficPolicy() = std::move(trafficPolicy);
  cpuPolicy.rxReasonToQueueOrderedList() = std::move(rxReasons);
  config.cpuTrafficPolicy() = std::move(cpuPolicy);
}

} // namespace facebook::fboss::utility
