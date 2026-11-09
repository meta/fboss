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
    auto ports = ensemble.masterLogicalInterfacePortIds();
    utility::addVrfForPorts(config, kTestVrf, {ports[0], ports[1]});
    return config;
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

  utility::EcmpSetupTargetedPorts6 ecmpHelper(RouterID vrf) const {
    return utility::EcmpSetupTargetedPorts6(
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), vrf);
  }

  void programRoute(RouterID vrf, PortID egressPort) {
    auto helper = ecmpHelper(vrf);
    boost::container::flat_set<PortDescriptor> nhops{
        PortDescriptor(egressPort)};
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return helper.resolveNextHops(in, nhops);
    });
    auto wrapper = getSw()->getRouteUpdater();
    helper.programRoutes(&wrapper, nhops, {kTestPrefix});
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

TEST_F(AgentVrfTest, MoveInterfaceToDefaultVrf) {
  auto setup = [this]() {
    programRoute(kTestVrf, vrfEgressPort());
    programRoute(RouterID(0), defaultVrfEgressPort());
    // Routes through the moving interface are removed before its RIF moves
    auto config = AgentHwTest::initialConfig(*getAgentEnsemble());
    utility::addVrfForPorts(config, kTestVrf, {ingressPort()});
    applyNewConfig(config);
  };

  auto verify = [this]() {
    auto outPkts = [this](PortID port) {
      return utility::getPortOutPkts(getLatestPortStats(port));
    };
    // The kTestVrf route lost its next hop, so traffic in kTestVrf is dropped
    auto sentBefore = outPkts(ingressPort());
    auto movedBefore = outPkts(vrfEgressPort());
    auto defaultBefore = outPkts(defaultVrfEgressPort());
    getSw()->sendPacketOutOfPortAsync(makePacket(ingressPort()), ingressPort());
    WITH_RETRIES({ EXPECT_EVENTUALLY_GT(outPkts(ingressPort()), sentBefore); });
    EXPECT_EQ(outPkts(vrfEgressPort()), movedBefore);
    EXPECT_EQ(outPkts(defaultVrfEgressPort()), defaultBefore);

    // The moved interface routes in the default VRF
    defaultBefore = outPkts(defaultVrfEgressPort());
    getSw()->sendPacketOutOfPortAsync(
        makePacket(vrfEgressPort()), vrfEgressPort());
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(outPkts(defaultVrfEgressPort()), defaultBefore + 1);
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentVrfTest, DeleteVrfAndInterface) {
  auto setup = [this]() {
    programRoute(kTestVrf, vrfEgressPort());
    programRoute(RouterID(0), defaultVrfEgressPort());
    // Delete kTestVrf together with the egress port's interface, while the
    // kTestVrf route still goes through it
    auto config = AgentHwTest::initialConfig(*getAgentEnsemble());
    std::set<int32_t> egressVlans;
    for (const auto& vlanPort : *config.vlanPorts()) {
      if (PortID(*vlanPort.logicalPort()) == vrfEgressPort()) {
        egressVlans.insert(*vlanPort.vlanID());
      }
    }
    auto& intfs = *config.interfaces();
    intfs.erase(
        std::remove_if(
            intfs.begin(),
            intfs.end(),
            [&](const auto& intf) {
              return (intf.portID() &&
                      PortID(*intf.portID()) == vrfEgressPort()) ||
                  (*intf.type() == cfg::InterfaceType::VLAN &&
                   egressVlans.contains(*intf.vlanID()));
            }),
        intfs.end());
    // Its VLAN goes with it; the port moves to the default VLAN
    auto& vlans = *config.vlans();
    vlans.erase(
        std::remove_if(
            vlans.begin(),
            vlans.end(),
            [&](const auto& vlan) { return egressVlans.contains(*vlan.id()); }),
        vlans.end());
    for (auto& vlanPort : *config.vlanPorts()) {
      if (PortID(*vlanPort.logicalPort()) == vrfEgressPort()) {
        vlanPort.vlanID() = *config.defaultVlan();
      }
    }
    for (auto& port : *config.ports()) {
      if (PortID(*port.logicalID()) == vrfEgressPort()) {
        port.state() = cfg::PortState::DISABLED;
        if (!egressVlans.empty()) {
          port.ingressVlan() = *config.defaultVlan();
        }
      }
    }
    applyNewConfig(config);
  };

  auto verify = [this]() {
    EXPECT_EQ(getProgrammedState()->getVrfs()->getNodeIf(kTestVrf), nullptr);
    // The ingress interface moved to the default VRF and routes there
    auto defaultBefore =
        utility::getPortOutPkts(getLatestPortStats(defaultVrfEgressPort()));
    getSw()->sendPacketOutOfPortAsync(makePacket(ingressPort()), ingressPort());
    WITH_RETRIES({
      EXPECT_EVENTUALLY_EQ(
          utility::getPortOutPkts(getLatestPortStats(defaultVrfEgressPort())),
          defaultBefore + 1);
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
