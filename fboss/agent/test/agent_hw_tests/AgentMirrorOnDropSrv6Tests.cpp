// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_hw_tests/AgentMirrorOnDropStatelessTest.h"

#include <gtest/gtest.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/Srv6TestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

class AgentMirrorOnDropSrv6Test : public AgentMirrorOnDropStatelessTest {
 protected:
  static inline const folly::IPAddressV6 kSrv6OuterSrcIp{"100::1"};

  static inline const folly::IPAddressV6 kMidpointMySidPrefix{"fdad:ffff:1::"};
  static constexpr uint8_t kMidpointMySidPrefixLen{48};

  static inline const folly::IPAddressV6 kDecapMySidAddr{"3001:db8:7fff::"};
  static constexpr uint8_t kDecapMySidPrefixLen{48};

  static inline const folly::IPAddressV6 kBindingSidPrefix{"fc00:100:1::"};
  static constexpr uint8_t kBindingSidPrefixLen{48};
  static inline const folly::IPAddressV6 kBindingSidSid0{
      "3001:db8:1:2:3:4:5:6"};
  static inline const folly::IPAddressV6 kBgpRoute0{"2001::1"};
  static inline const folly::IPAddressV6 kOpenrPrefix0{"fdad::1:0"};

  static inline const folly::IPAddressV6 kSrv6TunnelSrcIp{"2001:db8::1"};
  static inline const folly::IPAddressV6 kEncapRoutePrefix{"2800:2::"};
  static constexpr uint8_t kEncapRoutePrefixLen{64};
  static inline const folly::IPAddressV6 kEncapRouteDstIp{"2800:2::1"};
  static inline const folly::IPAddressV6 kSrv6EncapSid{"3001:db8:1:2:3:4:5:6"};
  static inline const folly::IPAddressV6 kNonEncapRoutePrefix{"2800:3::"};
  static constexpr uint8_t kNonEncapRoutePrefixLen{64};
  static inline const folly::IPAddressV6 kNonEncapRouteDstIp{"2800:3::1"};
  static constexpr int kEncapEgressMtu{1500};
  static constexpr size_t kSmallPayloadSize{1300};
  static constexpr size_t kMtuBoundaryPayloadSize{1430};

  void setCmdLineFlagOverrides() const override {
    AgentMirrorOnDropStatelessTest::setCmdLineFlagOverrides();
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_resolve_nexthops_from_id = true;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::MIRROR_ON_DROP,
        ProductionFeature::MIRROR_ON_DROP_STATELESS,
        ProductionFeature::SRV6_MIDPOINT,
        ProductionFeature::SRV6_DECAP,
        ProductionFeature::SRV6_BINDING_SID,
        ProductionFeature::SRV6_ENCAP};
  }

  void sendSrv6Packet(
      const PortID& injectPort,
      const folly::IPAddressV6& outerDst) {
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto txPacket = utility::makeIpInIpTxPacket(
        getSw(),
        getVlanIDForTx().value(),
        intfMac,
        intfMac,
        kSrv6OuterSrcIp,
        outerDst,
        folly::IPAddressV6("2001:db8::1"),
        folly::IPAddressV6("2001:db8::2"),
        8000,
        8001,
        0,
        0,
        24,
        64);
    getSw()->sendPacketOutOfPortAsync(std::move(txPacket), injectPort);
  }

  // Inject a plain UDP packet of the given payload size to dstIp.
  void sendPlainPacket(
      const PortID& injectPort,
      const folly::IPAddressV6& dstIp,
      size_t payloadSize) {
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto txPacket = utility::makeUDPTxPacket(
        getSw(),
        getVlanIDForTx(),
        intfMac,
        intfMac,
        kSrv6OuterSrcIp,
        dstIp,
        8000,
        8001,
        0 /* trafficClass */,
        64 /* hopLimit */,
        std::vector<uint8_t>(payloadSize, 0xff));
    getSw()->sendPacketOutOfPortAsync(std::move(txPacket), injectPort);
  }

  void setupModAndCollector(
      cfg::SwitchConfig& config,
      const PortID& collectorPortId) {
    config.mirrorOnDropReports()->push_back(
        makeMirrorOnDropReport("mod-srv6-test"));
    auto* asic =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    utility::addTrapPacketAcl(
        asic,
        &config,
        folly::CIDRNetwork(folly::IPAddress(kCollectorIp_), 128),
        cfg::ToCpuAction::TRAP);
    applyNewConfig(config);
    setupEcmpTraffic(collectorPortId, kCollectorIp_, kCollectorNextHopMac_);
    waitForStateUpdates(getSw());
  }

  void captureAndValidateModDrop(
      utility::SwSwitchPacketSnooper& snooper,
      const PortID& injectionPortId,
      const MirrorOnDropDropReasonCodes& expectedReasons,
      const folly::IPAddressV6& innerSrc,
      const folly::IPAddressV6& innerDst) {
    WITH_RETRIES_N(10, {
      auto frameRx = snooper.waitForPacket(1);
      ASSERT_EVENTUALLY_TRUE(frameRx.has_value());
      auto fields = parseMirrorOnDropPacket(frameRx->get());
      XLOG(INFO) << "Captured MirrorOnDrop packet, ingress drop reason: "
                 << fields.dropReasonIngress
                 << ", egress drop reason: " << fields.dropReasonEgress
                 << ", inner src: " << fields.innerSrcIp
                 << ", inner dst: " << fields.innerDstIp << ":\n"
                 << PktUtil::hexDump(frameRx->get());
      ASSERT_EVENTUALLY_EQ(
          fields.dropReasonIngress, expectedReasons.ingressDropReason);
      ASSERT_EVENTUALLY_EQ(
          fields.dropReasonEgress, expectedReasons.egressDropReason);
      validateMirrorOnDropPacket(
          frameRx->get(), injectionPortId, expectedReasons, innerDst, innerSrc);
    });
  }

  // Inject an SRv6 packet (received-SRv6 path) and verify the resulting MoD
  // export. The sampled packet is the injected packet, so its inner src/dst are
  // kSrv6OuterSrcIp -> outerDst.
  void sendAndVerifyModPacket(
      const PortID& injectionPortId,
      const folly::IPAddressV6& outerDst,
      const MirrorOnDropDropReasonCodes& expectedReasons) {
    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "mod-srv6-snooper",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        impl()->snooperReceivePacketType());
    snooper.ignoreUnclaimedRxPkts();

    sendSrv6Packet(injectionPortId, outerDst);

    captureAndValidateModDrop(
        snooper, injectionPortId, expectedReasons, kSrv6OuterSrcIp, outerDst);
  }

  void programBindingSidRoutes() {
    auto ecmpHelper = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto routeUpdater = getSw()->getRouteUpdater();

    auto getNhopIp = [&ecmpHelper](int idx) {
      auto nhop = ecmpHelper.nhop(idx);
      if (nhop.linkLocalNhopIp.has_value()) {
        return folly::IPAddress(nhop.linkLocalNhopIp.value());
      }
      return folly::IPAddress(nhop.ip);
    };

    RouteNextHopSet openrNhops0{
        ResolvedNextHop(getNhopIp(0), ecmpHelper.nhop(0).intf, ECMP_WEIGHT)};
    routeUpdater.addRoute(
        RouterID(0),
        kOpenrPrefix0,
        112,
        ClientID::OPENR,
        RouteNextHopEntry(openrNhops0, AdminDistance::OPENR));

    routeUpdater.addRoute(
        RouterID(0),
        kBgpRoute0,
        128,
        ClientID::BGPD,
        RouteNextHopEntry(
            RouteNextHopSet{UnresolvedNextHop(
                folly::IPAddress(folly::IPAddressV6("fdad::1:1")),
                ECMP_WEIGHT)},
            AdminDistance::EBGP));

    routeUpdater.program();
  }

  void addSrv6EncapRoute(
      const folly::IPAddressV6& prefix,
      uint8_t prefixLen,
      const folly::IPAddressV6& sid) {
    auto ecmpHelper = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto nhop = ecmpHelper.nhop(0);
    CHECK(nhop.linkLocalNhopIp.has_value());
    RouteNextHopSet nhops{ResolvedNextHop(
        folly::IPAddress(*nhop.linkLocalNhopIp),
        nhop.intf,
        ECMP_WEIGHT,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::vector<folly::IPAddressV6>{sid},
        TunnelType::SRV6_ENCAP,
        std::string("srv6Tunnel0"))};
    auto routeUpdater = getSw()->getRouteUpdater();
    routeUpdater.addRoute(
        RouterID(0),
        prefix,
        prefixLen,
        ClientID::BGPD,
        RouteNextHopEntry(nhops, AdminDistance::EBGP));
    routeUpdater.program();
  }

  void addPlainRoute(const folly::IPAddressV6& prefix, uint8_t prefixLen) {
    auto ecmpHelper = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto nhop = ecmpHelper.nhop(0);
    CHECK(nhop.linkLocalNhopIp.has_value());
    RouteNextHopSet nhops{ResolvedNextHop(
        folly::IPAddress(*nhop.linkLocalNhopIp), nhop.intf, ECMP_WEIGHT)};
    auto routeUpdater = getSw()->getRouteUpdater();
    routeUpdater.addRoute(
        RouterID(0),
        prefix,
        prefixLen,
        ClientID::BGPD,
        RouteNextHopEntry(nhops, AdminDistance::EBGP));
    routeUpdater.program();
  }

  // Program MoD + collector + a headend SRv6 encap route AND a plain route out
  // the same egress port.
  void setupSrv6EncapWithEgressMtu(const PortID& collectorPortId) {
    auto config = getAgentEnsemble()->getCurrentConfig();

    config.srv6Tunnels() = {utility::makeSrv6TunnelConfig(
        "srv6Tunnel0", InterfaceID(config.interfaces()[0].intfID().value()))};

    auto ecmpHelper = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    const auto encapEgressPort = ecmpHelper.nhop(0).portDesc;
    const auto encapEgressIntf = ecmpHelper.nhop(0).intf;
    for (auto& intf : *config.interfaces()) {
      if (InterfaceID(*intf.intfID()) == encapEgressIntf) {
        intf.mtu() = kEncapEgressMtu;
      }
    }

    setupModAndCollector(config, collectorPortId);

    // Resolve the encap egress neighbor (so the encapped packet forwards) and
    // the collector neighbor (so MoD exports are delivered). Use the default
    // (synthetic) next-hop MAC, not my_mac, so a forwarded packet that loops
    // back is not re-routed.
    auto resolveHelper = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    applyNewState(
        [&, collectorPortId](std::shared_ptr<SwitchState> in) {
          return resolveHelper.resolveNextHops(
              in,
              {encapEgressPort, PortDescriptor{collectorPortId}},
              /*useLinkLocal=*/true);
        },
        "resolve encap egress + collector neighbors");

    addSrv6EncapRoute(kEncapRoutePrefix, kEncapRoutePrefixLen, kSrv6EncapSid);
    addPlainRoute(kNonEncapRoutePrefix, kNonEncapRoutePrefixLen);
  }

  // Inject a plain packet of the given size to dstIp (which hits the encap
  // route) and verify the egress MTU drop is exported. The sampled packet is
  // the encapped packet, so its inner src/dst are the tunnel src and the SID.
  void sendAndVerifyMtuDrop(
      const PortID& injectionPortId,
      const folly::IPAddressV6& dstIp,
      size_t payloadSize) {
    utility::SwSwitchPacketSnooper snooper(
        getSw(),
        "mod-srv6-snooper",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        impl()->snooperReceivePacketType());
    snooper.ignoreUnclaimedRxPkts();

    sendPlainPacket(injectionPortId, dstIp, payloadSize);

    // This is an egress drop (MTU exceeded after encap), so the Tajo punt
    // header reports the egress system port
    auto ecmpHelper = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    const auto egressPort = ecmpHelper.nhop(0).portDesc.phyPortID();
    captureAndValidateModDrop(
        snooper,
        egressPort,
        getSrv6EncapMtuExceededDropReasons(),
        kSrv6TunnelSrcIp,
        kSrv6EncapSid);
  }

  // Inject a plain packet of the given size to dstIp and confirm it is
  // forwarded out the encap egress port (its outBytes increase)
  void sendAndVerifyForwarded(
      const PortID& injectionPortId,
      const folly::IPAddressV6& dstIp,
      size_t payloadSize) {
    auto ecmpHelper = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    const auto egressPort = ecmpHelper.nhop(0).portDesc.phyPortID();
    const auto bytesBefore = *getLatestPortStats(egressPort).outBytes_();

    sendPlainPacket(injectionPortId, dstIp, payloadSize);

    WITH_RETRIES({
      const auto bytesAfter = *getLatestPortStats(egressPort).outBytes_();
      EXPECT_EVENTUALLY_GT(bytesAfter, bytesBefore);
    });
  }
};

// Single test exercising all SRv6 drop scenarios with MOD.
// Setup installs midpoint, decap, and binding SID MySid entries with
// neighbors resolved. Verify exercises each drop sequentially, then
// unresolves the midpoint neighbor for the final scenario.
TEST_F(AgentMirrorOnDropSrv6Test, Srv6Drops) {
  PortID mySidPortId = masterLogicalInterfacePortIds()[0];
  PortID collectorPortId = masterLogicalInterfacePortIds()[1];
  PortID injectionPortId = masterLogicalInterfacePortIds()[2];

  const folly::IPAddressV6 kDecapNonLastDst{"3001:db8:7fff:1:2::"};
  const folly::IPAddressV6 kBindingSidNonLastDst{"fc00:100:1:2::"};
  const folly::IPAddressV6 kMidpointValidDst{"fdad:ffff:1:2::"};

  auto setup = [&]() {
    auto config = getAgentEnsemble()->getCurrentConfig();

    auto portName = utility::portNameForConfig(config, mySidPortId);
    config.mySidConfig() =
        utility::makeAdjacencyMySidConfig(portName, "fdad:ffff::/32", 1);

    config.srv6Tunnels() = {utility::makeSrv6TunnelConfig(
        "srv6Tunnel0", InterfaceID(config.interfaces()[0].intfID().value()))};

    setupModAndCollector(config, collectorPortId);

    auto ecmpHelper = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        getLocalMacAddress());
    PortDescriptor portDesc{mySidPortId};
    applyNewState(
        [&ecmpHelper, &portDesc](std::shared_ptr<SwitchState> in) {
          return ecmpHelper.resolveNextHops(
              in, {portDesc}, /*useLinkLocal=*/true);
        },
        "resolve mysid neighbor");
    utility::waitForMySidResolveOrUnresolve(
        [this]() { return getProgrammedState(); },
        kMidpointMySidPrefix,
        kMidpointMySidPrefixLen,
        /*resolved=*/true);

    auto ecmpHelper2 = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    applyNewState(
        [&ecmpHelper2, collectorPortId](std::shared_ptr<SwitchState> in) {
          return ecmpHelper2.resolveNextHops(
              in,
              {PortDescriptor{collectorPortId}},
              /*useLinkLocal=*/true);
        },
        "resolve collector neighbor");

    utility::addDecapMySidEntry(getSw(), kDecapMySidAddr, kDecapMySidPrefixLen);

    programBindingSidRoutes();
    utility::addBindingSidEntry(
        getSw(),
        kBindingSidPrefix,
        kBindingSidPrefixLen,
        {utility::makeSrv6NextHopThrift(kBgpRoute0, kBindingSidSid0)});
  };

  auto verify = [&]() {
    // Todo: re-enable or delete once MT-902 is closed
    // XLOG(INFO) << "--- Midpoint is-last-SID drop ---";
    // sendAndVerifyModPacket(
    //     injectionPortId,
    //     kMidpointMySidPrefix,
    //     getSrv6MidpointIsLastSidDropReason());

    XLOG(INFO) << "--- Decap non-last-segment drop ---";
    sendAndVerifyModPacket(
        injectionPortId,
        kDecapNonLastDst,
        getSrv6DecapNonLastSegmentDropReasons());

    XLOG(INFO) << "--- Binding SID non-last-SID drop ---";
    sendAndVerifyModPacket(
        injectionPortId,
        kBindingSidNonLastDst,
        getSrv6BindingSidNonLastSidDropReasons());

    XLOG(INFO) << "--- Midpoint unresolved neighbor drop ---";
    auto ecmpHelper = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        getLocalMacAddress());
    PortDescriptor portDesc{mySidPortId};
    applyNewState(
        [&ecmpHelper, &portDesc](std::shared_ptr<SwitchState> in) {
          return ecmpHelper.unresolveNextHops(
              in, {portDesc}, /*useLinkLocal=*/true);
        },
        "unresolve mysid neighbor");
    utility::waitForMySidResolveOrUnresolve(
        [this]() { return getProgrammedState(); },
        kMidpointMySidPrefix,
        kMidpointMySidPrefixLen,
        /*resolved=*/false);

    sendAndVerifyModPacket(
        injectionPortId,
        kMidpointValidDst,
        getSrv6MidpointUnresolvedDropReasons());

    // Re-resolve so the next warmboot iteration starts from the same state
    // as setup() established.
    auto ecmpHelper2 = utility::EcmpSetupAnyNPorts<folly::IPAddressV6>(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        getLocalMacAddress());
    applyNewState(
        [&ecmpHelper2, &portDesc](std::shared_ptr<SwitchState> in) {
          return ecmpHelper2.resolveNextHops(
              in, {portDesc}, /*useLinkLocal=*/true);
        },
        "re-resolve mysid neighbor");
    utility::waitForMySidResolveOrUnresolve(
        [this]() { return getProgrammedState(); },
        kMidpointMySidPrefix,
        kMidpointMySidPrefixLen,
        /*resolved=*/true);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Egress MTU-exceeded drop after SRv6 tunnel-header imposition. Post-encap L3
// size = 88B headers + payload; non-encap L3 size = 48B headers + payload.
//   small payload, encap:  88+1300 = 1388 (< 1500) -> ok
//   boundary payload, no encap:   48+1430 = 1478 (< 1500) -> ok
//   boundary payload, encap:      88+1430 = 1518 (> 1500) -> drop
TEST_F(AgentMirrorOnDropSrv6Test, Srv6EncapMtuExceededDrop) {
  PortID collectorPortId = masterLogicalInterfacePortIds()[1];
  PortID injectionPortId = masterLogicalInterfacePortIds()[2];

  auto setup = [&]() { setupSrv6EncapWithEgressMtu(collectorPortId); };

  auto verify = [&]() {
    XLOG(INFO) << "--- Case 1: small payload, encap route: expect forward ---";
    sendAndVerifyForwarded(
        injectionPortId, kEncapRouteDstIp, kSmallPayloadSize);

    XLOG(INFO)
        << "--- Case 2: boundary payload, non-encap route: expect forward ---";
    sendAndVerifyForwarded(
        injectionPortId, kNonEncapRouteDstIp, kMtuBoundaryPayloadSize);

    XLOG(INFO)
        << "--- Case 3: boundary payload, encap route: expect MoD MTU drop ---";
    sendAndVerifyMtuDrop(
        injectionPortId, kEncapRouteDstIp, kMtuBoundaryPayloadSize);
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
