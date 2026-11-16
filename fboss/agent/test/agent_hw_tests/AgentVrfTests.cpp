// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/test/AgentHwTest.h"

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/gen-cpp2/production_features_types.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/PortStatsTestUtils.h"
#include "fboss/agent/test/utils/RouteTestUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <folly/IPAddress.h>

namespace facebook::fboss {

namespace {
const RouterID kTestVrf{1001};
const RoutePrefixV6 kTestPrefix{folly::IPAddressV6("2001:db8:100::"), 64};
} // namespace

class AgentVrfTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::MULTI_VRF};
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = AgentHwTest::initialConfig(ensemble);
    utility::addVrfForPorts(
        config, kTestVrf, vrfPorts(ensemble.masterLogicalInterfacePortIds()));
    return config;
  }

  static std::vector<PortID> vrfPorts(const std::vector<PortID>& ports) {
    std::vector<PortID> vrfPorts{ports[0], ports[1]};
    if (ports.size() > 3) {
      vrfPorts.push_back(ports[3]);
    }
    return vrfPorts;
  }

  // Move the interfaces of vrfPorts() into kTestVrf. Done in cold boot setup
  // only: the bindings persist across warm boot.
  void bindVrfPorts() {
    std::map<InterfaceID, RouterID> interfaceVrfs;
    for (const auto& port : vrfPorts(masterLogicalInterfacePortIds())) {
      interfaceVrfs.emplace(interfaceOf(port), kTestVrf);
    }
    getSw()->updateInterfaceVrfs(interfaceVrfs);
  }

  PortID ingressPort() const {
    return masterLogicalInterfacePortIds()[0];
  }
  PortID vrfEgressPort() const {
    return masterLogicalInterfacePortIds()[1];
  }
  PortID defaultVrfEgressPort() const {
    return masterLogicalInterfacePortIds()[2];
  }
  // Second kTestVrf egress port, when there are enough ports
  PortID vrfSecondEgressPort() const {
    return masterLogicalInterfacePortIds()[3];
  }

  utility::EcmpSetupTargetedPorts6 ecmpHelper(RouterID vrf) const {
    return utility::EcmpSetupTargetedPorts6(
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), vrf);
  }

  void programRoute(RouterID vrf, PortID egressPort) {
    programRoute(vrf, {PortDescriptor(egressPort)});
  }

  void programRoute(
      RouterID vrf,
      const boost::container::flat_set<PortDescriptor>& nhops) {
    resolveNeighbors(vrf, nhops);
    auto helper = ecmpHelper(vrf);
    auto wrapper = getSw()->getRouteUpdater();
    helper.programRoutes(&wrapper, nhops, {kTestPrefix});
  }

  void resolveNeighbors(
      RouterID vrf,
      const boost::container::flat_set<PortDescriptor>& nhops) {
    auto helper = ecmpHelper(vrf);
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper.resolveNextHops(in, nhops);
    });
  }

  InterfaceID interfaceOf(PortID port) const {
    return getProgrammedState()->getInterfaceIDForPort(PortDescriptor(port));
  }

  // Sent out of port and looped back, so routed in port's VRF
  std::unique_ptr<TxPacket> makePacket(PortID port) const {
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    std::optional<VlanID> vlan;
    if (getVlanIDForTx()) {
      vlan =
          getProgrammedState()->getPorts()->getNodeIf(port)->getIngressVlan();
    }
    return utility::makeUDPTxPacket(
        getSw(),
        vlan,
        srcMac,
        intfMac,
        folly::IPAddressV6(kTestSrcIpV6),
        folly::IPAddressV6("2001:db8:100::1"),
        kTestSrcPort,
        kTestDstPort);
  }
};

TEST_F(AgentVrfTest, RouteInVrf) {
  auto setup = [this]() {
    bindVrfPorts();
    programRoute(kTestVrf, vrfEgressPort());
    programRoute(RouterID(0), defaultVrfEgressPort());
  };

  auto verify = [this]() {
    EXPECT_TRUE(
        utility::isRouteToNexthop(
            kTestPrefix.network(),
            kTestPrefix.mask(),
            ecmpHelper(kTestVrf).ip(PortDescriptor(vrfEgressPort())),
            *getAgentEnsemble(),
            kTestVrf));
    EXPECT_TRUE(
        utility::isRouteToNexthop(
            kTestPrefix.network(),
            kTestPrefix.mask(),
            ecmpHelper(RouterID(0)).ip(PortDescriptor(defaultVrfEgressPort())),
            *getAgentEnsemble()));

    auto vrfOutBefore =
        utility::getPortOutPkts(getLatestPortStats(vrfEgressPort()));
    auto defaultVrfOutBefore =
        utility::getPortOutPkts(getLatestPortStats(defaultVrfEgressPort()));
    // Looped back into ingressPort, so routed in kTestVrf
    getSw()->sendPacketOutOfPortAsync(makePacket(ingressPort()), ingressPort());
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(
          utility::getPortOutPkts(getLatestPortStats(vrfEgressPort())),
          vrfOutBefore + 1);
    });
    EXPECT_EQ(
        utility::getPortOutPkts(getLatestPortStats(defaultVrfEgressPort())),
        defaultVrfOutBefore);

    // Without a route in kTestVrf, the packet is dropped rather than routed
    // by the default VRF route
    auto vrfHelper = ecmpHelper(kTestVrf);
    {
      auto wrapper = getSw()->getRouteUpdater();
      vrfHelper.unprogramRoutes(&wrapper, {kTestPrefix});
    }
    // Loopback ingress is not counted, so wait for the packet to be sent
    auto sentBefore =
        utility::getPortOutPkts(getLatestPortStats(ingressPort()));
    vrfOutBefore = utility::getPortOutPkts(getLatestPortStats(vrfEgressPort()));
    defaultVrfOutBefore =
        utility::getPortOutPkts(getLatestPortStats(defaultVrfEgressPort()));
    getSw()->sendPacketOutOfPortAsync(makePacket(ingressPort()), ingressPort());
    WITH_RETRIES({
      EXPECT_EVENTUALLY_GT(
          utility::getPortOutPkts(getLatestPortStats(ingressPort())),
          sentBefore);
    });
    EXPECT_EQ(
        utility::getPortOutPkts(getLatestPortStats(vrfEgressPort())),
        vrfOutBefore);
    EXPECT_EQ(
        utility::getPortOutPkts(getLatestPortStats(defaultVrfEgressPort())),
        defaultVrfOutBefore);

    // Restore the route for the next verify
    auto wrapper = getSw()->getRouteUpdater();
    vrfHelper.programRoutes(
        &wrapper, {PortDescriptor(vrfEgressPort())}, {kTestPrefix});
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVrfTest, MoveInterfaceAtRuntime) {
  if (masterLogicalInterfacePortIds().size() < 4) {
    GTEST_SKIP() << "Needs 4 interface ports";
  }
  auto setup = [this]() {
    bindVrfPorts();
    programRoute(
        kTestVrf,
        {PortDescriptor(vrfEgressPort()),
         PortDescriptor(vrfSecondEgressPort())});
    programRoute(RouterID(0), defaultVrfEgressPort());
  };

  auto verify = [this]() {
    auto outPkts = [this](PortID port) {
      return utility::getPortOutPkts(getLatestPortStats(port));
    };
    auto movingIntf = interfaceOf(vrfSecondEgressPort());

    // The kTestVrf ECMP route keeps its next hop on the other interface
    getSw()->updateInterfaceVrfs({{movingIntf, RouterID(0)}});
    auto vrfBefore = outPkts(vrfEgressPort());
    auto movedBefore = outPkts(vrfSecondEgressPort());
    for (int i = 0; i < 10; ++i) {
      getSw()->sendPacketOutOfPortAsync(
          makePacket(ingressPort()), ingressPort());
    }
    WITH_RETRIES(
        { EXPECT_EVENTUALLY_EQ(outPkts(vrfEgressPort()), vrfBefore + 10); });
    EXPECT_EQ(outPkts(vrfSecondEgressPort()), movedBefore);

    // The moved interface routes in the default VRF
    auto defaultBefore = outPkts(defaultVrfEgressPort());
    getSw()->sendPacketOutOfPortAsync(
        makePacket(vrfSecondEgressPort()), vrfSecondEgressPort());
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(outPkts(defaultVrfEgressPort()), defaultBefore + 1);
    });

    // Move it back for the next verify
    getSw()->updateInterfaceVrfs({{movingIntf, kTestVrf}});
    resolveNeighbors(kTestVrf, {PortDescriptor(vrfSecondEgressPort())});
    EXPECT_EQ(
        getProgrammedState()->getInterfaceVrfs()->getVrf(movingIntf), kTestVrf);
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
