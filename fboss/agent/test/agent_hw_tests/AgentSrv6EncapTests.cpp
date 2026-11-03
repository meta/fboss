// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/test/utils/Srv6TestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

struct PhysicalPortSrv6 {
  static constexpr bool isTrunk = false;
};
struct AggregatePortSrv6 {
  static constexpr bool isTrunk = true;
};
using Srv6EncapPortTypes =
    ::testing::Types<PhysicalPortSrv6, AggregatePortSrv6>;

template <typename PortType>
class AgentSrv6EncapTest : public AgentHwTest {
 protected:
  static constexpr bool kIsTrunk = PortType::isTrunk;

  // All 6 uSids populated
  static inline const folly::IPAddressV6 kSid0{"3001:db8:1:2:3:4:5:6"};
  // 3 uSids populated
  static inline const folly::IPAddressV6 kSid1{"3001:db8:4:5:6::"};
  static inline const folly::IPAddressV6 kSid2{"3001:db8:7:8:9::"};

  const folly::IPAddressV6 kEncapRoutePrefix{"2800:2::"};
  static constexpr uint8_t kEncapRoutePrefixLen{64};
  const folly::IPAddressV6 kEncapRouteDstIp{"2800:2::1"};
  static inline const folly::IPAddressV4 kRecursiveV4Prefix{"100.0.0.0"};
  static constexpr uint8_t kRecursiveV4PrefixLen{24};
  static constexpr int kNumNextHops{4};
  static constexpr uint8_t kECT1{1};

  // NHG names for SRv6 encap routes (also used as counter IDs)
  // Same names used for both v4 and v6 routes
  static inline const std::string kNhgSid0{"kSid0"};
  static inline const std::string kNhgSid1OrSid2{"kSid1_or_kSid2"};

  // Production recursive SRv6: a child prefix programmed by OpenR (no SID) and
  // TE_Agent (kSid0), that a BGP parent (kEncapRoutePrefix) resolves through.
  const folly::IPAddressV6 kChildPrefix{"2901::"};
  static constexpr uint8_t kChildPrefixLen{48};
  const folly::IPAddress kChildDstIp{"2901::1234"};
  const folly::IPAddress kBgpRecursiveNhop{"2901::1"};
  static inline const std::string kChildRouteCounter{"recursiveChildSid"};

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (kIsTrunk) {
      return {
          ProductionFeature::SRV6_ENCAP,
          ProductionFeature::L3_QOS,
          ProductionFeature::ECN,
          ProductionFeature::LAG,
          ProductionFeature::ROUTE_COUNTERS};
    }
    return {
        ProductionFeature::SRV6_ENCAP,
        ProductionFeature::L3_QOS,
        ProductionFeature::ECN,
        ProductionFeature::ROUTE_COUNTERS};
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_resolve_nexthops_from_id = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto masterLogicalPorts = ensemble.masterLogicalPortIds();
    cfg::SwitchConfig cfg;
    if constexpr (kIsTrunk) {
      std::vector<utility::AggregatePortInfo> aggPorts;
      aggPorts.reserve(kNumNextHops);
      for (int i = 0; i < kNumNextHops; ++i) {
        aggPorts.push_back({AggregatePortID(i + 1), {masterLogicalPorts[i]}});
      }
      cfg = utility::oneAggregatePortPerInterfaceConfig(
          ensemble.getSw(),
          masterLogicalPorts,
          aggPorts,
          true /*interfaceHasSubnet*/);
    } else {
      cfg = utility::onePortPerInterfaceConfig(
          ensemble.getSw(), masterLogicalPorts, true /*interfaceHasSubnet*/);
    }
    addSrv6TunnelConfig(cfg);
    cfg.loadBalancers() =
        utility::getEcmpFullWithFlowLabelTrunkFullWithFlowLabelHashConfig(
            ensemble.getL3Asics());
    auto asic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
    utility::addTrapPacketAcl(
        asic,
        &cfg,
        {folly::CIDRNetwork{kSid0, 128},
         folly::CIDRNetwork{kSid1, 128},
         folly::CIDRNetwork{kSid2, 128}});
    utility::addOlympicQueueConfig(
        &cfg,
        ensemble.getL3Asics(),
        /*addWredConfig=*/false,
        /*addEcnConfig=*/true);
    utility::addOlympicQosMaps(cfg, ensemble.getL3Asics());
    return cfg;
  }

  void applyConfigAndEnableTrunks(const cfg::SwitchConfig& config) {
    this->applyNewConfig(config);
    this->applyNewState(
        [](const std::shared_ptr<SwitchState> state) {
          return utility::enableTrunkPorts(state);
        },
        "enable trunk ports");
  }

  template <typename IPAddrT = folly::IPAddressV6>
  utility::EcmpSetupAnyNPorts<IPAddrT> makeEcmpHelper() {
    return utility::EcmpSetupAnyNPorts<IPAddrT>(
        this->getProgrammedState(), this->getSw()->needL2EntryForNeighbor());
  }

  void resolveNextHops(int numNextHops) {
    auto ecmpHelper = makeEcmpHelper<folly::IPAddressV6>();
    this->resolveNeighbors(ecmpHelper, numNextHops, true /* useLinkLocal */);
  }

  void unresolveNextHops(int numNextHops) {
    auto ecmpHelper = makeEcmpHelper<folly::IPAddressV6>();
    this->unresolveNeighbors(ecmpHelper, numNextHops, true /* useLinkLocal */);
  }
  void setupHelper(
      bool resolveNeighbors = true,
      bool programEncapRoutes = true) {
    if constexpr (kIsTrunk) {
      applyConfigAndEnableTrunks(
          this->initialConfig(*this->getAgentEnsemble()));
    }
    if (resolveNeighbors) {
      resolveNextHops(kNumNextHops);
    }
    if (programEncapRoutes) {
      // IPv6 encap routes (v6 next hops)
      addEncapRoute<folly::CIDRNetworkV6>(
          {kEncapRoutePrefix, kEncapRoutePrefixLen}, {{kSid0}}, kNhgSid0);
      addEncapRoute<folly::CIDRNetworkV6>(
          {folly::IPAddressV6("2800:3::"), kEncapRoutePrefixLen},
          {{kSid1}, {kSid2}},
          kNhgSid1OrSid2);
      addEncapRoute<folly::CIDRNetworkV6>(
          {folly::IPAddressV6("2800:4::"), kEncapRoutePrefixLen},
          {{kSid1}, {kSid2}},
          kNhgSid1OrSid2);
      // IPv4 encap routes (v4 next hops) - use same NHG names as v6
      addEncapRoute<folly::CIDRNetworkV4>(
          {folly::IPAddressV4("100.0.0.0"), 24}, {{kSid0}}, kNhgSid0);
      addEncapRoute<folly::CIDRNetworkV4>(
          {folly::IPAddressV4("200.0.0.0"), 24},
          {{kSid1}, {kSid2}},
          kNhgSid1OrSid2);
      addEncapRoute<folly::CIDRNetworkV4>(
          {folly::IPAddressV4("201.0.0.0"), 24},
          {{kSid1}, {kSid2}},
          kNhgSid1OrSid2);
    }
  }

  // Programs recursive SRv6 routes for testing SID list override:
  //   OpenR route A (2901::/48) -> nhop(0), nhop(1), each carrying kSid2
  //   OpenR route B (2902::/48) -> nhop(2), nhop(3), each carrying kSid2
  //   SRv6 route (routePrefix/kEncapRoutePrefixLen) -> 2901::1 (kSid0),
  //     2902::1 (kSid1), resolving recursively through the OpenR routes.
  // After resolution, the SRv6 route expands to 4 next hops carrying the
  // outer SID lists (kSid0/kSid1), which override the inner OpenR SID list
  // (kSid2).
  void addRecursiveSrv6Routes(const folly::IPAddressV6& routePrefix) {
    auto ecmpHelper = makeEcmpHelper();
    auto routeUpdater = this->getSw()->getRouteUpdater();

    // Helper to get link-local IP for IPv6 next hops
    auto getNhopIp = [&ecmpHelper](int idx) {
      auto nhop = ecmpHelper.nhop(idx);
      if (nhop.linkLocalNhopIp.has_value()) {
        return folly::IPAddress(nhop.linkLocalNhopIp.value());
      }
      return folly::IPAddress(nhop.ip);
    };

    // Inner OpenR routes carry their own SID list (kSid2) so that recursive
    // resolution is exercised against a non-empty inner SID list; the outer
    // SRv6 route's SIDs (kSid0/kSid1) must override it.
    auto makeSidCarryingNhop = [](const folly::IPAddress& ip,
                                  InterfaceID intf) {
      return ResolvedNextHop(
          ip,
          intf,
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::vector<folly::IPAddressV6>{kSid2},
          TunnelType::SRV6_ENCAP,
          std::string("srv6Tunnel0"));
    };

    // OpenR route A (2901::/48) -> nhop(0), nhop(1), link-local nexthops
    // carrying kSid2
    RouteNextHopSet openrNhopsA{
        makeSidCarryingNhop(getNhopIp(0), ecmpHelper.nhop(0).intf),
        makeSidCarryingNhop(getNhopIp(1), ecmpHelper.nhop(1).intf)};
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2901::"),
        48,
        ClientID::OPENR,
        RouteNextHopEntry(openrNhopsA, AdminDistance::OPENR));

    // OpenR route B (2902::/48) -> nhop(2), nhop(3), link-local nexthops
    // carrying kSid2
    RouteNextHopSet openrNhopsB{
        makeSidCarryingNhop(getNhopIp(2), ecmpHelper.nhop(0).intf),
        makeSidCarryingNhop(getNhopIp(3), ecmpHelper.nhop(1).intf)};
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2902::"),
        48,
        ClientID::OPENR,
        RouteNextHopEntry(openrNhopsB, AdminDistance::OPENR));

    // SRv6 route -> 2901::1 (kSid0), 2902::1 (kSid1)
    // These unresolved nexthops carry SRV6 fields and resolve recursively
    // over the OpenR routes above, whose link-local nexthops carry kSid2
    // (overridden by the outer kSid0/kSid1 after resolution).
    RouteNextHopSet srv6Nhops{
        UnresolvedNextHop(
            folly::IPAddress("2901::1"),
            ECMP_WEIGHT,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::vector<folly::IPAddressV6>{kSid0},
            TunnelType::SRV6_ENCAP,
            std::string("srv6Tunnel0")),
        UnresolvedNextHop(
            folly::IPAddress("2902::1"),
            ECMP_WEIGHT,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::vector<folly::IPAddressV6>{kSid1},
            TunnelType::SRV6_ENCAP,
            std::string("srv6Tunnel0"))};
    routeUpdater.addRoute(
        RouterID(0),
        routePrefix,
        kEncapRoutePrefixLen,
        ClientID::TE_AGENT,
        RouteNextHopEntry(srv6Nhops, AdminDistance::TE_AGENT));
    routeUpdater.program();
  }

  void programRecursiveOpenrRoutes() {
    auto ecmpHelper = makeEcmpHelper();
    auto routeUpdater = this->getSw()->getRouteUpdater();

    auto getNhopIp = [&ecmpHelper](int idx) {
      auto nhop = ecmpHelper.nhop(idx);
      if (nhop.linkLocalNhopIp.has_value()) {
        return folly::IPAddress(nhop.linkLocalNhopIp.value());
      }
      return folly::IPAddress(nhop.ip);
    };

    RouteNextHopSet openrNhopsA{
        ResolvedNextHop(getNhopIp(0), ecmpHelper.nhop(0).intf, ECMP_WEIGHT),
        ResolvedNextHop(getNhopIp(1), ecmpHelper.nhop(1).intf, ECMP_WEIGHT)};
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2901::"),
        48,
        ClientID::OPENR,
        RouteNextHopEntry(openrNhopsA, AdminDistance::OPENR));

    RouteNextHopSet openrNhopsB{
        ResolvedNextHop(getNhopIp(2), ecmpHelper.nhop(2).intf, ECMP_WEIGHT),
        ResolvedNextHop(getNhopIp(3), ecmpHelper.nhop(3).intf, ECMP_WEIGHT)};
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2902::"),
        48,
        ClientID::OPENR,
        RouteNextHopEntry(openrNhopsB, AdminDistance::OPENR));

    routeUpdater.program();
  }

  void removeRecursiveOpenrRoutes() {
    auto routeUpdater = this->getSw()->getRouteUpdater();
    routeUpdater.delRoute(
        RouterID(0), folly::IPAddressV6("2901::"), 48, ClientID::OPENR);
    routeUpdater.delRoute(
        RouterID(0), folly::IPAddressV6("2902::"), 48, ClientID::OPENR);
    routeUpdater.program();
  }

  void addRecursiveSrv6RoutesWithV4(
      const folly::IPAddressV6& v6Prefix,
      uint8_t v6PrefixLen,
      const folly::IPAddressV4& v4Prefix,
      uint8_t v4PrefixLen) {
    auto ecmpHelper = makeEcmpHelper();
    auto routeUpdater = this->getSw()->getRouteUpdater();

    auto getNhopIp = [&ecmpHelper](int idx) {
      auto nhop = ecmpHelper.nhop(idx);
      if (nhop.linkLocalNhopIp.has_value()) {
        return folly::IPAddress(nhop.linkLocalNhopIp.value());
      }
      return folly::IPAddress(nhop.ip);
    };

    RouteNextHopSet openrNhopsA{
        ResolvedNextHop(getNhopIp(0), ecmpHelper.nhop(0).intf, ECMP_WEIGHT),
        ResolvedNextHop(getNhopIp(1), ecmpHelper.nhop(1).intf, ECMP_WEIGHT)};
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2901::"),
        48,
        ClientID::OPENR,
        RouteNextHopEntry(openrNhopsA, AdminDistance::OPENR));

    RouteNextHopSet openrNhopsB{
        ResolvedNextHop(getNhopIp(2), ecmpHelper.nhop(2).intf, ECMP_WEIGHT),
        ResolvedNextHop(getNhopIp(3), ecmpHelper.nhop(3).intf, ECMP_WEIGHT)};
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2902::"),
        48,
        ClientID::OPENR,
        RouteNextHopEntry(openrNhopsB, AdminDistance::OPENR));

    RouteNextHopSet srv6Nhops{
        UnresolvedNextHop(
            folly::IPAddress("2901::1"),
            ECMP_WEIGHT,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::vector<folly::IPAddressV6>{kSid0},
            TunnelType::SRV6_ENCAP,
            std::string("srv6Tunnel0")),
        UnresolvedNextHop(
            folly::IPAddress("2902::1"),
            ECMP_WEIGHT,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::vector<folly::IPAddressV6>{kSid1},
            TunnelType::SRV6_ENCAP,
            std::string("srv6Tunnel0"))};
    routeUpdater.addRoute(
        RouterID(0),
        v6Prefix,
        v6PrefixLen,
        ClientID::TE_AGENT,
        RouteNextHopEntry(srv6Nhops, AdminDistance::TE_AGENT));
    routeUpdater.addRoute(
        RouterID(0),
        v4Prefix,
        v4PrefixLen,
        ClientID::TE_AGENT,
        RouteNextHopEntry(srv6Nhops, AdminDistance::TE_AGENT));
    routeUpdater.program();
  }

  void addRecursiveSrv6RoutesSameSidListSameRif() {
    utility::EcmpSetupTargetedPorts<folly::IPAddressV6> ecmpHelper(
        this->getProgrammedState(), this->getSw()->needL2EntryForNeighbor());

    auto makeLinkLocalNhop = [](utility::EcmpNextHop<folly::IPAddressV6> nhop,
                                const folly::IPAddressV6& linkLocalIp) {
      nhop.linkLocalNhopIp = linkLocalIp;
      return nhop;
    };

    const auto rif0Nhop = ecmpHelper.getNextHops()[0];
    const folly::IPAddressV6 linkLocalIp0{"fe80:face:b11c::1"};
    const folly::IPAddressV6 linkLocalIp1{"fe80:face:b11c::2"};
    const auto rif0Ip0 = makeLinkLocalNhop(rif0Nhop, linkLocalIp0);
    const auto rif0Ip1 = makeLinkLocalNhop(rif0Nhop, linkLocalIp1);

    this->applyNewState(
        [&ecmpHelper, &rif0Ip0, &rif0Ip1](
            const std::shared_ptr<SwitchState> state) {
          auto newState = ecmpHelper.resolveNextHop(
              state, rif0Ip0, true /* useLinkLocal */);
          return ecmpHelper.resolveNextHop(
              newState, rif0Ip1, true /* useLinkLocal */);
        },
        "resolve recursive SRv6 link-local next hops");

    auto makeResolvedNhop = [](const auto& nhop) {
      return ResolvedNextHop(
          folly::IPAddress(nhop.linkLocalNhopIp.value()),
          nhop.intf,
          ECMP_WEIGHT);
    };

    auto routeUpdater = this->getSw()->getRouteUpdater();
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2901::"),
        48,
        ClientID::OPENR,
        RouteNextHopEntry(
            RouteNextHopSet{
                makeResolvedNhop(rif0Ip0), makeResolvedNhop(rif0Ip1)},
            AdminDistance::OPENR));
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2902::"),
        48,
        ClientID::OPENR,
        RouteNextHopEntry(
            RouteNextHopSet{
                makeResolvedNhop(rif0Ip0), makeResolvedNhop(rif0Ip1)},
            AdminDistance::OPENR));

    auto makeSrv6Nhop = [this](const folly::IPAddress& ip) {
      return UnresolvedNextHop(
          ip,
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::vector<folly::IPAddressV6>{kSid0},
          TunnelType::SRV6_ENCAP,
          std::string("srv6Tunnel0"));
    };

    routeUpdater.addRoute(
        RouterID(0),
        kEncapRoutePrefix,
        kEncapRoutePrefixLen,
        ClientID::TE_AGENT,
        RouteNextHopEntry(
            RouteNextHopSet{makeSrv6Nhop(folly::IPAddress("2901::1"))},
            AdminDistance::TE_AGENT));
    routeUpdater.addRoute(
        RouterID(0),
        folly::IPAddressV6("2800:3::"),
        kEncapRoutePrefixLen,
        ClientID::TE_AGENT,
        RouteNextHopEntry(
            RouteNextHopSet{makeSrv6Nhop(folly::IPAddress("2902::1"))},
            AdminDistance::TE_AGENT));
    routeUpdater.program();
  }

  // Programs the SAME prefix (kEncapRoutePrefix) from two clients to exercise
  // admin-distance preference: OpenR (plain link-local nexthop, no SID list)
  // and TE_Agent (link-local nexthop carrying kSid0). TE_Agent has the lower
  // admin distance, so it wins and the route is SRv6-encapped with kSid0.
  void addTeAgentPreferredOverOpenrRoute() {
    auto ecmpHelper = makeEcmpHelper();
    auto getNhopIp = [&ecmpHelper](int idx) {
      auto nhop = ecmpHelper.nhop(idx);
      if (nhop.linkLocalNhopIp.has_value()) {
        return folly::IPAddress(nhop.linkLocalNhopIp.value());
      }
      return folly::IPAddress(nhop.ip);
    };
    auto routeUpdater = this->getSw()->getRouteUpdater();

    // OpenR route: plain link-local nexthop, no SID list (higher admin
    // distance, should lose).
    routeUpdater.addRoute(
        RouterID(0),
        kEncapRoutePrefix,
        kEncapRoutePrefixLen,
        ClientID::OPENR,
        RouteNextHopEntry(
            RouteNextHopSet{ResolvedNextHop(
                getNhopIp(0), ecmpHelper.nhop(0).intf, ECMP_WEIGHT)},
            AdminDistance::OPENR));

    // TE_Agent route: link-local nexthop carrying kSid0 (lower admin distance,
    // should win and drive the SRv6 encap).
    routeUpdater.addRoute(
        RouterID(0),
        kEncapRoutePrefix,
        kEncapRoutePrefixLen,
        ClientID::TE_AGENT,
        RouteNextHopEntry(
            RouteNextHopSet{ResolvedNextHop(
                getNhopIp(0),
                ecmpHelper.nhop(0).intf,
                ECMP_WEIGHT,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::vector<folly::IPAddressV6>{kSid0},
                TunnelType::SRV6_ENCAP,
                std::string("srv6Tunnel0"))},
            AdminDistance::TE_AGENT));
    routeUpdater.program();
  }

  // Production recursive SRv6 topology:
  //   OpenR    programs kChildPrefix -> kNumNextHops nhops, NO SID list.
  //   TE_Agent programs kChildPrefix -> the SAME nhops, WITH kSid0 (wins by
  //            admin distance), counted by kChildRouteCounter.
  //   BGP      programs the parent kEncapRoutePrefix -> a next hop inside
  //            kChildPrefix, resolving recursively through the child, counted
  //            by (inherited) kChildRouterCounter
  // The parent inherits the child's SID list, so both prefixes egress the same
  // SID-list next hops.
  void addProductionRecursiveSrv6Routes() {
    auto ecmpHelper = makeEcmpHelper();
    auto getNhopIp = [&ecmpHelper](int idx) {
      auto nhop = ecmpHelper.nhop(idx);
      if (nhop.linkLocalNhopIp.has_value()) {
        return folly::IPAddress(nhop.linkLocalNhopIp.value());
      }
      return folly::IPAddress(nhop.ip);
    };
    auto routeUpdater = this->getSw()->getRouteUpdater();

    // (a) OpenR child: same nhops, no SID list (loses on admin distance).
    RouteNextHopSet openrNhops;
    for (int i = 0; i < kNumNextHops; ++i) {
      openrNhops.insert(
          ResolvedNextHop(getNhopIp(i), ecmpHelper.nhop(i).intf, ECMP_WEIGHT));
    }
    routeUpdater.addRoute(
        RouterID(0),
        kChildPrefix,
        kChildPrefixLen,
        ClientID::OPENR,
        RouteNextHopEntry(openrNhops, AdminDistance::OPENR));

    // (b) TE_Agent child: the same nhops carrying kSid0 (wins on admin
    // distance).
    RouteNextHopSet teAgentNhops;
    for (int i = 0; i < kNumNextHops; ++i) {
      teAgentNhops.insert(ResolvedNextHop(
          getNhopIp(i),
          ecmpHelper.nhop(i).intf,
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::vector<folly::IPAddressV6>{kSid0},
          TunnelType::SRV6_ENCAP,
          std::string("srv6Tunnel0")));
    }
    auto rib = this->getSw()->getRib();
    rib->addOrUpdateNamedNextHopGroups(
        this->getSw()->getScopeResolver(),
        {{kChildRouteCounter, teAgentNhops}},
        createRibToSwitchStateFunction(),
        this->getSw());

    UnicastRoute teAgentRoute;
    teAgentRoute.dest()->ip() =
        facebook::network::toBinaryAddress(kChildPrefix);
    teAgentRoute.dest()->prefixLength() = kChildPrefixLen;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup_ref() = kChildRouteCounter;
    teAgentRoute.namedRouteDestination() = namedDest;
    teAgentRoute.counterID() = kChildRouteCounter;
    routeUpdater.addRoute(RouterID(0), ClientID::TE_AGENT, teAgentRoute);

    // (c) BGP parent: next hop is inside kChildPrefix, resolving recursively
    //     through the child (and inheriting its SID list).
    routeUpdater.addRoute(
        RouterID(0),
        kEncapRoutePrefix,
        kEncapRoutePrefixLen,
        ClientID::BGPD,
        RouteNextHopEntry(
            RouteNextHopSet{UnresolvedNextHop(kBgpRecursiveNhop, ECMP_WEIGHT)},
            AdminDistance::EBGP));

    routeUpdater.program();
  }

  // Deletes one client's entry for kChildPrefix, leaving any other client's
  // entry for the same prefix in place.
  void delChildRoute(ClientID clientID) {
    auto routeUpdater = this->getSw()->getRouteUpdater();
    routeUpdater.delRoute(RouterID(0), kChildPrefix, kChildPrefixLen, clientID);
    routeUpdater.program();
  }

  template <typename CIDRNetworkT>
  void addEncapRoute(
      const CIDRNetworkT& prefix,
      const std::vector<std::vector<folly::IPAddressV6>>& sidLists,
      const std::string& nhgName) {
    RouteNextHopSet nhops;
    // Always use ipv6 link local nhops since that;s the prod
    // use case
    auto ecmpHelper = makeEcmpHelper<folly::IPAddressV6>();
    for (auto i = 0; i < sidLists.size(); ++i) {
      auto nhop = ecmpHelper.nhop(i);
      CHECK(nhop.linkLocalNhopIp.has_value());
      nhops.insert(ResolvedNextHop(
          folly::IPAddress(*nhop.linkLocalNhopIp),
          nhop.intf,
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          sidLists[i],
          TunnelType::SRV6_ENCAP,
          std::string("srv6Tunnel0")));
    }

    auto rib = this->getSw()->getRib();
    std::vector<std::pair<std::string, RouteNextHopSet>> groups;
    groups.emplace_back(nhgName, nhops);
    rib->addOrUpdateNamedNextHopGroups(
        this->getSw()->getScopeResolver(),
        groups,
        createRibToSwitchStateFunction(),
        this->getSw());

    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress(prefix.first));
    route.dest()->prefixLength() = prefix.second;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup_ref() = nhgName;
    route.namedRouteDestination() = namedDest;
    route.counterID() = nhgName;

    auto routeUpdater = this->getSw()->getRouteUpdater();
    routeUpdater.addRoute(RouterID(0), ClientID::TE_AGENT, route);
    routeUpdater.program();
  }

  PortID getEgressPort(const PortDescriptor& portDesc) const {
    if (portDesc.isPhysicalPort()) {
      return portDesc.phyPortID();
    }
    auto aggPort = this->getProgrammedState()->getAggregatePorts()->getNodeIf(
        portDesc.aggPortID());
    return aggPort->sortedSubports().front().portID;
  }

  void verifyEncapPacket(
      const std::vector<PortID>& egressPorts,
      bool ecnMarked,
      bool isV4 = false,
      const std::vector<folly::IPAddressV6>& expectedSids = {kSid0},
      std::optional<PortID> injectPort = std::nullopt,
      std::optional<folly::IPAddress> dstIp = std::nullopt,
      const std::string& counterID = "") {
    const auto& sids = expectedSids;

    std::map<PortID, int64_t> bytesBefore;
    for (auto port : egressPorts) {
      bytesBefore[port] = *this->getLatestPortStats(port).outBytes_();
    }

    int64_t counterBytesBefore = 0;
    int64_t counterPacketsBefore = 0;
    if (!counterID.empty()) {
      auto hwSwitchStats = this->getHwSwitchStats();
      const auto statsTimestampBefore = hwSwitchStats.timestamp().value();
      WITH_RETRIES({
        hwSwitchStats = this->getHwSwitchStats();
        EXPECT_EVENTUALLY_GT(
            hwSwitchStats.timestamp().value(), statsTimestampBefore);
        const auto& routeCounters =
            *hwSwitchStats.counterStats()->routeCounters();
        EXPECT_EVENTUALLY_TRUE(
            routeCounters.find(counterID) != routeCounters.end());
      });
      auto& routeCounters = *hwSwitchStats.counterStats()->routeCounters();
      auto it = routeCounters.find(counterID);
      ASSERT_NE(it, routeCounters.end())
          << "Route counter " << counterID << " not found in fresh stats";
      counterBytesBefore = it->second.bytes().value_or(0);
      counterPacketsBefore = it->second.packets().value_or(0);
    }

    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(this->getProgrammedState());
    constexpr auto kTc{42};
    constexpr auto kTtl{24};
    auto tcField = ecnMarked ? static_cast<uint8_t>((kTc << 2) | 0x3)
                             : static_cast<uint8_t>(kTc << 2);
    auto srcIp =
        isV4 ? folly::IPAddress("10.0.0.1") : folly::IPAddress("1::10");
    auto pktDstIp = dstIp.has_value()
        ? dstIp.value()
        : (isV4 ? folly::IPAddress("100.0.0.1")
                : folly::IPAddress(kEncapRouteDstIp));

    XLOG(DBG2) << " Verifying : v4: " << isV4 << " with counter ID: "
               << (counterID.size() ? counterID : "none")
               << " dst IP: " << pktDstIp
               << " from CPU: " << (injectPort ? "no" : "yes");
    auto txPacket = utility::makeUDPTxPacket(
        this->getSw(),
        this->getVlanIDForTx(),
        intfMac,
        intfMac,
        srcIp,
        pktDstIp,
        8000,
        8001,
        tcField,
        kTtl);

    auto origFrame = utility::makeEthFrame(*txPacket);

    utility::SwSwitchPacketSnooper snooper(this->getSw(), "srv6EncapSnooper");

    if (injectPort.has_value()) {
      this->getSw()->sendPacketOutOfPortAsync(
          std::move(txPacket), injectPort.value());
    } else {
      this->sendPacketSwitchedAsync(std::move(txPacket));
    }

    auto frameRx = snooper.waitForPacket(1);
    WITH_RETRIES({
      bool anyPortGotBytes = false;
      for (auto port : egressPorts) {
        auto bytesAfter = *this->getLatestPortStats(port).outBytes_();
        if (bytesAfter > bytesBefore[port]) {
          anyPortGotBytes = true;
        }
      }
      EXPECT_EVENTUALLY_TRUE(anyPortGotBytes);
      if (!frameRx.has_value()) {
        frameRx = snooper.waitForPacket(1);
      }
      EXPECT_EVENTUALLY_TRUE(frameRx.has_value());
    });
    ASSERT_TRUE(frameRx.has_value());
    folly::io::Cursor cursor((*frameRx).get());
    utility::EthFrame frame(cursor);
    auto ethHdr = frame.header();
    // Outer header is always IPv6 (SRv6 encap)
    EXPECT_EQ(
        ethHdr.etherType, static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6));
    auto v6Payload = frame.v6PayLoad();
    EXPECT_TRUE(v6Payload.has_value());
    auto v6Hdr = v6Payload->header();
    // Outer header dst addr should match one of the expected SIDs
    bool sidMatch = std::any_of(sids.begin(), sids.end(), [&](const auto& sid) {
      return v6Hdr.dstAddr == sid;
    });
    EXPECT_TRUE(sidMatch) << "Outer DA " << v6Hdr.dstAddr
                          << " does not match any expected SID";
    // Flow label must be non 0
    EXPECT_NE(v6Hdr.flowLabel, 0);
    EXPECT_EQ(v6Hdr.trafficClass & 0x3, ecnMarked ? 0x3 : 0);
    EXPECT_EQ(v6Hdr.trafficClass >> 2, kTc);
    // TTL is decremented
    EXPECT_EQ(v6Hdr.hopLimit, kTtl - 1);
    // Compare origPacket against inner packet
    if (isV4) {
      auto origPacket = origFrame.v4PayLoad();
      ASSERT_TRUE(origPacket.has_value());
      auto innerV4 = v6Payload->v4PayLoad();
      ASSERT_NE(innerV4, nullptr);
      // Inner packet should match origPacket. Note makeEthFrame(txPacket)
      // already does a TTL decrement by default, so we don't have to account
      // for it here.
      EXPECT_EQ(*innerV4, *origPacket);
    } else {
      auto origPacket = origFrame.v6PayLoad();
      ASSERT_TRUE(origPacket.has_value());
      // Inner packet should match origPacket. Note makeEthFrame(txPacket)
      // already does a TTL decrement by default, so we don't have to account
      // for it here.
      EXPECT_EQ(*v6Payload->v6PayLoad(), *origPacket);
    }

    if (!counterID.empty()) {
      WITH_RETRIES({
        auto hwSwitchStats = this->getHwSwitchStats();
        auto& routeCounters = *hwSwitchStats.counterStats()->routeCounters();
        auto it = routeCounters.find(counterID);
        ASSERT_EVENTUALLY_TRUE(it != routeCounters.end())
            << "Route counter " << counterID << " not found";
        EXPECT_EVENTUALLY_GT(
            it->second.bytes().value_or(0), counterBytesBefore);
        EXPECT_EVENTUALLY_EQ(
            it->second.packets().value_or(0), counterPacketsBefore + 1);
      });
    }
  }

  void verifyEncapPacketCpuAndFrontPanel(
      const std::vector<PortID>& egressPorts,
      const std::vector<folly::IPAddressV6>& expectedSids = {kSid0},
      const std::string& counterID = "") {
    auto injectPort = findInjectPort(egressPorts);
    for (bool isV4 : {false, true}) {
      // ECN not marked
      verifyEncapPacket(
          egressPorts,
          false,
          isV4,
          expectedSids,
          std::nullopt,
          std::nullopt,
          counterID);
      verifyEncapPacket(
          egressPorts,
          false,
          isV4,
          expectedSids,
          injectPort,
          std::nullopt,
          counterID);
      // ECN marked
      verifyEncapPacket(
          egressPorts,
          true,
          isV4,
          expectedSids,
          std::nullopt,
          std::nullopt,
          counterID);
      verifyEncapPacket(
          egressPorts,
          true,
          isV4,
          expectedSids,
          injectPort,
          std::nullopt,
          counterID);
    }
  }

  // Send a packet for the encap route while its only next hop is down or
  // unresolved, and expect it discarded on ingress instead of encapped out.
  // kEncapRoutePrefix / 100.0.0.0/24 carry a single sidList, so they resolve
  // to exactly one next hop (ecmpHelper.nhop(0)) and lose their only path
  // when that port goes down.
  // Front panel ingress only: a CPU-injected packet takes a different path
  // into the pipeline, so it is not the ingress-discard case under test.
  void verifyEncapRouteDrop(PortID injectPort, PortID egressPort, bool isV4) {
    // inDiscards must move and inSrv6MySidDiscards must not. inDstNullDiscards
    // is logged only, so a run still shows which counter a resolution-failure
    // drop actually lands in.
    auto statsStr = [](const auto& stats) {
      auto srv6Discards = stats.inSrv6MySidDiscards_();
      return fmt::format(
          "inDiscards={} inDstNullDiscards={} inSrv6MySidDiscards={}",
          *stats.inDiscards_(),
          *stats.inDstNullDiscards_(),
          srv6Discards.has_value() ? std::to_string(*srv6Discards) : "unset");
    };
    auto portStatsBefore = this->getLatestPortStats(injectPort);
    auto egressStatsBefore = this->getLatestPortStats(egressPort);
    auto srv6DiscardsBefore =
        portStatsBefore.inSrv6MySidDiscards_().value_or(0);
    XLOG(DBG2) << "verifyEncapRouteDrop: inner=" << (isV4 ? "v4" : "v6")
               << " ingressPort=" << injectPort
               << " before: " << statsStr(portStatsBefore);

    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(this->getProgrammedState());
    constexpr auto kTc{42};
    constexpr auto kTtl{24};
    auto srcIp =
        isV4 ? folly::IPAddress("10.0.0.1") : folly::IPAddress("1::10");
    auto dstIp = isV4 ? folly::IPAddress("100.0.0.1")
                      : folly::IPAddress(kEncapRouteDstIp);
    auto txPacket = utility::makeUDPTxPacket(
        this->getSw(),
        this->getVlanIDForTx(),
        intfMac,
        intfMac,
        srcIp,
        dstIp,
        8000,
        8001,
        static_cast<uint8_t>(kTc << 2),
        kTtl);
    this->getSw()->sendPacketOutOfPortAsync(std::move(txPacket), injectPort);

    WITH_RETRIES({
      auto portStatsAfter = this->getLatestPortStats(injectPort);
      auto egressStatsAfter = this->getLatestPortStats(egressPort);
      EXPECT_EVENTUALLY_GT(
          *portStatsAfter.inDiscards_(), *portStatsBefore.inDiscards_());
      // Nothing should make it out the (down) egress port.
      EXPECT_EVENTUALLY_EQ(
          *egressStatsAfter.outBytes_(), *egressStatsBefore.outBytes_());
    });

    auto portStatsAfter = this->getLatestPortStats(injectPort);
    XLOG(DBG2) << "verifyEncapRouteDrop: inner=" << (isV4 ? "v4" : "v6")
               << " ingressPort=" << injectPort
               << "  after: " << statsStr(portStatsAfter);

    // A resolution-failure drop is not local-SID processing, so the mysid
    // counter must stay put. Checked after the loop rather than as an
    // EXPECT_EVENTUALLY_EQ inside it: these counters are monotonic, so an
    // increment would never retry away, and the loop would burn its whole
    // timeout before failing.
    EXPECT_EQ(
        portStatsAfter.inSrv6MySidDiscards_().value_or(0), srv6DiscardsBefore);
  }

  PortID findInjectPort(const std::vector<PortID>& egressPorts) {
    for (const auto& portMap :
         std::as_const(*this->getProgrammedState()->getPorts())) {
      for (const auto& [_, port] : std::as_const(*portMap.second)) {
        if (port->isPortUp() &&
            std::find(egressPorts.begin(), egressPorts.end(), port->getID()) ==
                egressPorts.end()) {
          return port->getID();
        }
      }
    }
    throw FbossError("No UP port found besides egress ports");
  }

 private:
  void addSrv6TunnelConfig(cfg::SwitchConfig& cfg) const {
    std::vector<cfg::Srv6Tunnel> tunnelList;
    tunnelList.push_back(
        utility::makeSrv6TunnelConfig(
            "srv6Tunnel0", InterfaceID(cfg.interfaces()[0].intfID().value())));
    cfg.srv6Tunnels() = tunnelList;
  }
};

TYPED_TEST_SUITE(AgentSrv6EncapTest, Srv6EncapPortTypes);

TYPED_TEST(AgentSrv6EncapTest, sendPacketToEncapRoute) {
  auto setup = [this]() { this->setupHelper(); };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    auto egressPort = this->getEgressPort(ecmpHelper.nhop(0).portDesc);
    this->verifyEncapPacketCpuAndFrontPanel(
        {egressPort}, {this->kSid0}, this->kNhgSid0);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6EncapTest, sendPacketToEncapRouteAfterLinkFlap) {
  auto setup = [this]() {
    this->setupHelper();
    // Flap ports and re-resolve neighbors
    auto ecmpHelper = this->makeEcmpHelper();
    auto egressPort = this->getEgressPort(ecmpHelper.nhop(0).portDesc);
    this->bringDownPort(egressPort);
    this->unresolveNextHops(2);
    this->bringUpPort(egressPort);
    // For aggregate ports, the SAI fast-path link down handler disables
    // the LAG member (EgressDisable=true). Since LACP is not running in
    // tests, nobody updates the switch state to reflect this. We must
    // first set forwarding to DISABLED (to match SAI state), then back
    // to ENABLED to generate a state delta that triggers
    // SaiLagManager::changeLag() to re-enable the SAI LAG member.
    constexpr bool isTrunk = TestFixture::kIsTrunk;

    if constexpr (isTrunk) {
      this->applyNewState(
          [](const std::shared_ptr<SwitchState> state) {
            return utility::disableTrunkPorts(state);
          },
          "disable trunk ports to sync with SAI state");
      this->applyNewState(
          [](const std::shared_ptr<SwitchState> state) {
            return utility::enableTrunkPorts(state);
          },
          "re-enable trunk ports after link flap");
    }
    this->resolveNextHops(2);
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    auto egressPort = this->getEgressPort(ecmpHelper.nhop(0).portDesc);
    this->verifyEncapPacketCpuAndFrontPanel(
        {egressPort}, {this->kSid0}, "kSid0");
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6EncapTest, sendPacketToEncapRouteUnresolvedDropped) {
  auto setup = [this]() {
    this->setupHelper();
    auto ecmpHelper = this->makeEcmpHelper();
    auto egressPort = this->getEgressPort(ecmpHelper.nhop(0).portDesc);

    this->bringDownPort(egressPort);
    XLOG(DBG2) << "Brought down egress port " << egressPort;
    this->unresolveNextHops(2);
    XLOG(DBG2) << "Unresolved neighbors for the encap route next hop";
  };

  // The route's only next hop is both down and unresolved, so traffic for it
  // must be discarded on ingress rather than leaking out some other port.
  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    auto egressPort = this->getEgressPort(ecmpHelper.nhop(0).portDesc);
    auto injectPort = this->findInjectPort({egressPort});
    for (bool isV4 : {false, true}) {
      this->verifyEncapRouteDrop(injectPort, egressPort, isV4);
    }
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6EncapTest, recursiveResolutionPreservesSidList) {
  auto setup = [this]() {
    // Skip programming direct encap routes to avoid colliding
    // SRv6 managed next hop keys with recursive resolution.
    this->setupHelper(true /*resolveNeighbors*/, false /*programEncapRoutes*/);
    this->addRecursiveSrv6Routes(this->kEncapRoutePrefix);
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    // Recursive resolution expands to kNumNextHops next hops —
    // the packet may egress on any of these ports.
    std::vector<PortID> egressPorts;
    egressPorts.reserve(this->kNumNextHops);
    for (int i = 0; i < this->kNumNextHops; ++i) {
      egressPorts.push_back(this->getEgressPort(ecmpHelper.nhop(i).portDesc));
    }
    this->verifyEncapPacket(
        egressPorts,
        false /*ecnMarked*/,
        false /*isV4*/,
        {this->kSid0, this->kSid1});
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6EncapTest, recursiveResolutionSameSidListSameRif) {
  auto setup = [this]() {
    // Skip programming direct encap routes to avoid colliding
    // SRv6 managed next hop keys with recursive resolution.
    this->setupHelper(false /*resolveNeighbors*/, false /*programEncapRoutes*/);
    this->addRecursiveSrv6RoutesSameSidListSameRif();
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    auto rif0EgressPort = this->getEgressPort(ecmpHelper.nhop(0).portDesc);

    this->verifyEncapPacket(
        {rif0EgressPort}, false /*ecnMarked*/, false /*isV4*/, {this->kSid0});
    this->verifyEncapPacket(
        {rif0EgressPort},
        false /*ecnMarked*/,
        false /*isV4*/,
        {this->kSid0},
        std::nullopt,
        folly::IPAddress("2800:3::1"));
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

// Production recursive SRv6: a BGP parent resolves recursively through a child
// prefix that OpenR (no SID) and TE_Agent (kSid0) both program; TE_Agent wins,
// so both the child and the parent egress the same next hops carrying kSid0.
// Verified from the ASIC: outer DA == kSid0, egress-port bytes, and per-route
// counters increment for both prefixes.
TYPED_TEST(AgentSrv6EncapTest, recursiveBgpParentInheritsTeAgentChildSidList) {
  auto setup = [this]() {
    // Skip direct encap routes to avoid colliding SRv6 managed next hop keys
    // with recursive resolution.
    this->setupHelper(true /*resolveNeighbors*/, false /*programEncapRoutes*/);
    this->addProductionRecursiveSrv6Routes();
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    // Both child and parent expand to the same kNumNextHops next hops; the
    // packet may egress on any of these ports.
    std::vector<PortID> egressPorts;
    egressPorts.reserve(this->kNumNextHops);
    for (int i = 0; i < this->kNumNextHops; ++i) {
      egressPorts.push_back(this->getEgressPort(ecmpHelper.nhop(i).portDesc));
    }
    // 1. Child prefix egresses the SID-list next hops with kSid0.
    this->verifyEncapPacket(
        egressPorts,
        false /*ecnMarked*/,
        false /*isV4*/,
        {this->kSid0},
        std::nullopt /*injectPort*/,
        this->kChildDstIp,
        this->kChildRouteCounter);
    // 2. Parent prefix, resolved recursively through the child, egresses the
    //    same SID-list next hops with the inherited kSid0.
    this->verifyEncapPacket(
        egressPorts,
        false /*ecnMarked*/,
        false /*isV4*/,
        {this->kSid0},
        std::nullopt /*injectPort*/,
        this->kEncapRouteDstIp,
        this->kChildRouteCounter);
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6EncapTest, resolveNeighborsAfterRouteProgram) {
  auto setup = [this]() {
    this->setupHelper(false /*resolveNeighbors*/);
    // Resolve neighbors after route programming
    this->resolveNextHops(2);
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    auto egressPort = this->getEgressPort(ecmpHelper.nhop(0).portDesc);
    this->verifyEncapPacketCpuAndFrontPanel(
        {egressPort}, {this->kSid0}, "kSid0");
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6EncapTest, multipleSidListsSameNextHop) {
  auto setup = [this]() {
    this->setupHelper(true /*resolveNeighbors*/, false /*programEncapRoutes*/);

    auto ecmpHelper = this->makeEcmpHelper();
    auto nhop0 = ecmpHelper.nhop(0);
    auto nhop1 = ecmpHelper.nhop(1);
    auto rib = this->getSw()->getRib();

    auto makeNhopSet = [](const auto& nhop,
                          const std::vector<folly::IPAddressV6>& sidList) {
      // Helper to get link-local IP for IPv6 next hops
      auto getNhopIp = [](const auto& nhop) {
        if (nhop.linkLocalNhopIp.has_value()) {
          return nhop.linkLocalNhopIp.value();
        }
        return nhop.ip;
      };
      RouteNextHopSet nhops;
      nhops.insert(ResolvedNextHop(
          getNhopIp(nhop),
          nhop.intf,
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          sidList,
          TunnelType::SRV6_ENCAP,
          std::string("srv6Tunnel0")));
      return nhops;
    };

    // Phase 1: Two routes with different named NHGs both via nhop(0)
    // NHG A: nhop(0) with kSid0
    // NHG B: nhop(0) with kSid1
    {
      std::vector<std::pair<std::string, RouteNextHopSet>> groups;
      groups.emplace_back("nhg-sid0", makeNhopSet(nhop0, {this->kSid0}));
      groups.emplace_back("nhg-sid1", makeNhopSet(nhop0, {this->kSid1}));
      rib->addOrUpdateNamedNextHopGroups(
          this->getSw()->getScopeResolver(),
          groups,
          createRibToSwitchStateFunction(),
          this->getSw());

      auto routeUpdater = this->getSw()->getRouteUpdater();

      UnicastRoute routeA;
      routeA.dest()->ip() = facebook::network::toBinaryAddress(
          folly::IPAddress(this->kEncapRoutePrefix));
      routeA.dest()->prefixLength() = this->kEncapRoutePrefixLen;
      NamedRouteDestination namedDestA;
      namedDestA.nextHopGroup_ref() = "nhg-sid0";
      routeA.namedRouteDestination() = namedDestA;
      routeA.counterID() = "nhg-sid0";
      routeUpdater.addRoute(RouterID(0), ClientID::TE_AGENT, routeA);

      UnicastRoute routeB;
      routeB.dest()->ip() = facebook::network::toBinaryAddress(
          folly::IPAddress(folly::IPAddressV6("2800:3::")));
      routeB.dest()->prefixLength() = this->kEncapRoutePrefixLen;
      NamedRouteDestination namedDestB;
      namedDestB.nextHopGroup_ref() = "nhg-sid1";
      routeB.namedRouteDestination() = namedDestB;
      routeB.counterID() = "nhg-sid1";
      routeUpdater.addRoute(RouterID(0), ClientID::TE_AGENT, routeB);

      routeUpdater.program();
    }

    // Verify both routes encap correctly while sharing nhop(0)
    auto egressPort = this->getEgressPort(ecmpHelper.nhop(0).portDesc);
    this->verifyEncapPacket(
        {egressPort},
        false /*ecnMarked*/,
        false /*isV4*/,
        {this->kSid0},
        std::nullopt,
        std::nullopt,
        "nhg-sid0");
    this->verifyEncapPacket(
        {egressPort},
        false /*ecnMarked*/,
        false /*isV4*/,
        {this->kSid1},
        std::nullopt /*injectPort*/,
        folly::IPAddress("2800:3::1"),
        "nhg-sid1");

    // Phase 2: Update NHG B to use nhop(1) instead of nhop(0)
    {
      std::vector<std::pair<std::string, RouteNextHopSet>> updatedGroups;
      updatedGroups.emplace_back("nhg-sid1", makeNhopSet(nhop1, {this->kSid1}));
      rib->addOrUpdateNamedNextHopGroups(
          this->getSw()->getScopeResolver(),
          updatedGroups,
          createRibToSwitchStateFunction(),
          this->getSw());
    }
  };

  // After warmboot, final state:
  //   Route A: kEncapRoutePrefix/64 -> nhop(0) with kSid0
  //   Route B: 2800:3::/64 -> nhop(1) with kSid1
  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    auto egressPort0 = this->getEgressPort(ecmpHelper.nhop(0).portDesc);
    auto egressPort1 = this->getEgressPort(ecmpHelper.nhop(1).portDesc);

    // Route A encaps with kSid0 via nhop(0)
    this->verifyEncapPacket(
        {egressPort0},
        false /*ecnMarked*/,
        false /*isV4*/,
        {this->kSid0},
        std::nullopt,
        std::nullopt,
        "nhg-sid0");

    // Route B encaps with kSid1 via nhop(1)
    this->verifyEncapPacket(
        {egressPort1},
        false /*ecnMarked*/,
        false /*isV4*/,
        {this->kSid1},
        std::nullopt /*injectPort*/,
        folly::IPAddress("2800:3::1"),
        "nhg-sid1");
  };
  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6EncapTest, verifySrv6EncapEcnMarking) {
  auto setup = [this]() {
    if constexpr (TestFixture::kIsTrunk) {
      this->applyConfigAndEnableTrunks(
          this->initialConfig(*this->getAgentEnsemble()));
    }
    this->resolveNextHops(2);
    this->template addEncapRoute<folly::CIDRNetworkV6>(
        {this->kEncapRoutePrefix, this->kEncapRoutePrefixLen},
        {{this->kSid0}},
        this->kNhgSid0);
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    auto egressPort = this->getEgressPort(ecmpHelper.nhop(0).portDesc);
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(this->getProgrammedState());

    // Send 512 SRv6 packets (DSCP 5, ECN ECT1, ~7000B payload).
    // In UNIFORM mode, outer header copies DSCP+ECN bits from inner.
    auto sendFloodPackets = [this, &intfMac]() {
      auto tcField = static_cast<uint8_t>((5 << 2) | this->kECT1);
      for (int i = 0; i < 512; ++i) {
        auto txPacket = utility::makeUDPTxPacket(
            this->getSw(),
            this->getVlanIDForTx(),
            intfMac,
            intfMac,
            folly::IPAddressV6("1::10"),
            this->kEncapRouteDstIp,
            8000,
            8001,
            tcField,
            64,
            std::vector<uint8_t>(7000, 0xff));
        this->sendPacketSwitchedAsync(std::move(txPacket));
      }
    };

    // After encap, outer dst = SID, ECN CE = 0x3
    auto isEcnMarked = [this](
                           const folly::IPAddressV6& dstAddr, uint8_t ecnBits) {
      return dstAddr == this->kSid0 && ecnBits == 0x3;
    };

    utility::verifySrv6EcnMarking(
        this->getAgentEnsemble(), egressPort, sendFloodPackets, isEcnMarked);
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

// Verify that SRv6-encapsulated packets are placed in the correct egress
// queue based on the inner packet's DSCP. In UNIFORM mode, inner DSCP is
// copied to the outer header, so the egress queue is determined by the
// inner DSCP value.
TYPED_TEST(AgentSrv6EncapTest, VerifyDscpQueueMapping) {
  auto setup = [this]() {
    if constexpr (TestFixture::kIsTrunk) {
      this->applyConfigAndEnableTrunks(
          this->initialConfig(*this->getAgentEnsemble()));
    }
    this->resolveNextHops(this->kNumNextHops);
    this->template addEncapRoute<folly::CIDRNetworkV6>(
        {this->kEncapRoutePrefix, this->kEncapRoutePrefixLen},
        {{this->kSid0}},
        this->kNhgSid0);
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    auto egressPort = this->getEgressPort(ecmpHelper.nhop(0).portDesc);
    auto injectPort = this->findInjectPort({egressPort});
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(this->getProgrammedState());

    auto sendPacket = [this, &intfMac](int dscp, bool frontPanel, PortID port) {
      auto txPacket = utility::makeUDPTxPacket(
          this->getSw(),
          this->getVlanIDForTx(),
          intfMac,
          intfMac,
          folly::IPAddressV6("1::10"),
          this->kEncapRouteDstIp,
          8000,
          8001,
          dscp << 2,
          255);
      if (frontPanel) {
        this->getSw()->sendPacketOutOfPortAsync(std::move(txPacket), port);
      } else {
        this->sendPacketSwitchedAsync(std::move(txPacket));
      }
    };

    // Send all packets first, then verify queue counters
    for (bool frontPanel : {false, true}) {
      auto portStatsBefore = this->getLatestPortStats(egressPort);
      for (const auto& [queue, dscps] : utility::kOlympicQueueToDscp()) {
        for (auto dscp : dscps) {
          sendPacket(dscp, frontPanel, injectPort);
        }
      }
      for (const auto& [queue, dscps] : utility::kOlympicQueueToDscp()) {
        utility::verifyQueueHit(
            portStatsBefore, queue, this->getSw(), egressPort, dscps.size());
      }
    }
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

TYPED_TEST(AgentSrv6EncapTest, multiHopUnresolvedToResolved) {
  auto setup = [this]() {
    // Start with neighbors unresolved and no encap routes.
    // Program OpenR routes + v6/v4 SRv6 encap routes atomically.
    // The SRv6 routes have unresolved next hops (2901::1, 2902::1) that
    // resolve recursively through the OpenR routes (2901::/48, 2902::/48).
    this->setupHelper(false /*resolveNeighbors*/, false /*programEncapRoutes*/);
    this->addRecursiveSrv6RoutesWithV4(
        this->kEncapRoutePrefix,
        this->kEncapRoutePrefixLen,
        this->kRecursiveV4Prefix,
        this->kRecursiveV4PrefixLen);

    // Resolve neighbors to complete the forwarding path.
    this->resolveNextHops(this->kNumNextHops);
  };

  auto verify = [this]() {
    auto ecmpHelper = this->makeEcmpHelper();
    std::vector<PortID> egressPorts;
    egressPorts.reserve(this->kNumNextHops);
    for (int i = 0; i < this->kNumNextHops; ++i) {
      egressPorts.push_back(this->getEgressPort(ecmpHelper.nhop(i).portDesc));
    }

    XLOG(DBG2) << "multiHopUnresolvedToResolved: verifying initial forwarding";
    this->verifyEncapPacketCpuAndFrontPanel(
        egressPorts, {this->kSid0, this->kSid1});

    XLOG(DBG2)
        << "multiHopUnresolvedToResolved: iteration 1 — unresolve neighbors, then remove OpenR routes";
    this->unresolveNextHops(this->kNumNextHops);
    this->removeRecursiveOpenrRoutes();

    XLOG(DBG2)
        << "multiHopUnresolvedToResolved: iteration 1 — re-add OpenR routes and re-resolve";
    this->programRecursiveOpenrRoutes();
    this->resolveNextHops(this->kNumNextHops);

    XLOG(DBG2)
        << "multiHopUnresolvedToResolved: iteration 1 — verifying forwarding recovered";
    this->verifyEncapPacketCpuAndFrontPanel(
        egressPorts, {this->kSid0, this->kSid1});

    XLOG(DBG2)
        << "multiHopUnresolvedToResolved: iteration 2 — remove OpenR routes, then unresolve neighbors";
    this->removeRecursiveOpenrRoutes();
    this->unresolveNextHops(this->kNumNextHops);

    XLOG(DBG2)
        << "multiHopUnresolvedToResolved: iteration 2 — re-add OpenR routes and re-resolve";
    this->programRecursiveOpenrRoutes();
    this->resolveNextHops(this->kNumNextHops);

    XLOG(DBG2)
        << "multiHopUnresolvedToResolved: iteration 2 — verifying forwarding recovered";
    this->verifyEncapPacketCpuAndFrontPanel(
        egressPorts, {this->kSid0, this->kSid1});
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

// A counted route that loses its last next hop must detach its counter in
// hardware before the agent drops its reference to it.
TYPED_TEST(AgentSrv6EncapTest, openRAndTeAgentRoutesChurn) {
  // Skip direct encap routes to avoid colliding SRv6 managed next hop keys
  // with recursive resolution, matching the other recursive tests.
  this->setupHelper(true /*resolveNeighbors*/, false /*programEncapRoutes*/);
  this->addProductionRecursiveSrv6Routes();

  auto ecmpHelper = this->makeEcmpHelper();
  std::vector<PortID> egressPorts;
  egressPorts.reserve(this->kNumNextHops);
  for (int i = 0; i < this->kNumNextHops; ++i) {
    egressPorts.push_back(this->getEgressPort(ecmpHelper.nhop(i).portDesc));
  }

  // The parent forwards over the child's SID-list next hops and counts into
  // the counter it inherited from the child.
  this->verifyEncapPacket(
      egressPorts,
      false /*ecnMarked*/,
      false /*isV4*/,
      {this->kSid0},
      std::nullopt /*injectPort*/,
      this->kEncapRouteDstIp,
      this->kChildRouteCounter);

  // Remove both child entries, so kChildPrefix goes away entirely and the
  // parent falls back to ::/0. On the unfixed agent the hw agent aborts here.
  this->delChildRoute(ClientID::OPENR);
  this->delChildRoute(ClientID::TE_AGENT);

  // The parent is now a drop route. Checked on inDstNullDiscards rather than
  // the generic inDiscards that verifyEncapRouteDrop uses: the parent falls
  // back to ::/0, so this must land as a null-route drop specifically, and
  // inDiscards would also be satisfied by an unrelated ingress drop.
  auto injectPort = this->findInjectPort(egressPorts);
  auto nullDiscardsBefore =
      *this->getLatestPortStats(injectPort).inDstNullDiscards_();
  std::map<PortID, int64_t> egressBytesBefore;
  for (auto port : egressPorts) {
    egressBytesBefore[port] = *this->getLatestPortStats(port).outBytes_();
  }

  constexpr auto kTc{42};
  constexpr auto kTtl{24};
  auto intfMac =
      getMacForFirstInterfaceWithPortsForTesting(this->getProgrammedState());
  this->getSw()->sendPacketOutOfPortAsync(
      utility::makeUDPTxPacket(
          this->getSw(),
          this->getVlanIDForTx(),
          intfMac,
          intfMac,
          folly::IPAddress("1::10"),
          folly::IPAddress(this->kEncapRouteDstIp),
          8000,
          8001,
          static_cast<uint8_t>(kTc << 2),
          kTtl),
      injectPort);

  WITH_RETRIES({
    EXPECT_EVENTUALLY_GT(
        *this->getLatestPortStats(injectPort).inDstNullDiscards_(),
        nullDiscardsBefore);
  });
  // And nothing leaked out of the next hops the parent used to egress on.
  for (auto port : egressPorts) {
    EXPECT_EQ(
        *this->getLatestPortStats(port).outBytes_(), egressBytesBefore[port]);
  }

  // Put the child back. The parent re-resolves, re-inherits the counter and
  // counts again -- which it cannot do if the counter was stranded in
  // hardware by the detach that never happened.
  this->addProductionRecursiveSrv6Routes();
  this->verifyEncapPacket(
      egressPorts,
      false /*ecnMarked*/,
      false /*isV4*/,
      {this->kSid0},
      std::nullopt /*injectPort*/,
      this->kEncapRouteDstIp,
      this->kChildRouteCounter);
}

} // namespace facebook::fboss
