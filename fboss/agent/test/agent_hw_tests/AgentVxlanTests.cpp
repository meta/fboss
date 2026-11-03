// Copyright (c) Meta Platforms, Inc. and affiliates.

// VXLAN encap/decap hardware tests (IPv6 only, default VRF).
//
// Counter tests check egress port stats: on a standalone switch
// the forwarded frame leaves with the underlay neighbor MAC, so
// a snooper never sees it. Packet-level checks live in
// AgentVxlanSnoopTest, which mirrors egress to a snooper.

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/MirrorTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/VxlanTestUtils.h"

namespace facebook::fboss {

class AgentVxlanTest : public AgentHwTest {
 protected:
  static constexpr uint32_t kVni{10001};
  static inline const std::string kTunnelId{"vxlantunnel0"};
  static inline const folly::MacAddress kRemoteMac{"02:00:00:00:00:64"};
  static inline const folly::MacAddress kHostMac{"02:01:02:03:04:05"};
  static constexpr int64_t kVxlanOverhead = 70;

  cfg::SwitchConfig interfaceConfig(const AgentEnsemble& ensemble) const {
    return utility::onePortPerInterfaceConfig(
        ensemble.getSw(), ensemble.masterLogicalPortIds(), true);
  }

  cfg::VxLanTunnel tunnelConfig(
      const cfg::SwitchConfig& cfg,
      const folly::IPAddressV6& localVtep,
      const folly::IPAddressV6& remoteVtep) const {
    auto tunnel = utility::makeVxlanTunnelConfig(
        kTunnelId,
        InterfaceID(cfg.interfaces()[0].intfID().value()),
        localVtep,
        remoteVtep);
    tunnel.ttlMode() = cfg::TunnelMode::UNIFORM;
    tunnel.dscpMode() = cfg::TunnelMode::UNIFORM;
    tunnel.ecnMode() = cfg::TunnelMode::UNIFORM;
    return tunnel;
  }

  void addEncapMapping(cfg::VxLanTunnel& tunnel) const {
    tunnel.vrf2Vni() = std::map<int64_t, int64_t>{{0, kVni}};
  }

  void addDecapMapping(cfg::VxLanTunnel& tunnel) const {
    tunnel.vniToVrf() = std::map<int64_t, int64_t>{{kVni, 0}};
  }

  cfg::SwitchConfig vxlanConfig(
      const AgentEnsemble& ensemble,
      const folly::IPAddressV6& localVtep,
      const folly::IPAddressV6& remoteVtep) const {
    auto cfg = interfaceConfig(ensemble);
    auto tunnel = tunnelConfig(cfg, localVtep, remoteVtep);
    addEncapMapping(tunnel);
    // BRCM builds its VNI -> VR table only from decap entries,
    // and encap next hop creation looks the VNI up in it.
    addDecapMapping(tunnel);
    cfg.vxlanTunnels() = {tunnel};
    return cfg;
  }

  PortID logicalPort(size_t idx) const {
    return PortID(this->masterLogicalPortIds()[idx]);
  }

  folly::MacAddress intfMac() {
    return getMacForFirstInterfaceWithPortsForTesting(
        this->getProgrammedState());
  }

  utility::EcmpSetupAnyNPorts<folly::IPAddressV6> makeEcmpHelper() {
    return utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        this->getProgrammedState(), this->getSw()->needL2EntryForNeighbor());
  }

  auto resolvedNhops(int count, bool useLinkLocal = false) {
    auto helper = makeEcmpHelper();
    // The underlay routes below use the subnet address itself, so
    // that neighbor must be resolved, not just the link-local.
    this->resolveNeighbors(helper, count, useLinkLocal);
    return helper;
  }

  void addStaticRoute(
      const folly::IPAddress& prefix,
      uint8_t prefixLength,
      const RouteNextHopSet& nextHops) {
    auto routeUpdater = this->getSw()->getRouteUpdater();
    routeUpdater.addRoute(
        RouterID(0),
        prefix,
        prefixLength,
        ClientID::STATIC_ROUTE,
        RouteNextHopEntry(nextHops, AdminDistance::STATIC_ROUTE));
    routeUpdater.program();
  }

  void removeStaticRoute(const folly::IPAddress& prefix, uint8_t prefixLength) {
    auto routeUpdater = this->getSw()->getRouteUpdater();
    routeUpdater.delRoute(
        RouterID(0), prefix, prefixLength, ClientID::STATIC_ROUTE);
    routeUpdater.program();
  }

  void addUnderlayRoute(
      const folly::IPAddressV6& vtep,
      const RouteNextHopSet& nextHops) {
    addStaticRoute(folly::IPAddress(vtep), 128, nextHops);
  }

  void addOverlayRoute(
      const folly::IPAddressV6& prefix,
      const RouteNextHopSet& nextHops) {
    addStaticRoute(folly::IPAddress(prefix), 64, nextHops);
  }

  // One route with a plain next hop on each listed port.
  void addEcmpRoute(
      const folly::IPAddress& prefix,
      uint8_t prefixLength,
      const std::vector<int>& memberIdx) {
    auto nhops = resolvedNhops(4);
    RouteNextHopSet nextHops;
    for (int idx : memberIdx) {
      auto nhop = nhops.nhop(idx);
      nextHops.insert(plainNextHop(folly::IPAddress(nhop.ip), nhop.intf));
    }
    addStaticRoute(prefix, prefixLength, nextHops);
  }

  ResolvedNextHop plainNextHop(const folly::IPAddress& addr, InterfaceID intf)
      const {
    return ResolvedNextHop(addr, intf, ECMP_WEIGHT);
  }

  ResolvedNextHop vxlanNextHop(
      const folly::IPAddressV6& vtep,
      InterfaceID intf,
      const folly::MacAddress& overlayDstMac) const {
    return ResolvedNextHop(
        folly::IPAddress(vtep),
        intf,
        ECMP_WEIGHT,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        {},
        TunnelType::VXLAN,
        kTunnelId,
        std::nullopt,
        NextHopRole::PRIMARY,
        std::nullopt,
        overlayDstMac);
  }

  // One overlay next hop to the VTEP, resolved by one underlay
  // next hop. The underlay route comes first: the SDK resolves
  // the remote VTEP through it at next hop creation.
  void addVxlanRoute(
      int nhopIdx,
      const folly::IPAddressV6& vtep,
      const folly::IPAddressV6& overlayPrefix,
      const folly::MacAddress& overlayDstMac) {
    auto nhops = resolvedNhops(nhopIdx + 1);
    auto nhop = nhops.nhop(nhopIdx);
    addUnderlayRoute(
        vtep, {plainNextHop(folly::IPAddress(nhop.ip), nhop.intf)});
    addOverlayRoute(
        overlayPrefix, {vxlanNextHop(vtep, nhop.intf, overlayDstMac)});
  }

  // The decap tunnel term destination must be a switch-owned
  // IP or the ASIC does not terminate on it.
  static inline const folly::IPAddressV6 kDecapLocalVtep{"1::"};
  static inline const folly::IPAddressV6 kDecapRemoteVtep{"11::2"};
  static inline const folly::IPAddressV6 kDecapInnerSrcIp{"100::2"};
  static inline const folly::IPAddressV6 kDecapInnerDstIp{"100::1"};

  cfg::SwitchConfig decapConfig(const AgentEnsemble& ensemble) const {
    return vxlanConfig(ensemble, kDecapLocalVtep, kDecapRemoteVtep);
  }

  static inline const folly::IPAddressV6 kDecapOverlayPrefix{"101::"};

  // Route the decapped inner destination out the second port.
  void addDecapRoute() {
    auto nhops = resolvedNhops(2, true /*useLinkLocal*/);
    auto nhop = nhops.nhop(1);
    CHECK(nhop.linkLocalNhopIp.has_value());
    addStaticRoute(
        folly::IPAddress(kDecapInnerDstIp),
        128,
        {plainNextHop(folly::IPAddress(*nhop.linkLocalNhopIp), nhop.intf)});
  }

  // The tunnel DMAC is written as the inner DMAC of an
  // encapped packet, and a terminated packet is routed only
  // when its inner DMAC is the my-station MAC (the switch
  // base MAC), so all tests program this as the tunnel DMAC.
  folly::MacAddress stationMac() {
    auto switchId = this->getAgentEnsemble()
                        ->scopeResolver()
                        .scope(logicalPort(0))
                        .switchId();
    return this->getSw()->getLocalMac(switchId);
  }

  // Without a load balancer config no SAI hash object is
  // created, and neither ECMP level spreads these flows.
  void programHashObject(cfg::SwitchConfig& cfg) {
    auto ensemble = this->getAgentEnsemble();
    auto asic = checkSameAndGetAsicForTesting(ensemble->getL3Asics());
    cfg.loadBalancers()->push_back(utility::getEcmpFullHashConfig({asic}));
  }

  void applyHashConfig() {
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    programHashObject(cfg);
    this->applyNewConfig(cfg);
  }

  std::unique_ptr<TxPacket> makeInnerPacket(
      const folly::IPAddressV6& srcIp,
      const folly::IPAddressV6& dstIp,
      uint16_t srcPort,
      uint16_t dstPort,
      uint8_t trafficClass = 0,
      uint8_t hopLimit = 64) {
    return utility::makeUDPTxPacket(
        this->getSw(),
        this->getVlanIDForTx(),
        kHostMac,
        intfMac(),
        folly::IPAddress(srcIp),
        folly::IPAddress(dstIp),
        srcPort,
        dstPort,
        trafficClass,
        hopLimit);
  }

  int64_t innerPacketLength(const folly::IPAddressV6& dstIp) {
    auto packet =
        makeInnerPacket(folly::IPAddressV6("100::10"), dstIp, 8000, 8001);
    return static_cast<int64_t>(packet->buf()->computeChainDataLength());
  }

  // Send numFlows flows to dst and return the per-port egress
  // packet deltas once every flow has egressed.
  std::map<PortID, int64_t> countEgressPackets(
      const folly::IPAddressV6& dst,
      int numFlows,
      const std::vector<PortID>& ports,
      std::optional<size_t> ingressIdx = std::nullopt,
      std::map<PortID, int64_t>* egressBytes = nullptr) {
    std::map<PortID, HwPortStats> before;
    for (const auto& port : ports) {
      before[port] = getLatestPortStats(port);
    }
    for (int i = 0; i < numFlows; ++i) {
      auto packet = makeInnerPacket(
          folly::IPAddressV6("100::" + std::to_string(10 + i)),
          dst,
          static_cast<uint16_t>(8000 + i),
          8001);
      if (ingressIdx.has_value()) {
        this->getAgentEnsemble()->ensureSendPacketOutOfPort(
            std::move(packet), logicalPort(*ingressIdx));
      } else {
        this->sendPacketSwitchedAsync(std::move(packet));
      }
    }
    std::map<PortID, int64_t> packets;
    WITH_RETRIES({
      int64_t total = 0;
      for (const auto& port : ports) {
        auto after = getLatestPortStats(port);
        packets[port] =
            *after.outUnicastPkts_() - *before[port].outUnicastPkts_();
        total += packets[port];
      }
      EXPECT_EVENTUALLY_EQ(total, numFlows);
    });
    if (egressBytes) {
      for (const auto& port : ports) {
        auto after = getLatestPortStats(port);
        (*egressBytes)[port] = *after.outBytes_() - *before[port].outBytes_();
      }
    }
    return packets;
  }

  PortID mirrorPort() const {
    return logicalPort(2);
  }

  // Mirrored copies loop back into the mirror port and are
  // trapped to the CPU: the only way to observe an egressed
  // frame on a standalone switch.
  void applyEgressMirror(
      cfg::SwitchConfig cfg,
      const std::vector<PortID>& egressPorts) {
    auto ensemble = this->getAgentEnsemble();
    utility::addMirrorConfig<folly::IPAddressV6>(
        &cfg,
        *ensemble,
        utility::kEgressSpan,
        false /*truncate*/,
        utility::kDscpDefault,
        2 /*mirrorToPortIndex*/);
    for (auto port : egressPorts) {
      utility::findCfgPort(cfg, port)->egressMirror() = utility::kEgressSpan;
    }
    auto asic = checkSameAndGetAsicForTesting(ensemble->getL3Asics());
    utility::configureTrapAcl(asic, cfg, mirrorPort());
    this->applyNewConfig(cfg);
  }
};

class AgentVxlanEncapTest : public AgentVxlanTest {
 protected:
  static inline const folly::IPAddressV6 kLocalVtep{"10::1"};
  static inline const folly::IPAddressV6 kRemoteVtep{"10::2"};
  static inline const folly::IPAddressV6 kOverlayPrefix{"100::"};
  static inline const folly::IPAddressV6 kOverlayDst{"100::1"};
  static inline const folly::IPAddressV6 kRemoteVtep2{"10::3"};
  static inline const folly::IPAddressV6 kOverlayPrefix2{"101::"};
  static inline const folly::IPAddressV6 kOverlayDst2{"101::1"};
  static inline const folly::IPAddressV6 kOverlayPrefixEcmp{"104::"};
  static inline const folly::IPAddressV6 kOverlayDstEcmp{"104::1"};
  static inline const folly::IPAddressV6 kRemoteVtep3{"10::4"};
  static inline const folly::IPAddressV6 kOverlayPrefix3{"102::"};
  static inline const folly::IPAddressV6 kOverlayDst3{"102::1"};
  static inline const folly::IPAddressV6 kRemoteVtep4{"10::5"};
  static inline const folly::IPAddressV6 kOverlayPrefix4{"103::"};
  static inline const folly::IPAddressV6 kOverlayDst4{"103::1"};
  static inline const folly::IPAddressV6 kReresolutionVtep{"10::1:0:0:2"};
  static inline const folly::IPAddressV6 kLessSpecificPrefix{"10::"};
  static inline const folly::IPAddressV6 kMoreSpecificPrefix{"10::1:0:0:0"};

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::VXLAN_ENCAP};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    // Managed VXLAN next hops resolve through next hop IDs.
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_resolve_nexthops_from_id = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return vxlanConfig(ensemble, kLocalVtep, kRemoteVtep);
  }

  void setupVxlan() {
    addVxlanRoute(0, kRemoteVtep, kOverlayPrefix, stationMac());
  }

  void setupTwoVteps() {
    setupVxlan();
    addVxlanRoute(1, kRemoteVtep2, kOverlayPrefix2, stationMac());
  }

  void verifyEncapOnPort(
      const folly::IPAddressV6& overlayDst,
      PortID expectPort,
      const std::vector<PortID>& otherPorts) {
    auto txPacket =
        makeInnerPacket(folly::IPAddressV6("100::10"), overlayDst, 8000, 8001);
    auto innerLen =
        static_cast<int64_t>(txPacket->buf()->computeChainDataLength());
    auto expectBefore = getLatestPortStats(expectPort);
    std::map<PortID, HwPortStats> othersBefore;
    for (const auto& otherPort : otherPorts) {
      othersBefore[otherPort] = getLatestPortStats(otherPort);
    }
    this->sendPacketSwitchedAsync(std::move(txPacket));
    WITH_RETRIES({
      auto statsNow = getLatestPortStats(expectPort);
      EXPECT_EVENTUALLY_TRUE(*statsNow.outBytes_() > *expectBefore.outBytes_());
    });
    auto expectAfter = getNextUpdatedPortStats(expectPort);
    EXPECT_EQ(
        *expectAfter.outUnicastPkts_() - *expectBefore.outUnicastPkts_(), 1);
    EXPECT_EQ(
        *expectAfter.outBytes_() - *expectBefore.outBytes_(),
        innerLen + kVxlanOverhead);
    EXPECT_EQ(
        *expectAfter.inBytes_() - *expectBefore.inBytes_(),
        innerLen + kVxlanOverhead);
    for (const auto& otherPort : otherPorts) {
      auto otherAfter = getLatestPortStats(otherPort);
      EXPECT_EQ(*otherAfter.outBytes_(), *othersBefore[otherPort].outBytes_());
      EXPECT_EQ(
          *otherAfter.outUnicastPkts_(),
          *othersBefore[otherPort].outUnicastPkts_());
    }
  }

  void verifyTwoVteps() {
    verifyEncapOnPort(kOverlayDst, logicalPort(0), {logicalPort(1)});
    verifyEncapOnPort(kOverlayDst2, logicalPort(1), {logicalPort(0)});
  }

  void setupFourVteps() {
    setupTwoVteps();
    addVxlanRoute(2, kRemoteVtep3, kOverlayPrefix3, stationMac());
    addVxlanRoute(3, kRemoteVtep4, kOverlayPrefix4, stationMac());
  }

  std::vector<PortID> firstNLogicalPorts(size_t n) const {
    std::vector<PortID> ports;
    ports.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      ports.push_back(logicalPort(i));
    }
    return ports;
  }

  void verifyFourVteps() {
    auto ports = firstNLogicalPorts(4);
    const std::vector<folly::IPAddressV6> dsts{
        kOverlayDst, kOverlayDst2, kOverlayDst3, kOverlayDst4};
    for (int i = 0; i < 4; ++i) {
      std::vector<PortID> others;
      for (int j = 0; j < 4; ++j) {
        if (j != i) {
          others.push_back(ports[j]);
        }
      }
      verifyEncapOnPort(dsts[i], ports[i], others);
    }
  }

  void setupOverlayEcmp() {
    setupFourVteps();
    auto nhops = resolvedNhops(4);
    addOverlayRoute(
        kOverlayPrefixEcmp,
        {vxlanNextHop(kRemoteVtep, nhops.nhop(0).intf, stationMac()),
         vxlanNextHop(kRemoteVtep2, nhops.nhop(1).intf, stationMac()),
         vxlanNextHop(kRemoteVtep3, nhops.nhop(2).intf, stationMac()),
         vxlanNextHop(kRemoteVtep4, nhops.nhop(3).intf, stationMac())});
  }

  void verifyOverlayEcmp() {
    // Prove each member on its own first, so a balancing
    // failure cannot be a misconfigured member.
    verifyFourVteps();
    auto ports = firstNLogicalPorts(4);
    // Inject on a port outside the ECMP member set, so the
    // member counters see only encapped egress.
    std::map<PortID, int64_t> egressBytes;
    auto packets =
        countEgressPackets(kOverlayDstEcmp, 64, ports, 4, &egressBytes);
    int64_t encappedLen = innerPacketLength(kOverlayDstEcmp) + kVxlanOverhead;
    for (const auto& port : ports) {
      EXPECT_GT(packets[port], 0);
      EXPECT_EQ(egressBytes[port], packets[port] * encappedLen);
    }
  }

  void setupUnderlayEcmp() {
    applyHashConfig();
    addEcmpRoute(folly::IPAddress(kRemoteVtep), 128, {0, 1, 2, 3});
    auto nhops = resolvedNhops(1);
    addOverlayRoute(
        kOverlayPrefix,
        {vxlanNextHop(kRemoteVtep, nhops.nhop(0).intf, stationMac())});
  }

  void verifyUnderlayEcmp() {
    auto ports = firstNLogicalPorts(4);
    auto packets = countEgressPackets(kOverlayDst, 64, ports);
    for (const auto& port : ports) {
      EXPECT_GT(packets[port], 0);
    }
  }

  void setupUnderlayReresolution() {
    applyHashConfig();
    addEcmpRoute(folly::IPAddress(kLessSpecificPrefix), 64, {0, 1});
    // The overlay next hop is created after the less specific
    // underlay route, so the SDK resolves the VTEP through it.
    auto nhops = resolvedNhops(1);
    addOverlayRoute(
        kOverlayPrefix,
        {vxlanNextHop(kReresolutionVtep, nhops.nhop(0).intf, stationMac())});
  }

  void verifyUnderlayLb(const std::set<int>& memberIdx) {
    auto ports = firstNLogicalPorts(4);
    auto packets = countEgressPackets(kOverlayDst, 64, ports);
    for (int i = 0; i < 4; ++i) {
      if (memberIdx.count(i)) {
        EXPECT_GT(packets[ports[i]], 0)
            << "expected underlay egress on port " << ports[i];
      } else {
        EXPECT_EQ(packets[ports[i]], 0)
            << "unexpected underlay egress on port " << ports[i];
      }
    }
  }

  void verifyUnderlayReresolution() {
    // Resolved through the less specific /64: ports 0,1.
    verifyUnderlayLb({0, 1});
    addEcmpRoute(folly::IPAddress(kMoreSpecificPrefix), 96, {2, 3});
    // The more specific /96 takes over the underlay resolution.
    verifyUnderlayLb({2, 3});
    removeStaticRoute(folly::IPAddress(kMoreSpecificPrefix), 96);
    // Removing it falls back to the /64 resolution.
    verifyUnderlayLb({0, 1});
  }
};

// host - local-vtep - tunnel - remote-vtep-a - host-a (port0)
//                           \- remote-vtep-b - host-b (port1)
TEST_F(AgentVxlanEncapTest, encapOnCorrectEgressPortPerVtep) {
  auto setup = [this]() { setupTwoVteps(); };
  auto verify = [this]() { verifyTwoVteps(); };
  verifyAcrossWarmBoots(setup, verify);
}

// host - local-vtep - tunnel - remote-vteps - hosts (ports 0-3)
TEST_F(AgentVxlanEncapTest, egressOverlayEcmp) {
  auto setup = [this]() {
    applyHashConfig();
    setupOverlayEcmp();
  };
  auto verify = [this]() { verifyOverlayEcmp(); };
  verifyAcrossWarmBoots(setup, verify);
}

// host - local-vtep - tunnel (4 underlay paths) - remote-vtep - host
TEST_F(AgentVxlanEncapTest, encapUnderlayEcmp) {
  auto setup = [this]() { setupUnderlayEcmp(); };
  auto verify = [this]() { verifyUnderlayEcmp(); };
  verifyAcrossWarmBoots(setup, verify);
}

// host - local-vtep - tunnel - remote-vtep - host
TEST_F(AgentVxlanEncapTest, encapUnderlayReresolution) {
  auto setup = [this]() { setupUnderlayReresolution(); };
  auto verify = [this]() { verifyUnderlayReresolution(); };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
