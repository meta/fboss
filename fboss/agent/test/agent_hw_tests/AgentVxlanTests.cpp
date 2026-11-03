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
    for (auto port : ports) {
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
      for (auto port : ports) {
        auto after = getLatestPortStats(port);
        packets[port] =
            *after.outUnicastPkts_() - *before[port].outUnicastPkts_();
        total += packets[port];
      }
      EXPECT_EVENTUALLY_EQ(total, numFlows);
    });
    if (egressBytes) {
      for (auto port : ports) {
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

} // namespace facebook::fboss
