// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/EcmpTestUtils.h"
#include "fboss/agent/test/utils/TrunkTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

using utility::disableTrunkPort;
using utility::EcmpSetupTargetedPorts6;
using utility::enableTrunkPorts;

class AgentEcmpTrunkTest : public AgentHwTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    std::vector<PortID> members;
    for (auto port : getTrunkMemberPorts(ensemble)) {
      members.emplace_back(port);
    }
    return utility::oneAggregatePortPerInterfaceConfig(
        ensemble.getSw(), ensemble.masterLogicalPortIds(), {{kAggId, members}});
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::LAG};
  }

  std::vector<int32_t> getTrunkMemberPorts() const {
    return getTrunkMemberPorts(*getAgentEnsemble());
  }

  static std::vector<int32_t> getTrunkMemberPorts(
      const AgentEnsemble& ensemble) {
    return {
        (ensemble.masterLogicalPortIds()[kEcmpWidth - 1]),
        (ensemble.masterLogicalPortIds()[kEcmpWidth])};
  }

  boost::container::flat_set<PortDescriptor> getEcmpPorts() const {
    boost::container::flat_set<PortDescriptor> ports;
    for (auto i = 0; i < kEcmpWidth - 1; ++i) {
      ports.insert(PortDescriptor(PortID(masterLogicalPortIds()[i])));
    }
    ports.insert(PortDescriptor(kAggId));
    return ports;
  }

  void ensureHelper() {
    ecmpHelper_ = std::make_unique<EcmpSetupTargetedPorts6>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), RouterID(0));
  }

  int getEcmpSizeInHw() {
    facebook::fboss::utility::CIDRNetwork cidr;
    cidr.IPAddress() = "::";
    cidr.mask() = 0;
    return utility::getEcmpSizeInHw(getAgentEnsemble(), cidr);
  }

  void runEcmpWithTrunkMinLinks(uint8_t minlinks) {
    auto setup = [=, this]() {
      ensureHelper();
      auto state = enableTrunkPorts(getProgrammedState());
      applyNewState(
          [&](const std::shared_ptr<SwitchState>&) {
            return utility::setTrunkMinLinkCount(state, minlinks);
          },
          "set trunk min links");
      applyNewState(
          [&](const std::shared_ptr<SwitchState>& in) {
            return ecmpHelper_->resolveNextHops(in, getEcmpPorts());
          },
          "resolve next hops");
      auto wrapper = getSw()->getRouteUpdater();
      ecmpHelper_->programRoutes(&wrapper, getEcmpPorts());
    };

    auto verify = [=, this]() {
      ensureHelper();
      WITH_RETRIES({ EXPECT_EVENTUALLY_EQ(kEcmpWidth, getEcmpSizeInHw()); });
      // Disable one trunk member via LACP path (disableTrunkPort) instead
      // of bringDownPort to avoid triggering neighbor purging in the agent.
      auto state = disableTrunkPort(
          getProgrammedState(), kAggId, PortID(getTrunkMemberPorts()[0]));
      applyNewState(
          [&](const std::shared_ptr<SwitchState>&) { return state; },
          "disable trunk member");
      auto numEcmpMembersToExclude = minlinks >= 2 ? 1 : 0;
      WITH_RETRIES({
        EXPECT_EVENTUALLY_EQ(
            kEcmpWidth - numEcmpMembersToExclude, getEcmpSizeInHw());
      });

      state = enableTrunkPorts(getProgrammedState());
      applyNewState(
          [&](const std::shared_ptr<SwitchState>&) { return state; },
          "re-enable trunk ports");
      WITH_RETRIES({ EXPECT_EVENTUALLY_EQ(kEcmpWidth, getEcmpSizeInHw()); });
    };
    verifyAcrossWarmBoots(setup, verify);
  }

  static constexpr int kEcmpWidth = 7;
  const AggregatePortID kAggId{1};
  std::unique_ptr<EcmpSetupTargetedPorts6> ecmpHelper_;
};

TEST_F(AgentEcmpTrunkTest, TrunkNotRemovedFromEcmpWithMinLinks) {
  runEcmpWithTrunkMinLinks(1);
}

TEST_F(AgentEcmpTrunkTest, TrunkMemberRemoveWithRouteTest) {
  uint8_t minlinks = 2;
  auto setup = [=, this]() {
    ensureHelper();
    auto state = enableTrunkPorts(getProgrammedState());
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) {
          return utility::setTrunkMinLinkCount(state, minlinks);
        },
        "set trunk min links");

    std::vector<RoutePrefixV6> v6Prefixes = {
        RoutePrefixV6{folly::IPAddressV6("1000::1"), 48},
        RoutePrefixV6{folly::IPAddressV6("1001::1"), 48},
        RoutePrefixV6{folly::IPAddressV6("1002::1"), 48},
        RoutePrefixV6{folly::IPAddressV6("1003::1"), 48},
        RoutePrefixV6{folly::IPAddressV6("1004::1"), 64},
        RoutePrefixV6{folly::IPAddressV6("1005::1"), 64},
        RoutePrefixV6{folly::IPAddressV6("1006::1"), 64},
        RoutePrefixV6{folly::IPAddressV6("1007::1"), 128},
        RoutePrefixV6{folly::IPAddressV6("1008::1"), 128},
        RoutePrefixV6{folly::IPAddressV6("1009::1"), 128},
    };

    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper_->resolveNextHops(in, getEcmpPorts());
        },
        "resolve next hops");
    auto wrapper = getSw()->getRouteUpdater();
    ecmpHelper_->programRoutes(&wrapper, getEcmpPorts(), v6Prefixes);
  };

  auto verify = [=, this]() {
    auto trunkMembers = getTrunkMemberPorts();
    auto trunkMemberCount = trunkMembers.size();
    auto activeMemberCount = trunkMemberCount;
    for (const auto& member : trunkMembers) {
      utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
      auto state =
          disableTrunkPort(getProgrammedState(), kAggId, PortID(member));
      applyNewState(
          [&](const std::shared_ptr<SwitchState>&) { return state; },
          "disable trunk port");
      activeMemberCount--;
      utility::verifyAggregatePortMemberCount(
          *getAgentEnsemble(), kAggId, activeMemberCount);
    }
    auto state = enableTrunkPorts(getProgrammedState());
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) { return state; },
        "enable trunk ports");
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, trunkMemberCount);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(
    AgentEcmpTrunkTest,
    TrunkL2ResolveNhopThenLinkAndLACPDownThenUpThenStateUp) {
  uint8_t minlinks = 2;
  uint8_t numEcmpMembersToExclude = 1;
  auto setup = [=, this]() {
    ensureHelper();
    auto state = enableTrunkPorts(getProgrammedState());
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) {
          return utility::setTrunkMinLinkCount(state, minlinks);
        },
        "set trunk min links");
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper_->resolveNextHops(in, getEcmpPorts());
        },
        "resolve next hops");
    auto wrapper = getSw()->getRouteUpdater();
    ecmpHelper_->programRoutes(&wrapper, getEcmpPorts());
    WITH_RETRIES(ASSERT_EVENTUALLY_EQ(kEcmpWidth, getEcmpSizeInHw()));

    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks);

    bringDownPort(PortID(getTrunkMemberPorts()[0]));
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks - numEcmpMembersToExclude);

    state = disableTrunkPort(
        getProgrammedState(), kAggId, PortID(getTrunkMemberPorts()[0]));
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) { return state; },
        "disable trunk port");
  };

  auto verify = [=, this]() {
    WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
        kEcmpWidth - numEcmpMembersToExclude, getEcmpSizeInHw()));
    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks - numEcmpMembersToExclude);
  };

  auto setupPostWarmboot = [=, this]() {
    ensureHelper();
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper_->unresolveNextHops(in, {PortDescriptor(kAggId)});
        },
        "unresolve next hops");
    bringUpPort(PortID(getTrunkMemberPorts()[0]));
    WITH_RETRIES(ASSERT_EVENTUALLY_EQ(
        kEcmpWidth - numEcmpMembersToExclude, getEcmpSizeInHw()));
    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks - numEcmpMembersToExclude);
  };

  auto verifyPostWarmboot = [=, this]() {
    ensureHelper();
    auto state = enableTrunkPorts(getProgrammedState());
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) { return state; },
        "enable trunk ports");
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper_->resolveNextHops(in, getEcmpPorts());
        },
        "resolve next hops");
    WITH_RETRIES(ASSERT_EVENTUALLY_EQ(kEcmpWidth, getEcmpSizeInHw()));
    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

TEST_F(AgentEcmpTrunkTest, TrunkL2ResolveNhopThenLinkDownThenUpThenStateUp) {
  uint8_t minlinks = 2;
  uint8_t numEcmpMembersToExclude = 1;
  auto setup = [=, this]() {
    ensureHelper();
    auto state = enableTrunkPorts(getProgrammedState());
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) {
          return utility::setTrunkMinLinkCount(state, minlinks);
        },
        "set trunk min links");
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper_->resolveNextHops(in, getEcmpPorts());
        },
        "resolve next hops");
    auto wrapper = getSw()->getRouteUpdater();
    ecmpHelper_->programRoutes(&wrapper, getEcmpPorts());
    ASSERT_EQ(kEcmpWidth, getEcmpSizeInHw());

    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks);
    bringDownPort(PortID(getTrunkMemberPorts()[0]));
  };

  auto verify = [=, this]() {
    ASSERT_EQ(kEcmpWidth - numEcmpMembersToExclude, getEcmpSizeInHw());
    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(*getAgentEnsemble(), kAggId, 1);
  };

  auto setupPostWarmboot = [=, this]() {
    ensureHelper();
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper_->unresolveNextHops(in, {PortDescriptor(kAggId)});
        },
        "unresolve next hops");
    auto state = disableTrunkPort(
        getProgrammedState(), kAggId, PortID(getTrunkMemberPorts()[0]));
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) { return state; },
        "disable trunk port");
    bringUpPort(PortID(getTrunkMemberPorts()[0]));
    ASSERT_EQ(kEcmpWidth - numEcmpMembersToExclude, getEcmpSizeInHw());
    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(*getAgentEnsemble(), kAggId, 1);
  };

  auto verifyPostWarmboot = [=, this]() {
    ensureHelper();
    auto state = enableTrunkPorts(getProgrammedState());
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) { return state; },
        "enable trunk ports");
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper_->resolveNextHops(in, getEcmpPorts());
        },
        "resolve next hops");
    ASSERT_EQ(kEcmpWidth, getEcmpSizeInHw());
    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

TEST_F(
    AgentEcmpTrunkTest,
    TrunkL2ResolveNhopThenLinkAndLACPDownThenUpBeforeReinit) {
  uint8_t minlinks = 2;
  uint8_t numEcmpMembersToExclude = 1;
  auto setup = [=, this]() {
    ensureHelper();
    auto state = enableTrunkPorts(getProgrammedState());
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) {
          return utility::setTrunkMinLinkCount(state, minlinks);
        },
        "set trunk min links");
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper_->resolveNextHops(in, getEcmpPorts());
        },
        "resolve next hops");
    auto wrapper = getSw()->getRouteUpdater();
    ecmpHelper_->programRoutes(&wrapper, getEcmpPorts());
    ASSERT_EQ(kEcmpWidth, getEcmpSizeInHw());

    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks);

    bringDownPort(PortID(getTrunkMemberPorts()[0]));
    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(*getAgentEnsemble(), kAggId, 1);
    state = disableTrunkPort(
        getProgrammedState(), kAggId, PortID(getTrunkMemberPorts()[0]));
    state = ecmpHelper_->unresolveNextHops(
        state, {PortDescriptor(AggregatePortID(kAggId))});
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) { return state; },
        "disable trunk port and unresolve");
    bringUpPort(PortID(getTrunkMemberPorts()[0]));
  };

  auto verify = [=, this]() {
    ASSERT_EQ(kEcmpWidth - numEcmpMembersToExclude, getEcmpSizeInHw());
    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(*getAgentEnsemble(), kAggId, 1);
  };

  auto setupPostWarmboot = [=]() {};

  auto verifyPostWarmboot = [=, this]() {
    ensureHelper();
    auto state = enableTrunkPorts(getProgrammedState());
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) { return state; },
        "enable trunk ports");
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper_->resolveNextHops(in, getEcmpPorts());
        },
        "resolve next hops");
    ASSERT_EQ(kEcmpWidth, getEcmpSizeInHw());
    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

TEST_F(AgentEcmpTrunkTest, TrunkL2ResolveNhopThenLinkUpDownUpBeforeReinit) {
  uint8_t minlinks = 2;
  uint8_t numEcmpMembersToExclude = 1;
  auto setup = [=, this]() {
    ensureHelper();
    auto state = enableTrunkPorts(getProgrammedState());
    applyNewState(
        [&](const std::shared_ptr<SwitchState>&) {
          return utility::setTrunkMinLinkCount(state, minlinks);
        },
        "set trunk min links");
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return ecmpHelper_->resolveNextHops(in, getEcmpPorts());
        },
        "resolve next hops");
    auto wrapper = getSw()->getRouteUpdater();
    ecmpHelper_->programRoutes(&wrapper, getEcmpPorts());
    ASSERT_EQ(kEcmpWidth, getEcmpSizeInHw());

    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks);

    bringDownPort(PortID(getTrunkMemberPorts()[0]));
    utility::verifyAggregatePortMemberCount(*getAgentEnsemble(), kAggId, 1);
    bringUpPort(PortID(getTrunkMemberPorts()[0]));
    ASSERT_EQ(kEcmpWidth - numEcmpMembersToExclude, getEcmpSizeInHw());
    utility::verifyAggregatePortMemberCount(*getAgentEnsemble(), kAggId, 1);
  };

  auto verify = [=]() {};

  auto setupPostWarmboot = [=]() {};

  auto verifyPostWarmboot = [=, this]() {
    ASSERT_EQ(kEcmpWidth, getEcmpSizeInHw());
    utility::verifyPktFromAggregatePort(*getAgentEnsemble(), kAggId);
    utility::verifyAggregatePortMemberCount(
        *getAgentEnsemble(), kAggId, minlinks);
  };

  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

} // namespace facebook::fboss
