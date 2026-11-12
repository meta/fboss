/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fmt/core.h>
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/MatchAction.h"
#include "fboss/agent/state/PbrUtils.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/Srv6TestUtils.h"

DECLARE_bool(enable_nexthop_id_manager);

namespace facebook::fboss {

namespace {
constexpr uint8_t kDscp = 10;
constexpr uint8_t kDscp2 = 20;
constexpr uint8_t kDscp3 = 30;
// DSCP left unmapped -> internal traffic class 0, which the PBR ACL (TC 1) does
// not match, so traffic follows the route to the default NHG.
constexpr uint8_t kNoMatchDscp = 0;
constexpr auto kSrv6TunnelId = "srv6Tunnel0";
constexpr uint16_t kL4SrcPort = 10000;
constexpr uint16_t kL4DstPort = 20000;
constexpr int kNumPkts = 10;
constexpr uint64_t kVariance = 2;
constexpr auto kPbrRoutePrefix = "2222::";
constexpr auto kPbrDstIp = "2222::1";
constexpr auto kPbrSrcIp = "2001::1";

// Keyed off the physical port so a next hop keeps its SID across warm boot and
// across the member-change sequence.
std::vector<folly::IPAddressV6> makeSidList(PortID port) {
  auto index = static_cast<int>(port);
  return {folly::IPAddressV6(
      fmt::format("3001:db8:{:x}:{:x}::", (index >> 8) + 1, index & 0xFF))};
}

template <typename NextHopT, typename... Args>
NextHopT makeSrv6NextHop(PortID port, Args&&... args) {
  return NextHopT(
      std::forward<Args>(args)...,
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      makeSidList(port),
      TunnelType::SRV6_ENCAP,
      std::string(kSrv6TunnelId));
}
} // namespace

class AgentPbrTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::PBR_ACL, ProductionFeature::SRV6_ENCAP};
  }

 protected:
  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_enable_nexthop_id_manager = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    utility::addAclTable(
        &cfg,
        cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE(),
        1 /*priority*/,
        {cfg::AclTableActionType::COUNTER, cfg::AclTableActionType::REDIRECT},
        {cfg::AclTableQualifier::TC,
         cfg::AclTableQualifier::NEXT_HOP_GROUP_ID});
    utility::addQosMapsHelper(
        cfg,
        {{1 /*TC==CLASS_1*/, {kDscp}},
         {2 /*TC==CLASS_2*/, {kDscp2}},
         {3 /*TC==CLASS_3*/, {kDscp3}}},
        "pbrQos",
        ensemble.getSw()->getHwAsicTable()->getL3Asics());
    cfg.srv6Tunnels() = {utility::makeSrv6TunnelConfig(
        kSrv6TunnelId, InterfaceID(cfg.interfaces()[0].intfID().value()))};
    return cfg;
  }

  utility::EcmpSetupTargetedPorts6 ecmpHelper() const {
    return utility::EcmpSetupTargetedPorts6(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
  }

  RouteNextHopSet makeNamedNhgSet(
      const std::vector<PortDescriptor>& ports) const {
    auto helper = ecmpHelper();
    RouteNextHopSet nhops;
    for (const auto& port : ports) {
      nhops.insert(
          makeSrv6NextHop<UnresolvedNextHop>(
              port.phyPortID(), folly::IPAddress(helper.nhop(port).ip)));
    }
    return nhops;
  }

  void programPbrRoute(
      const std::string& nhgName,
      const std::string& prefix = kPbrRoutePrefix) {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress(prefix));
    route.dest()->prefixLength() = 64;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = nhgName;
    route.namedRouteDestination() = namedDest;
    auto updater = getSw()->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route);
    updater.program();
  }

  void addOrUpdateNamedNhgs(
      const std::vector<std::pair<std::string, RouteNextHopSet>>& groups) {
    auto nhgs = std::make_unique<std::vector<NextHopGroup>>();
    for (const auto& [name, nhops] : groups) {
      NextHopGroup group;
      group.name() = name;
      group.nexthops() = {};
      for (const auto& nhop : nhops) {
        group.nexthops()->push_back(nhop.toThrift());
      }
      nhgs->push_back(std::move(group));
    }
    ThriftHandler handler(getSw());
    handler.addOrUpdateNamedNextHopGroups(std::move(nhgs));
  }

  NextHopGroup makeLinkLocalNhgGroup(
      const std::string& name,
      const std::vector<PortDescriptor>& ports) const {
    NextHopGroup group;
    group.name() = name;
    group.nexthops() = {};
    auto helper = ecmpHelper();
    for (const auto& port : ports) {
      auto ecmpNhop = helper.nhop(port);
      CHECK(ecmpNhop.linkLocalNhopIp.has_value());
      NextHop nh = makeSrv6NextHop<ResolvedNextHop>(
          port.phyPortID(),
          folly::IPAddress(*ecmpNhop.linkLocalNhopIp),
          ecmpNhop.intf);
      group.nexthops()->push_back(nh.toThrift());
    }
    return group;
  }

  std::string kDefaultNhg() const {
    return "pbrDefaultNhg";
  }
  std::string kClassNhg() const {
    return "pbrClassNhg";
  }
  std::string kClass2Nhg() const {
    return "pbrClass2Nhg";
  }
  std::string kClass3Nhg() const {
    return "pbrClass3Nhg";
  }
  std::string kPolicy() const {
    return "pbrPolicy";
  }

  void addClassBasedPolicy(
      const std::string& policyName,
      const std::string& defaultNhg,
      const std::map<ForwardingClass, std::string>& class2Nhg) {
    ThriftHandler handler(getSw());
    ClassBasedPolicy cbfPolicy;
    cbfPolicy.name() = policyName;
    cbfPolicy.defaultNexthopGroup() = defaultNhg;
    for (const auto& [forwardingClass, nhgName] : class2Nhg) {
      NextHopGroup classGroup;
      classGroup.name() = nhgName;
      cbfPolicy.class2NextHopGroup()[forwardingClass] = classGroup;
    }
    Policy policy;
    policy.cbfPolicy() = cbfPolicy;
    auto policies = std::make_unique<std::vector<Policy>>();
    policies->push_back(std::move(policy));
    handler.addPolicies(std::move(policies));
  }

  void addClassBasedPolicy(
      const std::map<ForwardingClass, std::string>& class2Nhg) {
    addClassBasedPolicy(kPolicy(), kDefaultNhg(), class2Nhg);
  }

  void addClassBasedPolicy() {
    addClassBasedPolicy({{ForwardingClass::CLASS_1, kClassNhg()}});
  }

  uint64_t sumOutPkts(const std::vector<PortID>& members) {
    uint64_t sum = 0;
    for (const auto& port : members) {
      sum += *getNextUpdatedPortStats(port).outUnicastPkts_();
    }
    return sum;
  }

  // Inject kNumPkts probes at the given DSCP, varying the L4 source port so
  // they spread across the egress group's members.
  uint64_t sendPbrBurst(uint8_t dscp, PortID injectPort) {
    return sendPbrBurst(dscp, injectPort, kPbrDstIp);
  }

  uint64_t
  sendPbrBurst(uint8_t dscp, PortID injectPort, const std::string& dstIp) {
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    uint64_t bytesSent = 0;
    for (int i = 0; i < kNumPkts; ++i) {
      auto pkt = utility::makeUDPTxPacket(
          getSw(),
          getVlanIDForTx(),
          srcMac,
          intfMac,
          folly::IPAddressV6(kPbrSrcIp),
          folly::IPAddressV6(dstIp),
          kL4SrcPort + i,
          kL4DstPort,
          dscp << 2 /*trafficClass byte*/,
          255 /*hopLimit*/);
      bytesSent += pkt->buf()->computeChainDataLength();
      getAgentEnsemble()->ensureSendPacketOutOfPort(std::move(pkt), injectPort);
    }
    return bytesSent;
  }

  uint64_t verifyBurst(
      uint8_t dscp,
      PortID injectPort,
      const std::vector<PortID>& expect,
      const std::vector<PortID>& notExpect) {
    return verifyBurst(dscp, injectPort, kPbrDstIp, expect, notExpect);
  }

  uint64_t verifyBurst(
      uint8_t dscp,
      PortID injectPort,
      const std::string& dstIp,
      const std::vector<PortID>& expect,
      const std::vector<PortID>& notExpect) {
    auto expectBefore = sumOutPkts(expect);
    auto notExpectBefore = sumOutPkts(notExpect);
    auto bytesSent = sendPbrBurst(dscp, injectPort, dstIp);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_GE(sumOutPkts(expect) - expectBefore, kNumPkts);
      // A redirect that only partly works leaks probes here in bulk, so the
      // bound has to stay well under kNumPkts. It is not zero because
      // control-plane traffic also egresses these ports.
      EXPECT_EVENTUALLY_LE(sumOutPkts(notExpect) - notExpectBefore, kVariance);
    });
    return bytesSent;
  }

  void verifyRedirectAndCounter(
      const std::vector<PortID>& classMembers,
      const std::vector<PortID>& defaultMembers,
      PortID injectPort) {
    auto state = getProgrammedState();
    auto aclMap = state->getAclsForTable(
        cfg::AclStage::INGRESS,
        cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE());
    ASSERT_NE(aclMap, nullptr);
    int pbrEntries = 0;
    int entriesWithCounter = 0;
    std::optional<std::string> counterName;
    std::vector<cfg::CounterType> counterTypes;
    for (const auto& [_, entry] : std::as_const(*aclMap)) {
      if (entry->getNextHopGroupId().has_value()) {
        ++pbrEntries;
        if (auto aclAction = entry->getAclAction()) {
          auto matchAction = MatchAction::fromThrift(aclAction->toThrift());
          if (auto trafficCounter = matchAction.getTrafficCounter()) {
            ++entriesWithCounter;
            counterName = *trafficCounter->name();
            counterTypes = *trafficCounter->types();
          }
        }
      }
    }
    // The fixture programs one policy with one traffic class, so a second
    // entry means a generation was retired without removing its entry.
    EXPECT_EQ(pbrEntries, 1);

    if (!isSupportedOnAllAsics(HwAsic::Feature::PBR_ACL)) {
      return;
    }
    EXPECT_EQ(entriesWithCounter, pbrEntries);
    ASSERT_TRUE(counterName.has_value());

    auto byteCounters =
        isSupportedOnAllAsics(HwAsic::Feature::ACL_BYTE_COUNTER) ||
        isSupportedOnAllAsics(HwAsic::Feature::PBR_ACL_BYTE_COUNTER);
    EXPECT_EQ(
        std::count(
            counterTypes.begin(), counterTypes.end(), cfg::CounterType::BYTES),
        1);
    // A counter whose entry was just programmed reports a garbage value on its
    // first stats poll, so only trust a baseline two successive ticks agree on.
    uint64_t pktsBefore = utility::getAclInOutPackets(getSw(), *counterName);
    uint64_t bytesBefore =
        utility::getAclInOutPackets(getSw(), *counterName, true /*bytes*/);
    WITH_RETRIES({
      getNextUpdatedPortStats(injectPort);
      auto pkts = utility::getAclInOutPackets(getSw(), *counterName);
      auto bytes =
          utility::getAclInOutPackets(getSw(), *counterName, true /*bytes*/);
      EXPECT_EVENTUALLY_EQ(pkts, pktsBefore);
      EXPECT_EVENTUALLY_EQ(bytes, bytesBefore);
      pktsBefore = pkts;
      bytesBefore = bytes;
    });
    auto bytesSent =
        verifyBurst(kDscp, injectPort, classMembers, defaultMembers);
    WITH_RETRIES({
      EXPECT_EVENTUALLY_GE(
          utility::getAclInOutPackets(getSw(), *counterName),
          pktsBefore + kNumPkts);
      if (byteCounters) {
        EXPECT_EVENTUALLY_GE(
            utility::getAclInOutPackets(getSw(), *counterName, true /*bytes*/),
            bytesBefore + bytesSent);
      }
    });
  }
};

TEST_F(AgentPbrTest, ProgramPolicyAcl) {
  auto setup = [this]() {
    auto helper = ecmpHelper();
    auto ports = helper.ecmpPortDescs(5);

    // 1. Resolve nexthops on the default + class NHG member ports.
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper.resolveNextHops(
          in, {ports[0], ports[1], ports[2], ports[3]});
    });

    // 2. Create the named next-hop groups the policy references. Both are
    // multi-member so the default resolves to a SAI next-hop-group (matching
    // the ACL's ROUTE_DST) and the class exercises ECMP redirect.
    addOrUpdateNamedNhgs(
        {{kDefaultNhg(), makeNamedNhgSet({ports[0], ports[1]})},
         {kClassNhg(), makeNamedNhgSet({ports[2], ports[3]})}});

    // 3. Add a class-based policy: default NHG + CLASS_1 -> class NHG.
    addClassBasedPolicy();

    // 4. Bind a route to the default NHG by name. Nothing is synthesized until
    // a route references the policy.
    programPbrRoute(kDefaultNhg());
  };

  auto verify = [this]() {
    auto ports = ecmpHelper().ecmpPortDescs(5);
    verifyRedirectAndCounter(
        {ports[2].phyPortID(), ports[3].phyPortID()},
        {ports[0].phyPortID(), ports[1].phyPortID()},
        ports[4].phyPortID());
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentPbrTest, ProgramPolicyAclViaPolicyRoute) {
  auto setup = [this]() {
    auto helper = ecmpHelper();
    auto ports = helper.ecmpPortDescs(5);

    // 1. Resolve neighbors at link-local so the link-local named-NHG members
    // resolve to programmable SAI next hops.
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper.resolveNextHops(
          in, {ports[0], ports[1], ports[2], ports[3]}, true /*useLinkLocal*/);
    });

    // 2. Create the named next-hop groups via Thrift (link-local next hops pass
    // validateLinkLocalNextHopInterfaces).
    ThriftHandler handler(getSw());
    auto nhgs = std::make_unique<std::vector<NextHopGroup>>();
    nhgs->push_back(makeLinkLocalNhgGroup(kDefaultNhg(), {ports[0], ports[1]}));
    nhgs->push_back(makeLinkLocalNhgGroup(kClassNhg(), {ports[2], ports[3]}));
    handler.addOrUpdateNamedNextHopGroups(std::move(nhgs));

    // 3. Add a class-based policy: default NHG + CLASS_1 -> class NHG.
    addClassBasedPolicy();

    // 4. Bind a route to the policy via policyName; the RIB resolves it to the
    // policy's default NHG.
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress(kPbrRoutePrefix));
    route.dest()->prefixLength() = 64;
    NamedRouteDestination namedDest;
    namedDest.policyName() = kPolicy();
    route.namedRouteDestination() = namedDest;
    auto updater = getSw()->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route);
    updater.program();
  };

  auto verify = [this]() {
    auto ports = ecmpHelper().ecmpPortDescs(5);
    verifyRedirectAndCounter(
        {ports[2].phyPortID(), ports[3].phyPortID()},
        {ports[0].phyPortID(), ports[1].phyPortID()},
        ports[4].phyPortID());
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentPbrTest, PbrRedirectNhgMemberChange) {
  auto setup = [this]() {
    auto helper = ecmpHelper();
    auto ports = helper.ecmpPortDescs(8);

    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper.resolveNextHops(
          in,
          {ports[0],
           ports[1],
           ports[2],
           ports[3],
           ports[4],
           ports[5],
           ports[6]});
    });

    // default NHG = ports[0..1]; CLASS_2 NHG initial members = ports[2..3];
    // CLASS_3 NHG = ports[4..5].
    addOrUpdateNamedNhgs(
        {{kDefaultNhg(), makeNamedNhgSet({ports[0], ports[1]})},
         {kClass2Nhg(), makeNamedNhgSet({ports[2], ports[3]})},
         {kClass3Nhg(), makeNamedNhgSet({ports[4], ports[5]})}});

    // CLASS_1 redirects to the default NHG, so its entry must be programmed
    // even though the redirect is a no-op on the wire.
    addClassBasedPolicy(
        {{ForwardingClass::CLASS_1, kDefaultNhg()},
         {ForwardingClass::CLASS_2, kClass2Nhg()},
         {ForwardingClass::CLASS_3, kClass3Nhg()}});

    programPbrRoute(kDefaultNhg());
  };

  auto verify = [this]() {
    if (!isSupportedOnAllAsics(HwAsic::Feature::PBR_ACL)) {
      return;
    }
    auto helper = ecmpHelper();
    auto ports = helper.ecmpPortDescs(8);
    const std::vector<PortID> defaultMembers = {
        ports[0].phyPortID(), ports[1].phyPortID()};
    const std::vector<PortID> class2InitialMembers = {
        ports[2].phyPortID(), ports[3].phyPortID()};
    const std::vector<PortID> class3Members = {
        ports[4].phyPortID(), ports[5].phyPortID()};
    const std::vector<PortID> class2NewMembers = {ports[6].phyPortID()};
    const auto injectPort = ports[7].phyPortID();

    auto setClass2NhgMembers = [&](const std::vector<PortDescriptor>& members) {
      addOrUpdateNamedNhgs({{kClass2Nhg(), makeNamedNhgSet(members)}});
    };

    // Re-establish the initial CLASS_2 members so the sequence is idempotent
    // across warm boots (a prior verify pass may have left the new members).
    setClass2NhgMembers({ports[2], ports[3]});

    // 1. Non-matching traffic follows the route to the default NHG.
    verifyBurst(kNoMatchDscp, injectPort, defaultMembers, class2InitialMembers);
    // 2. CLASS_1 redirects to the default NHG, so matching traffic lands where
    // the route would have sent it anyway.
    verifyBurst(kDscp, injectPort, defaultMembers, class2InitialMembers);
    // 3. CLASS_2 and CLASS_3 each redirect to their own group.
    verifyBurst(kDscp2, injectPort, class2InitialMembers, class3Members);
    verifyBurst(kDscp3, injectPort, class3Members, class2InitialMembers);

    // 4. Change only CLASS_2's members.
    setClass2NhgMembers({ports[6]});

    // 5. CLASS_2 egresses the new members, and CLASS_3 is undisturbed.
    verifyBurst(kDscp2, injectPort, class2NewMembers, class2InitialMembers);
    verifyBurst(kDscp3, injectPort, class3Members, class2NewMembers);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Two policies, so two ACL entries at the one shared PBR priority. They are
// told apart in hardware only by the SAI ACL entry label, so this is the
// real-SDK proof that a shared priority programs and survives warm boot.
TEST_F(AgentPbrTest, TwoPoliciesShareOnePriority) {
  const std::string kPolicyB = "pbrPolicyB";
  const std::string kDefaultNhgB = "pbrDefaultNhgB";
  const std::string kClassNhgB = "pbrClassNhgB";
  constexpr auto kPbrRoutePrefixB = "4444::";
  constexpr auto kPbrDstIpB = "4444::1";

  auto setup = [&, this]() {
    auto helper = ecmpHelper();
    auto ports = helper.ecmpPortDescs(5);

    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper.resolveNextHops(
          in, {ports[0], ports[1], ports[2], ports[3]});
    });

    // A matches the NHG on {0,1} and redirects to {2,3}; B matches the NHG on
    // {2,3} and redirects to {0,1}. Crossing them keeps both match NHGs
    // multi-member (so each resolves to a SAI next-hop-group that the ACL's
    // ROUTE_DST can equal) without needing more ports.
    addOrUpdateNamedNhgs(
        {{kDefaultNhg(), makeNamedNhgSet({ports[0], ports[1]})},
         {kClassNhg(), makeNamedNhgSet({ports[2], ports[3]})},
         {kDefaultNhgB, makeNamedNhgSet({ports[2], ports[3]})},
         {kClassNhgB, makeNamedNhgSet({ports[0], ports[1]})}});

    addClassBasedPolicy();
    addClassBasedPolicy(
        kPolicyB, kDefaultNhgB, {{ForwardingClass::CLASS_1, kClassNhgB}});

    programPbrRoute(kDefaultNhg());
    programPbrRoute(kDefaultNhgB, kPbrRoutePrefixB);
  };

  auto verify = [&, this]() {
    auto aclMap = getProgrammedState()->getAclsForTable(
        cfg::AclStage::INGRESS,
        cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE());
    ASSERT_NE(aclMap, nullptr);

    // 1. Both rules are present, at one priority, distinguished by name.
    auto entryA = aclMap->getEntryIf(makePbrAclEntryName(
        kPolicy(), ForwardingClass::CLASS_1, false /*temporary*/));
    auto entryB = aclMap->getEntryIf(makePbrAclEntryName(
        kPolicyB, ForwardingClass::CLASS_1, false /*temporary*/));
    ASSERT_NE(entryA, nullptr);
    ASSERT_NE(entryB, nullptr);
    EXPECT_EQ(entryA->getPriority(), FLAGS_pbr_acl_priority);
    EXPECT_EQ(entryB->getPriority(), FLAGS_pbr_acl_priority);

    if (!isSupportedOnAllAsics(HwAsic::Feature::PBR_ACL)) {
      return;
    }

    auto helper = ecmpHelper();
    auto ports = helper.ecmpPortDescs(5);
    const std::vector<PortID> groupA = {
        ports[0].phyPortID(), ports[1].phyPortID()};
    const std::vector<PortID> groupB = {
        ports[2].phyPortID(), ports[3].phyPortID()};
    const auto injectPort = ports[4].phyPortID();

    auto counterA = makePbrCounterName(ForwardingClass::CLASS_1, kClassNhg());
    auto counterB = makePbrCounterName(ForwardingClass::CLASS_1, kClassNhgB);
    auto pktsA = utility::getAclInOutPackets(getSw(), counterA);
    auto pktsB = utility::getAclInOutPackets(getSw(), counterB);

    // 2. Each rule redirects its own traffic to the other group's ports.
    verifyBurst(kDscp, injectPort, kPbrDstIp, groupB, groupA);
    verifyBurst(kDscp, injectPort, kPbrDstIpB, groupA, groupB);

    // 3. Each rule counts its own traffic, so hardware kept them apart.
    WITH_RETRIES({
      EXPECT_EVENTUALLY_GE(
          utility::getAclInOutPackets(getSw(), counterA), pktsA + kNumPkts);
      EXPECT_EVENTUALLY_GE(
          utility::getAclInOutPackets(getSw(), counterB), pktsB + kNumPkts);
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentPbrTest, UnreferencedPolicyProgramsNoAcl) {
  auto pbrAcls = [this]() {
    return getProgrammedState()->getAclsForTable(
        cfg::AclStage::INGRESS,
        cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE());
  };
  auto pbrEntry = [&]() {
    auto aclMap = pbrAcls();
    return aclMap
        ? aclMap->getEntryIf(makePbrAclEntryName(
              kPolicy(), ForwardingClass::CLASS_1, false /*temporary*/))
        : nullptr;
  };

  auto setup = [this]() {
    auto helper = ecmpHelper();
    auto ports = helper.ecmpPortDescs(5);

    // 1. Everything a working policy needs except a route bound to it.
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper.resolveNextHops(
          in, {ports[0], ports[1], ports[2], ports[3]});
    });
    addOrUpdateNamedNhgs(
        {{kDefaultNhg(), makeNamedNhgSet({ports[0], ports[1]})},
         {kClassNhg(), makeNamedNhgSet({ports[2], ports[3]})}});
    addClassBasedPolicy();
  };

  auto verify = [&]() {
    // 2. Nothing can match the policy, so the table stays empty -- checked by
    // count as well as by name, so an entry under an unexpected name still
    // fails.
    EXPECT_EQ(pbrEntry(), nullptr);
    auto aclMap = pbrAcls();
    EXPECT_EQ(aclMap ? aclMap->numEntries() : 0, 0);
  };

  auto setupPostWarmboot = [this]() { programPbrRoute(kDefaultNhg()); };

  auto verifyPostWarmboot = [&, this]() {
    // 3. The route makes the policy reachable, so the entry appears and
    // redirects.
    EXPECT_NE(pbrEntry(), nullptr);
    auto ports = ecmpHelper().ecmpPortDescs(5);
    verifyRedirectAndCounter(
        {ports[2].phyPortID(), ports[3].phyPortID()},
        {ports[0].phyPortID(), ports[1].phyPortID()},
        ports[4].phyPortID());
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

} // namespace facebook::fboss
