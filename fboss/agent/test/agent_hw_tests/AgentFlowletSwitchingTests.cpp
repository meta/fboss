/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/agent_hw_tests/AgentArsBase.h"

#include <folly/Conv.h>
#include <algorithm>
#include <array>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/EcmpResourceManager.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/MirrorTestUtils.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/agent/test/utils/ScaleTestUtils.h"
#include "fboss/agent/test/utils/UdfTestUtils.h"
#include "fboss/agent/types.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"

namespace facebook::fboss {

class AgentFlowletSwitchingTest : public AgentArsBase {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::DLB,
        ProductionFeature::UDF_WR_IMMEDIATE_ACL,
        ProductionFeature::SINGLE_ACL_TABLE};
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentArsBase::initialConfig(ensemble);
    auto backupSwitchingMode = isChenab(ensemble)
        ? cfg::SwitchingMode::FIXED_ASSIGNMENT
        : cfg::SwitchingMode::PER_PACKET_RANDOM;
    utility::addFlowletConfigs(
        cfg,
        ensemble.masterLogicalPortIds(),
        ensemble.isSai(),
        cfg::SwitchingMode::PER_PACKET_QUALITY,
        backupSwitchingMode);
    return cfg;
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_flowletSwitchingEnable = true;
    FLAGS_force_init_fp = false;
    FLAGS_enable_ecmp_random_spray = true;
  }
};

// Prune keys off the link state the ASIC sees, and BRCM only reports that with
// PHY loopback rather than the default MAC loopback. The adapter rejects PHY
// loopback on the 400G ports, so -- as AgentAdjFrrRouteTest does -- every port
// this fixture hands to a test is one of the 800G ports it put in PHY loopback.
class AgentFlowletSourcePortPruneTest : public AgentFlowletSwitchingTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentFlowletSwitchingTest::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::ARS_SOURCE_PORT_PRUNE);
    return features;
  }

 protected:
  // Balance is asserted as each member's share of the fair share rather than
  // as a min/max ratio: the ratio compares the two extremes and so compounds
  // their noise, which tightens the bound as the group narrows. The mean lies
  // between the extremes, so a 25% min/max deviation implies every member is
  // within [80%, 125%] of the fair share.
  static constexpr utility::MemberShareBounds kEvenDlbShareBounds{80, 125};
  // A pruned member hands its whole share to a single survivor rather than
  // spreading it, so with one of N+1 members pruned the absorber lands at
  // 2N/(N+1) of the fair share. Measured at every width, including widths
  // needing no padding.
  static constexpr utility::MemberShareBounds kPrunedDlbShareBounds{25, 200};
  // Injecting from a member skews DLB even with nothing pruned. DLB steers on
  // measured port load and the ingress port is carrying the injected burst
  // under PHY loopback, so it looks busy and is given less: measured 27% to
  // 39% min/max spread across widths, against under 5% when the ingress sits
  // outside the group. The spread is the mechanism working, not a defect, so
  // the band is only here to catch a member dropping out entirely -- which the
  // member count assertion covers more directly.
  static constexpr utility::MemberShareBounds kMemberIngressDlbShareBounds{
      25,
      200};

  static constexpr int kFlowCount = 2048;
  static constexpr int kPacketsPerFlow = 8;
  static constexpr int kPacketCount = kFlowCount * kPacketsPerFlow;
  // Any UDP port other than 4791 misses the flowlet ACL, so the burst is not
  // DLB eligible and falls to the SDK's internally created secondary group.
  // No ECMP_HASH_CANCEL ACL is needed here: the DLB secondary is static hash
  // already, so missing the flowlet rule is enough to reach it. The FRR tests
  // do install one, because their backup group is random spray and the spray
  // has to be cancelled to get a static hash contrast.
  static constexpr int kNonDlbL4DstPort = 1024;

  // Which selection mechanism a burst exercises.
  enum class TrafficType {
    // 4791, one 5 tuple: the ARS object forwards. DLB selects per packet, not
    // per flow -- it spreads on measured port and queue load rather than on
    // header entropy -- so a single tuple still reaches every member. Sending
    // one flow is what makes the spread meaningful: with kFlowCount tuples the
    // members would fill up from hashing alone and prove nothing about DLB.
    Dlb,
    // 1024, one 5 tuple: secondary group with no entropy, so its static hash
    // must settle on exactly one member.
    SecondarySingleFlow,
    // 1024, kFlowCount flows: secondary group with real entropy, so the hash
    // spreads across members.
    SecondaryMultiFlow,
  };

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return std::nullopt;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentFlowletSwitchingTest::initialConfig(ensemble);
    for (auto& port : *cfg.ports()) {
      if (*port.speed() == cfg::PortSpeed::EIGHTHUNDREDG) {
        port.loopbackMode() = cfg::PortLoopbackMode::PHY;
      }
    }
    // The ARS config does not program an ECMP hash on its own, so without this
    // every packet resolves to the same member and prune has nothing to
    // redistribute across.
    utility::addLoadBalancerToConfig(
        cfg,
        checkSameAndGetAsicForTesting(ensemble.getL3Asics()),
        utility::LBHash::FULL_HASH);
    cfg.switchSettings()->ecmpGroupSettings() = splitHorizonSettings();
    return cfg;
  }

  // Split horizon is CREATE_ONLY and SaiSwitch rejects an ecmpGroupSettings
  // change after the first config application, so the value can only arrive
  // through initialConfig(). A prune-on variant is therefore its own fixture
  // overriding this, not a setter called mid-test. Empty here: this fixture is
  // the prune-off side.
  virtual EcmpGroupSettingsMap splitHorizonSettings() const {
    return {};
  }

  static EcmpGroupSettingsMap splitHorizonOn(
      const std::vector<cfg::EcmpGroupType>& groupTypes) {
    cfg::EcmpGroupSettings settings;
    settings.enableSplitHorizon() = true;
    EcmpGroupSettingsMap enabled;
    for (auto groupType : groupTypes) {
      enabled.emplace(groupType, settings);
    }
    return enabled;
  }

  // Derived from the programmed state rather than cached from initialConfig(),
  // which a warm boot never calls.
  std::vector<PortID> getTestPorts() const override {
    std::vector<PortID> phyLoopbackPorts;
    auto state = getProgrammedState();
    for (auto portId : masterLogicalInterfacePortIds()) {
      auto port = state->getPorts()->getNodeIf(portId);
      if (port && port->getLoopbackMode() == cfg::PortLoopbackMode::PHY) {
        phyLoopbackPorts.push_back(portId);
      }
    }
    return phyLoopbackPorts;
  }

  // AgentArsBase::setup() indexes getTestPorts() at both [0, ecmpWidth) and
  // kFrontPanelPortForTest without bounds checking.
  void checkEnoughPhyLoopbackPorts(int ecmpWidth) const {
    size_t needed = std::max(ecmpWidth, kFrontPanelPortForTest + 1);
    auto available = getTestPorts().size();
    CHECK_GE(available, needed)
        << "need " << needed << " ports in PHY loopback, found " << available;
  }

  // Sends one burst into the group and checks how the group spread it. Delivery
  // of the whole burst is always asserted; everything else is opt in.
  //
  // ecmpWidth
  //   Members in the group. testPorts[0, ecmpWidth) are the members and stats
  //   are polled over exactly those ports.
  // ingressPortIdx
  //   Index into testPorts of the port the burst is injected on. Inside
  //   [0, ecmpWidth) it is a member, so split horizon can act on it; outside,
  //   it is not a pruning candidate and the phase serves as a control.
  //   When it is a member: the port is in PHY loopback, so its outUnicastPkts
  //   also counts what the test transmitted, and those are subtracted before
  //   judging what the group sent back out of it. It is then tallied apart from
  //   membersWithTraffic. When it is not a member it is outside the polled set
  //   entirely and nothing about it is measured.
  // traffic
  //   Which selection mechanism to exercise. Also decides the L4 destination
  //   port and whether the burst is one 5 tuple or kFlowCount of them.
  // expectPruned
  //   Whether the ingress must egress nothing. Only meaningful when the ingress
  //   is a member; ignored otherwise.
  // expectedMembersWithTraffic
  //   How many members other than the ingress must carry a share. Defaults to
  //   the whole group, less the ingress when the ingress is a member.
  // shareBounds
  //   If set, every forwarding member must carry a share of the traffic within
  //   these bounds of the fair share. Unset skips the balance check.
  // assertMemberCount
  //   Set false where which members forward cannot be pinned down: a DLB burst
  //   quantises onto flowlet sets, and a single flow can land on the ingress
  //   itself, which the count deliberately excludes.
  void sendFlowsAndVerifyPrune(
      int ecmpWidth,
      int ingressPortIdx,
      TrafficType traffic,
      bool expectPruned,
      std::optional<int> expectedMembersWithTraffic = std::nullopt,
      std::optional<utility::MemberShareBounds> shareBounds = std::nullopt,
      bool assertMemberCount = true) {
    const bool ingressIsMember = ingressPortIdx < ecmpWidth;
    const bool dlbExpected = traffic == TrafficType::Dlb;
    const int l4DstPort =
        dlbExpected ? utility::kUdfL4DstPort : kNonDlbL4DstPort;
    // Only SecondaryMultiFlow needs entropy: it is the one burst selected by a
    // static hash. DLB selects per packet on load, and the secondary single
    // flow case is asserting what a hash does with no entropy at all, so both
    // send one 5 tuple.
    const bool singleFlow = traffic != TrafficType::SecondaryMultiFlow;
    const int flowCount = singleFlow ? 1 : kFlowCount;
    const int packetsPerFlow = singleFlow ? kPacketCount : kPacketsPerFlow;
    auto testPorts = getTestPorts();
    std::vector<PortID> ecmpPorts(
        testPorts.begin(), testPorts.begin() + ecmpWidth);
    auto ingressPort = testPorts[ingressPortIdx];
    const auto counterName = getCounterName(AclType::FLOWLET);

    auto statsBefore = getNextUpdatedPortStats(ecmpPorts);
    auto aclPktsBefore = utility::getAclInOutPackets(getSw(), counterName);

    std::vector<uint8_t> rethHdr(16);
    rethHdr[15] = 0xFF; // non-zero sized packet, so DLB is engaged
    // setup() programs ::/0, so every source IP still resolves to this ECMP
    // group while giving the hash something to spread on.
    for (int flow = 0; flow < flowCount; flow++) {
      sendRoceTraffic(
          ingressPort,
          utility::kUdfRoceOpcodeWriteImmediate,
          rethHdr,
          packetsPerFlow,
          l4DstPort,
          0,
          folly::IPAddressV6(folly::to<std::string>("1001::", flow + 1)));
    }

    WITH_RETRIES({
      auto statsAfter = getNextUpdatedPortStats(ecmpPorts);
      int64_t egressed = 0;
      int64_t ingressEgressed = 0;
      int membersWithTraffic = 0;
      std::map<std::string, uint64_t> forwardedByMember;
      for (int i = 0; i < ecmpWidth; i++) {
        auto port = testPorts[i];
        auto delta = *statsAfter.at(port).outUnicastPkts_() -
            *statsBefore.at(port).outUnicastPkts_();
        if (i == ingressPortIdx) {
          delta -= kPacketCount;
          ingressEgressed = delta;
        } else if (delta > 0) {
          membersWithTraffic++;
        }
        // A member carrying nothing is left out: it would fail the floor and
        // also pull the fair share down for the members that do forward. The
        // member count assertion is what catches a member dropping out.
        if (delta > 0) {
          forwardedByMember.emplace(fmt::format("{}", port), delta);
        }
        egressed += delta;
        XLOG(DBG2) << "Ecmp egress port " << i << " (" << port << "): delta "
                   << delta;
      }
      // Only members are polled, so on a control phase the ingress is outside
      // ecmpPorts and ingressEgressed is not measured rather than zero.
      XLOG(DBG2) << "Ingress port " << ingressPort << " egressed "
                 << (ingressIsMember ? std::to_string(ingressEgressed)
                                     : std::string("not measured (non member)"))
                 << ", members with traffic " << membersWithTraffic
                 << ", expectPruned " << expectPruned;
      EXPECT_EVENTUALLY_GE(egressed, kPacketCount);

      auto aclPktsAfter = utility::getAclInOutPackets(getSw(), counterName);
      if (dlbExpected) {
        EXPECT_EVENTUALLY_GE(aclPktsAfter, aclPktsBefore + kPacketCount);
      } else {
        EXPECT_EVENTUALLY_EQ(aclPktsAfter, aclPktsBefore);
      }

      if (ingressIsMember) {
        if (expectPruned) {
          EXPECT_EVENTUALLY_EQ(0, ingressEgressed);
        } else if (traffic != TrafficType::SecondarySingleFlow) {
          // Only the static hash with no entropy is unassertable here: it picks
          // one member and that need not be the ingress. DLB spreads per packet
          // even on a single tuple, so an unpruned ingress must carry a share.
          EXPECT_EVENTUALLY_GT(ingressEgressed, 0);
        }
      }
      // Every member prune did not exclude has to carry a share. This is a
      // count, not a balance check -- how evenly they share is shareBounds'
      // job, and padding makes an even split impossible at some widths.
      if (assertMemberCount) {
        EXPECT_EVENTUALLY_EQ(
            expectedMembersWithTraffic.value_or(
                ingressIsMember ? ecmpWidth - 1 : ecmpWidth),
            membersWithTraffic);
      }
      if (shareBounds.has_value() && !forwardedByMember.empty()) {
        uint64_t lowestDelta = forwardedByMember.begin()->second;
        uint64_t highestDelta = lowestDelta;
        for (const auto& member : forwardedByMember) {
          if (member.second < lowestDelta) {
            lowestDelta = member.second;
          }
          if (member.second > highestDelta) {
            highestDelta = member.second;
          }
        }
        XLOG(DBG2) << "Load spread across " << forwardedByMember.size()
                   << " members, lowest " << lowestDelta << ", highest "
                   << highestDelta;
        EXPECT_EVENTUALLY_TRUE(
            utility::isWithinMemberShareBounds(
                forwardedByMember, *shareBounds));
      }
    });
  }
};

// Prune on for ARS groups. Split horizon is CREATE_ONLY and the setting cannot
// change after the first config application, so this is a fixture of its own
// rather than a flag flipped inside the test.
class AgentFlowletArsSourcePortPruneEnabledTest
    : public AgentFlowletSourcePortPruneTest {
 protected:
  EcmpGroupSettingsMap splitHorizonSettings() const override {
    return splitHorizonOn({cfg::EcmpGroupType::ARS});
  }
};

// DLB eligible traffic, so the ARS object forwards and the attribute under
// test is SAI_ARS_ATTR_EXTENSION_SOURCE_PORT_PRUNE. The ECMP group is sized so
// the front panel injection port is itself a member, otherwise there would be
// nothing for prune to suppress.
TEST_F(
    AgentFlowletArsSourcePortPruneEnabledTest,
    VerifyArsSourcePortPruneEnabled) {
  constexpr int kEcmpWidth = 12;
  static_assert(
      kEcmpWidth > kFrontPanelPortForTest,
      "front panel injection port must be inside the ECMP group");

  auto setup = [&]() {
    checkEnoughPhyLoopbackPorts(kEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
    this->setup(kEcmpWidth);
  };

  auto verify = [&]() {
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::Dlb,
        true,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        kPrunedDlbShareBounds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Counterpart with the flag never set: the ingress port stays in the ARS group
// and keeps carrying its share.
TEST_F(AgentFlowletSourcePortPruneTest, VerifyArsSourcePortPruneDisabled) {
  constexpr int kEcmpWidth = 12;
  static_assert(
      kEcmpWidth > kFrontPanelPortForTest,
      "front panel injection port must be inside the ECMP group");

  auto setup = [&]() {
    checkEnoughPhyLoopbackPorts(kEcmpWidth);
    this->setup(kEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
  };

  auto verify = [&]() {
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::Dlb,
        false,
        kEcmpWidth - 1 /* the count excludes the ingress, pruned or not */,
        kMemberIngressDlbShareBounds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// One of the cases that increments the ARS fail counter is a packet whose
// group has no member left to forward it out of. This drives that case:
//   1. program an ARS group with two members, both neighbors on the same port
//   2. ingress the traffic on that port, routed out of that same group
//   3. split horizon prunes both members, leaving nothing to forward on
//
// Two members rather than one because a single next hop route is programmed as
// a plain next hop and not a group, so it would carry no ARS object to count
// against.
class AgentFlowletArsDlbFailPacketsTest
    : public AgentFlowletArsSourcePortPruneEnabledTest {
 protected:
  static constexpr int kIngressPortIdx = 0;
  static constexpr int kFailPacketCount = 1;

  void setCmdLineFlagOverrides() const override {
    AgentFlowletArsSourcePortPruneEnabledTest::setCmdLineFlagOverrides();
    FLAGS_flowletStatsEnable = true;
  }

  // A second neighbor sharing the first one's port and interface. The
  // interface address and every next hop the helper builds differ only in the
  // last byte, so moving the byte above it lands outside all of them without
  // having to reason about which last byte values are taken.
  utility::EcmpNextHop<folly::IPAddressV6> secondNhopOnPort(
      const PortDescriptor& portDesc) const {
    auto first = helper_->nhop(portDesc);
    auto bytes = first.ip.toByteArray();
    bytes[bytes.size() - 2] += 1;
    auto mac = folly::MacAddress::fromHBO(first.mac.u64HBO() + 1);
    return utility::EcmpNextHop<folly::IPAddressV6>(
        folly::IPAddressV6(bytes), portDesc, mac, first.intf);
  }

  static int64_t dlbFailPackets(const multiswitch::HwSwitchStats& stats) {
    return stats.flowletStats()->l3EcmpDlbFailPackets().value();
  }
};

TEST_F(AgentFlowletArsDlbFailPacketsTest, VerifyArsDlbFailPackets) {
  auto setup = [&]() {
    ASSERT_FALSE(getTestPorts().empty());
    generateApplyConfig(AclType::FLOWLET);

    PortDescriptor ingressDesc(getTestPorts()[kIngressPortIdx]);
    auto nhop = helper_->nhop(ingressDesc);
    auto secondNhop = secondNhopOnPort(ingressDesc);

    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper_->resolveNextHop(
          helper_->resolveNextHop(in, nhop), secondNhop);
    });

    RouteNextHopSet nhops{
        UnresolvedNextHop(nhop.ip, ECMP_WEIGHT),
        UnresolvedNextHop(secondNhop.ip, ECMP_WEIGHT)};
    auto updater = getSw()->getRouteUpdater();
    updater.addRoute(
        RouterID(0),
        folly::IPAddressV6(kRoceDstIp),
        128,
        ClientID::BGPD,
        RouteNextHopEntry(nhops, AdminDistance::EBGP));
    updater.program();

    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto out = in->clone();
      for (const auto& [_, switchSetting] :
           std::as_const(*out->getSwitchSettings())) {
        auto newSwitchSettings = switchSetting->modify(&out);
        newSwitchSettings->setForceEcmpDynamicMemberUp(true);
      }
      return out;
    });
  };

  auto verify = [&]() {
    ASSERT_FALSE(getTestPorts().empty());
    auto ingressPort = getTestPorts()[kIngressPortIdx];
    auto switchId = getSw()->getScopeResolver()->scope(ingressPort).switchId();
    auto switchIndex =
        getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);

    int64_t failPacketsBefore = 0;
    WITH_RETRIES({
      auto switchStats = getSw()->getHwSwitchStatsExpensive();
      ASSERT_EVENTUALLY_TRUE(
          switchStats.find(switchIndex) != switchStats.end());
      failPacketsBefore = dlbFailPackets(switchStats.at(switchIndex));
    });

    sendRoceTraffic(
        ingressPort,
        utility::kUdfRoceOpcodeWriteImmediate,
        std::nullopt,
        kFailPacketCount,
        utility::kUdfL4DstPort,
        0);

    WITH_RETRIES({
      auto failPacketsAfter =
          dlbFailPackets(getSw()->getHwSwitchStatsExpensive(switchIndex));
      XLOG(DBG2) << "DLB fail packets: " << failPacketsBefore << " -> "
                 << failPacketsAfter;
      EXPECT_EVENTUALLY_EQ(
          failPacketsAfter, failPacketsBefore + kFailPacketCount);
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

class AgentFlowletAclPriorityTest : public AgentFlowletSwitchingTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::DLB,
        ProductionFeature::UDF_WR_IMMEDIATE_ACL,
        ProductionFeature::SINGLE_ACL_TABLE};
  }
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentFlowletSwitchingTest::initialConfig(ensemble);
    std::vector<std::string> udfGroups = {
        utility::kUdfAclRoceOpcodeGroupName,
        utility::kRoceUdfFlowletGroupName,
        utility::kUdfAclAethNakGroupName,
        utility::kUdfAclRethWrImmZeroGroupName,
    };
    addAclTableConfig(cfg, udfGroups);
    cfg.udfConfig() = utility::addUdfAclConfig(
        utility::kUdfOffsetBthOpcode | utility::kUdfOffsetBthReserved |
        utility::kUdfOffsetAethSyndrome | utility::kUdfOffsetRethDmaLength);
    return cfg;
  }

 protected:
  static constexpr int kNonRoceL4DstPort = 12345;
  static constexpr int kPacketCount = 1;

  struct AclCounters {
    int64_t roceAck{0};
    int64_t flowlet{0};
    int64_t sprayMiss{0};
    int64_t cancel{0};

    AclCounters operator-(const AclCounters& before) const {
      return AclCounters{
          .roceAck = roceAck - before.roceAck,
          .flowlet = flowlet - before.flowlet,
          .sprayMiss = sprayMiss - before.sprayMiss,
          .cancel = cancel - before.cancel};
    }
  };

  enum class ExpectedHit { RoceAck, Flowlet, RoceSprayMiss, Cancel };

  AclCounters readCounters() {
    auto read = [this](AclType aclType) {
      return static_cast<int64_t>(
          utility::getAclInOutPackets(getSw(), getCounterName(aclType)));
    };
    return AclCounters{
        .roceAck = read(AclType::UDF_ACK),
        .flowlet = read(AclType::UDF_FLOWLET),
        .sprayMiss = read(AclType::ROCE_SPRAY_MISS),
        .cancel = read(AclType::ECMP_HASH_CANCEL)};
  }

  // A counter the HW agent has not published yet reads as 0. After warm boot
  // the HW counters keep their values, so a baseline taken before the first
  // stats sync inflates every delta.
  bool countersPublished() {
    constexpr std::array<AclType, 4> kAclTypes{
        AclType::UDF_ACK,
        AclType::UDF_FLOWLET,
        AclType::ROCE_SPRAY_MISS,
        AclType::ECMP_HASH_CANCEL};
    for (const auto& [_, stats] : getSw()->getHwSwitchStatsExpensive()) {
      const auto& nameToCounter = *stats.aclStats()->statNameToCounterMap();
      if (std::all_of(kAclTypes.begin(), kAclTypes.end(), [&](AclType type) {
            return nameToCounter.contains(getCounterName(type) + ".packets");
          })) {
        return true;
      }
    }
    return false;
  }

  void applyAclConfig(bool withSprayMiss = true) {
    auto newCfg{initialConfig(*getAgentEnsemble())};
    std::vector<std::string> udfGroups =
        getUdfGroupsForAcl(AclType::UDF_FLOWLET_WITH_UDF_ACK);
    addAclTableConfig(newCfg, udfGroups);
    ASSERT_NO_FATAL_FAILURE(addSprayMissAcls(newCfg, withSprayMiss));
    applyNewConfig(newCfg);
  }

  void addSprayMissAcls(cfg::SwitchConfig& cfg, bool withSprayMiss = true) {
    const bool isSai = getAgentEnsemble()->isSai();
    // Entry order sets ACL priority, so this reproduces production's block.
    addAclAndStat(&cfg, AclType::UDF_FLOWLET_WITH_UDF_ACK, isSai);
    if (withSprayMiss) {
      addAclAndStat(&cfg, AclType::ROCE_SPRAY_MISS, isSai);
    }
    addAclAndStat(&cfg, AclType::ECMP_HASH_CANCEL, isSai);

    // Production pairs the flowlet counter with the flowlet action.
    bool patched = false;
    for (auto& matchToAction : *cfg.dataPlaneTrafficPolicy()->matchToAction()) {
      if (*matchToAction.matcher() == getAclName(AclType::UDF_FLOWLET)) {
        matchToAction.action()->flowletAction() = cfg::FlowletAction::FORWARD;
        patched = true;
        break;
      }
    }
    ASSERT_TRUE(patched) << "no matcher found for "
                         << getAclName(AclType::UDF_FLOWLET);
  }

  // Asserts the whole counter vector: the other three staying at zero is how
  // an over-matching entry is caught.
  void
  runStep(int roceOpcode, uint8_t bthReserved, int l4DstPort, ExpectedHit hit) {
    WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(countersPublished()); });
    auto before = readCounters();

    sendRoceTraffic(
        helper_->ecmpPortDescriptorAt(kFrontPanelPortForTest).phyPortID(),
        roceOpcode,
        std::optional<std::vector<uint8_t>>(),
        kPacketCount,
        l4DstPort,
        bthReserved);

    AclCounters deltas;
    WITH_RETRIES({
      deltas = readCounters() - before;
      auto expectDelta = [&](ExpectedHit owner, int64_t delta) {
        const int64_t expected = hit == owner ? kPacketCount : 0;
        EXPECT_EVENTUALLY_EQ(expected, delta);
      };
      expectDelta(ExpectedHit::RoceAck, deltas.roceAck);
      expectDelta(ExpectedHit::Flowlet, deltas.flowlet);
      expectDelta(ExpectedHit::RoceSprayMiss, deltas.sprayMiss);
      expectDelta(ExpectedHit::Cancel, deltas.cancel);
    });

    XLOG(DBG2) << fmt::format(
        "traffic={},opcode={},BTH.reserved=0x{:02x},dport={} "
        "udf_roce_ack={:+} flowlet={:+} spray_miss={:+} hash_cancel={:+}",
        l4DstPort == utility::kUdfL4DstPort ? "RoCE" : "non-RoCE",
        roceOpcode,
        bthReserved,
        l4DstPort,
        deltas.roceAck,
        deltas.flowlet,
        deltas.sprayMiss,
        deltas.cancel);
  }
};

class AgentFlowletMirrorTest : public AgentFlowletSwitchingTest {
 public:
  // TH* supports upto 4 different source types to mirror to same egress port.
  // Here IFP mirror action and ingress port sflow actions can generate 2 copies
  // going to same VIP or different VIP (different egress port in the test)
  enum MirrorScope {
    MIRROR_ONLY,
    MIRROR_SFLOW_SAME_VIP,
    MIRROR_SFLOW_DIFFERENT_VIP,
  };
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::DLB,
        ProductionFeature::SFLOWv6_SAMPLING,
        ProductionFeature::UDF_WR_IMMEDIATE_ACL,
        ProductionFeature::INGRESS_MIRRORING,
        ProductionFeature::SINGLE_ACL_TABLE};
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentFlowletSwitchingTest::initialConfig(ensemble);
    std::vector<std::string> udfGroups = getUdfGroupsForAcl(AclType::UDF_NAK);
    addAclTableConfig(cfg, udfGroups);
    cfg.udfConfig() = utility::addUdfAclConfig(
        utility::kUdfOffsetBthOpcode | utility::kUdfOffsetAethSyndrome);

    // mirror session for acl
    utility::configureSflowMirror(
        cfg, kAclMirror, false /* truncate */, aclDestinationVIP, 6344);

    return cfg;
  }

  void verifyMirror(MirrorScope scope) {
    // In addition to counting ACL hit with verifyAcl, verify packet mirrored
    auto mirrorPort = helper_->ecmpPortDescriptorAt(1).phyPortID();
    auto sflowPort = helper_->ecmpPortDescriptorAt(2).phyPortID();
    auto pktsMirrorBefore =
        *getNextUpdatedPortStats(mirrorPort).outUnicastPkts_();
    auto pktsSflowBefore =
        *getNextUpdatedPortStats(sflowPort).outUnicastPkts_();

    verifyAcl(AclType::UDF_NAK);

    WITH_RETRIES({
      auto pktsMirrorAfter =
          *getNextUpdatedPortStats(mirrorPort).outUnicastPkts_();
      auto pktsSflowAfter =
          *getNextUpdatedPortStats(sflowPort).outUnicastPkts_();
      XLOG(DBG2) << "PacketMirrorCounter: " << pktsMirrorBefore << " -> "
                 << pktsMirrorAfter
                 << " PacketSflowCounter: " << pktsSflowBefore << " -> "
                 << pktsSflowAfter;
      if (scope == MirrorScope::MIRROR_ONLY) {
        EXPECT_EVENTUALLY_GT(pktsMirrorAfter, pktsMirrorBefore);
      } else if (scope == MirrorScope::MIRROR_SFLOW_SAME_VIP) {
        EXPECT_EVENTUALLY_GE(pktsMirrorAfter, pktsMirrorBefore + 2);
      } else if (scope == MirrorScope::MIRROR_SFLOW_DIFFERENT_VIP) {
        EXPECT_EVENTUALLY_GT(pktsMirrorAfter, pktsMirrorBefore);
        EXPECT_EVENTUALLY_GT(pktsSflowAfter, pktsSflowBefore);
      }
    });
  }
};

// empty to UDF A
TEST_F(AgentFlowletSwitchingTest, VerifyFlowletToUdfFlowlet) {
  flowletSwitchingAclHitHelper(AclType::FLOWLET, AclType::UDF_FLOWLET);
}

// empty to UDF A + B
TEST_F(AgentFlowletSwitchingTest, VerifyFlowletToUdfFlowletWithUdfAck) {
  flowletSwitchingAclHitHelper(
      AclType::FLOWLET, AclType::UDF_FLOWLET_WITH_UDF_ACK);
}

// UDF A to UDF B
TEST_F(AgentFlowletSwitchingTest, VerifyUdfAckToUdfFlowlet) {
  flowletSwitchingAclHitHelper(AclType::UDF_ACK, AclType::UDF_FLOWLET);
}

// UDF A to UDF A + B
TEST_F(AgentFlowletSwitchingTest, VerifyUdfFlowletToUdfFlowletWithUdfAck) {
  flowletSwitchingAclHitHelper(
      AclType::UDF_FLOWLET, AclType::UDF_FLOWLET_WITH_UDF_ACK);
}

// UDF A to UDF A + B + C
TEST_F(AgentFlowletSwitchingTest, VerifyUdfFlowletToUdfFlowletWithUdfNak) {
  flowletSwitchingAclHitHelper(
      AclType::UDF_FLOWLET, AclType::UDF_FLOWLET_WITH_UDF_NAK);
}

// UDF A + B + C to UDF B + C
TEST_F(AgentFlowletSwitchingTest, VerifyUdfFlowletWithUdfNakToUdfNak) {
  flowletSwitchingAclHitHelper(
      AclType::UDF_FLOWLET_WITH_UDF_NAK, AclType::UDF_NAK);
}

// UDF A + B to UDF B
TEST_F(AgentFlowletSwitchingTest, VerifyUdfFlowletWithUdfAckToUdfAck) {
  flowletSwitchingAclHitHelper(
      AclType::UDF_FLOWLET_WITH_UDF_ACK, AclType::UDF_ACK);
}

// UDF A to UDF B
TEST_F(AgentFlowletSwitchingTest, VerifyUdfFlowletToUdfAck) {
  flowletSwitchingAclHitHelper(AclType::UDF_FLOWLET, AclType::UDF_ACK);
}

// UDF A + B to empty
TEST_F(AgentFlowletSwitchingTest, VerifyUdfFlowletWithUdfAckToFlowlet) {
  flowletSwitchingAclHitHelper(
      AclType::UDF_FLOWLET_WITH_UDF_ACK, AclType::FLOWLET);
}

class AgentFlowletSprayTest : public AgentFlowletSwitchingTest {
 protected:
  void setCmdLineFlagOverrides() const override {
    AgentFlowletSwitchingTest::setCmdLineFlagOverrides();
    FLAGS_dlbResourceCheckEnable = false;
    FLAGS_flowletStatsEnable = true;
    FLAGS_enable_ecmp_resource_manager = true;
    FLAGS_ars_resource_percentage = 100;
    FLAGS_ecmp_resource_manager_make_before_break_buffer = 0;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::DLB,
        ProductionFeature::ECMP_RANDOM_SPRAY,
        ProductionFeature::ACL_COUNTER,
        ProductionFeature::UDF_WR_IMMEDIATE_ACL,
        ProductionFeature::SINGLE_ACL_TABLE};
  }

  std::optional<size_t> maxRequiredInterfacePorts() const override {
    // VerifyEcmpRandomSpray uses ecmpPortDescriptorAt(0..13), needs >=14 ports
    return 15;
  }
};

/* Add route 3001::1 > DLB
 * Add route 4001::1 > Spray
 * Add route 5001::1 > Spray
 *
 * Test 1:
 * Send 3001::1 AR=1 hit ACL 1, do DLB
 * Send 3001::1 AR=0 hit ACL 2, cancel spray, static ECMP
 *
 * Test 2:
 * Send 4001::1 AR=1 hit ACL 1, no DLB, random spray
 * Send 4001::1 AR=0 hit ACL 2, cancel spray, static ECMP
 *
 * Test 3:
 * Send 5001::1 AR=1 hit ACL 1, no DLB, random spray
 * Send 5001::1 AR=0 hit ACL 2, cancel spray, static ECMP
 */
TEST_F(AgentFlowletSprayTest, VerifyEcmpRandomSpray) {
  auto populatePortsAndDescs =
      [this](
          int start,
          int end,
          std::vector<PortID>& portIDs,
          boost::container::flat_set<PortDescriptor>& portDescs) {
        std::vector<PortDescriptor> tempPortDescs;
        for (int w = start; w < end; ++w) {
          portIDs.push_back(helper_->ecmpPortDescriptorAt(w).phyPortID());
          tempPortDescs.emplace_back(helper_->ecmpPortDescriptorAt(w));
        }
        portDescs.insert(
            std::make_move_iterator(tempPortDescs.begin()),
            std::make_move_iterator(tempPortDescs.end()));
      };

  std::vector<PortID> dlbPortIDs;
  boost::container::flat_set<PortDescriptor> dlbPortDescs;
  populatePortsAndDescs(0, 4, dlbPortIDs, dlbPortDescs);

  std::vector<PortID> sprayPortIDs;
  boost::container::flat_set<PortDescriptor> randomSprayPortDescs;
  populatePortsAndDescs(4, 8, sprayPortIDs, randomSprayPortDescs);

  std::vector<PortID> sprayPortIDs2;
  boost::container::flat_set<PortDescriptor> randomSprayPortDescs2;
  populatePortsAndDescs(10, 14, sprayPortIDs2, randomSprayPortDescs2);

  auto setup = [this,
                dlbPortDescs,
                randomSprayPortDescs,
                randomSprayPortDescs2]() {
    generatePrefixes();

    auto newCfg{initialConfig(*getAgentEnsemble())};
    std::vector<std::string> udfGroups =
        getUdfGroupsForAcl(AclType::UDF_FLOWLET);
    addAclTableConfig(newCfg, udfGroups);

    // 1. add higher priority DLB enable ACL matching on BTH reserved field
    addAclAndStat(&newCfg, AclType::UDF_FLOWLET, getAgentEnsemble()->isSai());
    // overwrite match action to also include Flowlet action
    newCfg.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
    cfg::MatchAction matchAction = cfg::MatchAction();
    matchAction.flowletAction() = cfg::FlowletAction::FORWARD;
    auto aclCounterName = getCounterName(AclType::UDF_FLOWLET);
    matchAction.counter() = aclCounterName;
    utility::addMatcher(&newCfg, getAclName(AclType::UDF_FLOWLET), matchAction);

    // 2. add catch-all ECMP hash cancel ACL
    addAclAndStat(
        &newCfg, AclType::ECMP_HASH_CANCEL, getAgentEnsemble()->isSai());
    applyNewConfig(newCfg);

    // 200000 - 2000126 - DLB ECMP groups we don't care
    // 200127 - DLB ECMP group under test
    // 200128 - Random spray ECMP group under test
    const auto kMaxDlbEcmpGroup = getMaxArsGroups() - 1;
    auto wrapper = getSw()->getRouteUpdater();
    std::vector<RoutePrefixV6> prefixes128 = {
        prefixes.begin(), prefixes.begin() + kMaxDlbEcmpGroup};
    std::vector<boost::container::flat_set<PortDescriptor>> nhopSets128 = {
        nhopSets.begin(), nhopSets.begin() + kMaxDlbEcmpGroup};

    // generate route hitting DLB
    auto dlbPrefix = RoutePrefixV6{folly::IPAddressV6("3001::1"), 128};
    prefixes128.push_back(dlbPrefix);
    nhopSets128.push_back(dlbPortDescs);

    // generate route hitting random spray
    auto randomSprayPrefix = RoutePrefixV6{folly::IPAddressV6("4001::1"), 128};
    prefixes128.push_back(randomSprayPrefix);
    nhopSets128.push_back(randomSprayPortDescs);

    // generate route hitting random spray
    auto randomSprayPrefix2 = RoutePrefixV6{folly::IPAddressV6("5001::1"), 128};
    prefixes128.push_back(randomSprayPrefix2);
    nhopSets128.push_back(randomSprayPortDescs2);

    // 3. program routes
    helper_->programRoutes(&wrapper, nhopSets128, prefixes128);

    XLOG(DBG3) << "setting ECMP Member Status: ";
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      auto out = in->clone();
      for (const auto& [_, switchSetting] :
           std::as_const(*out->getSwitchSettings())) {
        auto newSwitchSettings = switchSetting->modify(&out);
        newSwitchSettings->setForceEcmpDynamicMemberUp(true);
      }
      return out;
    });
  };
  auto verify = [this, dlbPortIDs, sprayPortIDs, sprayPortIDs2]() {
    auto sendTrafficAndVerifyLB = [this](
                                      const folly::IPAddress& dstIp,
                                      int reserved,
                                      const std::vector<PortID>& ports,
                                      bool loadBalanceExpected,
                                      bool is_dlb = false) {
      auto switchId = getSw()
                          ->getScopeResolver()
                          ->scope(masterLogicalPortIds()[0])
                          .switchId();
      HwFlowletStats flowletStats;
      checkWithRetry(
          [&flowletStats, switchId, sw = getSw()]() {
            auto switchStats = sw->getHwSwitchStatsExpensive();
            if (switchStats.find(switchId) == switchStats.end()) {
              return false;
            }
            flowletStats = *switchStats.at(switchId).flowletStats();
            return true;
          },
          120,
          std::chrono::milliseconds(1000),
          " fetch port stats");

      auto reassignmentCounterBefore =
          flowletStats.l3EcmpDlbPortReassignmentCount().value();

      auto dlbAclCountBefore = utility::getAclInOutPackets(
          getSw(), getCounterName(AclType::UDF_FLOWLET));
      auto cancelAclCountBefore = utility::getAclInOutPackets(
          getSw(), getCounterName(AclType::ECMP_HASH_CANCEL));

      auto egressPort =
          helper_->ecmpPortDescriptorAt(kFrontPanelPortForTest).phyPortID();
      int packetCount = 200000;
      auto vlanId = getVlanIDForTx();
      auto intfMac =
          getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
      utility::pumpRoCETraffic(
          true,
          utility::getAllocatePktFn(getAgentEnsemble()),
          utility::getSendPktFunc(getAgentEnsemble()),
          intfMac,
          vlanId,
          egressPort,
          folly::IPAddress("1001::1"),
          dstIp,
          utility::kUdfL4DstPort,
          255,
          std::nullopt,
          packetCount,
          utility::kUdfRoceOpcodeAck,
          reserved,
          {});

      WITH_RETRIES({
        auto dlbAclCountAfter = utility::getAclInOutPackets(
            getSw(), getCounterName(AclType::UDF_FLOWLET));
        auto cancelAclCountAfter = utility::getAclInOutPackets(
            getSw(), getCounterName(AclType::ECMP_HASH_CANCEL));

        XLOG(DBG2) << "\n"
                   << "aclPacketCounter("
                   << getCounterName(AclType::UDF_FLOWLET)
                   << "): " << dlbAclCountBefore << " -> " << (dlbAclCountAfter)
                   << "\n"
                   << "aclPacketCounter("
                   << getCounterName(AclType::ECMP_HASH_CANCEL)
                   << "): " << cancelAclCountBefore << " -> "
                   << (cancelAclCountAfter) << "\n";

        if (reserved) {
          EXPECT_EVENTUALLY_GE(
              dlbAclCountAfter, dlbAclCountBefore + packetCount);
          EXPECT_EVENTUALLY_EQ(cancelAclCountAfter, cancelAclCountBefore);
        } else {
          EXPECT_EVENTUALLY_GE(
              cancelAclCountAfter, cancelAclCountBefore + packetCount);
          EXPECT_EVENTUALLY_EQ(dlbAclCountAfter, dlbAclCountBefore);
        }

        auto reassignmentCounterAfter =
            getSw()
                ->getHwSwitchStatsExpensive(switchId)
                .flowletStats()
                ->l3EcmpDlbPortReassignmentCount()
                .value();
        XLOG(DBG2) << "reassignmentCounter: " << reassignmentCounterBefore
                   << " -> " << reassignmentCounterAfter;
        if (is_dlb) {
          EXPECT_EVENTUALLY_GT(
              reassignmentCounterAfter, reassignmentCounterBefore);
        } else {
          EXPECT_EVENTUALLY_EQ(
              reassignmentCounterAfter, reassignmentCounterBefore);
        }

        auto portStats = getNextUpdatedPortStats(ports);
        for (const auto& [portId, stats] : portStats) {
          XLOG(DBG2) << "Ecmp egress Port: " << portId
                     << ", Count: " << *stats.outUnicastPkts_();
        }
        if (loadBalanceExpected) {
          EXPECT_EVENTUALLY_TRUE(utility::isLoadBalanced(portStats, 25));
        } else {
          EXPECT_EVENTUALLY_FALSE(utility::isLoadBalanced(portStats, 25));
        }
      });

      getAgentEnsemble()->clearPortStats();
    };

    // Test 1
    // Hit DLB ACL and do DLB
    sendTrafficAndVerifyLB(
        folly::IPAddress("3001::1"),
        utility::kRoceReserved,
        dlbPortIDs,
        true,
        true);
    // Miss DLB ACL and do static hash
    sendTrafficAndVerifyLB(folly::IPAddress("3001::1"), 0, dlbPortIDs, false);

    // Test 2
    // Hit DLB ACL but do random spray
    sendTrafficAndVerifyLB(
        folly::IPAddress("4001::1"),
        utility::kRoceReserved,
        sprayPortIDs,
        true);
    // Miss DLB ACL and do static hash
    sendTrafficAndVerifyLB(folly::IPAddress("4001::1"), 0, sprayPortIDs, false);

    // Test 3
    // Hit DLB ACL but do random spray. Verify another group just to be sure
    sendTrafficAndVerifyLB(
        folly::IPAddress("5001::1"),
        utility::kRoceReserved,
        sprayPortIDs2,
        true);
    // Miss DLB ACL and do static hash
    sendTrafficAndVerifyLB(
        folly::IPAddress("5001::1"), 0, sprayPortIDs2, false);
  };

  verifyAcrossWarmBoots(setup, verify);
};

TEST_F(AgentFlowletSwitchingTest, VerifyEcmp) {
  auto setup = [this]() {
    this->setup(kWideEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
  };

  auto verify = [this]() {
    auto verifyCounts = [this](int destPort, bool bumpOnHit) {
      // gather stats for all ECMP members
      int pktsBefore[kWideEcmpWidth];
      int pktsBeforeTotal = 0;
      for (int i = 0; i < kWideEcmpWidth; i++) {
        auto ecmpEgressPort = helper_->ecmpPortDescriptorAt(i).phyPortID();
        pktsBefore[i] =
            *getNextUpdatedPortStats(ecmpEgressPort).outUnicastPkts_();
        pktsBeforeTotal += pktsBefore[i];
      }
      // Use appropriate ACL counter based on ARS configuration
      auto aclPktCountBefore = utility::getAclInOutPackets(
          getSw(), getCounterName(AclType::FLOWLET));
      int packetCount = 1000;

      std::vector<uint8_t> rethHdr(16);
      rethHdr[15] = 0xFF; // non-zero sized packet
      auto egressPort =
          helper_->ecmpPortDescriptorAt(kFrontPanelPortForTest).phyPortID();
      sendRoceTraffic(
          egressPort,
          utility::kUdfRoceOpcodeWriteImmediate,
          rethHdr,
          packetCount,
          destPort);

      WITH_RETRIES({
        auto aclPktCountAfter = utility::getAclInOutPackets(
            getSw(), getCounterName(AclType::FLOWLET));

        int pktsAfter[kWideEcmpWidth];
        int pktsAfterTotal = 0;
        for (int i = 0; i < kWideEcmpWidth; i++) {
          auto ecmpEgressPort = helper_->ecmpPortDescriptorAt(i).phyPortID();
          pktsAfter[i] =
              *getNextUpdatedPortStats(ecmpEgressPort).outUnicastPkts_();
          pktsAfterTotal += pktsAfter[i];
          XLOG(DBG2) << "Ecmp egress Port: " << ecmpEgressPort
                     << ", Count: " << pktsBefore[i] << " -> " << pktsAfter[i];
        }

        XLOG(DBG2) << "\n"
                   << "aclPacketCounter(" << getCounterName(AclType::FLOWLET)
                   << "): " << aclPktCountBefore << " -> " << (aclPktCountAfter)
                   << "\n";

        // Irrespective of which LB mechanism is used, all packets should egress
        EXPECT_EVENTUALLY_GE(pktsAfterTotal, pktsBeforeTotal + packetCount);

        // Verify ACL count also for hit and miss
        if (bumpOnHit) {
          EXPECT_EVENTUALLY_GE(
              aclPktCountAfter, aclPktCountBefore + packetCount);
        } else {
          EXPECT_EVENTUALLY_EQ(aclPktCountAfter, aclPktCountBefore);
          // also verify traffic is not load-balanced, implying,
          // 3 out of the 4 egress ports should have 0 count
          int zeroCount = 0;
          for (int i = 0; i < kWideEcmpWidth; i++) {
            if (pktsAfter[i] - pktsBefore[i] == 0) {
              zeroCount++;
            }
          }
          EXPECT_EVENTUALLY_EQ(kWideEcmpWidth - 1, zeroCount);
        }
      });
    };

    // Verify DLB is hit with ACL matching packet
    verifyCounts(4791, true);
    // Verify packet is still ECMP'd without DLB using static hash
    verifyCounts(1024, false);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Pruning off: every member, including the one traffic ingressed on, has to
// carry an even share and nothing may be dropped.
TEST_F(
    AgentFlowletSourcePortPruneTest,
    VerifyDlbSecondaryNoPruneFourMembersBalancesEvenly) {
  constexpr int kEcmpWidth = 4;
  constexpr int kEcmpMemberInjectionPort = 0;

  auto setup = [&]() {
    checkEnoughPhyLoopbackPorts(kEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
    this->setup(kEcmpWidth);
  };

  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: nothing is a pruning candidate,
    // so these are the controls for the member ingress phases below.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::Dlb,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondarySingleFlow,
        false,
        1 /* no entropy, so exactly one member */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    // Phases 4-6, ingress on a member: same three bursts with the ingress
    // inside the group, so split horizon can act on it.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::Dlb,
        false,
        kEcmpWidth - 1 /* the count excludes the ingress */,
        kMemberIngressDlbShareBounds);
    // Nothing is pruned, so the hash may pick the ingress itself -- and the
    // helper counts the ingress separately, which would read as zero members.
    // Phase 2 already pins the one member property with an ingress outside the
    // group; here only absence of loss is assertable.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondarySingleFlow,
        false,
        std::nullopt,
        std::nullopt,
        false /* assertMemberCount */);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        kEvenDlbShareBounds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(
    AgentFlowletSourcePortPruneTest,
    VerifyDlbSecondaryNoPruneFiveMembersBalancesEvenly) {
  constexpr int kEcmpWidth = 5;
  constexpr int kEcmpMemberInjectionPort = 0;

  auto setup = [&]() {
    checkEnoughPhyLoopbackPorts(kEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
    this->setup(kEcmpWidth);
  };

  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: nothing is a pruning candidate,
    // so these are the controls for the member ingress phases below.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::Dlb,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondarySingleFlow,
        false,
        1 /* no entropy, so exactly one member */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    // Phases 4-6, ingress on a member: same three bursts with the ingress
    // inside the group, so split horizon can act on it.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::Dlb,
        false,
        kEcmpWidth - 1 /* the count excludes the ingress */,
        kMemberIngressDlbShareBounds);
    // Nothing is pruned, so the hash may pick the ingress itself -- and the
    // helper counts the ingress separately, which would read as zero members.
    // Phase 2 already pins the one member property with an ingress outside the
    // group; here only absence of loss is assertable.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondarySingleFlow,
        false,
        std::nullopt,
        std::nullopt,
        false /* assertMemberCount */);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        kEvenDlbShareBounds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(
    AgentFlowletSourcePortPruneTest,
    VerifyDlbSecondaryNoPruneSixMembersBalancesEvenly) {
  constexpr int kEcmpWidth = 6;
  constexpr int kEcmpMemberInjectionPort = 0;

  auto setup = [&]() {
    checkEnoughPhyLoopbackPorts(kEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
    this->setup(kEcmpWidth);
  };

  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: nothing is a pruning candidate,
    // so these are the controls for the member ingress phases below.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::Dlb,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondarySingleFlow,
        false,
        1 /* no entropy, so exactly one member */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    // Phases 4-6, ingress on a member: same three bursts with the ingress
    // inside the group, so split horizon can act on it.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::Dlb,
        false,
        kEcmpWidth - 1 /* the count excludes the ingress */,
        kMemberIngressDlbShareBounds);
    // Nothing is pruned, so the hash may pick the ingress itself -- and the
    // helper counts the ingress separately, which would read as zero members.
    // Phase 2 already pins the one member property with an ingress outside the
    // group; here only absence of loss is assertable.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondarySingleFlow,
        false,
        std::nullopt,
        std::nullopt,
        false /* assertMemberCount */);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        kEvenDlbShareBounds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Pruning on, four members: needs no padding, so the pruned member's slot goes
// to a single survivor which then carries twice what the others do.
TEST_F(
    AgentFlowletArsSourcePortPruneEnabledTest,
    VerifyDlbSecondaryPruneFourMembersRedistributesToOneMember) {
  constexpr int kEcmpWidth = 4;
  constexpr int kEcmpMemberInjectionPort = 0;

  auto setup = [&]() {
    checkEnoughPhyLoopbackPorts(kEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
    this->setup(kEcmpWidth);
  };

  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: nothing is a pruning candidate,
    // so these are the controls for the member ingress phases below.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::Dlb,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondarySingleFlow,
        false,
        1 /* no entropy, so exactly one member */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    // Phases 4-6, ingress on a member: same three bursts with the ingress
    // inside the group, so split horizon can act on it.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::Dlb,
        true,
        kEcmpWidth - 1 /* the count excludes the ingress */,
        kPrunedDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondarySingleFlow,
        true,
        1 /* one member; pruning only changes which one */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondaryMultiFlow,
        true,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        kPrunedDlbShareBounds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Pruning on, five and six members: padding rounds the secondary up to eight
// slots with duplicate entries, so the spread is 2:1 by construction and the
// DLB phase asserts only that nothing is lost. Member count and balance are
// asserted on the hash cancelled phases, which are not slot quantised the same
// way.
TEST_F(
    AgentFlowletArsSourcePortPruneEnabledTest,
    VerifyDlbSecondaryPruneFiveMembersNoLoss) {
  constexpr int kEcmpWidth = 5;
  constexpr int kEcmpMemberInjectionPort = 0;

  auto setup = [&]() {
    checkEnoughPhyLoopbackPorts(kEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
    this->setup(kEcmpWidth);
  };

  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: nothing is a pruning candidate,
    // so these are the controls for the member ingress phases below.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::Dlb,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondarySingleFlow,
        false,
        1 /* no entropy, so exactly one member */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    // Phases 4-6, ingress on a member: same three bursts with the ingress
    // inside the group, so split horizon can act on it.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::Dlb,
        true,
        kEcmpWidth - 1 /* the count excludes the ingress */,
        kPrunedDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondarySingleFlow,
        true,
        1 /* one member; pruning only changes which one */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondaryMultiFlow,
        true,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        kPrunedDlbShareBounds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(
    AgentFlowletArsSourcePortPruneEnabledTest,
    VerifyDlbSecondaryPruneSixMembersNoLoss) {
  constexpr int kEcmpWidth = 6;
  constexpr int kEcmpMemberInjectionPort = 0;

  auto setup = [&]() {
    checkEnoughPhyLoopbackPorts(kEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
    this->setup(kEcmpWidth);
  };

  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: nothing is a pruning candidate,
    // so these are the controls for the member ingress phases below.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::Dlb,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondarySingleFlow,
        false,
        1 /* no entropy, so exactly one member */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    // Phases 4-6, ingress on a member: same three bursts with the ingress
    // inside the group, so split horizon can act on it.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::Dlb,
        true,
        kEcmpWidth - 1 /* the count excludes the ingress */,
        kPrunedDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondarySingleFlow,
        true,
        1 /* one member; pruning only changes which one */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondaryMultiFlow,
        true,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        kPrunedDlbShareBounds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Third group category: a plain ECMP group running random spray.
//
// There is no config knob that asks for one. A group becomes a spray group by
// spilling over: once every ARS group is spoken for, the next group the
// resource manager creates cannot be DLB and is created as random spray
// instead. So the fixture fills ARS capacity with filler routes and only then
// programs the group under test, which is the ::/0 route the shared helper
// already targets.
class AgentFlowletSprayGroupPruneTest : public AgentFlowletSourcePortPruneTest {
 protected:
  void setCmdLineFlagOverrides() const override {
    AgentFlowletSourcePortPruneTest::setCmdLineFlagOverrides();
    // Same knobs VerifyEcmpRandomSpray uses to make the spillover reachable:
    // no resource ceiling of its own, and the whole ARS pool available so the
    // fill is deterministic.
    FLAGS_dlbResourceCheckEnable = false;
    FLAGS_flowletStatsEnable = true;
    FLAGS_enable_ecmp_resource_manager = true;
    FLAGS_ars_resource_percentage = 100;
    FLAGS_ecmp_resource_manager_make_before_break_buffer = 0;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features =
        AgentFlowletSourcePortPruneTest::getProductionFeaturesVerified();
    features.push_back(ProductionFeature::ECMP_RANDOM_SPRAY);
    return features;
  }

  // Consume every ARS group, then program ::/0 across the ports under test so
  // it is the one that spills over into spray.
  void setupSprayGroup(int ecmpWidth) {
    checkEnoughPhyLoopbackPorts(ecmpWidth);
    generateApplyConfig(AclType::FLOWLET);
    generatePrefixes();

    const auto fillCount = getMaxArsGroups();
    CHECK_GE(prefixes.size(), fillCount)
        << "need " << fillCount << " filler prefixes to exhaust ARS, have "
        << prefixes.size();
    std::vector<RoutePrefixV6> fillPrefixes{
        prefixes.begin(), prefixes.begin() + fillCount};
    std::vector<boost::container::flat_set<PortDescriptor>> fillNhopSets{
        nhopSets.begin(), nhopSets.begin() + fillCount};
    auto wrapper = getSw()->getRouteUpdater();
    helper_->programRoutes(&wrapper, fillNhopSets, fillPrefixes);

    // ARS is now full, so this one is created as random spray.
    this->setup(ecmpWidth);
  }
};

// Pruning off. Phase 2/5 is the tell that this really is a spray group: one
// 5 tuple with no entropy still reaches every member, which a hashing group
// cannot do. If the spillover failed and the group came out as DLB or static
// hash, that phase collapses onto one member and the test fails rather than
// quietly passing against the wrong group type.
//
// A spray group carries no ARS object, so there is no secondary group and no
// static hash: all three traffic types spray across every member.
TEST_F(AgentFlowletSprayGroupPruneTest, VerifySprayGroupNoPruneSpreadsEvenly) {
  constexpr int kEcmpWidth = 6;
  constexpr int kEcmpMemberInjectionPort = 0;

  auto setup = [&]() { setupSprayGroup(kEcmpWidth); };

  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: nothing is a pruning candidate,
    // so these are the controls for the member ingress phases below.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::Dlb,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondarySingleFlow,
        false,
        kEcmpWidth /* spray ignores entropy, so one flow still reaches all */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    // Phases 4-6, ingress on a member: same three bursts with the ingress
    // inside the group, so split horizon could act on it if it were on.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::Dlb,
        false,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        kPrunedDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondarySingleFlow,
        false,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        kEvenDlbShareBounds);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Pruning on.
class AgentFlowletSprayGroupPruneEnabledTest
    : public AgentFlowletSprayGroupPruneTest {
 protected:
  EcmpGroupSettingsMap splitHorizonSettings() const override {
    return splitHorizonOn({cfg::EcmpGroupType::ECMP_SPRAY});
  }
};

TEST_F(
    AgentFlowletSprayGroupPruneEnabledTest,
    VerifySprayGroupPruneExcludesIngress) {
  constexpr int kEcmpWidth = 6;
  constexpr int kEcmpMemberInjectionPort = 0;

  auto setup = [&]() { setupSprayGroup(kEcmpWidth); };

  auto verify = [&]() {
    // Phases 1-3, ingress outside the group: unchanged from the no prune case,
    // which is what shows pruning does not perturb a group it cannot act on.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::Dlb,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondarySingleFlow,
        false,
        kEcmpWidth /* spray ignores entropy, so one flow still reaches all */,
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kFrontPanelPortForTest,
        TrafficType::SecondaryMultiFlow,
        false,
        kEcmpWidth,
        kEvenDlbShareBounds);
    // Phases 4-6, ingress on a member: the ingress must now carry nothing, and
    // its share redistributes onto a single survivor.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::Dlb,
        true,
        kEcmpWidth - 1,
        // No bound. On a spray group a member ingress already skews the spread
        // before pruning, and pruning then hands the ingress share to a single
        // survivor that is often one of the already hot ports -- measured 177%,
        // past any bound loose enough to still mean anything. The member count
        // above is the guarantee that every survivor keeps carrying traffic.
        std::nullopt);
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondarySingleFlow,
        true,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        std::nullopt);
    // No deviation bound: member ingress already skews spray, and pruning
    // hands the ingress share to one survivor. Member count and the ingress
    // carrying nothing are the split horizon signals; balance is asserted in
    // the ingress-outside phases.
    sendFlowsAndVerifyPrune(
        kEcmpWidth,
        kEcmpMemberInjectionPort,
        TrafficType::SecondaryMultiFlow,
        true,
        kEcmpWidth - 1 /* ingress is excluded from the count */,
        std::nullopt);
  };

  verifyAcrossWarmBoots(setup, verify);
}

class AgentFlowletWideArsSwitchingTest : public AgentFlowletSwitchingTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::DLB,
        ProductionFeature::VIRTUAL_ARS_GROUP,
        ProductionFeature::UDF_WR_IMMEDIATE_ACL,
        ProductionFeature::SINGLE_ACL_TABLE};
  }
  void setCmdLineFlagOverrides() const override {
    AgentFlowletSwitchingTest::setCmdLineFlagOverrides();
    FLAGS_dlbResourceCheckEnable = true;
    FLAGS_enable_route_resource_protection = true;
    FLAGS_ecmp_width = kWideEcmpWidth;
    FLAGS_ars_resource_percentage = 100;
  }

 protected:
  static constexpr int kWideEcmpWidth = 256;
  static constexpr int kStatsCheckInterval = 25;
  static constexpr int kMinWidthForArsVirtualGroup = 65;
  static constexpr int kMaxVirtualArsGroups = 255;

  std::vector<PortID> getSubsidiaryPorts(const AgentEnsemble& ensemble) const {
    auto portsByControllingPort = utility::getSubsidiaryPortIDs(
        ensemble.getSw()->getPlatformMapping()->getPlatformPorts());
    const auto& platformPorts =
        ensemble.getSw()->getPlatformMapping()->getPlatformPorts();
    std::vector<PortID> ports;
    const SwitchID currentSwitchId = getCurrentSwitchIdForTesting();
    for (const auto& [controllingPort, subPorts] : portsByControllingPort) {
      if (ports.size() >= kWideEcmpWidth) {
        break;
      }
      auto ctrlIt = platformPorts.find(static_cast<int32_t>(controllingPort));
      if (ctrlIt == platformPorts.end() ||
          *ctrlIt->second.mapping()->portType() !=
              cfg::PortType::INTERFACE_PORT) {
        continue;
      }
      if (ensemble.scopeResolver().scope(PortID(controllingPort)).switchId() !=
          currentSwitchId) {
        continue;
      }
      for (auto subPort : subPorts) {
        auto subIt = platformPorts.find(static_cast<int32_t>(subPort));
        if (subIt != platformPorts.end() &&
            *subIt->second.mapping()->portType() ==
                cfg::PortType::MANAGEMENT_PORT) {
          continue;
        }
        ports.push_back(subPort);
      }
    }
    CHECK_GE(ports.size(), kWideEcmpWidth);
    return ports;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto ports = getSubsidiaryPorts(ensemble);
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(), ports, true /*interfaceHasSubnet*/);
    const auto& platformPorts =
        ensemble.getSw()->getPlatformMapping()->getPlatformPorts();

    // Force speed to 200G to create atleast 256 ports for th6
    for (auto& port : *cfg.ports()) {
      if (*port.speed() > cfg::PortSpeed::TWOHUNDREDG) {
        auto platIt =
            platformPorts.find(static_cast<int32_t>(*port.logicalID()));
        if (platIt != platformPorts.end()) {
          for (const auto& [profileID, profileCfg] :
               *platIt->second.supportedProfiles()) {
            if (utility::getSpeed(profileID) == cfg::PortSpeed::TWOHUNDREDG) {
              port.profileID() = profileID;
              port.speed() = cfg::PortSpeed::TWOHUNDREDG;
              break;
            }
          }
        }
      }
    }
    auto backupSwitchingMode = isChenab(ensemble)
        ? cfg::SwitchingMode::FIXED_ASSIGNMENT
        : cfg::SwitchingMode::PER_PACKET_RANDOM;
    utility::addFlowletConfigs(
        cfg,
        ports,
        ensemble.isSai(),
        cfg::SwitchingMode::PER_PACKET_QUALITY,
        backupSwitchingMode);
    cfg.flowletSwitchingConfig()->minWidthForArsVirtualGroup() =
        kMinWidthForArsVirtualGroup;
    cfg.flowletSwitchingConfig()->maxArsVirtualGroups() = kMaxVirtualArsGroups;
    cfg.flowletSwitchingConfig()->maxArsVirtualGroupWidth() = kWideEcmpWidth;
    return cfg;
  }

  std::vector<PortID> getTestPorts() const override {
    return getSubsidiaryPorts(*getAgentEnsemble());
  }

  void verifyTrafficDistribution(int ecmpWidth, int destPort, bool bumpOnHit) {
    std::vector<int> pktsBefore(ecmpWidth);
    int pktsBeforeTotal = 0;
    for (int i = 0; i < ecmpWidth; i += kStatsCheckInterval) {
      auto ecmpEgressPort = helper_->ecmpPortDescriptorAt(i).phyPortID();
      pktsBefore[i] =
          *getNextUpdatedPortStats(ecmpEgressPort).outUnicastPkts_();
      pktsBeforeTotal += pktsBefore[i];
    }

    auto aclPktCountBefore =
        utility::getAclInOutPackets(getSw(), getCounterName(AclType::FLOWLET));
    constexpr int packetCount = 1000;

    std::vector<uint8_t> rethHdr(16);
    rethHdr[15] = 0xFF;
    auto egressPort =
        helper_->ecmpPortDescriptorAt(kFrontPanelPortForTest).phyPortID();
    sendRoceTraffic(
        egressPort,
        utility::kUdfRoceOpcodeWriteImmediate,
        rethHdr,
        packetCount,
        destPort);

    WITH_RETRIES_N(5, {
      auto aclPktCountAfter = utility::getAclInOutPackets(
          getSw(), getCounterName(AclType::FLOWLET));

      std::vector<int> pktsAfter(ecmpWidth);
      int pktsAfterTotal = 0;
      for (int i = 0; i < ecmpWidth; i += kStatsCheckInterval) {
        auto ecmpEgressPort = helper_->ecmpPortDescriptorAt(i).phyPortID();
        pktsAfter[i] =
            *getNextUpdatedPortStats(ecmpEgressPort).outUnicastPkts_();
        pktsAfterTotal += pktsAfter[i];
        XLOG(DBG2) << "Ecmp egress Port: " << ecmpEgressPort
                   << ", Count: " << pktsBefore[i] << " -> " << pktsAfter[i];
      }

      XLOG(DBG2) << "\n"
                 << "aclPacketCounter(" << getCounterName(AclType::FLOWLET)
                 << "): " << aclPktCountBefore << " -> " << aclPktCountAfter
                 << "\n";

      EXPECT_EVENTUALLY_GT(pktsAfterTotal, pktsBeforeTotal);

      if (bumpOnHit) {
        EXPECT_EVENTUALLY_GE(aclPktCountAfter, aclPktCountBefore + packetCount);
      } else {
        EXPECT_EVENTUALLY_EQ(aclPktCountAfter, aclPktCountBefore);
        int zeroCount = 0;
        for (int i = 0; i < ecmpWidth; i += kStatsCheckInterval) {
          if (pktsAfter[i] - pktsBefore[i] == 0) {
            zeroCount++;
          }
        }
        int sampledPorts =
            (ecmpWidth + kStatsCheckInterval - 1) / kStatsCheckInterval;
        EXPECT_EVENTUALLY_GE(zeroCount, sampledPorts - 1);
      }
    });
  }
};

TEST_F(AgentFlowletWideArsSwitchingTest, VerifyEcmp) {
  auto setup = [this]() {
    this->setup(kWideEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
  };

  auto verify = [this]() {
    // Verify DLB is hit with ACL matching packet
    verifyTrafficDistribution(kWideEcmpWidth, 4791, true);
    // Verify packet is still ECMP'd without DLB using static hash
    verifyTrafficDistribution(kWideEcmpWidth, 1024, false);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentFlowletWideArsSwitchingTest, EcmpScaleTest) {
  static constexpr int kDefaultRouteEcmpWidth = 256;
  static constexpr int kSecondaryEcmpWidth = 90;
  static constexpr int kNumSecondaryEcmpGroups = 254;

  auto setup = [this]() {
    this->setup(kDefaultRouteEcmpWidth);
    generateApplyConfig(AclType::FLOWLET);
    generatePrefixes();

    auto wrapper = getSw()->getRouteUpdater();

    std::vector<RoutePrefixV6> testPrefixes;
    std::vector<boost::container::flat_set<PortDescriptor>> testNhopSets;
    testPrefixes.reserve(kNumSecondaryEcmpGroups);
    testNhopSets.reserve(kNumSecondaryEcmpGroups);

    for (int i = 0; i < kNumSecondaryEcmpGroups; ++i) {
      testPrefixes.push_back(prefixes[i]);
      // Create unique nhop sets from 256 ports using rotation:
      // Groups 0-253: rotation with step=1, each group starts at port i
      boost::container::flat_set<PortDescriptor> nhopSet;
      int rotation = i % kDefaultRouteEcmpWidth;
      for (int j = 0; j < kSecondaryEcmpWidth; ++j) {
        int portIndex = (rotation + j) % kDefaultRouteEcmpWidth;
        nhopSet.insert(helper_->ecmpPortDescriptorAt(portIndex));
      }
      testNhopSets.push_back(nhopSet);
    }

    helper_->programRoutes(&wrapper, testNhopSets, testPrefixes);

    XLOG(DBG2) << "EcmpScaleTest::setup - programmed "
               << kNumSecondaryEcmpGroups << " secondary ECMP groups with "
               << kSecondaryEcmpWidth << " members each, "
               << "default route has " << kDefaultRouteEcmpWidth << " members";
  };

  auto verify = [this]() {
    // Verify DLB is hit with ACL matching packet
    verifyTrafficDistribution(kDefaultRouteEcmpWidth, 4791, true);
    // Verify packet is still ECMP'd without DLB using static hash
    verifyTrafficDistribution(kDefaultRouteEcmpWidth, 1024, false);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Two wide virtual ARS groups sharing part of their width, with slots reserved
// for alternate members. The adapter only programs the DGM parameters from a
// virtual group, so this only runs where VIRTUAL_ARS_GROUP is supported.
class AgentFlowletWideArsAlternateMemberTest
    : public AgentFlowletWideArsSwitchingTest {
 protected:
  static constexpr int kAlternateMembers = 8;
  static constexpr int kCommonMembersThreshold = 4;
  static constexpr int kFirstGroupWidth = 150;
  static constexpr int kSecondGroupWidth = 120;
  static constexpr int kCommonMembers = 30;
  // Overlap the tail of the first group with the head of the second.
  static constexpr int kSecondGroupStart = kFirstGroupWidth - kCommonMembers;
  static constexpr int kPortsUsed = kSecondGroupStart + kSecondGroupWidth;
  // Quality bands are three bits wide. At 7 the adapter documents the DGM
  // action as always valid, so the alternate path is always a candidate.
  static constexpr int kMaxQualityBand = 7;
  static constexpr int kTrafficPacketCount = 1000;
  // Member kFrontPanelPortForTest is left out of the totals below because
  // traffic is injected out of it, so even a fully collected spread accounts
  // for slightly less than kTrafficPacketCount. Leave room for that member's
  // share rather than waiting for a total that never arrives.
  static constexpr int kMinAccountedPackets = kTrafficPacketCount * 9 / 10;
  static constexpr int kDlbDestPort = 4791;
  // Alternate members all come out of the shared set, so once DGM moves the
  // flows onto them the shared set carries essentially everything.
  static constexpr double kAllTrafficOnSharedMembers = 0.9;
  // How far the overlap between the two groups slides when the common set is
  // churned. Members entering the window become common for the first time and
  // members leaving it go stale, both in the same update.
  static constexpr int kOverlapShift = 10;
  static constexpr int kShiftedStart = kSecondGroupStart - kOverlapShift;
  static constexpr int kShiftedEnd = kFirstGroupWidth - kOverlapShift;

  static_assert(
      kFirstGroupWidth >= kMinWidthForArsVirtualGroup &&
          kSecondGroupWidth >= kMinWidthForArsVirtualGroup,
      "both groups have to be wide enough to be backed by a virtual group");
  static_assert(
      kPortsUsed <= kWideEcmpWidth,
      "not enough ports to build both groups");
  static_assert(
      kCommonMembers > kCommonMembersThreshold,
      "common members have to exceed the threshold for the adapter to promote "
      "any of them to alternates");
  static_assert(
      kShiftedStart > 0 && kShiftedEnd < kFirstGroupWidth,
      "the shifted overlap has to leave first group members on both sides of "
      "the window so the update both adds and stales common members");
  static_assert(
      (kShiftedEnd - kShiftedStart) + (kPortsUsed - kFirstGroupWidth) >=
          kMinWidthForArsVirtualGroup,
      "the second group has to stay wide enough to be backed by a virtual "
      "group once the overlap moves");

  virtual int primaryPathQualityThreshold() const = 0;
  virtual int alternatePathCost() const = 0;
  virtual int alternatePathBias() const = 0;

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentFlowletWideArsSwitchingTest::initialConfig(ensemble);
    auto& flowletCfg = *cfg.flowletSwitchingConfig();
    flowletCfg.arsVirtualGroupAlternateMembers() = kAlternateMembers;
    flowletCfg.arsVirtualGroupCommonMembersThreshold() =
        kCommonMembersThreshold;
    flowletCfg.primaryPathQualityThreshold() = primaryPathQualityThreshold();
    flowletCfg.alternatePathCost() = alternatePathCost();
    flowletCfg.alternatePathBias() = alternatePathBias();
    return cfg;
  }

  // The traffic check sends to the default route, so make that the first
  // virtual group rather than standing a third one up beside it: a member only
  // counts as common once it appears in every group of the super group, so an
  // extra group would leave the two below with nothing shared.
  void setupTwoVirtualGroups() {
    this->setup(kFirstGroupWidth);
    generateApplyConfig(AclType::FLOWLET);
    generatePrefixes();

    boost::container::flat_set<PortDescriptor> secondGroup;
    for (int i = kSecondGroupStart; i < kPortsUsed; ++i) {
      secondGroup.insert(helper_->ecmpPortDescriptorAt(i));
    }
    std::vector<boost::container::flat_set<PortDescriptor>> nhopSets = {
        secondGroup};
    std::vector<RoutePrefixV6> testPrefixes = {prefixes[0]};

    auto wrapper = getSw()->getRouteUpdater();
    helper_->programRoutes(&wrapper, nhopSets, testPrefixes);

    XLOG(DBG2) << "Virtual ARS groups of " << kFirstGroupWidth << " and "
               << kSecondGroupWidth << " members sharing " << kCommonMembers
               << ", up to " << kAlternateMembers
               << " of the shared ones promoted to alternates; quality "
               << "threshold " << primaryPathQualityThreshold() << ", cost "
               << alternatePathCost() << ", bias " << alternatePathBias();
  }

  static std::vector<int> memberRange(int begin, int end) {
    std::vector<int> members;
    for (int i = begin; i < end; ++i) {
      members.push_back(i);
    }
    return members;
  }

  // Reprograms the second virtual group over the given first group member
  // indices. A member is common only while it sits in every group, so moving
  // this set is how members are made to gain or lose common status.
  void programSecondGroup(const std::vector<int>& memberIndices) {
    boost::container::flat_set<PortDescriptor> secondGroup;
    for (auto i : memberIndices) {
      secondGroup.insert(helper_->ecmpPortDescriptorAt(i));
    }
    std::vector<boost::container::flat_set<PortDescriptor>> nhopSets = {
        secondGroup};
    std::vector<RoutePrefixV6> testPrefixes = {prefixes[0]};
    auto wrapper = getSw()->getRouteUpdater();
    helper_->programRoutes(&wrapper, nhopSets, testPrefixes);
  }

  // Share of the traffic that left on first group members in [begin, end).
  double trafficFractionInRange(
      const std::vector<int64_t>& perMember,
      int begin,
      int end) const {
    int64_t inRange = 0;
    int64_t totalPkts = 0;
    for (int i = 0; i < kFirstGroupWidth; ++i) {
      if (i == kFrontPanelPortForTest) {
        continue;
      }
      totalPkts += perMember[i];
      if (i >= begin && i < end) {
        inRange += perMember[i];
      }
    }
    return static_cast<double>(inRange) / static_cast<double>(totalPkts);
  }

  // Walks the adapter through the common member transitions and leaves behind
  // exactly the two group state setupTwoVirtualGroups() would have, so the
  // caller's verify() and the warm boot rerun of it are unchanged. What the
  // warm boot then has to restore is a ring that has been churned rather than
  // one built in a single shot.
  void walkCommonMemberTransitions() {
    this->setup(kFirstGroupWidth);
    generateApplyConfig(AclType::FLOWLET);
    generatePrefixes();

    // One virtual group on its own: every member passes the common test
    // trivially, but each is already primary in its own DLB and a port cannot
    // sit in both sections, so none of them can become an alternate.
    {
      SCOPED_TRACE("first virtual group only");
      EXPECT_LT(
          trafficFractionInRange(
              sendDlbTrafficAndCountPerMember(),
              kSecondGroupStart,
              kFirstGroupWidth),
          kAllTrafficOnSharedMembers);
    }

    // Overlapping the tail of the first group takes the shared members past
    // the threshold. They are already in the ring from the first group, so the
    // adapter promotes them when the add re-confirms them rather than on the
    // insert path.
    {
      SCOPED_TRACE("second virtual group added");
      programSecondGroup(memberRange(kSecondGroupStart, kPortsUsed));
      EXPECT_GT(
          trafficFractionInRange(
              sendDlbTrafficAndCountPerMember(),
              kSecondGroupStart,
              kFirstGroupWidth),
          kAllTrafficOnSharedMembers);
    }

    // Slide the overlap down. Members entering the window become common for
    // the first time while those leaving it go stale, and the stale entries
    // only give their alternate slots back at the end of the member add, so
    // the new ones are promoted by the rescan that follows the stale scan.
    {
      SCOPED_TRACE("overlap window moved");
      auto shifted = memberRange(kShiftedStart, kShiftedEnd);
      auto beyondFirstGroup = memberRange(kFirstGroupWidth, kPortsUsed);
      shifted.insert(
          shifted.end(), beyondFirstGroup.begin(), beyondFirstGroup.end());
      programSecondGroup(shifted);
      EXPECT_GT(
          trafficFractionInRange(
              sendDlbTrafficAndCountPerMember(), kShiftedStart, kShiftedEnd),
          kAllTrafficOnSharedMembers);
    }

    // Put the original overlap back so verify() sees the same state it would
    // have after setupTwoVirtualGroups().
    programSecondGroup(memberRange(kSecondGroupStart, kPortsUsed));
  }

  // Sends DLB traffic once and returns the per member packet counts for the
  // first group, indexed the way ecmpPortDescriptorAt is. Every member is
  // counted rather than sampled: only a handful end up as alternates, so
  // sampling would miss them.
  std::vector<int64_t> sendDlbTrafficAndCountPerMember() {
    std::vector<PortID> ports;
    ports.reserve(kFirstGroupWidth);
    for (int i = 0; i < kFirstGroupWidth; ++i) {
      ports.push_back(helper_->ecmpPortDescriptorAt(i).phyPortID());
    }
    auto before = getNextUpdatedPortStats(ports);
    auto aclBefore =
        utility::getAclInOutPackets(getSw(), getCounterName(AclType::FLOWLET));

    std::vector<uint8_t> rethHdr(16);
    rethHdr[15] = 0xFF;
    sendRoceTraffic(
        helper_->ecmpPortDescriptorAt(kFrontPanelPortForTest).phyPortID(),
        utility::kUdfRoceOpcodeWriteImmediate,
        rethHdr,
        kTrafficPacketCount,
        kDlbDestPort);

    // The flowlet ACL counter and the port counters are refreshed by separate
    // collection cycles, so a single snapshot can catch either one still
    // holding its pre-traffic value. Retry until both have caught up -
    // asserting on one snapshot reports a half-collected count as a routing
    // failure, and the counts are recomputed each round so the returned
    // per-member spread comes from the snapshot that satisfied both.
    std::vector<int64_t> perMember(kFirstGroupWidth);
    int64_t sharedPkts = 0;
    int64_t totalPkts = 0;
    WITH_RETRIES({
      auto after = getNextUpdatedPortStats(ports);
      sharedPkts = 0;
      totalPkts = 0;
      for (int i = 0; i < kFirstGroupWidth; ++i) {
        // Traffic is injected out of this member and loops back in, so its
        // counter carries the injected packets as well as any routed to it and
        // cannot be compared against the rest.
        if (i == kFrontPanelPortForTest) {
          continue;
        }
        auto port = ports[i];
        perMember[i] =
            *after[port].outUnicastPkts_() - *before[port].outUnicastPkts_();
        totalPkts += perMember[i];
        if (i >= kSecondGroupStart) {
          sharedPkts += perMember[i];
        }
      }
      XLOG(DBG2) << "Traffic on shared members: " << sharedPkts << "/"
                 << totalPkts;
      // Traffic that misses the ACL is statically hashed onto a single member,
      // which would land wholly inside or outside the shared set and make the
      // counts above meaningless either way.
      EXPECT_EVENTUALLY_GE(
          utility::getAclInOutPackets(
              getSw(), getCounterName(AclType::FLOWLET)),
          aclBefore + kTrafficPacketCount);
      EXPECT_EVENTUALLY_GE(totalPkts, kMinAccountedPackets);
    });
    CHECK_GT(totalPkts, 0) << "no traffic reached the group";
    return perMember;
  }

  // Share of the traffic that left on members the two groups have in common.
  // Alternate members are promoted out of that shared set, so this is what
  // separates an alternate path decision from a primary one.
  double sharedMemberTrafficFraction(const std::vector<int64_t>& perMember) {
    int64_t sharedPkts = 0;
    int64_t totalPkts = 0;
    for (int i = 0; i < kFirstGroupWidth; ++i) {
      if (i == kFrontPanelPortForTest) {
        continue;
      }
      totalPkts += perMember[i];
      if (i >= kSecondGroupStart) {
        sharedPkts += perMember[i];
      }
    }
    return static_cast<double>(sharedPkts) / static_cast<double>(totalPkts);
  }
};

// DGM only applies while the primary sits in band 0, switching to an alternate
// costs the most a band can, and nothing biases the choice toward it, so the
// primary members carry the traffic.
class AgentFlowletArsPrimaryMemberTest
    : public AgentFlowletWideArsAlternateMemberTest {
 protected:
  int primaryPathQualityThreshold() const override {
    return 0;
  }
  int alternatePathCost() const override {
    return kMaxQualityBand;
  }
  int alternatePathBias() const override {
    return 0;
  }
};

TEST_F(AgentFlowletArsPrimaryMemberTest, VerifyEcmp) {
  auto setup = [this]() { setupTwoVirtualGroups(); };
  auto verify = [this]() {
    auto perMember = sendDlbTrafficAndCountPerMember();
    // Sampled the way the other wide ARS tests are. Spread over the primary
    // members every sampled one sees traffic, including those outside the
    // range shared with the second group; had DGM moved the flows onto
    // alternates only the shared members would.
    for (int i = 0; i < kFirstGroupWidth; i += kStatsCheckInterval) {
      EXPECT_GT(perMember[i], 0) << "no traffic on member " << i;
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

// The mirror image: a threshold of 7 keeps the DGM action always valid so the
// alternate is always a candidate, switching is free, and the alternate gets
// the largest bias the band allows.
class AgentFlowletArsAlternateMemberTest
    : public AgentFlowletWideArsAlternateMemberTest {
 protected:
  int primaryPathQualityThreshold() const override {
    return kMaxQualityBand;
  }
  int alternatePathCost() const override {
    return 0;
  }
  int alternatePathBias() const override {
    return kMaxQualityBand;
  }
};

TEST_F(AgentFlowletArsAlternateMemberTest, VerifyEcmp) {
  auto setup = [this]() { walkCommonMemberTransitions(); };
  auto verify = [this]() {
    EXPECT_GT(
        sharedMemberTrafficFraction(sendDlbTrafficAndCountPerMember()),
        kAllTrafficOnSharedMembers);
  };
  verifyAcrossWarmBoots(setup, verify);
}

// UDF A to empty
TEST_F(AgentFlowletSwitchingTest, VerifyUdfFlowletToFlowlet) {
  flowletSwitchingAclHitHelper(AclType::UDF_FLOWLET, AclType::FLOWLET);
}

TEST_F(AgentFlowletSwitchingTest, VerifyUdfFlowletToUdfWrImmZero) {
  flowletSwitchingAclHitHelper(AclType::UDF_FLOWLET, AclType::UDF_WR_IMM_ZERO);
}

TEST_F(AgentFlowletSwitchingTest, VerifyUdfWrImmZeroToUdfFlowlet) {
  flowletSwitchingAclHitHelper(AclType::UDF_WR_IMM_ZERO, AclType::UDF_FLOWLET);
}

TEST_F(AgentFlowletSwitchingTest, VerifyOneUdfGroupAddition) {
  verifyUdfAddDelete(AclType::UDF_FLOWLET, AclType::UDF_FLOWLET_WITH_UDF_ACK);
}

TEST_F(AgentFlowletSwitchingTest, VerifyOneUdfGroupDeletion) {
  verifyUdfAddDelete(AclType::UDF_FLOWLET_WITH_UDF_ACK, AclType::UDF_FLOWLET);
}

TEST_F(AgentFlowletSwitchingTest, VerifyUdfNakToUdfAckWithNak) {
  flowletSwitchingAclHitHelper(AclType::UDF_NAK, AclType::UDF_ACK_WITH_NAK);
}

TEST_F(AgentFlowletSwitchingTest, VerifyUdfAckWithNakToUdfNak) {
  flowletSwitchingAclHitHelper(AclType::UDF_ACK_WITH_NAK, AclType::UDF_NAK);
}

TEST_F(AgentFlowletSwitchingTest, VerifyUdfAndSendQueueAction) {
  auto setup = [this]() {
    this->setup();
    generateApplyConfig(AclType::UDF_ACK);
  };

  auto verify = [this]() {
    auto outPort = helper_->ecmpPortDescriptorAt(0).phyPortID();
    auto portStatsBefore = getNextUpdatedPortStats(outPort);
    auto pktsBefore = *portStatsBefore.outUnicastPkts_();
    auto pktsQueueBefore = portStatsBefore.queueOutPackets_()[kOutQueue];

    verifyAcl(AclType::UDF_ACK);

    WITH_RETRIES({
      auto portStatsAfter = getNextUpdatedPortStats(outPort);
      auto pktsAfter = *portStatsAfter.outUnicastPkts_();
      auto pktsQueueAfter = portStatsAfter.queueOutPackets_()[kOutQueue];
      XLOG(DBG2) << "Port Counter: " << pktsBefore << " -> " << pktsAfter
                 << "\nPort Queue " << kOutQueue
                 << " Counter: " << pktsQueueBefore << " -> " << pktsQueueAfter;
      EXPECT_EVENTUALLY_GT(pktsAfter, pktsBefore);
      EXPECT_EVENTUALLY_GT(pktsQueueAfter, pktsQueueBefore);
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentFlowletMirrorTest, VerifyUdfNakMirrorAction) {
  auto setup = [this]() {
    this->setup();
    const auto& ensemble = *getAgentEnsemble();
    auto newCfg{initialConfig(ensemble)};
    addAclAndStat(
        &newCfg, AclType::UDF_NAK, ensemble.isSai(), true /* addMirror */);
    applyNewConfig(newCfg);
    resolveMirror(kAclMirror, utility::kMirrorToPortIndex);
  };

  auto verify = [this]() { verifyMirror(MirrorScope::MIRROR_ONLY); };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentFlowletMirrorTest, VerifyUdfNakMirrorSflowSameVip) {
  auto setup = [this]() {
    this->setup();
    const auto& ensemble = *getAgentEnsemble();
    auto newCfg{initialConfig(ensemble)};
    addAclAndStat(
        &newCfg, AclType::UDF_NAK, ensemble.isSai(), true /* addMirror */);

    // mirror session for ingress port sflow
    // use same VIP as ACL mirror, only dst port varies
    utility::configureSflowMirror(
        newCfg, kSflowMirrorName, false /* truncate */, aclDestinationVIP);
    // configure sampling on traffic port
    addSamplingConfig(newCfg);

    applyNewConfig(newCfg);
    resolveMirror(kAclMirror, utility::kMirrorToPortIndex);
  };

  auto verify = [this]() { verifyMirror(MirrorScope::MIRROR_SFLOW_SAME_VIP); };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentFlowletMirrorTest, VerifyUdfNakMirrorSflowDifferentVip) {
  auto setup = [this]() {
    this->setup();
    const auto& ensemble = *getAgentEnsemble();
    auto newCfg{initialConfig(ensemble)};
    addAclAndStat(
        &newCfg, AclType::UDF_NAK, ensemble.isSai(), true /* addMirror */);

    // mirror session for ingress port sflow
    utility::configureSflowMirror(
        newCfg, kSflowMirrorName, false /* truncate */, sflowDestinationVIP);
    // configure sampling on traffic port
    addSamplingConfig(newCfg);

    applyNewConfig(newCfg);
    resolveMirror(kAclMirror, utility::kMirrorToPortIndex);
    resolveMirror(kSflowMirrorName, utility::kSflowToPortIndex);
  };

  auto verify = [this]() {
    verifyMirror(MirrorScope::MIRROR_SFLOW_DIFFERENT_VIP);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Skip this and next test due to lack of TCAM in ACL table on TH3
TEST_F(AgentFlowletAclPriorityTest, VerifyUdfAclPriority) {
  auto setup = [this]() {
    this->setup();
    const auto& ensemble = *getAgentEnsemble();
    auto newCfg{initialConfig(ensemble)};
    // production priorities
    addAclAndStat(&newCfg, AclType::UDF_NAK, ensemble.isSai());
    addAclAndStat(&newCfg, AclType::UDF_ACK, ensemble.isSai());
    addAclAndStat(&newCfg, AclType::UDF_WR_IMM_ZERO, ensemble.isSai());
    addAclAndStat(&newCfg, AclType::UDF_FLOWLET, ensemble.isSai());
    // Keep this at the end since each of the above calls update udfConfig
    // differently
    newCfg.udfConfig() = utility::addUdfAclConfig(
        utility::kUdfOffsetBthOpcode | utility::kUdfOffsetBthReserved |
        utility::kUdfOffsetAethSyndrome | utility::kUdfOffsetRethDmaLength);
    applyNewConfig(newCfg);
  };

  auto verify = [this]() {
    verifyAcl(AclType::UDF_NAK);
    verifyAcl(AclType::UDF_ACK);
    verifyAcl(AclType::UDF_WR_IMM_ZERO);
    verifyAcl(AclType::UDF_FLOWLET);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentFlowletAclPriorityTest, VerifyUdfAclPriorityWB) {
  auto setup = [this]() {
    this->setup();
    auto newCfg{initialConfig(*getAgentEnsemble())};
    applyNewConfig(newCfg);
  };

  auto setupPostWarmboot = [this]() {
    this->setup();
    const auto& ensemble = *getAgentEnsemble();
    auto newCfg{initialConfig(ensemble)};
    // production priorities
    addAclAndStat(&newCfg, AclType::UDF_NAK, ensemble.isSai());
    addAclAndStat(&newCfg, AclType::UDF_ACK, ensemble.isSai());
    addAclAndStat(&newCfg, AclType::UDF_WR_IMM_ZERO, ensemble.isSai());
    addAclAndStat(&newCfg, AclType::UDF_FLOWLET, ensemble.isSai());
    // Keep this at the end since each of the above calls update udfConfig
    // differently
    newCfg.udfConfig() = utility::addUdfAclConfig(
        utility::kUdfOffsetBthOpcode | utility::kUdfOffsetBthReserved |
        utility::kUdfOffsetAethSyndrome | utility::kUdfOffsetRethDmaLength);
    applyNewConfig(newCfg);
  };

  auto verifyPostWarmboot = [this]() {
    verifyAcl(AclType::UDF_NAK);
    verifyAcl(AclType::UDF_ACK);
    verifyAcl(AclType::UDF_WR_IMM_ZERO);
    verifyAcl(AclType::UDF_FLOWLET);
  };

  verifyAcrossWarmBoots(setup, []() {}, setupPostWarmboot, verifyPostWarmboot);
}

TEST_F(AgentFlowletSwitchingTest, CreateMaxDlbGroups) {
  auto verify = [this] {
    generatePrefixes();
    const auto kMaxDlbEcmpGroup = getMaxArsGroups();
    // install 60% of max DLB ecmp groups
    {
      int count = static_cast<int>(0.6 * kMaxDlbEcmpGroup);
      auto wrapper = getSw()->getRouteUpdater();
      std::vector<RoutePrefixV6> prefixes60 = {
          prefixes.begin(), prefixes.begin() + count};
      std::vector<boost::container::flat_set<PortDescriptor>> nhopSets60 = {
          nhopSets.begin(), nhopSets.begin() + count};
      helper_->programRoutes(&wrapper, nhopSets60, prefixes60);
    }
    // install 128 groups, failed update
    {
      auto wrapper = getSw()->getRouteUpdater();
      std::vector<RoutePrefixV6> prefixes128 = {
          prefixes.begin(), prefixes.begin() + kMaxDlbEcmpGroup};
      std::vector<boost::container::flat_set<PortDescriptor>> nhopSets128 = {
          nhopSets.begin(), nhopSets.begin() + kMaxDlbEcmpGroup};
      EXPECT_THROW(
          helper_->programRoutes(&wrapper, nhopSets128, prefixes128),
          FbossError);

      // overflow the dlb groups and ensure that the dlb resource stat
      // is updated. Also once routes are removed, the stat should reset.
      // TODO - Add support for SAI
      if (!getAgentEnsemble()->isSai()) {
        FLAGS_dlbResourceCheckEnable = false;
        std::vector<RoutePrefixV6> prefixes129 = {
            prefixes.begin(), prefixes.begin() + kMaxDlbEcmpGroup + 1};
        std::vector<boost::container::flat_set<PortDescriptor>> nhopSets129 = {
            nhopSets.begin(), nhopSets.begin() + kMaxDlbEcmpGroup + 1};
        EXPECT_NO_THROW(
            helper_->programRoutes(&wrapper, nhopSets129, prefixes129));
        auto switchId = getSw()
                            ->getScopeResolver()
                            ->scope(masterLogicalPortIds()[0])
                            .switchId();
        WITH_RETRIES({
          auto stats = getHwSwitchStats(switchId);
          EXPECT_EVENTUALLY_TRUE(*stats.arsExhausted());
        });
        helper_->unprogramRoutes(&wrapper, prefixes129);
        WITH_RETRIES({
          auto stats =
              getAgentEnsemble()->getSw()->getHwSwitchStatsExpensive(switchId);
          EXPECT_EVENTUALLY_FALSE(*stats.arsExhausted());
        });
        FLAGS_dlbResourceCheckEnable = true;
      }
    }
    // install 10% of max DLB ecmp groups
    {
      int count = static_cast<int>(0.1 * kMaxDlbEcmpGroup);
      auto wrapper = getSw()->getRouteUpdater();
      std::vector<RoutePrefixV6> prefixes10 = {
          prefixes.begin() + kMaxDlbEcmpGroup,
          prefixes.begin() + kMaxDlbEcmpGroup + count};
      std::vector<boost::container::flat_set<PortDescriptor>> nhopSets10 = {
          nhopSets.begin() + kMaxDlbEcmpGroup,
          nhopSets.begin() + kMaxDlbEcmpGroup + count};
      EXPECT_NO_THROW(helper_->programRoutes(&wrapper, nhopSets10, prefixes10));
      helper_->unprogramRoutes(&wrapper, prefixes10);
    }
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentFlowletSwitchingTest, ApplyDlbResourceCheck) {
  // Start with 60% ECMP groups
  auto setup = [this]() {
    generatePrefixes();
    const auto kMaxDlbEcmpGroup = getMaxArsGroups();
    int count = static_cast<int>(0.6 * kMaxDlbEcmpGroup);
    auto wrapper = getSw()->getRouteUpdater();
    std::vector<RoutePrefixV6> prefixes60 = {
        prefixes.begin(), prefixes.begin() + count};
    std::vector<boost::container::flat_set<PortDescriptor>> nhopSets60 = {
        nhopSets.begin(), nhopSets.begin() + count};
    helper_->programRoutes(&wrapper, nhopSets60, prefixes60);
  };
  // Post warmboot, dlb resource check is enforced since >75%
  auto setupPostWarmboot = [this]() {
    generatePrefixes();
    const auto kMaxDlbEcmpGroup = getMaxArsGroups();
    {
      auto wrapper = getSw()->getRouteUpdater();
      std::vector<RoutePrefixV6> prefixes128 = {
          prefixes.begin(), prefixes.begin() + kMaxDlbEcmpGroup};
      std::vector<boost::container::flat_set<PortDescriptor>> nhopSets128 = {
          nhopSets.begin(), nhopSets.begin() + kMaxDlbEcmpGroup};
      EXPECT_THROW(
          helper_->programRoutes(&wrapper, nhopSets128, prefixes128),
          FbossError);
    }
    // install 10% of max DLB ecmp groups
    {
      int count = static_cast<int>(0.1 * kMaxDlbEcmpGroup);
      auto wrapper = getSw()->getRouteUpdater();
      std::vector<RoutePrefixV6> prefixes10 = {
          prefixes.begin(), prefixes.begin() + count};
      std::vector<boost::container::flat_set<PortDescriptor>> nhopSets10 = {
          nhopSets.begin(), nhopSets.begin() + count};
      helper_->programRoutes(&wrapper, nhopSets10, prefixes10);
    }
  };
  verifyAcrossWarmBoots(setup, [] {}, setupPostWarmboot, [] {});
}

TEST_F(AgentFlowletSwitchingTest, VerifyEcmpSwitchingMode) {
  auto setup = [this]() { this->setup(4); };

  auto verify = [this]() {
    auto ecmpResourceMgr = getSw()->getEcmpResourceManager();
    if (ecmpResourceMgr) {
      auto flowletSwitchConfig =
          getSw()->getState()->getFlowletSwitchingConfig();
      EXPECT_NE(flowletSwitchConfig, nullptr);
      auto backupEcmpGroupType =
          getSw()->getEcmpResourceManager()->getBackupEcmpSwitchingMode();
      ASSERT_TRUE(backupEcmpGroupType.has_value());
      EXPECT_EQ(
          *backupEcmpGroupType, flowletSwitchConfig->getBackupSwitchingMode());
    }
    RoutePrefixV6 prefix{folly::IPAddressV6("::"), 0};
    verifyFwdSwitchingMode(prefix, cfg::SwitchingMode::PER_PACKET_QUALITY);
  };

  verifyAcrossWarmBoots(setup, verify);
}

class AgentFlowletBcmTest : public AgentFlowletSwitchingTest {
 protected:
  void setCmdLineFlagOverrides() const override {
    AgentFlowletSwitchingTest::setCmdLineFlagOverrides();
    FLAGS_dlbResourceCheckEnable = false;
    FLAGS_update_route_with_dlb_type = true;
  }
};

TEST_F(AgentFlowletBcmTest, VerifySwitchingModeUpdateSwState) {
  generatePrefixes();
  const auto kMaxDlbEcmpGroup = getMaxArsGroups();
  // Create two test prefix vectors
  std::vector<RoutePrefixV6> testPrefixes1 = {
      prefixes.begin(), prefixes.begin() + kMaxDlbEcmpGroup};
  std::vector<RoutePrefixV6> testPrefixes2 = {
      prefixes.begin() + kMaxDlbEcmpGroup,
      prefixes.begin() + kMaxDlbEcmpGroup + 128};
  std::vector<boost::container::flat_set<PortDescriptor>> testNhopSets1 = {
      nhopSets.begin(), nhopSets.begin() + kMaxDlbEcmpGroup};
  std::vector<boost::container::flat_set<PortDescriptor>> testNhopSets2 = {
      nhopSets.begin() + kMaxDlbEcmpGroup,
      nhopSets.begin() + kMaxDlbEcmpGroup + 128};

  auto verifySwitchingMode =
      [](const std::shared_ptr<SwitchState> state,
         const std::vector<RoutePrefixV6>& testPrefixes,
         const std::optional<cfg::SwitchingMode>& expectedMode) {
        for (const auto& prefix : testPrefixes) {
          auto route = findRoute<folly::IPAddressV6>(
              RouterID(0), {prefix.network(), prefix.mask()}, state);
          const auto& fwd = route->getForwardInfo();
          auto switchingMode = fwd.getOverrideEcmpSwitchingMode();
          EXPECT_EQ(switchingMode, expectedMode);
        }
      };

  auto setup = [=, this]() {
    {
      auto wrapper = getSw()->getRouteUpdater();
      helper_->programRoutes(&wrapper, testNhopSets1, testPrefixes1);
    }
    // verify ecmp switching mode not filled in
    verifySwitchingMode(getProgrammedState(), testPrefixes1, std::nullopt);
    {
      auto wrapper = getSw()->getRouteUpdater();
      helper_->programRoutes(&wrapper, testNhopSets2, testPrefixes2);
    }
    // verify ecmp switching mode not filled in
    verifySwitchingMode(getProgrammedState(), testPrefixes2, std::nullopt);
  };

  auto verifyPostWarmboot = [=, this]() {
    // First verify if thrift state is correctly written prior to warmboot
    auto wbState = getSw()->getWarmBootHelper()->getWarmBootState();
    auto state = SwitchState::fromThrift(*wbState.swSwitchState());
    verifySwitchingMode(state, testPrefixes1, std::nullopt);
    verifySwitchingMode(
        state,
        testPrefixes2,
        std::optional<cfg::SwitchingMode>(
            cfg::SwitchingMode::PER_PACKET_RANDOM));

    // Now verify if warmboot state is updated in sw state
    verifySwitchingMode(getProgrammedState(), testPrefixes1, std::nullopt);
    verifySwitchingMode(
        getProgrammedState(),
        testPrefixes2,
        std::optional<cfg::SwitchingMode>(
            cfg::SwitchingMode::PER_PACKET_RANDOM));
  };
  verifyAcrossWarmBoots(setup, [] {}, [] {}, verifyPostWarmboot);
}

/*
 * roce-spray-miss: an unqualified RoCEv2 entry below the DLB enable ACL,
 * splitting non-sprayed RDMA out of the catch-all so it can be counted.
 *
 * It is the non-sprayed bucket only because it is appended after udf-flowlet
 * and first match wins. Swapping the two would disable spray with no config
 * error, so every step asserts the whole counter vector, not just its own.
 */
TEST_F(AgentFlowletAclPriorityTest, VerifyRoceSprayMissAcl) {
  auto setup = [this]() {
    this->setup();
    applyAclConfig();
  };

  auto verify = [this]() {
    runStep(
        utility::kUdfRoceOpcodeWriteImmediate,
        utility::kRoceReserved,
        utility::kUdfL4DstPort,
        ExpectedHit::Flowlet);
    runStep(
        utility::kUdfRoceOpcodeWriteImmediate,
        0,
        utility::kUdfL4DstPort,
        ExpectedHit::RoceSprayMiss);
    runStep(
        utility::kUdfRoceOpcodeWriteImmediate,
        0,
        kNonRoceL4DstPort,
        ExpectedHit::Cancel);
    runStep(
        utility::kUdfRoceOpcodeAck,
        utility::kRoceReserved,
        utility::kUdfL4DstPort,
        ExpectedHit::RoceAck);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Reaching the four-entry block from the three-entry form production runs
// today. Inserting third renumbers the catch-all, which SAI can only do by
// removing and re-creating it.
TEST_F(AgentFlowletAclPriorityTest, VerifyRoceSprayMissAclInsertion) {
  auto setup = [this]() {
    this->setup();
    applyAclConfig(/*withSprayMiss*/ false);
  };

  auto setupPostWarmboot = [this]() { applyAclConfig(/*withSprayMiss*/ true); };

  auto verifyPostWarmboot = [this]() {
    runStep(
        utility::kUdfRoceOpcodeWriteImmediate,
        0,
        utility::kUdfL4DstPort,
        ExpectedHit::RoceSprayMiss);
    runStep(
        utility::kUdfRoceOpcodeWriteImmediate,
        0,
        kNonRoceL4DstPort,
        ExpectedHit::Cancel);
    runStep(
        utility::kUdfRoceOpcodeWriteImmediate,
        utility::kRoceReserved,
        utility::kUdfL4DstPort,
        ExpectedHit::Flowlet);
  };

  verifyAcrossWarmBoots(setup, []() {}, setupPostWarmboot, verifyPostWarmboot);
}

} // namespace facebook::fboss
