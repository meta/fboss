// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
//
#include <fmt/format.h>

#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/agent_hw_tests/AgentVoqSwitchTests.h"
#include "fboss/agent/test/utils/VoqTestUtils.h"

#include "fboss/agent/DsfStateUpdaterUtil.h"
#include "fboss/agent/FabricConnectivityManager.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/hw/HwResourceStatsPublisher.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/test/utils/DsfConfigUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/NetworkAITestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/agent/test/utils/RouteTestUtils.h"

using namespace facebook::fb303;

namespace facebook::fboss {

class AgentVoqSwitchWithMultipleDsfNodesTest : public AgentVoqSwitchTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentVoqSwitchTest::initialConfig(ensemble);
    cfg.dsfNodes() = *overrideDsfNodes(*cfg.dsfNodes());
    return cfg;
  }

  std::optional<std::map<int64_t, cfg::DsfNode>> overrideDsfNodes(
      const std::map<int64_t, cfg::DsfNode>& curDsfNodes) const {
    return utility::addRemoteIntfNodeCfg(curDsfNodes, 1);
  }

 protected:
  void assertVoqTailDrops(
      const folly::IPAddressV6& nbrIp,
      const SystemPortID& sysPortId) {
    auto sendPkts = [=, this]() {
      for (auto i = 0; i < 1000; ++i) {
        sendPacket(nbrIp, std::nullopt);
      }
    };
    auto voqDiscardBytes = 0;
    WITH_RETRIES({
      sendPkts();
      auto sysPortStats = utility::getRemoteSysPortStatsForSwitchUnderTest(
          getSw(),
          getProgrammedState(),
          getCurrentSwitchIndexForTesting(),
          sysPortId);
      ASSERT_EVENTUALLY_TRUE(sysPortStats.has_value());
      voqDiscardBytes =
          sysPortStats->queueOutDiscardBytes_()->at(utility::getDefaultQueue());
      XLOG(INFO) << " VOQ discard bytes: " << voqDiscardBytes;
      EXPECT_EVENTUALLY_GT(voqDiscardBytes, 0);
    });
    WITH_RETRIES({
      if (checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
              ->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO3) {
        auto switchIndices = getSw()->getSwitchInfoTable().getSwitchIndices();
        int totalVoqResourceExhaustionDrops = 0;
        for (const auto& switchIndex : switchIndices) {
          auto switchStats = getSw()->getHwSwitchStatsExpensive()[switchIndex];
          const auto voqExhaustionDrop = std::as_const(switchStats)
                                             .switchDropStats()
                                             ->voqResourceExhaustionDrops();
          CHECK(voqExhaustionDrop.has_value());
          XLOG(INFO) << " Voq resource exhaustion drops for switchIndex "
                     << switchIndex << " : " << *voqExhaustionDrop;
          totalVoqResourceExhaustionDrops += *voqExhaustionDrop;
        }
        EXPECT_EVENTUALLY_GT(totalVoqResourceExhaustionDrops, 0);
      }
    });
    checkStatsStabilize(10);
  }

  cfg::SwitchConfig getShelEnabledConfig(
      const cfg::SwitchConfig& inputConfig) const {
    auto config = inputConfig;
    // Set SHEL configuration
    cfg::SelfHealingEcmpLagConfig shelConfig;
    shelConfig.shelSrcIp() = "2222::1";
    shelConfig.shelDstIp() = "2222::2";
    shelConfig.shelPeriodicIntervalMS() = 5000;
    config.switchSettings()->selfHealingEcmpLagConfig() = std::move(shelConfig);
    // Enable selfHealingEcmpLag on Interface Ports
    for (auto& port : *config.ports()) {
      if (port.portType() == cfg::PortType::INTERFACE_PORT ||
          port.portType() == cfg::PortType::HYPER_PORT) {
        port.selfHealingECMPLagEnable() = true;
      }
    }
    return config;
  }
};

TEST_F(AgentVoqSwitchWithMultipleDsfNodesTest, twoDsfNodes) {
  verifyAcrossWarmBoots([] {}, [] {});
}

TEST_F(AgentVoqSwitchWithMultipleDsfNodesTest, remoteSystemPort) {
  auto setup = [this]() {
    auto getStats = [this] {
      auto switchID = getCurrentSwitchIdForTesting();
      return std::make_tuple(
          getAgentEnsemble()->getFb303Counter(
              std::string(kSystemPortsFree), switchID),
          getAgentEnsemble()->getFb303Counter(
              std::string(kVoqsFree), switchID));
    };
    auto [beforeSysPortsFree, beforeVoqsFree] = getStats();
    const auto expectedVoqsConsumed = isDualStage3Q2QMode() ? 3 : 8;
    const auto validateResourceCounterDecrements =
        beforeSysPortsFree > 0 && beforeVoqsFree >= expectedVoqsConsumed;
    const auto kRemoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteSysPort(
          in,
          scopeResolver(),
          kRemoteSysPortId,
          utility::getRemoteVoqSwitchId(getSw()));
    });
    WITH_RETRIES({
      EXPECT_EVENTUALLY_NE(
          getProgrammedState()->getRemoteSystemPorts()->getNodeIf(
              kRemoteSysPortId),
          nullptr);
    });
    if (!validateResourceCounterDecrements) {
      XLOG(INFO) << "Resource availability counters are not reporting enough "
                 << "free VOQ resources for decrement validation";
      return;
    }
    WITH_RETRIES({
      auto [afterSysPortsFree, afterVoqsFree] = getStats();
      XLOG(INFO) << " Before sysPortsFree: " << beforeSysPortsFree
                 << " voqsFree: " << beforeVoqsFree
                 << " after sysPortsFree: " << afterSysPortsFree
                 << " voqsFree: " << afterVoqsFree;
      EXPECT_EVENTUALLY_EQ(beforeSysPortsFree - 1, afterSysPortsFree);
      EXPECT_EVENTUALLY_EQ(
          beforeVoqsFree - expectedVoqsConsumed, afterVoqsFree);
    });
  };
  verifyAcrossWarmBoots(setup, [] {});
}

TEST_F(AgentVoqSwitchWithMultipleDsfNodesTest, remoteRouterInterface) {
  auto setup = [this]() {
    const auto kRemoteSysPortIdA =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    const auto kRemoteSysPortIdB =
        utility::getRemoteSysPortId(getSw(), getProgrammedState(), 1);
    const auto kRemoteIntfIdA = utility::getRemoteIntfId(kRemoteSysPortIdA);
    const auto kRemoteIntfIdB = utility::getRemoteIntfId(kRemoteSysPortIdB);
    auto remoteSwitchId = utility::getRemoteVoqSwitchId(getSw());

    // Helper to apply a DSF state update via DsfStateUpdaterUtil, which
    // processes interface route deltas through processRemoteInterfaceRoutes.
    auto dsfUpdate = [&](const std::shared_ptr<SystemPortMap>& sysPorts,
                         const std::shared_ptr<InterfaceMap>& rifs,
                         const std::string& desc) {
      std::map<SwitchID, std::shared_ptr<SystemPortMap>> switchId2SysPorts;
      std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Rifs;
      switchId2SysPorts[remoteSwitchId] = sysPorts;
      switchId2Rifs[remoteSwitchId] = rifs;

      auto sw = getSw();
      sw->getRib()->updateStateInRibThread(
          [sw, switchId2SysPorts, switchId2Rifs, desc]() {
            sw->updateStateWithHwFailureProtection(
                desc,
                [sw, switchId2SysPorts, switchId2Rifs](
                    const std::shared_ptr<SwitchState>& in) {
                  return DsfStateUpdaterUtil::getUpdatedState(
                      in,
                      sw->getScopeResolver(),
                      sw->getRib(),
                      switchId2SysPorts,
                      switchId2Rifs);
                });
          });
    };

    // Phase 1: Add two remote RIFs with distinct prefixes.
    {
      auto sysPorts = std::make_shared<SystemPortMap>();
      sysPorts->addNode(
          utility::makeRemoteSysPort(kRemoteSysPortIdA, remoteSwitchId));
      sysPorts->addNode(
          utility::makeRemoteSysPort(kRemoteSysPortIdB, remoteSwitchId, 0, 2));

      auto rifs = std::make_shared<InterfaceMap>();
      rifs->addNode(
          utility::makeRemoteInterface(
              kRemoteIntfIdA,
              {
                  {folly::IPAddress("100::1"), 64},
                  {folly::IPAddress("100.0.0.1"), 24},
              }));
      rifs->addNode(
          utility::makeRemoteInterface(
              kRemoteIntfIdB,
              {
                  {folly::IPAddress("101::1"), 64},
                  {folly::IPAddress("101.0.0.1"), 24},
              }));

      dsfUpdate(sysPorts, rifs, "Add two remote RIFs");
    }

    // Wait for both RIFs and their connected routes to be programmed.
    WITH_RETRIES({
      auto state = getProgrammedState();
      auto remoteIntfs = state->getRemoteInterfaces()->getAllNodes();
      EXPECT_EVENTUALLY_NE(remoteIntfs->getNodeIf(kRemoteIntfIdA), nullptr);
      EXPECT_EVENTUALLY_NE(remoteIntfs->getNodeIf(kRemoteIntfIdB), nullptr);

      auto fibContainer = state->getFibsInfoMap()->getFibContainer(RouterID(0));
      EXPECT_EVENTUALLY_NE(
          fibContainer->getFibV4()->exactMatch(
              RoutePrefixV4{folly::IPAddressV4("100.0.0.0"), 24}),
          nullptr);
      EXPECT_EVENTUALLY_NE(
          fibContainer->getFibV4()->exactMatch(
              RoutePrefixV4{folly::IPAddressV4("101.0.0.0"), 24}),
          nullptr);
    });

    // Phase 2: Move prefix from RIF B to RIF A, remove RIF B.
    // This exercises the cancel-out bug fix in processRemoteInterfaceRoutes.
    // DsfStateUpdaterUtil::getUpdatedState computes the delta:
    //   Changed RIF A: delete 100.x prefix, add 101.x prefix
    //   Removed RIF B: delete 101.x prefix
    // Without the fix, the delete of 101.x from B incorrectly cancels the
    // add of 101.x for A, leaving the route missing from the FIB.
    {
      auto sysPorts = std::make_shared<SystemPortMap>();
      sysPorts->addNode(
          utility::makeRemoteSysPort(kRemoteSysPortIdA, remoteSwitchId));

      auto rifs = std::make_shared<InterfaceMap>();
      rifs->addNode(
          utility::makeRemoteInterface(
              kRemoteIntfIdA,
              {
                  {folly::IPAddress("101::1"), 64},
                  {folly::IPAddress("101.0.0.1"), 24},
              }));

      dsfUpdate(sysPorts, rifs, "Move prefix between remote RIFs");
    }
  };

  auto verify = [this]() {
    const auto kRemoteIntfIdA = utility::getRemoteIntfId(
        utility::getRemoteSysPortId(getSw(), getProgrammedState()));
    const auto kRemoteIntfIdB = utility::getRemoteIntfId(
        utility::getRemoteSysPortId(getSw(), getProgrammedState(), 1));
    WITH_RETRIES({
      auto state = getProgrammedState();
      auto remoteIntfs = state->getRemoteInterfaces()->getAllNodes();
      EXPECT_EVENTUALLY_NE(remoteIntfs->getNodeIf(kRemoteIntfIdA), nullptr);
      EXPECT_EVENTUALLY_EQ(remoteIntfs->getNodeIf(kRemoteIntfIdB), nullptr);

      auto fibContainer = state->getFibsInfoMap()->getFibContainer(RouterID(0));
      auto fibV4 = fibContainer->getFibV4();
      auto fibV6 = fibContainer->getFibV6();

      // Moved prefix routes should exist and be connected
      auto movedV4 =
          fibV4->exactMatch(RoutePrefixV4{folly::IPAddressV4("101.0.0.0"), 24});
      EXPECT_EVENTUALLY_NE(movedV4, nullptr);
      if (movedV4) {
        EXPECT_TRUE(movedV4->isConnected());
        auto nhops = getNextHops(state, movedV4->getForwardInfo());
        ASSERT_EQ(nhops.size(), 1);
        EXPECT_EQ(
            utility::createTunIntfName(nhops.begin()->intf()),
            utility::createTunIntfName(kRemoteIntfIdA));
      }
      auto movedV6 =
          fibV6->exactMatch(RoutePrefixV6{folly::IPAddressV6("101::"), 64});
      EXPECT_EVENTUALLY_NE(movedV6, nullptr);
      if (movedV6) {
        EXPECT_TRUE(movedV6->isConnected());
        auto nhops = getNextHops(state, movedV6->getForwardInfo());
        ASSERT_EQ(nhops.size(), 1);
        EXPECT_EQ(
            utility::createTunIntfName(nhops.begin()->intf()),
            utility::createTunIntfName(kRemoteIntfIdA));
      }

      // Old prefix routes should be gone
      EXPECT_EVENTUALLY_EQ(
          fibV4->exactMatch(RoutePrefixV4{folly::IPAddressV4("100.0.0.0"), 24}),
          nullptr);
      EXPECT_EVENTUALLY_EQ(
          fibV6->exactMatch(RoutePrefixV6{folly::IPAddressV6("100::"), 64}),
          nullptr);
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

class AgentVoqSwitchWithTwoRemoteDsfNodesTest
    : public AgentVoqSwitchWithMultipleDsfNodesTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentVoqSwitchTest::initialConfig(ensemble);
    cfg.dsfNodes() = *utility::addRemoteIntfNodeCfg(*cfg.dsfNodes(), 2);
    return cfg;
  }
};

TEST_F(
    AgentVoqSwitchWithTwoRemoteDsfNodesTest,
    crossSwitchInterfaceRouteMigration) {
  // Two remote switches (A, B) symmetrically swap interface IPs via
  // separate DSF updates. Without the ownership check fix, the second
  // update's delete removes the first update's newly added route.

  auto constexpr remotePortIdA = 401;
  auto constexpr remotePortIdB = 501;
  const auto prefixA =
      RoutePrefixV6{folly::IPAddressV6("2401:db00:e702:50:1000::20"), 127};
  const auto prefixB =
      RoutePrefixV6{folly::IPAddressV6("2401:db00:e702:58:1000::"), 127};
  const auto ipA = folly::IPAddress("2401:db00:e702:50:1000::21");
  const auto ipB = folly::IPAddress("2401:db00:e702:58:1000::1");

  auto logRoutes = [this](const std::string& phase) {
    auto state = getProgrammedState();
    auto fibV6 =
        state->getFibsInfoMap()->getFibContainer(RouterID(0))->getFibV6();
    XLOG(DBG2) << "=== Remote interface routes (" << phase << ") ===";
    for (const auto& [_, route] : std::as_const(*fibV6)) {
      auto entry = route->getEntryForClient(ClientID::REMOTE_INTERFACE_ROUTE);
      if (!entry) {
        continue;
      }
      auto nhops = getClientNextHops(state, *entry);
      XLOG(DBG2) << "  " << route->prefix().network() << "/"
                 << route->prefix().mask() << " -> "
                 << (nhops.empty()
                         ? "none"
                         : folly::to<std::string>(
                               "intf ", nhops.begin()->intfID().value()))
                 << " (connected=" << route->isConnected() << ")";
    }
  };

  auto setup = [&, this]() {
    int numCores =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getNumCores();
    auto switchIdA = static_cast<SwitchID>(numCores);
    auto switchIdB = static_cast<SwitchID>(numCores * 2);

    auto dsfUpdate = [this](
                         const SwitchID& switchId,
                         int portId,
                         const folly::IPAddress& ip,
                         const std::string& desc) {
      auto sysPorts = std::make_shared<SystemPortMap>();
      sysPorts->addNode(
          utility::makeRemoteSysPort(SystemPortID(portId), switchId));
      auto rifs = std::make_shared<InterfaceMap>();
      rifs->addNode(
          utility::makeRemoteInterface(InterfaceID(portId), {{ip, 127}}));

      std::map<SwitchID, std::shared_ptr<SystemPortMap>> switchId2SysPorts;
      std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Rifs;
      switchId2SysPorts[switchId] = sysPorts;
      switchId2Rifs[switchId] = rifs;
      auto sw = getSw();
      sw->getRib()->updateStateInRibThread(
          [sw, switchId2SysPorts, switchId2Rifs, desc]() {
            sw->updateStateWithHwFailureProtection(
                desc,
                [sw, switchId2SysPorts, switchId2Rifs](
                    const std::shared_ptr<SwitchState>& in) {
                  return DsfStateUpdaterUtil::getUpdatedState(
                      in,
                      sw->getScopeResolver(),
                      sw->getRib(),
                      switchId2SysPorts,
                      switchId2Rifs);
                });
          });
    };

    // Phase 1: Initial state — A owns prefixA, B owns prefixB
    dsfUpdate(switchIdA, remotePortIdA, ipA, "Add switch A initial state");
    dsfUpdate(switchIdB, remotePortIdB, ipB, "Add switch B initial state");
    WITH_RETRIES({
      auto fibV6 = getProgrammedState()
                       ->getFibsInfoMap()
                       ->getFibContainer(RouterID(0))
                       ->getFibV6();
      EXPECT_EVENTUALLY_NE(fibV6->exactMatch(prefixA), nullptr);
      EXPECT_EVENTUALLY_NE(fibV6->exactMatch(prefixB), nullptr);
    });
    logRoutes("after Phase 1: initial state");

    // Phase 2: A swaps to B's old IP
    dsfUpdate(switchIdA, remotePortIdA, ipB, "Switch A swaps to B's old IP");
    logRoutes("after Phase 2: switch A swapped");

    // Phase 3: B swaps to A's old IP — without the fix, this deletes
    // A's newly added route because the RIB only matches by prefix.
    dsfUpdate(switchIdB, remotePortIdB, ipA, "Switch B swaps to A's old IP");
    logRoutes("after Phase 3: switch B swapped");
  };

  auto verify = [&, this]() {
    logRoutes("verify: current state");
    WITH_RETRIES({
      auto state = getProgrammedState();
      auto fibV6 =
          state->getFibsInfoMap()->getFibContainer(RouterID(0))->getFibV6();
      // Both prefixes must exist, owned by the swapped interfaces
      auto routeForB = fibV6->exactMatch(prefixA);
      EXPECT_EVENTUALLY_NE(routeForB, nullptr);
      if (routeForB) {
        EXPECT_TRUE(routeForB->isConnected());
        auto nhops = getNextHops(state, routeForB->getForwardInfo());
        ASSERT_EQ(nhops.size(), 1);
        EXPECT_EQ(nhops.begin()->intf(), InterfaceID(remotePortIdB));
      }
      auto routeForA = fibV6->exactMatch(prefixB);
      EXPECT_EVENTUALLY_NE(routeForA, nullptr);
      if (routeForA) {
        EXPECT_TRUE(routeForA->isConnected());
        auto nhops = getNextHops(state, routeForA->getForwardInfo());
        ASSERT_EQ(nhops.size(), 1);
        EXPECT_EQ(nhops.begin()->intf(), InterfaceID(remotePortIdA));
      }
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchWithMultipleDsfNodesTest, addRemoveRemoteNeighbor) {
  auto setup = [this]() {
    // in addRemoteIntfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getNumCores();
    const auto kRemoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteSysPort(
          in,
          scopeResolver(),
          kRemoteSysPortId,
          static_cast<SwitchID>(
              numCores * getAgentEnsemble()->getNumL3Asics()));
    });
    const auto kIntfId = utility::getRemoteIntfId(kRemoteSysPortId);
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteInterface(
          in,
          scopeResolver(),
          kIntfId,
          // TODO - following assumes we haven't
          // already used up the subnets below for
          // local interfaces. In that sense it
          // has a implicit coupling with how ConfigFactory
          // generates subnets for local interfaces
          {
              {folly::IPAddress("100::1"), 64},
              {folly::IPAddress("100.0.0.1"), 24},
          });
    });
    folly::IPAddressV6 kNeighborIp("100::2");
    PortDescriptor kPort(kRemoteSysPortId);
    // Add neighbor
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoveRemoteNeighbor(
          in,
          scopeResolver(),
          kNeighborIp,
          kIntfId,
          kPort,
          true,
          utility::getDummyEncapIndex(getAgentEnsemble()));
    });

    // Remove neighbor
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoveRemoteNeighbor(
          in,
          scopeResolver(),
          kNeighborIp,
          kIntfId,
          kPort,
          false,
          utility::getDummyEncapIndex(getAgentEnsemble()));
    });
  };
  verifyAcrossWarmBoots(setup, [] {});
}

TEST_F(AgentVoqSwitchWithMultipleDsfNodesTest, voqDelete) {
  folly::IPAddressV6 kNeighborIp("100::2");
  auto setup = [this, kNeighborIp]() {
    const auto kRemoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    const auto kIntfId = utility::getRemoteIntfId(kRemoteSysPortId);
    // in addRemoteIntfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getNumCores();
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteSysPort(
          in,
          scopeResolver(),
          kRemoteSysPortId,
          static_cast<SwitchID>(
              numCores * getAgentEnsemble()->getNumL3Asics()));
    });
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteInterface(
          in,
          scopeResolver(),
          kIntfId,
          // TODO - following assumes we haven't
          // already used up the subnets below for
          // local interfaces. In that sense it
          // has a implicit coupling with how ConfigFactory
          // generates subnets for local interfaces
          {
              {folly::IPAddress("100::1"), 64},
              {folly::IPAddress("100.0.0.1"), 24},
          });
    });
    PortDescriptor kPort(kRemoteSysPortId);
    // Add neighbor
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoveRemoteNeighbor(
          in,
          scopeResolver(),
          kNeighborIp,
          kIntfId,
          kPort,
          true,
          utility::getDummyEncapIndex(getAgentEnsemble()));
    });
  };
  auto verify = [this, kNeighborIp]() {
    const auto kRemoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    auto getVoQDeletedPkts = [=, this]() -> std::optional<int64_t> {
      if (!isSupportedOnAllAsics(HwAsic::Feature::VOQ_DELETE_COUNTER)) {
        return 0L;
      }
      auto sysPortStats = utility::getRemoteSysPortStatsForSwitchUnderTest(
          getSw(),
          getProgrammedState(),
          getCurrentSwitchIndexForTesting(),
          kRemoteSysPortId,
          true /* refreshStats */);
      if (!sysPortStats.has_value()) {
        return std::nullopt;
      }
      return folly::copy(
                 sysPortStats->queueCreditWatchdogDeletedPackets_().value())
          .at(utility::getDefaultQueue());
    };

    std::optional<int64_t> voqDeletedPktsBefore;
    WITH_RETRIES({
      voqDeletedPktsBefore = getVoQDeletedPkts();
      ASSERT_EVENTUALLY_TRUE(voqDeletedPktsBefore.has_value());
    });
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto frontPanelPort = ecmpHelper.ecmpPortDescriptorAt(1).phyPortID();
    for (auto i = 0; i < 100; ++i) {
      // Send pkts via front panel
      sendPacket(kNeighborIp, frontPanelPort, std::vector<uint8_t>(1024, 0xff));
    }
    WITH_RETRIES({
      auto voqDeletedPktsAfter = getVoQDeletedPkts();
      ASSERT_EVENTUALLY_TRUE(voqDeletedPktsAfter.has_value());
      XLOG(INFO) << "Voq deleted pkts, before: " << *voqDeletedPktsBefore
                 << " after: " << *voqDeletedPktsAfter;
      EXPECT_EVENTUALLY_EQ(*voqDeletedPktsBefore + 100, *voqDeletedPktsAfter);
    });
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchWithMultipleDsfNodesTest, stressAddRemoveObjects) {
  auto setup = [=, this]() {
    // Disable credit watchdog
    utility::enableCreditWatchdog(getAgentEnsemble(), false);
  };
  auto verify = [this]() {
    auto numIterations = 500;
    const auto kRemoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    folly::IPAddressV6 kNeighborIp("100::2");
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    // in addRemoteIntfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getNumCores();
    const auto kPort = ecmpHelper.ecmpPortDescriptorAt(0);
    const auto kIntfId = utility::getRemoteIntfId(kRemoteSysPortId);
    auto addObjects = [&]() {
      // add local neighbor
      addRemoveNeighbor(kPort, NeighborOp::ADD);
      // Remote objs
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return utility::addRemoteSysPort(
            in,
            scopeResolver(),
            kRemoteSysPortId,
            static_cast<SwitchID>(
                numCores * getAgentEnsemble()->getNumL3Asics()));
      });
      const auto kRemoteIntfId = utility::getRemoteIntfId(kRemoteSysPortId);
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return utility::addRemoteInterface(
            in,
            scopeResolver(),
            kRemoteIntfId,
            // TODO - following assumes we haven't
            // already used up the subnets below for
            // local interfaces. In that sense it
            // has a implicit coupling with how ConfigFactory
            // generates subnets for local interfaces
            {
                {folly::IPAddress("100::1"), 64},
                {folly::IPAddress("100.0.0.1"), 24},
            });
      });
      PortDescriptor kRemotePortDesc(kRemoteSysPortId);
      // Add neighbor
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return utility::addRemoveRemoteNeighbor(
            in,
            scopeResolver(),
            kNeighborIp,
            kRemoteIntfId,
            kRemotePortDesc,
            true,
            utility::getDummyEncapIndex(getAgentEnsemble()));
      });
    };
    auto removeObjects = [&]() {
      addRemoveNeighbor(kPort, NeighborOp::DEL);
      // Remove neighbor
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return utility::addRemoveRemoteNeighbor(
            in,
            scopeResolver(),
            kNeighborIp,
            kIntfId,
            kPort,
            false,
            utility::getDummyEncapIndex(getAgentEnsemble()));
      });
      // Remove rif
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return utility::removeRemoteInterface(in, kIntfId);
      });
      // Remove sys port
      applyNewState([&](const std::shared_ptr<SwitchState>& in) {
        return utility::removeRemoteSysPort(in, kRemoteSysPortId);
      });
    };
    for (auto i = 0; i < numIterations; ++i) {
      addObjects();
      // Delete on all but the last iteration. In the last iteration
      // we will leave the entries intact and then forward pkts
      // to this VOQ
      if (i < numIterations - 1) {
        removeObjects();
      }
    }
    assertVoqTailDrops(kNeighborIp, kRemoteSysPortId);
    auto beforePkts = folly::copy(
        getLatestPortStats(kPort.phyPortID()).outUnicastPkts_().value());
    // CPU send
    sendPacket(ecmpHelper.ip(kPort), std::nullopt);
    auto frontPanelPort = ecmpHelper.ecmpPortDescriptorAt(1).phyPortID();
    sendPacket(ecmpHelper.ip(kPort), frontPanelPort);
    WITH_RETRIES({
      auto afterPkts =
          getLatestPortStats(kPort.phyPortID()).outUnicastPkts_().value();
      EXPECT_EVENTUALLY_EQ(afterPkts, beforePkts + 2);
    });
    // removeObjects before exiting for WB
    removeObjects();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchWithMultipleDsfNodesTest, voqTailDropCounter) {
  folly::IPAddressV6 kNeighborIp("100::2");
  auto setup = [this, kNeighborIp]() {
    const auto kRemoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    const auto kIntfId = utility::getRemoteIntfId(kRemoteSysPortId);
    // in addRemoteIntfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getNumCores();
    // Disable credit watchdog
    utility::enableCreditWatchdog(getAgentEnsemble(), false);
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteSysPort(
          in,
          scopeResolver(),
          kRemoteSysPortId,
          static_cast<SwitchID>(
              numCores * getAgentEnsemble()->getNumL3Asics()));
    });
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteInterface(
          in,
          scopeResolver(),
          kIntfId,
          {
              {folly::IPAddress("100::1"), 64},
              {folly::IPAddress("100.0.0.1"), 24},
          });
    });
    PortDescriptor kPort(kRemoteSysPortId);
    // Add neighbor
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoveRemoteNeighbor(
          in,
          scopeResolver(),
          kNeighborIp,
          kIntfId,
          kPort,
          true,
          utility::getDummyEncapIndex(getAgentEnsemble()));
    });
  };

  auto verify = [this, kNeighborIp]() {
    const auto kRemoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    const bool verifyCgmLog =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->isSupported(
                HwAsic::Feature::VENDOR_SWITCH_CONGESTION_MANAGEMENT_ERRORS);
    const auto switchIds =
        getSw()->getSwitchInfoTable().getSwitchIdsOfType(cfg::SwitchType::VOQ);
    if (verifyCgmLog) {
      for (const auto& switchId : switchIds) {
        getAgentEnsemble()
            ->getHwAgentTestClient(switchId)
            ->sync_installLogCapture();
      }
    }
    assertVoqTailDrops(kNeighborIp, kRemoteSysPortId);
    if (!verifyCgmLog) {
      return;
    }
    WITH_RETRIES({
      bool found = false;
      for (const auto& switchId : switchIds) {
        std::vector<std::string> matches;
        getAgentEnsemble()
            ->getHwAgentTestClient(switchId)
            ->sync_getMatchingLogMessages(
                matches,
                "DROP reasons CGM: (0x20000) "
                "bcmCosqDropReasonVoqWordsSharedMaxSizeReject");
        found |= !matches.empty();
      }
      EXPECT_EVENTUALLY_TRUE(found);
    });
  };
  verifyAcrossWarmBoots(setup, verify);
};

TEST_F(
    AgentVoqSwitchWithMultipleDsfNodesTest,
    sendPktsToRemoteUnresolvedNeighbor) {
  auto setup = [this]() {
    const auto kRemoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    const auto kIntfId = utility::getRemoteIntfId(kRemoteSysPortId);
    // in addRemoteIntfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getNumCores();
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteSysPort(
          in,
          scopeResolver(),
          kRemoteSysPortId,
          static_cast<SwitchID>(
              numCores * getAgentEnsemble()->getNumL3Asics()));
    });
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteInterface(
          in,
          scopeResolver(),
          kIntfId,
          {
              {folly::IPAddress("100::1"), 64},
              {folly::IPAddress("100.0.0.1"), 24},
          });
    });
  };

  auto verify = [=, this]() {
    PortID portId = masterLogicalInterfaceOrHyperPortIds()[0];
    folly::IPAddressV6 kNeighbor6Ip("100::2");
    folly::IPAddressV4 kNeighbor4Ip("100.0.0.2");
    auto portStatsBefore = getLatestPortStats(portId);
    auto switchDropStatsBefore = getAggregatedSwitchDropStats();
    sendPacket(kNeighbor6Ip, portId);
    sendPacket(kNeighbor4Ip, portId);
    WITH_RETRIES({
      auto portStatsAfter = getLatestPortStats(portId);
      auto switchDropStatsAfter = getAggregatedSwitchDropStats();
      EXPECT_EVENTUALLY_EQ(
          2,
          *portStatsAfter.inDiscardsRaw_() - *portStatsBefore.inDiscardsRaw_());
      EXPECT_EVENTUALLY_EQ(
          2,
          *portStatsAfter.inDstNullDiscards_() -
              *portStatsBefore.inDstNullDiscards_());
      EXPECT_EVENTUALLY_EQ(
          *portStatsAfter.inDiscardsRaw_(),
          *portStatsAfter.inDstNullDiscards_());
      EXPECT_EVENTUALLY_EQ(
          *switchDropStatsAfter.ingressPacketPipelineRejectDrops() -
              *switchDropStatsBefore.ingressPacketPipelineRejectDrops(),
          2);
      // Pipeline reject drop, not a queue resolution drop,
      // which happens say when a pkt comes in with a non router
      // MAC
      EXPECT_EQ(
          *switchDropStatsAfter.queueResolutionDrops() -
              *switchDropStatsBefore.queueResolutionDrops(),
          0);
    });
  };
  verifyAcrossWarmBoots(setup, verify);
};

TEST_F(AgentVoqSwitchWithMultipleDsfNodesTest, verifyDscpToVoqMapping) {
  folly::IPAddressV6 kNeighborIp("100::2");
  auto setup = [this, kNeighborIp]() {
    const auto kRemoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    const auto kIntfId = utility::getRemoteIntfId(kRemoteSysPortId);
    // in addRemoteIntfNodeCfg, we use numCores to calculate the remoteSwitchId
    // keeping remote switch id passed below in sync with it
    int numCores =
        checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics())
            ->getNumCores();
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteSysPort(
          in,
          scopeResolver(),
          kRemoteSysPortId,
          static_cast<SwitchID>(
              numCores * getAgentEnsemble()->getNumL3Asics()));
    });
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoteInterface(
          in,
          scopeResolver(),
          kIntfId,
          // TODO - following assumes we haven't
          // already used up the subnets below for
          // local interfaces. In that sense it
          // has a implicit coupling with how ConfigFactory
          // generates subnets for local interfaces
          {
              {folly::IPAddress("100::1"), 64},
              {folly::IPAddress("100.0.0.1"), 24},
          });
    });
    PortDescriptor kPort(kRemoteSysPortId);
    // Add neighbor
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return utility::addRemoveRemoteNeighbor(
          in,
          scopeResolver(),
          kNeighborIp,
          kIntfId,
          kPort,
          true,
          utility::getDummyEncapIndex(getAgentEnsemble()));
    });
  };

  auto verify = [this, kNeighborIp]() {
    const auto kRemoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState());
    for (const auto& q2dscps : utility::kNetworkAIQueueToDscp()) {
      auto tc = q2dscps.first;
      auto voqId = utility::getTrafficClassToVoqId(
          checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics()), tc);
      std::optional<HwSysPortStats> statsBefore;
      WITH_RETRIES({
        statsBefore = utility::getRemoteSysPortStatsForSwitchUnderTest(
            getSw(),
            getProgrammedState(),
            getCurrentSwitchIndexForTesting(),
            kRemoteSysPortId,
            true /* refreshStats */);
        ASSERT_EVENTUALLY_TRUE(statsBefore.has_value());
      });
      auto queueBytesBefore = statsBefore->queueOutBytes_()->at(voqId) +
          statsBefore->queueOutDiscardBytes_()->at(voqId);
      for (auto dscp : q2dscps.second) {
        XLOG(DBG2) << "send packet with dscp " << static_cast<int>(dscp)
                   << " expected on voq " << voqId;
        sendPacket(
            kNeighborIp,
            std::nullopt,
            std::optional<std::vector<uint8_t>>(),
            dscp);
      }
      WITH_RETRIES_N(10, {
        auto statsAfter = utility::getRemoteSysPortStatsForSwitchUnderTest(
            getSw(),
            getProgrammedState(),
            getCurrentSwitchIndexForTesting(),
            kRemoteSysPortId,
            true /* refreshStats */);
        ASSERT_EVENTUALLY_TRUE(statsAfter.has_value());
        auto queueBytesAfter = statsAfter->queueOutBytes_()->at(voqId) +
            statsAfter->queueOutDiscardBytes_()->at(voqId);
        XLOG(DBG2) << "voq " << voqId << " stats before: " << queueBytesBefore
                   << " stats after: " << queueBytesAfter;
        EXPECT_EVENTUALLY_GT(queueBytesAfter, queueBytesBefore);
      });
    }
  };
  verifyAcrossWarmBoots(setup, verify);
};

TEST_F(AgentVoqSwitchWithMultipleDsfNodesTest, resolveRouteToLoopbackIp) {
  const auto ipAddr = folly::IPAddressV6("42::42");
  constexpr auto prefixLen = 128;
  auto getRemoteLoopbackSysPort = [&]() {
    auto remoteSysPorts =
        getProgrammedState()->getRemoteSystemPorts()->getAllNodes();
    auto remoteIntfs =
        getProgrammedState()->getRemoteInterfaces()->getAllNodes();
    for (const auto& [intfId, remoteIntf] : *remoteIntfs) {
      auto sysPortId = SystemPortID(static_cast<int>(intfId));
      auto remoteSysPort = remoteSysPorts->getNodeIf(sysPortId);
      if (remoteSysPort &&
          remoteSysPort->getSwitchId() ==
              utility::getRemoteVoqSwitchId(getSw()) &&
          remoteIntf->getNdpTable()->size() > 0) {
        return sysPortId;
      }
    }
    XLOG(FATAL) << "Unable to find remote loopback system port";
    return SystemPortID(0);
  };

  auto getLoopbackNeighborIp = [&]() {
    auto remoteIntf = getProgrammedState()->getRemoteInterfaces()->getNode(
        utility::getRemoteIntfId(getRemoteLoopbackSysPort()));
    auto nbrTable = remoteIntf->getNdpTable();
    CHECK_GE(nbrTable->size(), 1);
    return folly::IPAddress(nbrTable->cbegin()->first);
  };

  auto setup = [&, this]() {
    auto remoteIntf = getProgrammedState()->getRemoteInterfaces()->getNode(
        utility::getRemoteIntfId(getRemoteLoopbackSysPort()));
    auto neighborIp = getLoopbackNeighborIp();
    auto routeUpdater = getSw()->getRouteUpdater();
    RouteNextHopSet nextHopSet{
        ResolvedNextHop(neighborIp, remoteIntf->getID(), 1)};
    routeUpdater.addRoute(
        RouterID(0),
        ipAddr,
        prefixLen,
        ClientID::BGPD,
        RouteNextHopEntry(nextHopSet, AdminDistance::EBGP));
    routeUpdater.addRoute(
        RouterID(0),
        neighborIp,
        prefixLen,
        ClientID::BGPD,
        RouteNextHopEntry(nextHopSet, AdminDistance::EBGP));
    routeUpdater.program();
  };
  auto verify = [&]() {
    auto sendPacketAndVerifyFwding = [&]() {
      auto neighborIp = getLoopbackNeighborIp();
      auto sysPortID = getRemoteLoopbackSysPort();
      auto ingressPorts = FLAGS_hyper_port ? masterLogicalHyperPortIds()
                                           : masterLogicalInterfacePortIds();
      CHECK(!ingressPorts.empty());
      auto ingressPort = ingressPorts.front();
      std::optional<HwSysPortStats> beforeStats;
      WITH_RETRIES({
        beforeStats = utility::getRemoteSysPortStatsForSwitchUnderTest(
            getSw(),
            getProgrammedState(),
            getCurrentSwitchIndexForTesting(),
            sysPortID);
        EXPECT_EVENTUALLY_TRUE(beforeStats.has_value());
      });
      CHECK(beforeStats.has_value());
      sendPacket(ipAddr, ingressPort);
      sendPacket(neighborIp, ingressPort);

      auto getWatchdogDeletePkts = [](const auto& stats) {
        return stats.queueCreditWatchdogDeletedPackets_()->at(
            utility::getGlobalRcyDefaultQueue());
      };
      WITH_RETRIES({
        auto afterStats = utility::getRemoteSysPortStatsForSwitchUnderTest(
            getSw(),
            getProgrammedState(),
            getCurrentSwitchIndexForTesting(),
            sysPortID);
        EXPECT_EVENTUALLY_TRUE(afterStats.has_value());
        if (!afterStats.has_value()) {
          return;
        }
        XLOG(DBG2) << "Before: " << getWatchdogDeletePkts(*beforeStats)
                   << " After: " << getWatchdogDeletePkts(*afterStats);
        EXPECT_EVENTUALLY_EQ(
            getWatchdogDeletePkts(*afterStats),
            getWatchdogDeletePkts(*beforeStats) + 2);
      });
    };

    sendPacketAndVerifyFwding();

    // Remove route that has the same IP as neighbor
    auto routeUpdater = getSw()->getRouteUpdater();
    routeUpdater.delRoute(
        RouterID(0), getLoopbackNeighborIp(), prefixLen, ClientID::BGPD);
    routeUpdater.program();

    sendPacketAndVerifyFwding();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVoqSwitchWithMultipleDsfNodesTest, resolveRouteToRemoteNeighbor) {
  const auto intfAddrV6 = std::make_pair(folly::IPAddress("100::1"), 64);
  const auto intfAddrV4 = std::make_pair(folly::IPAddress("100.0.0.1"), 24);
  const auto hyperIntfAddrV6 = std::make_pair(folly::IPAddress("101::1"), 64);
  const auto hyperIntfAddrV4 =
      std::make_pair(folly::IPAddress("101.0.0.1"), 24);

  const auto ipAddr = folly::IPAddressV6("42::42");
  const auto hyperIpAddr = folly::IPAddressV6("43::43");
  constexpr auto prefixLen = 128;

  auto makeSwitchId2SystemPorts = [=](auto& remoteSwitchID,
                                      const auto& remoteSysPortId,
                                      const auto& remoteHyperSysPortId) {
    std::map<SwitchID, std::shared_ptr<SystemPortMap>> switchId2SystemPorts;
    auto remoteSysPort =
        utility::makeRemoteSysPort(remoteSysPortId, remoteSwitchID);
    auto remoteHyperSysPort = utility::makeRemoteSysPort(
        remoteHyperSysPortId,
        remoteSwitchID,
        0,
        2,
        static_cast<int>(cfg::PortSpeed::THREEPOINTTWOT),
        HwAsic::InterfaceNodeRole::IN_CLUSTER_NODE,
        cfg::PortType::HYPER_PORT);
    auto remoteSysPorts = std::make_shared<SystemPortMap>();
    remoteSysPorts->addNode(std::move(remoteSysPort));
    remoteSysPorts->addNode(std::move(remoteHyperSysPort));
    switchId2SystemPorts[remoteSwitchID] = std::move(remoteSysPorts);
    return switchId2SystemPorts;
  };

  auto makeSwitchId2Rifs = [=](auto& remoteSwitchID,
                               const auto& remoteSysPortId,
                               const auto& remoteHyperSysPortId) {
    const auto kRemoteIntfId = utility::getRemoteIntfId(remoteSysPortId);
    const auto kRemoteHyperIntfId =
        utility::getRemoteIntfId(remoteHyperSysPortId);
    std::map<SwitchID, std::shared_ptr<InterfaceMap>> switchId2Rifs;
    auto remoteIntf = utility::makeRemoteInterface(
        kRemoteIntfId,
        {
            intfAddrV6,
            intfAddrV4,
        });
    auto remoteHyperIntf = utility::makeRemoteInterface(
        kRemoteHyperIntfId,
        {
            hyperIntfAddrV6,
            hyperIntfAddrV4,
        });
    auto remoteIntfs = std::make_shared<InterfaceMap>();
    remoteIntfs->addNode(std::move(remoteIntf));
    remoteIntfs->addNode(std::move(remoteHyperIntf));
    switchId2Rifs[remoteSwitchID] = std::move(remoteIntfs);
    return switchId2Rifs;
  };

  auto addRemoteSysPortAndIntf = [&](auto swSwitch,
                                     auto& remoteSwitchID,
                                     const auto& remoteSysPortId,
                                     const auto& remoteHyperSysPortId) {
    auto updateDsfStateFn = [=](const std::shared_ptr<SwitchState>& in) {
      auto switchId2SystemPorts = makeSwitchId2SystemPorts(
          remoteSwitchID, remoteSysPortId, remoteHyperSysPortId);
      auto switchId2Rifs = makeSwitchId2Rifs(
          remoteSwitchID, remoteSysPortId, remoteHyperSysPortId);

      return DsfStateUpdaterUtil::getUpdatedState(
          in,
          swSwitch->getScopeResolver(),
          swSwitch->getRib(),
          switchId2SystemPorts,
          switchId2Rifs);
    };
    swSwitch->getRib()->updateStateInRibThread([swSwitch, updateDsfStateFn]() {
      swSwitch->updateStateWithHwFailureProtection(
          fmt::format("Update state for node: {}", 0), updateDsfStateFn);
    });
  };

  auto setup = [&, this]() {
    auto remoteSwitchID = utility::getRemoteVoqSwitchId(getSw());
    const auto remoteSysPortId =
        utility::getAvailableRemoteSysPortId(getSw(), getProgrammedState());
    const auto remoteHyperSysPortId =
        utility::getAvailableRemoteSysPortId(getSw(), getProgrammedState(), 1);
    addRemoteSysPortAndIntf(
        getSw(), remoteSwitchID, remoteSysPortId, remoteHyperSysPortId);
    WITH_RETRIES({
      auto state = getProgrammedState();
      EXPECT_EVENTUALLY_NE(
          state->getRemoteSystemPorts()->getNodeIf(remoteSysPortId), nullptr);
      EXPECT_EVENTUALLY_NE(
          state->getRemoteSystemPorts()->getNodeIf(remoteHyperSysPortId),
          nullptr);
      EXPECT_EVENTUALLY_NE(
          state->getRemoteInterfaces()->getNodeIf(
              utility::getRemoteIntfId(remoteSysPortId)),
          nullptr);
      EXPECT_EVENTUALLY_NE(
          state->getRemoteInterfaces()->getNodeIf(
              utility::getRemoteIntfId(remoteHyperSysPortId)),
          nullptr);
    });

    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto routeUpdater = getSw()->getRouteUpdater();

    boost::container::flat_set<PortDescriptor> regularPortDescs{
        PortDescriptor(remoteSysPortId)};
    boost::container::flat_set<PortDescriptor> hyperPortDescs{
        PortDescriptor(remoteHyperSysPortId)};
    boost::container::flat_set<PortDescriptor> allPortDescs{
        PortDescriptor(remoteSysPortId), PortDescriptor(remoteHyperSysPortId)};
    // Multi-NPU tests must resolve only the sysports created above. The
    // flattened remote sysport map can include sysports for other local NPUs.
    utility::resolveRemoteNhops(getAgentEnsemble(), ecmpHelper, allPortDescs);

    ecmpHelper.programRoutes(
        &routeUpdater, regularPortDescs, {RoutePrefixV6{ipAddr, prefixLen}});
    ecmpHelper.programRoutes(
        &routeUpdater, hyperPortDescs, {RoutePrefixV6{hyperIpAddr, prefixLen}});
  };
  auto verify = [&, this]() {
    auto routeInfo =
        utility::getRouteInfo(ipAddr, prefixLen, *getAgentEnsemble());
    EXPECT_TRUE(*routeInfo.exists() && !*routeInfo.isProgrammedToDrop());
    auto hyperRouteInfo =
        utility::getRouteInfo(hyperIpAddr, prefixLen, *getAgentEnsemble());
    EXPECT_TRUE(
        *hyperRouteInfo.exists() && !*hyperRouteInfo.isProgrammedToDrop());

    // Re-discover the dynamically allocated remote sysport IDs from state
    // (setup() is skipped on warm boot, so locals from setup are unavailable).
    const auto remoteSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState(), 0);
    const auto remoteHyperSysPortId =
        utility::getRemoteSysPortId(getSw(), getProgrammedState(), 1);
    std::optional<HwSysPortStats> beforeStats;
    std::optional<HwSysPortStats> beforeHyperStats;
    WITH_RETRIES({
      beforeStats = utility::getRemoteSysPortStatsForSwitchUnderTest(
          getSw(),
          getProgrammedState(),
          getCurrentSwitchIndexForTesting(),
          remoteSysPortId);
      beforeHyperStats = utility::getRemoteSysPortStatsForSwitchUnderTest(
          getSw(),
          getProgrammedState(),
          getCurrentSwitchIndexForTesting(),
          remoteHyperSysPortId);
      ASSERT_EVENTUALLY_TRUE(beforeStats.has_value());
      ASSERT_EVENTUALLY_TRUE(beforeHyperStats.has_value());
    });
    sendPacket(ipAddr, std::nullopt /* frontPanelPort */);
    sendPacket(hyperIpAddr, std::nullopt /* frontPanelPort */);
    auto getWatchdogDeletePkts = [](const auto& stats) {
      return stats.queueCreditWatchdogDeletedPackets_()->at(
          utility::getDefaultQueue());
    };
    WITH_RETRIES({
      auto afterStats = utility::getRemoteSysPortStatsForSwitchUnderTest(
          getSw(),
          getProgrammedState(),
          getCurrentSwitchIndexForTesting(),
          remoteSysPortId);
      auto afterHyperStats = utility::getRemoteSysPortStatsForSwitchUnderTest(
          getSw(),
          getProgrammedState(),
          getCurrentSwitchIndexForTesting(),
          remoteHyperSysPortId);
      ASSERT_EVENTUALLY_TRUE(afterStats.has_value());
      ASSERT_EVENTUALLY_TRUE(afterHyperStats.has_value());
      XLOG(DBG2) << "Regular port - Before: "
                 << getWatchdogDeletePkts(*beforeStats)
                 << " After: " << getWatchdogDeletePkts(*afterStats);
      XLOG(DBG2) << "Hyper port - Before: "
                 << getWatchdogDeletePkts(*beforeHyperStats)
                 << " After: " << getWatchdogDeletePkts(*afterHyperStats);
      EXPECT_EVENTUALLY_GT(
          getWatchdogDeletePkts(*afterStats),
          getWatchdogDeletePkts(*beforeStats));
      EXPECT_EVENTUALLY_GT(
          getWatchdogDeletePkts(*afterHyperStats),
          getWatchdogDeletePkts(*beforeHyperStats));
    });

    // Unresolve neighbor
    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    boost::container::flat_set<PortDescriptor> portDescs{
        PortDescriptor(remoteSysPortId), PortDescriptor(remoteHyperSysPortId)};
    utility::unresolveRemoteNhops(getAgentEnsemble(), ecmpHelper, portDescs);

    routeInfo = utility::getRouteInfo(ipAddr, prefixLen, *getAgentEnsemble());
    EXPECT_TRUE(*routeInfo.exists() && *routeInfo.isProgrammedToDrop());
    hyperRouteInfo =
        utility::getRouteInfo(hyperIpAddr, prefixLen, *getAgentEnsemble());
    EXPECT_TRUE(
        *hyperRouteInfo.exists() && *hyperRouteInfo.isProgrammedToDrop());

    // Resolve neighbor
    utility::resolveRemoteNhops(getAgentEnsemble(), ecmpHelper, portDescs);
    routeInfo = utility::getRouteInfo(ipAddr, prefixLen, *getAgentEnsemble());
    EXPECT_TRUE(*routeInfo.exists() && !*routeInfo.isProgrammedToDrop());
    hyperRouteInfo =
        utility::getRouteInfo(hyperIpAddr, prefixLen, *getAgentEnsemble());
    EXPECT_TRUE(
        *hyperRouteInfo.exists() && !*hyperRouteInfo.isProgrammedToDrop());
  };
  verifyAcrossWarmBoots(setup, verify);
}

class AgentVoqShelSwitchTest : public AgentVoqSwitchWithMultipleDsfNodesTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config =
        AgentVoqSwitchWithMultipleDsfNodesTest::initialConfig(ensemble);
    return getShelEnabledConfig(config);
  }

  // SHEL (self-healing ECMP lag) is exercised across all NIF ports, so opt out
  // of the default interface-port cap and configure the full port set.
  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return std::nullopt;
  }

 protected:
  void verifyShelEnabled(bool desiredShelEnable, bool shelEnabled) {
    auto state = getProgrammedState();
    for (const auto& portMap : std::as_const(*state->getPorts())) {
      for (const auto& port : std::as_const(*portMap.second)) {
        if (port.second->getPortType() == cfg::PortType::INTERFACE_PORT ||
            port.second->getPortType() == cfg::PortType::HYPER_PORT) {
          if (desiredShelEnable) {
            EXPECT_TRUE(
                port.second->getDesiredSelfHealingECMPLagEnable().has_value());
            if (port.second->getDesiredSelfHealingECMPLagEnable().has_value()) {
              EXPECT_EQ(
                  port.second->getDesiredSelfHealingECMPLagEnable().value(),
                  desiredShelEnable);
            }
          }
          if (shelEnabled) {
            EXPECT_TRUE(port.second->getSelfHealingECMPLagEnable().has_value());
            if (port.second->getSelfHealingECMPLagEnable().has_value()) {
              EXPECT_EQ(
                  port.second->getSelfHealingECMPLagEnable().value(),
                  shelEnabled);
            }
          }
        }
      }
    }
  }

  void verifyShelPortState(bool enabled) {
    WITH_RETRIES({
      auto hwSwitchStats = getHwSwitchStats();
      auto sysPortShelState = hwSwitchStats.sysPortShelState();
      auto state = getProgrammedState();
      for (const auto& portMap : std::as_const(*state->getPorts())) {
        for (const auto& port : std::as_const(*portMap.second)) {
          if ((port.second->getPortType() != cfg::PortType::INTERFACE_PORT &&
               port.second->getPortType() != cfg::PortType::HYPER_PORT) ||
              scopeResolver().scope(port.second).switchId() !=
                  getCurrentSwitchIdForTesting()) {
            continue;
          }

          auto systemPortId = getSystemPortID(
              PortDescriptor(port.second->getID()), cfg::Scope::GLOBAL);
          auto systemPortIdKey = static_cast<int>(systemPortId);
          EXPECT_EVENTUALLY_TRUE(sysPortShelState->contains(systemPortIdKey));
          if (sysPortShelState->contains(systemPortIdKey)) {
            EXPECT_EVENTUALLY_EQ(
                sysPortShelState->at(systemPortIdKey),
                (enabled ? cfg::PortState::ENABLED : cfg::PortState::DISABLED));
          }
        }
      }
    })
  }
};

TEST_F(AgentVoqShelSwitchTest, init) {
  auto setup = []() {};
  auto verify = [&, this]() {
    verifyShelEnabled(true /*desiredShelEnable*/, false /*shelEnabled*/);

    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto localSysPortDescs = resolveLocalNhops(ecmpHelper);
    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &routeUpdater,
        boost::container::flat_set<PortDescriptor>(
            std::make_move_iterator(localSysPortDescs.begin()),
            std::make_move_iterator(localSysPortDescs.end())));

    verifyShelEnabled(true /*desiredShelEnable*/, true /*shelEnabled*/);

    // Toggle ports to trigger callback
    auto state = getProgrammedState();
    for (const auto& portMap : std::as_const(*state->getPorts())) {
      for (const auto& port : std::as_const(*portMap.second)) {
        if ((port.second->getPortType() == cfg::PortType::INTERFACE_PORT ||
             port.second->getPortType() == cfg::PortType::HYPER_PORT) &&
            port.second->getSelfHealingECMPLagEnable()) {
          bringDownPort(port.second->getID());
          bringUpPort(port.second->getID());
        }
      }
    }

    verifyShelPortState(true /*enabled*/);
  };
  auto setupPostWarmboot = [&, this]() {
    // Verify SHEL port state is reconstructed after WB
    verifyShelPortState(true /*enabled*/);

    // Remove default route to disable shelState
    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.unprogramRoutes(&routeUpdater);
    verifyShelEnabled(true /*desiredShelEnable*/, false /*shelEnabled*/);

    // Disable selfHealingEcmpLag on Interface Ports
    auto config = getSw()->getConfig();
    config.switchSettings()->selfHealingEcmpLagConfig().reset();

    for (auto& port : *config.ports()) {
      if (port.portType() == cfg::PortType::INTERFACE_PORT ||
          port.portType() == cfg::PortType::HYPER_PORT) {
        port.selfHealingECMPLagEnable() = false;
      }
    }
    applyNewConfig(config);
  };
  auto verifyPostWarmboot = [&, this]() {
    verifyShelEnabled(false /*desiredShelEnable*/, false /*shelEnabled*/);
  };
  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

TEST_F(AgentVoqShelSwitchTest, routeChurn) {
  constexpr auto numChurn = 100;
  auto setup = []() {};
  auto verify = [&, this]() {
    verifyShelEnabled(true /*desiredShelEnable*/, false /*shelEnabled*/);

    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto localSysPortDescs = resolveLocalNhops(ecmpHelper);
    auto portDescriptors = boost::container::flat_set<PortDescriptor>(
        std::make_move_iterator(localSysPortDescs.begin()),
        std::make_move_iterator(localSysPortDescs.end()));
    auto routeUpdater = getSw()->getRouteUpdater();

    for (int i = 0; i < numChurn; i++) {
      ecmpHelper.programRoutes(&routeUpdater, portDescriptors);
      verifyShelEnabled(true /*desiredShelEnable*/, true /*shelEnabled*/);
      ecmpHelper.unprogramRoutes(&routeUpdater);
      verifyShelEnabled(true /*desiredShelEnable*/, false /*shelEnabled*/);
    }
    ecmpHelper.programRoutes(&routeUpdater, portDescriptors);

    // Toggle ports to trigger callback
    auto state = getProgrammedState();
    for (const auto& portMap : std::as_const(*state->getPorts())) {
      for (const auto& port : std::as_const(*portMap.second)) {
        if ((port.second->getPortType() == cfg::PortType::INTERFACE_PORT ||
             port.second->getPortType() == cfg::PortType::HYPER_PORT) &&
            port.second->getSelfHealingECMPLagEnable()) {
          bringDownPort(port.second->getID());
          bringUpPort(port.second->getID());
        }
      }
    }

    verifyShelPortState(true /*enabled*/);
    ecmpHelper.unprogramRoutes(&routeUpdater);
  };

  verifyAcrossWarmBoots(setup, verify);
}

class AgentVoqShelWBEnableTest : public AgentVoqShelSwitchTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return AgentVoqSwitchWithMultipleDsfNodesTest::initialConfig(ensemble);
  }
};

TEST_F(AgentVoqShelWBEnableTest, wbEnable) {
  auto setup = []() {};
  auto verify = [&, this]() {
    verifyShelEnabled(false /*desiredShelEnable*/, false /*shelEnabled*/);
  };
  auto setupPostWarmboot = [&, this]() {
    applyNewConfig(getShelEnabledConfig(getSw()->getConfig()));
    verifyShelEnabled(true /*desiredShelEnable*/, false /*shelEnabled*/);

    utility::EcmpSetupTargetedPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto localSysPortDescs = resolveLocalNhops(ecmpHelper);
    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &routeUpdater,
        boost::container::flat_set<PortDescriptor>(
            std::make_move_iterator(localSysPortDescs.begin()),
            std::make_move_iterator(localSysPortDescs.end())));

    verifyShelEnabled(true /*desiredShelEnable*/, true /*shelEnabled*/);

    // Toggle ports to trigger callback
    auto state = getProgrammedState();
    for (const auto& portMap : std::as_const(*state->getPorts())) {
      for (const auto& port : std::as_const(*portMap.second)) {
        if ((port.second->getPortType() == cfg::PortType::INTERFACE_PORT ||
             port.second->getPortType() == cfg::PortType::HYPER_PORT) &&
            port.second->getSelfHealingECMPLagEnable()) {
          bringDownPort(port.second->getID());
          bringUpPort(port.second->getID());
        }
      }
    }

    verifyShelPortState(true /*enabled*/);
  };
  auto verifyPostWarmboot = []() {};
  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

} // namespace facebook::fboss
