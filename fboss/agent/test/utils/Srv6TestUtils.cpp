// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/utils/Srv6TestUtils.h"
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/SwSwitchMySidUpdater.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/SwitchState.h"

#include <fmt/core.h>
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss::utility {

cfg::Srv6Tunnel makeSrv6TunnelConfig(
    const std::string& name,
    InterfaceID interfaceId) {
  cfg::Srv6Tunnel tunnel;
  tunnel.srv6TunnelId() = name;
  tunnel.underlayIntfID() = interfaceId;
  tunnel.tunnelType() = TunnelType::SRV6_ENCAP;
  tunnel.srcIp() = "2001:db8::1";
  tunnel.ttlMode() = cfg::TunnelMode::UNIFORM;
  tunnel.dscpMode() = cfg::TunnelMode::UNIFORM;
  tunnel.ecnMode() = cfg::TunnelMode::UNIFORM;
  return tunnel;
}

cfg::Srv6Tunnel makeSrv6DecapTunnelConfig(const std::string& name) {
  cfg::Srv6Tunnel tunnel;
  tunnel.srv6TunnelId() = name;
  tunnel.tunnelType() = TunnelType::SRV6_DECAP;
  tunnel.ttlMode() = cfg::TunnelMode::UNIFORM;
  tunnel.dscpMode() = cfg::TunnelMode::UNIFORM;
  // PIPE maps to SAI_TUNNEL_DECAP_ECN_MODE_COPY_FROM_OUTER (see
  // getSaiDecapEcnMode) — the RBB decap ECN behavior.
  tunnel.ecnMode() = cfg::TunnelMode::PIPE;
  return tunnel;
}

cfg::SwitchConfig srv6EcmpInitialConfig(const AgentEnsemble& ensemble) {
  auto cfg = utility::onePortPerInterfaceConfig(
      ensemble.getSw(),
      ensemble.masterLogicalPortIds(),
      true /*interfaceHasSubnet*/);
  cfg.srv6Tunnels() = {makeSrv6TunnelConfig(
      "srv6Tunnel0", InterfaceID(cfg.interfaces()[0].intfID().value()))};
  cfg.loadBalancers() = {
      getEcmpFullWithFlowLabelHashConfig(ensemble.getL3Asics())};
  return cfg;
}

void verifySrv6EcnMarking(
    AgentEnsemble* ensemble,
    PortID egressPort,
    const std::function<void()>& sendFloodPackets,
    const std::function<
        bool(const folly::IPAddressV6& dstAddr, uint8_t ecnBits)>&
        isEcnMarkedPacket) {
  auto* sw = ensemble->getSw();
  // Record baseline ECN counter
  auto beforeStats = ensemble->getLatestPortStats(egressPort);
  auto beforeEcnCounter = *beforeStats.outEcnCounter_();

  // Disable TX on egress port to build queue congestion
  setCreditWatchdogAndPortTx(ensemble, egressPort, false);

  // Flood ECN-capable packets (caller provides the packet construction)
  sendFloodPackets();

  // Create snooper to capture trapped packets
  SwSwitchPacketSnooper snooper(sw, "srv6EcnSnooper");
  snooper.ignoreUnclaimedRxPkts();

  // Re-enable TX — queued packets drain through pipeline
  setCreditWatchdogAndPortTx(ensemble, egressPort, true);

  // Verify ECN counter incremented
  WITH_RETRIES_N_TIMED(10, std::chrono::milliseconds(1000), {
    auto afterStats = ensemble->getLatestPortStats(egressPort);
    auto afterEcnCounter = *afterStats.outEcnCounter_();
    EXPECT_EVENTUALLY_GT(afterEcnCounter, beforeEcnCounter);
  });

  // Congestion CE-marks only a subset of the flooded packets, and the earliest
  // trapped copies predate the queue buildup, so drain every trapped copy
  // rather than inspecting however many a retry loop happens to reach.
  bool foundEcnMarked = false;
  constexpr int kMaxEcnPktsToInspect = 2000;
  for (int i = 0; i < kMaxEcnPktsToInspect && !foundEcnMarked; ++i) {
    auto frameRx = snooper.waitForPacket(1);
    if (!frameRx.has_value()) {
      break;
    }
    folly::io::Cursor cursor((*frameRx).get());
    EthFrame frame(cursor);
    auto v6Payload = frame.v6PayLoad();
    if (v6Payload.has_value()) {
      auto v6Hdr = v6Payload->header();
      if (isEcnMarkedPacket(v6Hdr.dstAddr, v6Hdr.trafficClass & 0x3)) {
        foundEcnMarked = true;
      }
    }
  }
  EXPECT_TRUE(foundEcnMarked);
}

std::string portNameForConfig(const cfg::SwitchConfig& cfg, PortID portId) {
  for (const auto& port : *cfg.ports()) {
    if (*port.logicalID() == static_cast<int32_t>(portId) &&
        port.name().has_value() && !port.name()->empty()) {
      return *port.name();
    }
  }
  throw FbossError("No name found for port ", portId);
}

cfg::MySidConfig makeAdjacencyMySidConfig(
    const std::string& portName,
    const std::string& locatorPrefix,
    int functionId) {
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = locatorPrefix;
  cfg::MySidEntryConfig entry;
  cfg::AdjacencyMySidConfig adjConfig;
  adjConfig.portName() = portName;
  adjConfig.isV6() = true;
  entry.adjacency() = adjConfig;
  mySidConfig.entries()[functionId] = entry;
  return mySidConfig;
}

cfg::MySidConfig makeDecapMySidConfig(
    const std::vector<std::string>& locatorPrefixes) {
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = locatorPrefixes.front();
  mySidConfig.locatorPrefixes() = locatorPrefixes;
  mySidConfig.entries()[kSrv6DecapFunctionId].decap() = cfg::DecapMySidConfig{};
  return mySidConfig;
}

void waitForMySidResolveOrUnresolve(
    const std::function<std::shared_ptr<SwitchState>()>& getState,
    const folly::IPAddressV6& sidPrefix,
    uint8_t prefixLen,
    bool resolved) {
  auto sidKey =
      folly::to<std::string>(sidPrefix.str(), "/", static_cast<int>(prefixLen));
  WITH_RETRIES({
    std::shared_ptr<MySid> mySid;
    for (const auto& [_, mySidMap] : std::as_const(*getState()->getMySids())) {
      if (auto node = mySidMap->getNodeIf(sidKey)) {
        mySid = node;
        break;
      }
    }
    ASSERT_EVENTUALLY_NE(mySid, nullptr) << "mysid not in switch state";
    EXPECT_EVENTUALLY_EQ(mySid->getResolvedNextHopsId().has_value(), resolved)
        << "mysid " << sidKey
        << (resolved ? " not resolved" : " still resolved")
        << " (adjacencyInterfaceId="
        << (mySid->getAdjacencyInterfaceId().has_value()
                ? std::to_string(*mySid->getAdjacencyInterfaceId())
                : "<unset>")
        << ")";
  });
}

void addDecapMySidEntry(
    SwSwitch* sw,
    const folly::IPAddressV6& addr,
    uint8_t prefixLen) {
  MySidEntry entry;
  entry.type() = MySidType::DECAPSULATE_AND_LOOKUP;
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() = facebook::network::toBinaryAddress(addr);
  prefix.prefixLength() = prefixLen;
  entry.mySid() = prefix;

  auto rib = sw->getRib();
  auto ribMySidToSwitchStateFunc =
      createRibMySidToSwitchStateFunction(std::nullopt);
  rib->update(
      sw->getScopeResolver(),
      {entry},
      {},
      "addDecapMySidEntry",
      ribMySidToSwitchStateFunc,
      sw);
}

namespace {
// SID address for a scale MySID entry: 3001:db8:<index + sidOffset>::.
folly::IPAddressV6 makeScaleMySidAddr(int index, int sidOffset) {
  return folly::IPAddressV6(fmt::format("3001:db8:{:x}::", index + sidOffset));
}
constexpr uint8_t kScaleMySidPrefixLen = 48;
} // namespace

std::vector<MySidWithNextHops> makeAdjacencyMySidEntries(
    const EcmpSetupAnyNPorts6& ecmpHelper,
    int numNhops,
    int numEntries,
    int sidOffset) {
  std::vector<MySidWithNextHops> entries;
  entries.reserve(numEntries);
  for (int i = 0; i < numEntries; ++i) {
    auto ecmpNhop = ecmpHelper.nhop(i % numNhops);
    state::MySidFields fields;
    fields.type() = MySidType::ADJACENCY_MICRO_SID;
    fields.adjacencyInterfaceId() = static_cast<int32_t>(ecmpNhop.intf);
    // EcmpSetupAnyNPorts6 always yields IPv6 next hops.
    fields.isV6() = true;
    fields.clientId() = ClientID::STATIC_ROUTE;
    facebook::network::thrift::IPPrefix prefix;
    prefix.prefixAddress() =
        facebook::network::toBinaryAddress(makeScaleMySidAddr(i, sidOffset));
    prefix.prefixLength() = kScaleMySidPrefixLen;
    fields.mySid() = prefix;
    auto mySid = std::make_shared<MySid>(fields);
    RouteNextHopSet nhops{ResolvedNextHop(
        folly::IPAddress(ecmpNhop.ip), ecmpNhop.intf, ECMP_WEIGHT)};
    entries.push_back(
        {std::move(mySid), std::move(nhops), std::nullopt /* nhgName */});
  }
  return entries;
}

void programMySidEntries(SwSwitch* sw, std::vector<MySidWithNextHops> entries) {
  auto rib = sw->getRib();
  auto ribMySidFunc = createRibMySidToSwitchStateFunction(std::nullopt);
  rib->update(
      sw->getScopeResolver(),
      std::move(entries),
      {} /* toUnresolveIfMatch */,
      {} /* toDelete */,
      "addMySidEntries",
      ribMySidFunc,
      sw);
}

void deleteScaleMySidEntries(SwSwitch* sw, int numEntries, int sidOffset) {
  auto rib = sw->getRib();
  auto ribMySidFunc = createRibMySidToSwitchStateFunction(std::nullopt);
  for (int i = 0; i < numEntries; ++i) {
    IpPrefix prefix;
    prefix.ip() =
        facebook::network::toBinaryAddress(makeScaleMySidAddr(i, sidOffset));
    prefix.prefixLength() = kScaleMySidPrefixLen;
    rib->update(
        sw->getScopeResolver(),
        std::vector<MySidEntry>{} /* toAdd */,
        {prefix},
        "deleteMySidEntry",
        ribMySidFunc,
        sw);
  }
}

void addBindingSidEntry(
    SwSwitch* sw,
    const folly::IPAddressV6& mySidAddr,
    uint8_t prefixLen,
    const std::vector<NextHopThrift>& nhops) {
  ThriftHandler handler(sw);
  auto entries = std::make_unique<std::vector<MySidEntry>>();
  MySidEntry bindingEntry;
  bindingEntry.type() = MySidType::BINDING_MICRO_SID;
  facebook::network::thrift::IPPrefix mySidPrefix;
  mySidPrefix.prefixAddress() = facebook::network::toBinaryAddress(mySidAddr);
  mySidPrefix.prefixLength() = prefixLen;
  bindingEntry.mySid() = mySidPrefix;
  bindingEntry.nextHops() = nhops;
  entries->push_back(bindingEntry);
  handler.addMySidEntries(std::move(entries));
}

NextHopThrift makeSrv6NextHopThrift(
    const folly::IPAddressV6& nhopAddr,
    const folly::IPAddressV6& sid,
    const std::string& tunnelId) {
  NextHopThrift nhop;
  nhop.address() = facebook::network::toBinaryAddress(nhopAddr);
  nhop.srv6SegmentList() = {facebook::network::toBinaryAddress(sid)};
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop.tunnelId() = tunnelId;
  return nhop;
}

} // namespace facebook::fboss::utility
