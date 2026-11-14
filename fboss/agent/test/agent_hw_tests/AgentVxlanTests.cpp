// Copyright (c) Meta Platforms, Inc. and affiliates.

// VXLAN encap/decap hardware tests (IPv6 only). The overlay
// is in a non-default VRF; the underlay is in the default VRF.
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
  // Overlay VRFs, each mapped 1:1 to a VNI on the tunnel. The
  // underlay stays in the default VRF.
  static inline const RouterID kOverlayVrf{1001};
  static constexpr uint32_t kVni{10001};
  static inline const RouterID kOverlayVrf2{1002};
  static constexpr uint32_t kVni2{10002};
  // BRCM sets its default VNI from the default VR's decap map
  // entry and uses it when a tunnel next hop carries no VNI, so
  // the default VR needs an entry even though no overlay route
  // uses it.
  static constexpr uint32_t kDefaultVni{0xFFFF};
  static inline const std::string kTunnelId{"vxlantunnel0"};
  static inline const folly::MacAddress kRemoteMac{"02:00:00:00:00:64"};
  static inline const folly::MacAddress kHostMac{"02:01:02:03:04:05"};
  static constexpr int64_t kVxlanOverhead = 70;
  static inline const folly::IPAddressV6 kLocalVtep{"10::1"};
  static inline const folly::IPAddressV6 kRemoteVtep{"10::2"};
  static inline const folly::IPAddressV6 kOverlayPrefix{"100::"};
  static inline const folly::IPAddressV6 kOverlayDst{"100::1"};

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

  // Overlay VRFs and the indexes of the logical ports in them
  using VrfPorts = std::map<RouterID, std::vector<size_t>>;

  // Decapped traffic egresses on ports 1-4 in the first overlay
  // VRF and on port 5 in the second. Port 0 stays in the
  // default VRF as the VXLAN ingress.
  static inline const VrfPorts kDecapVrfPorts{
      {kOverlayVrf, {1, 2, 3, 4}},
      {kOverlayVrf2, {5}}};

  // Add the VRFs to config. bindVrfPorts() moves their ports' interfaces.
  void addVrfPorts(
      cfg::SwitchConfig& cfg,
      const AgentEnsemble& ensemble,
      const VrfPorts& vrfPorts) const {
    auto portIds = ensemble.masterLogicalPortIds();
    for (const auto& [vrf, portIdxs] : vrfPorts) {
      std::vector<PortID> ports;
      ports.reserve(portIdxs.size());
      for (auto idx : portIdxs) {
        ports.push_back(portIds[idx]);
      }
      utility::addVrfForPorts(cfg, vrf, ports);
    }
  }

  // Move the interfaces of the ports into their VRFs. Done in cold boot
  // setup only: the bindings persist across warm boot.
  void bindVrfPorts(const VrfPorts& vrfPorts) {
    std::map<InterfaceID, RouterID> interfaceVrfs;
    for (const auto& [vrf, portIdxs] : vrfPorts) {
      for (const auto& port : logicalPorts(portIdxs)) {
        interfaceVrfs.emplace(
            getProgrammedState()->getInterfaceIDForPort(PortDescriptor(port)),
            vrf);
      }
    }
    getSw()->updateInterfaceVrfs(interfaceVrfs);
  }

  std::vector<PortID> logicalPorts(const std::vector<size_t>& idxs) const {
    std::vector<PortID> ports;
    ports.reserve(idxs.size());
    for (auto idx : idxs) {
      ports.push_back(logicalPort(idx));
    }
    return ports;
  }

  std::vector<PortID> firstNLogicalPorts(size_t n) const {
    std::vector<size_t> idxs;
    idxs.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      idxs.push_back(i);
    }
    return logicalPorts(idxs);
  }

  using VrfVniMap = std::map<int64_t, int64_t>;

  // Encap and decap mapper contents for the tunnel: the
  // default VR entry plus one entry per overlay VRF.
  std::pair<VrfVniMap, VrfVniMap> vniMaps(
      const std::vector<std::pair<RouterID, uint32_t>>& overlayMappings = {})
      const {
    VrfVniMap vrf2Vni;
    VrfVniMap vniToVrf;
    auto addMapping = [&vrf2Vni, &vniToVrf](RouterID vrf, uint32_t vni) {
      vrf2Vni[static_cast<int64_t>(vrf)] = vni;
      vniToVrf[vni] = static_cast<int64_t>(vrf);
    };
    addMapping(RouterID(0), kDefaultVni);
    addMapping(kOverlayVrf, kVni);
    for (const auto& [vrf, vni] : overlayMappings) {
      addMapping(vrf, vni);
    }
    return {vrf2Vni, vniToVrf};
  }

  cfg::SwitchConfig vxlanConfig(
      const AgentEnsemble& ensemble,
      const folly::IPAddressV6& localVtep,
      const folly::IPAddressV6& remoteVtep,
      const std::vector<std::pair<RouterID, uint32_t>>& overlayMappings = {})
      const {
    auto cfg = interfaceConfig(ensemble);
    auto tunnel = tunnelConfig(cfg, localVtep, remoteVtep);
    auto [vrf2Vni, vniToVrf] = vniMaps(overlayMappings);
    tunnel.vrf2Vni() = vrf2Vni;
    // BRCM builds its VNI -> VR table only from decap entries,
    // and encap next hop creation looks the VNI up in it.
    tunnel.vniToVrf() = vniToVrf;
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

  // Overlay routes pass level 1 against the level 0 underlay, so
  // the VXLAN next hop resolves in the next level and keeps its
  // VTEP address and tunnel fields instead of flattening onto
  // the underlay.
  void addStaticRoute(
      RouterID vrf,
      const folly::IPAddress& prefix,
      uint8_t prefixLength,
      const RouteNextHopSet& nextHops,
      int32_t level = 0) {
    auto routeUpdater = this->getSw()->getRouteUpdater();
    routeUpdater.addRoute(
        vrf,
        prefix,
        prefixLength,
        ClientID::STATIC_ROUTE,
        RouteNextHopEntry(nextHops, AdminDistance::STATIC_ROUTE),
        std::nullopt,
        level == 0 ? std::nullopt : std::optional<int32_t>(level));
    routeUpdater.program();
  }

  void removeStaticRoute(
      RouterID vrf,
      const folly::IPAddress& prefix,
      uint8_t prefixLength) {
    auto routeUpdater = this->getSw()->getRouteUpdater();
    routeUpdater.delRoute(vrf, prefix, prefixLength, ClientID::STATIC_ROUTE);
    routeUpdater.program();
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
    addStaticRoute(RouterID(0), prefix, prefixLength, nextHops);
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
        // The remote VTEP resolves in the default (underlay)
        // VRF, not in the overlay VRF of the route.
        RouterID(0),
        false /* resolvedInNextLevel */,
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
    addStaticRoute(
        RouterID(0),
        folly::IPAddress(vtep),
        128,
        {plainNextHop(folly::IPAddress(nhop.ip), nhop.intf)});
    addStaticRoute(
        kOverlayVrf,
        folly::IPAddress(overlayPrefix),
        64,
        {vxlanNextHop(vtep, nhop.intf, overlayDstMac)},
        1);
  }

  // The decap tunnel term destination must be a switch-owned
  // IP or the ASIC does not terminate on it.
  static inline const folly::IPAddressV6 kDecapLocalVtep{"1::"};
  static inline const folly::IPAddressV6 kDecapRemoteVtep{"11::2"};
  static inline const folly::IPAddressV6 kDecapInnerSrcIp{"100::2"};
  static inline const folly::IPAddressV6 kDecapInnerDstIp{"100::1"};
  static inline const folly::IPAddressV6 kDecapOverlayPrefix{"101::"};

  cfg::SwitchConfig decapConfig(const AgentEnsemble& ensemble) const {
    // Two VNIs decap into two overlay VRFs: kVni into kOverlayVrf
    // and kVni2 into kOverlayVrf2.
    auto cfg = vxlanConfig(
        ensemble, kDecapLocalVtep, kDecapRemoteVtep, {{kOverlayVrf2, kVni2}});
    addVrfPorts(cfg, ensemble, kDecapVrfPorts);
    return cfg;
  }

  // Route the decapped inner destination out the second port in
  // the first overlay VRF and out the sixth port in the second,
  // so each VNI's decapped traffic has its own egress.
  void addDecapRoute() {
    auto nhops = resolvedNhops(6, true /*useLinkLocal*/);
    auto nhop1 = nhops.nhop(1);
    CHECK(nhop1.linkLocalNhopIp.has_value());
    addStaticRoute(
        kOverlayVrf,
        folly::IPAddress(kDecapInnerDstIp),
        128,
        {plainNextHop(folly::IPAddress(*nhop1.linkLocalNhopIp), nhop1.intf)});
    auto nhop5 = nhops.nhop(5);
    CHECK(nhop5.linkLocalNhopIp.has_value());
    addStaticRoute(
        kOverlayVrf2,
        folly::IPAddress(kDecapInnerDstIp),
        128,
        {plainNextHop(folly::IPAddress(*nhop5.linkLocalNhopIp), nhop5.intf)});
  }

  // TODO(CS00012485397): SIP lookup needs resolution. Decap
  // only terminates once an overlay next hop to the remote
  // VTEP programs the SIP->SVP match for its packets.
  void addDecapOverlayNextHop() {
    addVxlanRoute(0, kDecapRemoteVtep, kDecapOverlayPrefix, stationMac());
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

  std::map<PortID, HwPortStats> portStatsSnapshot(
      const std::vector<PortID>& ports) {
    std::map<PortID, HwPortStats> stats;
    for (const auto& port : ports) {
      stats[port] = getLatestPortStats(port);
    }
    return stats;
  }

  // Per-port egress packet deltas once totalPackets have egressed.
  std::map<PortID, int64_t> egressPacketDeltas(
      const std::vector<PortID>& ports,
      const std::map<PortID, HwPortStats>& before,
      int64_t totalPackets) {
    std::map<PortID, int64_t> packets;
    WITH_RETRIES({
      int64_t total = 0;
      for (auto port : ports) {
        auto after = getLatestPortStats(port);
        packets[port] =
            *after.outUnicastPkts_() - *before.at(port).outUnicastPkts_();
        total += packets[port];
      }
      EXPECT_EVENTUALLY_EQ(total, totalPackets);
    });
    return packets;
  }

  // Send numFlows flows to dst and return the per-port egress
  // packet deltas once every flow has egressed.
  std::map<PortID, int64_t> countEgressPackets(
      const folly::IPAddressV6& dst,
      int numFlows,
      const std::vector<PortID>& ports,
      std::optional<size_t> ingressIdx = std::nullopt,
      std::map<PortID, int64_t>* egressBytes = nullptr) {
    auto before = portStatsSnapshot(ports);
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
    auto packets = egressPacketDeltas(ports, before, numFlows);
    if (egressBytes) {
      for (const auto& port : ports) {
        auto after = getLatestPortStats(port);
        (*egressBytes)[port] = *after.outBytes_() - *before[port].outBytes_();
      }
    }
    return packets;
  }

  // Invoke fn on each snooped frame until it has accepted
  // target frames or the snooper times out.
  template <typename Fn>
  void forEachSnoopedFrame(
      utility::SwSwitchPacketSnooper& snooper,
      int target,
      Fn&& fn) {
    int accepted = 0;
    while (accepted < target) {
      auto pktBuf = snooper.waitForPacket(2);
      if (!pktBuf.has_value()) {
        break;
      }
      folly::io::Cursor cursor(pktBuf->get());
      if (fn(utility::EthFrame(cursor))) {
        ++accepted;
      }
    }
  }

  // As above, for frames that parse as VXLAN.
  template <typename Fn>
  void forEachSnoopedVxlanPacket(
      utility::SwSwitchPacketSnooper& snooper,
      int target,
      Fn&& fn) {
    forEachSnoopedFrame(snooper, target, [&fn](const utility::EthFrame& frame) {
      auto vxlan = utility::parseVxlanPacket(frame);
      return vxlan.has_value() && fn(frame, *vxlan);
    });
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
  // Inner traffic ingresses on port 4, outside the underlay
  // egress set, so its interface is in the overlay VRF.
  static inline const VrfPorts kEncapVrfPorts{{kOverlayVrf, {4}}};

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::VXLAN_ENCAP, ProductionFeature::MULTI_VRF};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    // Managed VXLAN next hops resolve through next hop IDs.
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_resolve_nexthops_from_id = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = vxlanConfig(ensemble, kLocalVtep, kRemoteVtep);
    addVrfPorts(cfg, ensemble, kEncapVrfPorts);
    return cfg;
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
    // Inject on the overlay VRF ingress port, so the inner
    // lookup happens in the overlay VRF.
    this->getAgentEnsemble()->ensureSendPacketOutOfPort(
        std::move(txPacket), logicalPort(4));
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
    bindVrfPorts(kEncapVrfPorts);
    addVxlanRoute(0, kRemoteVtep, kOverlayPrefix, stationMac());
    addVxlanRoute(1, kRemoteVtep2, kOverlayPrefix2, stationMac());
    addVxlanRoute(2, kRemoteVtep3, kOverlayPrefix3, stationMac());
    addVxlanRoute(3, kRemoteVtep4, kOverlayPrefix4, stationMac());
    auto nhops = resolvedNhops(4);
    const std::vector<folly::IPAddressV6> vteps{
        kRemoteVtep, kRemoteVtep2, kRemoteVtep3, kRemoteVtep4};
    std::vector<ResolvedNextHop> members;
    members.reserve(vteps.size());
    for (int i = 0; i < 4; ++i) {
      members.push_back(
          vxlanNextHop(vteps[i], nhops.nhop(i).intf, stationMac()));
    }
    RouteNextHopSet nextHops(members.begin(), members.end());
    addStaticRoute(
        kOverlayVrf, folly::IPAddress(kOverlayPrefixEcmp), 64, nextHops, 1);
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
    bindVrfPorts(kEncapVrfPorts);
    applyHashConfig();
    addEcmpRoute(folly::IPAddress(kRemoteVtep), 128, {0, 1, 2, 3});
    auto nhops = resolvedNhops(1);
    addStaticRoute(
        kOverlayVrf,
        folly::IPAddress(kOverlayPrefix),
        64,
        {vxlanNextHop(kRemoteVtep, nhops.nhop(0).intf, stationMac())},
        1);
  }

  void verifyUnderlayEcmp() {
    auto ports = firstNLogicalPorts(4);
    auto packets = countEgressPackets(kOverlayDst, 64, ports, 4);
    for (const auto& port : ports) {
      EXPECT_GT(packets[port], 0);
    }
  }

  void setupUnderlayReresolution() {
    bindVrfPorts(kEncapVrfPorts);
    applyHashConfig();
    addEcmpRoute(folly::IPAddress(kLessSpecificPrefix), 64, {0, 1});
    // The overlay next hop is created after the less specific
    // underlay route, so the SDK resolves the VTEP through it.
    auto nhops = resolvedNhops(1);
    addStaticRoute(
        kOverlayVrf,
        folly::IPAddress(kOverlayPrefix),
        64,
        {vxlanNextHop(kReresolutionVtep, nhops.nhop(0).intf, stationMac())},
        1);
  }

  void verifyUnderlayLb(const std::set<int>& memberIdx) {
    auto ports = firstNLogicalPorts(4);
    auto packets = countEgressPackets(kOverlayDst, 64, ports, 4);
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
    removeStaticRoute(RouterID(0), folly::IPAddress(kMoreSpecificPrefix), 96);
    // Removing it falls back to the /64 resolution.
    verifyUnderlayLb({0, 1});
  }
};

// host [overlay vrf] - local-vtep - tunnel [default vrf]
//   - remote-vtep-a - host-a (port0)
//   - remote-vtep-b - host-b (port1)
TEST_F(AgentVxlanEncapTest, encapOnCorrectEgressPortPerVtep) {
  auto setup = [this]() {
    bindVrfPorts(kEncapVrfPorts);
    addVxlanRoute(0, kRemoteVtep, kOverlayPrefix, stationMac());
    addVxlanRoute(1, kRemoteVtep2, kOverlayPrefix2, stationMac());
  };
  auto verify = [this]() {
    verifyEncapOnPort(kOverlayDst, logicalPort(0), {logicalPort(1)});
    verifyEncapOnPort(kOverlayDst2, logicalPort(1), {logicalPort(0)});
  };
  verifyAcrossWarmBoots(setup, verify);
}

// host [overlay vrf] - local-vtep - tunnel [default vrf] - remote-vteps - hosts
// (ports 0-3)
TEST_F(AgentVxlanEncapTest, egressOverlayEcmp) {
  auto setup = [this]() {
    applyHashConfig();
    setupOverlayEcmp();
  };
  auto verify = [this]() { verifyOverlayEcmp(); };
  verifyAcrossWarmBoots(setup, verify);
}

// host [overlay vrf] - local-vtep - tunnel [default vrf, 4 underlay paths] -
// remote-vtep - host
TEST_F(AgentVxlanEncapTest, encapUnderlayEcmp) {
  auto setup = [this]() { setupUnderlayEcmp(); };
  auto verify = [this]() { verifyUnderlayEcmp(); };
  verifyAcrossWarmBoots(setup, verify);
}

// host [overlay vrf] - local-vtep - tunnel [default vrf] - remote-vtep - host
TEST_F(AgentVxlanEncapTest, encapUnderlayReresolution) {
  auto setup = [this]() { setupUnderlayReresolution(); };
  auto verify = [this]() { verifyUnderlayReresolution(); };
  verifyAcrossWarmBoots(setup, verify);
}

class AgentVxlanDecapTest : public AgentVxlanTest {
 protected:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::VXLAN_DECAP, ProductionFeature::MULTI_VRF};
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return decapConfig(ensemble);
  }

  void setupDecap() {
    bindVrfPorts(kDecapVrfPorts);
    addDecapRoute();
    addDecapOverlayNextHop();
  }

  void verifyDecap() {
    verifyDecapToVrf(kVni, logicalPort(1));
    verifyDecapToVrf(kVni2, logicalPort(5));
  }

  void verifyDecapToVrf(uint32_t vni, PortID egressPort) {
    auto txPacket = utility::makeVxlanTxPacket(
        utility::makeAllocator(this->getSw()),
        this->getVlanIDForTx(),
        kRemoteMac,
        intfMac(),
        kDecapRemoteVtep,
        kDecapLocalVtep,
        vni,
        kHostMac,
        stationMac(),
        kDecapInnerSrcIp,
        kDecapInnerDstIp,
        8000,
        8001);
    auto outerLen =
        static_cast<int64_t>(txPacket->buf()->computeChainDataLength());
    auto ingressPort = logicalPort(0);
    auto ingressBefore = getLatestPortStats(ingressPort);
    auto egressBefore = getLatestPortStats(egressPort);
    this->getAgentEnsemble()->ensureSendPacketOutOfPort(
        std::move(txPacket), ingressPort);
    WITH_RETRIES({
      auto statsNow = getLatestPortStats(egressPort);
      EXPECT_EVENTUALLY_TRUE(*statsNow.outBytes_() > *egressBefore.outBytes_());
    });
    auto egressAfter = getNextUpdatedPortStats(egressPort);
    auto ingressAfter = getLatestPortStats(ingressPort);
    EXPECT_EQ(
        *egressAfter.outUnicastPkts_() - *egressBefore.outUnicastPkts_(), 1);
    EXPECT_EQ(
        *egressAfter.outBytes_() - *egressBefore.outBytes_(),
        outerLen - kVxlanOverhead);
    EXPECT_EQ(*ingressAfter.inBytes_() - *ingressBefore.inBytes_(), outerLen);
  }

  // Route the decapped inner destination over ports 1-4.
  // Port 0 stays the VXLAN ingress, outside the member set.
  void setupDecapEcmp() {
    bindVrfPorts(kDecapVrfPorts);
    applyHashConfig();
    auto nhops = resolvedNhops(5, true /*useLinkLocal*/);
    RouteNextHopSet nextHops;
    for (int idx = 1; idx <= 4; ++idx) {
      auto nhop = nhops.nhop(idx);
      CHECK(nhop.linkLocalNhopIp.has_value());
      nextHops.insert(
          plainNextHop(folly::IPAddress(*nhop.linkLocalNhopIp), nhop.intf));
    }
    addStaticRoute(
        kOverlayVrf, folly::IPAddress(kDecapInnerDstIp), 128, nextHops);
    addDecapOverlayNextHop();
  }

  void verifyDecapEcmp() {
    auto ports = logicalPorts({1, 2, 3, 4});
    auto before = portStatsSnapshot(ports);
    int64_t outerLen = 0;
    constexpr int kNumFlows = 64;
    for (int i = 0; i < kNumFlows; ++i) {
      auto txPacket = utility::makeVxlanTxPacket(
          utility::makeAllocator(this->getSw()),
          this->getVlanIDForTx(),
          kRemoteMac,
          intfMac(),
          kDecapRemoteVtep,
          kDecapLocalVtep,
          kVni,
          kHostMac,
          stationMac(),
          kDecapInnerSrcIp,
          kDecapInnerDstIp,
          static_cast<uint16_t>(8000 + i),
          8001);
      outerLen =
          static_cast<int64_t>(txPacket->buf()->computeChainDataLength());
      this->getAgentEnsemble()->ensureSendPacketOutOfPort(
          std::move(txPacket), logicalPort(0));
    }
    auto packets = egressPacketDeltas(ports, before, kNumFlows);
    int64_t innerLen = outerLen - kVxlanOverhead;
    for (auto port : ports) {
      auto after = getLatestPortStats(port);
      EXPECT_GT(packets[port], 0);
      EXPECT_EQ(
          *after.outBytes_() - *before[port].outBytes_(),
          packets[port] * innerLen);
    }
  }
};

// remote-vtep - tunnel [default vrf] - local-vtep - host [overlay vrf 1]
// (port1)
//                                                  \- host [overlay vrf 2]
//                                                  (port5)
TEST_F(AgentVxlanDecapTest, receivePacketOnVxlanDecapTunnel) {
  auto setup = [this]() { setupDecap(); };
  auto verify = [this]() { verifyDecap(); };
  verifyAcrossWarmBoots(setup, verify);
}

// remote-vtep - tunnel [default vrf] - local-vtep - hosts [overlay vrf] (ports
// 1-4)
TEST_F(AgentVxlanDecapTest, decapEgressEcmp) {
  auto setup = [this]() { setupDecapEcmp(); };
  auto verify = [this]() { verifyDecapEcmp(); };
  verifyAcrossWarmBoots(setup, verify);
}

class AgentVxlanSnoopTest : public AgentVxlanTest {
 protected:
  // Inner traffic ingresses on ports 4 and 5, one per overlay
  // VRF.
  static inline const VrfPorts kSnoopVrfPorts{
      {kOverlayVrf, {4}},
      {kOverlayVrf2, {5}}};

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::VXLAN_ENCAP,
        ProductionFeature::VXLAN_DECAP,
        ProductionFeature::MULTI_VRF,
        ProductionFeature::EGRESS_MIRRORING};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    // Managed VXLAN next hops resolve through next hop IDs.
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_resolve_nexthops_from_id = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    // Each overlay VRF maps to its own VNI on this tunnel.
    auto cfg =
        vxlanConfig(ensemble, kLocalVtep, kRemoteVtep, {{kOverlayVrf2, kVni2}});
    addVrfPorts(cfg, ensemble, kSnoopVrfPorts);
    return cfg;
  }

  void setupEncapSnoop() {
    bindVrfPorts(kSnoopVrfPorts);
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    programHashObject(cfg);
    applyEgressMirror(cfg, {logicalPort(0)});
    addVxlanRoute(0, kRemoteVtep, kOverlayPrefix, stationMac());
  }

  void setupTwoOverlayVrfs() {
    bindVrfPorts(kSnoopVrfPorts);
    auto cfg = this->initialConfig(*this->getAgentEnsemble());
    programHashObject(cfg);
    applyEgressMirror(cfg, {logicalPort(0)});
    addVxlanRoute(0, kRemoteVtep, kOverlayPrefix, stationMac());
    auto nhops = resolvedNhops(1);
    addStaticRoute(
        kOverlayVrf2,
        folly::IPAddress(kOverlayPrefix),
        64,
        {vxlanNextHop(kRemoteVtep, nhops.nhop(0).intf, stationMac())},
        1);
  }

  void verifyTwoOverlayVrfsTwoVnis() {
    utility::SwSwitchPacketSnooper snooper(this->getSw(), "vxlanTwoVrfSnooper");
    snooper.ignoreUnclaimedRxPkts();
    constexpr int kNumFlows = 16;
    // Inner sports 8000+ ingress on port 4 (first overlay VRF),
    // 9000+ on port 5 (second overlay VRF).
    for (int i = 0; i < kNumFlows; ++i) {
      auto packet = makeInnerPacket(
          folly::IPAddressV6("100::10"),
          kOverlayDst,
          static_cast<uint16_t>(8000 + i),
          8001);
      this->getAgentEnsemble()->ensureSendPacketOutOfPort(
          std::move(packet), logicalPort(4));
    }
    for (int i = 0; i < kNumFlows; ++i) {
      auto packet = makeInnerPacket(
          folly::IPAddressV6("100::10"),
          kOverlayDst,
          static_cast<uint16_t>(9000 + i),
          8001);
      this->getAgentEnsemble()->ensureSendPacketOutOfPort(
          std::move(packet), logicalPort(5));
    }
    int fromFirstVrf = 0;
    int fromSecondVrf = 0;
    forEachSnoopedVxlanPacket(
        snooper,
        2 * kNumFlows,
        [&](const utility::EthFrame&, const utility::VxlanPacket& vxlan) {
          const auto& innerV6 = vxlan.innerFrame.v6PayLoad();
          if (!innerV6.has_value() || !innerV6->udpPayload().has_value()) {
            return false;
          }
          auto sport = innerV6->udpPayload()->header().srcPort;
          if (sport >= 8000 && sport < 8000 + kNumFlows) {
            // Ingressed in the first overlay VRF: its VNI.
            EXPECT_EQ(vxlan.vni, kVni);
            ++fromFirstVrf;
            return true;
          }
          if (sport >= 9000 && sport < 9000 + kNumFlows) {
            // Ingressed in the second overlay VRF: the other VNI.
            EXPECT_EQ(vxlan.vni, kVni2);
            ++fromSecondVrf;
            return true;
          }
          return false;
        });
    EXPECT_EQ(fromFirstVrf, kNumFlows);
    EXPECT_EQ(fromSecondVrf, kNumFlows);
  }

  void verifyUdpSportVaries() {
    utility::SwSwitchPacketSnooper snooper(this->getSw(), "vxlanSportSnooper");
    snooper.ignoreUnclaimedRxPkts();
    constexpr int kNumFlows = 64;
    for (int i = 0; i < kNumFlows; ++i) {
      auto packet = makeInnerPacket(
          folly::IPAddressV6("100::" + std::to_string(10 + i)),
          kOverlayDst,
          static_cast<uint16_t>(8000 + i),
          8001);
      this->getAgentEnsemble()->ensureSendPacketOutOfPort(
          std::move(packet), logicalPort(4));
    }
    std::set<uint16_t> sports;
    int captured = 0;
    forEachSnoopedVxlanPacket(
        snooper,
        kNumFlows,
        [&](const utility::EthFrame&, const utility::VxlanPacket& vxlan) {
          sports.insert(vxlan.udp.header().srcPort);
          ++captured;
          return true;
        });
    XLOG(INFO) << "UDP sport capture: " << captured << " VXLAN packets, "
               << sports.size() << " distinct source ports";
    EXPECT_GT(captured, 0);
    // The outer UDP source port is the only per-flow entropy
    // the underlay ECMP can hash on; a fixed port fails here.
    EXPECT_GE(sports.size(), 2);
  }

  void verifyUniformHeaderCopy() {
    // The traffic class byte is (DSCP << 2) | ECN. Vary one
    // inner field at a time so a wrong copy has a unique
    // signature.
    struct Probe {
      uint16_t dstPort;
      uint8_t trafficClass;
      uint8_t hopLimit;
    };
    const std::vector<Probe> probes{
        {9001, static_cast<uint8_t>((46 << 2) | 0x2), 37},
        {9002, static_cast<uint8_t>((46 << 2) | 0x2), 99},
        {9003, static_cast<uint8_t>((10 << 2) | 0x2), 99},
        {9004, static_cast<uint8_t>((10 << 2) | 0x1), 99},
    };
    utility::SwSwitchPacketSnooper snooper(this->getSw(), "vxlanHdrSnooper");
    snooper.ignoreUnclaimedRxPkts();
    for (const auto& probe : probes) {
      auto packet = makeInnerPacket(
          folly::IPAddressV6("100::10"),
          kOverlayDst,
          8000,
          probe.dstPort,
          probe.trafficClass,
          probe.hopLimit);
      this->getAgentEnsemble()->ensureSendPacketOutOfPort(
          std::move(packet), logicalPort(4));
    }
    int verified = 0;
    forEachSnoopedVxlanPacket(
        snooper,
        static_cast<int>(probes.size()),
        [&](const utility::EthFrame& frame, const utility::VxlanPacket& vxlan) {
          const auto& innerV6 = vxlan.innerFrame.v6PayLoad();
          if (!innerV6.has_value() || !innerV6->udpPayload().has_value()) {
            return false;
          }
          auto dstPort = innerV6->udpPayload()->header().dstPort;
          const Probe* probe = nullptr;
          for (const auto& candidate : probes) {
            if (candidate.dstPort == dstPort) {
              probe = &candidate;
              break;
            }
          }
          if (!probe) {
            return false;
          }
          utility::VxlanEncapExpected expected;
          expected.outerSrcIp = folly::IPAddress(kLocalVtep);
          expected.outerDstIp = folly::IPAddress(kRemoteVtep);
          expected.outerTrafficClass = probe->trafficClass;
          // The inner packet is routed before it is encapsulated,
          // so the copied hop limit is already decremented.
          expected.outerHopLimit = static_cast<uint8_t>(probe->hopLimit - 1);
          expected.vni = kVni;
          expected.innerDstMac = stationMac();
          expected.innerDstIp = folly::IPAddress(kOverlayDst);
          expected.innerDstPort = probe->dstPort;
          utility::verifyVxlanEncap(frame, expected);
          ++verified;
          return true;
        });
    EXPECT_EQ(verified, static_cast<int>(probes.size()));
  }

  void setupDecapSnoop() {
    bindVrfPorts(kDecapVrfPorts);
    applyEgressMirror(decapConfig(*this->getAgentEnsemble()), {logicalPort(1)});
    addDecapRoute();
    addDecapOverlayNextHop();
  }

  void verifyDecapUniformHeaderCopy() {
    // Inner traffic class is DSCP 10, ECN ECT for every
    // probe. Vary the outer header so DSCP copy, ECN
    // merge, and TTL copy are checked on separate
    // packets, then DSCP and ECN together.
    struct Probe {
      uint16_t dstPort;
      uint8_t outerTrafficClass;
      uint8_t outerHopLimit;
      uint8_t expectedTrafficClass;
    };
    const std::vector<Probe> probes{
        // DSCP only: outer ECN matches the inner ECT.
        {9101,
         static_cast<uint8_t>((46 << 2) | 0x2),
         99,
         static_cast<uint8_t>((46 << 2) | 0x2)},
        // ECN only: outer DSCP matches the inner DSCP.
        {9102,
         static_cast<uint8_t>((10 << 2) | 0x3),
         99,
         static_cast<uint8_t>((10 << 2) | 0x3)},
        // Both: outer DSCP is copied and outer CE merges.
        {9103,
         static_cast<uint8_t>((46 << 2) | 0x3),
         99,
         static_cast<uint8_t>((46 << 2) | 0x3)},
        // TTL only: outer traffic class matches the inner.
        {9104,
         static_cast<uint8_t>((10 << 2) | 0x2),
         42,
         static_cast<uint8_t>((10 << 2) | 0x2)},
    };
    utility::SwSwitchPacketSnooper snooper(
        this->getSw(), "vxlanDecapHdrSnooper");
    snooper.ignoreUnclaimedRxPkts();
    for (const auto& probe : probes) {
      auto txPacket = utility::makeVxlanTxPacket(
          utility::makeAllocator(this->getSw()),
          this->getVlanIDForTx(),
          kRemoteMac,
          intfMac(),
          kDecapRemoteVtep,
          kDecapLocalVtep,
          kVni,
          kHostMac,
          stationMac(),
          kDecapInnerSrcIp,
          kDecapInnerDstIp,
          8000,
          probe.dstPort,
          probe.outerTrafficClass,
          static_cast<uint8_t>((10 << 2) | 0x2),
          probe.outerHopLimit);
      this->getAgentEnsemble()->ensureSendPacketOutOfPort(
          std::move(txPacket), logicalPort(0));
    }
    int verified = 0;
    forEachSnoopedFrame(
        snooper,
        static_cast<int>(probes.size()),
        [&](const utility::EthFrame& frame) {
          const auto& v6 = frame.v6PayLoad();
          if (!v6.has_value() || !v6->udpPayload().has_value()) {
            return false;
          }
          auto dstPort = v6->udpPayload()->header().dstPort;
          const Probe* probe = nullptr;
          for (const auto& candidate : probes) {
            if (candidate.dstPort == dstPort) {
              probe = &candidate;
              break;
            }
          }
          if (!probe) {
            return false;
          }
          utility::VxlanDecapExpected expected;
          // UNIFORM decap copies the outer DSCP to the inner
          // header; STANDARD ECN turns an inner ECT into CE when
          // the outer header carries CE.
          expected.innerTrafficClass = probe->expectedTrafficClass;
          // The decapped packet is routed before it egresses, so
          // the copied hop limit is already decremented.
          expected.innerHopLimit =
              static_cast<uint8_t>(probe->outerHopLimit - 1);
          expected.innerSrcIp = folly::IPAddress(kDecapInnerSrcIp);
          expected.innerDstIp = folly::IPAddress(kDecapInnerDstIp);
          expected.innerDstPort = probe->dstPort;
          utility::verifyVxlanDecap(frame, expected);
          ++verified;
          return true;
        });
    EXPECT_EQ(verified, static_cast<int>(probes.size()));
  }
};

// hosts [overlay vrf 1 + 2] - local-vtep - tunnel [default vrf] - remote-vtep -
// host
TEST_F(AgentVxlanSnoopTest, encapTwoOverlayVrfsTwoVnis) {
  auto setup = [this]() { setupTwoOverlayVrfs(); };
  auto verify = [this]() { verifyTwoOverlayVrfsTwoVnis(); };
  verifyAcrossWarmBoots(setup, verify);
}

// host [overlay vrf] - local-vtep - tunnel [default vrf] - remote-vtep - host
TEST_F(AgentVxlanSnoopTest, encapUdpSportVariesAcrossFlows) {
  auto setup = [this]() { setupEncapSnoop(); };
  auto verify = [this]() { verifyUdpSportVaries(); };
  verifyAcrossWarmBoots(setup, verify);
}

// host [overlay vrf] - local-vtep - tunnel [default vrf] - remote-vtep - host
TEST_F(AgentVxlanSnoopTest, encapUniformCopiesToOuterHeader) {
  auto setup = [this]() { setupEncapSnoop(); };
  auto verify = [this]() { verifyUniformHeaderCopy(); };
  verifyAcrossWarmBoots(setup, verify);
}

// remote-vtep - tunnel [default vrf] - local-vtep - host [overlay vrf] (port1)
TEST_F(AgentVxlanSnoopTest, decapUniformCopiesToInnerHeader) {
  auto setup = [this]() { setupDecapSnoop(); };
  auto verify = [this]() { verifyDecapUniformHeaderCopy(); };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
