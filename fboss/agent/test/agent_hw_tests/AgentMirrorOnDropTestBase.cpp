/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fmt/format.h>

#include "fboss/agent/test/agent_hw_tests/AgentMirrorOnDropTestBase.h"

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/PfcTestUtils.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"

namespace facebook::fboss {

cfg::SwitchConfig AgentMirrorOnDropTestBase::initialConfig(
    const AgentEnsemble& ensemble) const {
  auto config = utility::onePortPerInterfaceConfig(
      ensemble.getSw(),
      ensemble.masterLogicalPortIds(),
      true /*interfaceHasSubnet*/);
  // To allow infinite traffic loops.
  utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), config);
  return config;
}

std::vector<ProductionFeature>
AgentMirrorOnDropTestBase::getProductionFeaturesVerified() const {
  return {ProductionFeature::MIRROR_ON_DROP};
}

std::string AgentMirrorOnDropTestBase::portDesc(const PortID& portId) const {
  const auto& cfg = getAgentEnsemble()->getCurrentConfig();
  for (const auto& port : *cfg.ports()) {
    if (PortID(*port.logicalID()) == portId) {
      return fmt::format("portId={} name={}", *port.logicalID(), *port.name());
    }
  }
  return "";
}

void AgentMirrorOnDropTestBase::setupEcmpTraffic(
    const PortID& portId,
    const folly::IPAddressV6& addr,
    const folly::MacAddress& nextHopMac,
    bool disableTtlDecrement) {
  utility::EcmpSetupTargetedPorts6 ecmpHelper{
      getProgrammedState(), getSw()->needL2EntryForNeighbor(), nextHopMac};
  const PortDescriptor port{portId};
  RoutePrefixV6 route{addr, 128};
  applyNewState([&](const std::shared_ptr<SwitchState>& state) {
    return ecmpHelper.resolveNextHops(state, {port});
  });
  auto routeUpdater = getSw()->getRouteUpdater();
  ecmpHelper.programRoutes(
      &routeUpdater,
      {port},
      {std::move(route)},
      {},
      std::nullopt,
      disableTtlDecrement ? std::make_optional(true) : std::nullopt);
  if (disableTtlDecrement) {
    for (auto& nhop : ecmpHelper.getNextHops()) {
      utility::disablePortTTLDecrementIfSupported(
          getAgentEnsemble(), ecmpHelper.getRouterId(), nhop);
    }
  }
}

void AgentMirrorOnDropTestBase::unprogramEcmpTraffic(
    const folly::IPAddressV6& addr) {
  utility::EcmpSetupTargetedPorts6 ecmpHelper{
      getProgrammedState(),
      getSw()->needL2EntryForNeighbor(),
      kCollectorNextHopMac_};
  auto routeUpdater = getSw()->getRouteUpdater();
  ecmpHelper.unprogramRoutes(&routeUpdater, {RoutePrefixV6{addr, 128}});
}

void AgentMirrorOnDropTestBase::unresolveEcmpTraffic(const PortID& portId) {
  utility::EcmpSetupTargetedPorts6 ecmpHelper{
      getProgrammedState(), getSw()->needL2EntryForNeighbor()};
  applyNewState([&](const std::shared_ptr<SwitchState>& state) {
    return ecmpHelper.unresolveNextHops(state, {PortDescriptor(portId)});
  });
}

void AgentMirrorOnDropTestBase::addDropPacketAcl(
    cfg::SwitchConfig* config,
    const PortID& portId) {
  auto* acl = utility::addAcl_DEPRECATED(
      config, fmt::format("drop-packet-{}", portId), cfg::AclActionType::DENY);
  acl->etherType() = cfg::EtherType::IPv6;
  acl->srcPort() = portId;
}

void AgentMirrorOnDropTestBase::configureMmuDropBuffers(
    cfg::SwitchConfig& config,
    const PortID& injectionPortId,
    int priority) {
  const auto* hwAsic =
      checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
  utility::setupPfcBuffers(
      getAgentEnsemble(),
      config,
      {injectionPortId},
      {},
      {priority},
      utility::PfcBufferParams::getPfcBufferParams(
          hwAsic->getAsicType(), kDefaultGlobalSharedBytes));
}

void AgentMirrorOnDropTestBase::applyMirrorOnDropConfig(
    cfg::SwitchConfig& config,
    cfg::MirrorOnDropReport report,
    const PortID& collectorPort) {
  config.mirrorOnDropReports()->push_back(std::move(report));
  auto* asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
  utility::addTrapPacketAcl(
      asic,
      &config,
      folly::CIDRNetwork{folly::IPAddress{kCollectorIp_}, 128},
      cfg::ToCpuAction::TRAP);
  applyNewConfig(config);
  setupEcmpTraffic(collectorPort, kCollectorIp_, kCollectorNextHopMac_);
  waitForStateUpdates(getSw());
}

void AgentMirrorOnDropTestBase::resetMirrorOnDropFlows() {
  auto config = getAgentEnsemble()->getCurrentConfig();
  auto reports = *config.mirrorOnDropReports();
  config.mirrorOnDropReports()->clear();
  applyNewConfig(config);
  *config.mirrorOnDropReports() = std::move(reports);
  applyNewConfig(config);
  waitForStateUpdates(getSw());
}

std::unique_ptr<TxPacket> AgentMirrorOnDropTestBase::makePacket(
    const folly::IPAddressV6& dstIp,
    int priority,
    size_t payloadSize) {
  // Create a structured payload pattern.
  std::vector<uint8_t> payload(payloadSize, 0);
  for (size_t i = 0; i < payload.size() / 2; ++i) {
    payload[i * 2] = i & 0xff;
    payload[i * 2 + 1] = (i >> 8) & 0xff;
  }

  return utility::makeUDPTxPacket(
      getSw(),
      getVlanIDForTx(),
      utility::kLocalCpuMac(),
      getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()),
      kPacketSrcIp_,
      dstIp,
      kPacketSrcPort,
      kPacketDstPort,
      (priority * 8) << 2, // dscp is the 6 MSBs in TC
      255,
      std::move(payload));
}

std::unique_ptr<TxPacket> AgentMirrorOnDropTestBase::sendPackets(
    int count,
    std::optional<PortID> portId,
    const folly::IPAddressV6& dstIp,
    int priority,
    size_t payloadSize) {
  // Build a reference packet for the caller to compare against the captured
  // MoD payload. The reference is never sent — each loop iteration builds a
  // fresh packet because sendPacketAsync takes ownership.
  auto refPacket = makePacket(dstIp, priority, payloadSize);
  XLOG(DBG3) << "Sending " << count << " packets:\n"
             << PktUtil::hexDump(refPacket->buf());
  for (int i = 0; i < count; ++i) {
    if (portId.has_value()) {
      getAgentEnsemble()->sendPacketAsync(
          makePacket(dstIp, priority, payloadSize),
          PortDescriptor(*portId),
          std::nullopt);
    } else {
      getAgentEnsemble()->sendPacketAsync(
          makePacket(dstIp, priority, payloadSize), std::nullopt, std::nullopt);
    }
  }
  return refPacket;
}

SystemPortID AgentMirrorOnDropTestBase::systemPortId(const PortID& portId) {
  return getSystemPortID(
      portId, getProgrammedState(), scopeResolver().scope(portId).switchId());
}

} // namespace facebook::fboss
