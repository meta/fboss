// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/FbossError.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/lib/CommonUtils.h"

#include "fboss/agent/test/gen-cpp2/production_features_types.h"

#include <chrono>
#include <thread>

namespace facebook::fboss {

namespace {
constexpr int kNumPkts = 100;
const folly::MacAddress kVirtualMac("02:fb:00:00:00:01");
// Differs from kVirtualMac only in the last bit, so it is not a MY_MAC match
const folly::MacAddress kVirtualMacPlusOne(
    folly::MacAddress::fromHBO(kVirtualMac.u64HBO() + 1));

int vlanOf(const cfg::SwitchConfig& config, PortID port) {
  for (const auto& vlanPort : *config.vlanPorts()) {
    if (PortID(*vlanPort.logicalPort()) == port) {
      return *vlanPort.vlanID();
    }
  }
  throw FbossError("No VLAN configured for port ", port);
}
} // namespace

// Packets ingress on port A and are expected to be routed out of port B, the
// route next hop. With one port per interface, A and B are in different VLANs
// and MY_MAC is programmed for A's VLAN.
class AgentMyMacTest : public AgentHwTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(), ensemble.masterLogicalPortIds());
    const auto ports = ensemble.masterLogicalInterfacePortIds();
    CHECK_NE(vlanOf(config, ports[0]), vlanOf(config, ports[1]));
    return config;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::MY_MAC};
  }

 protected:
  PortID ingressPort() const {
    return masterLogicalInterfacePortIds()[0];
  }

  PortID egressPort() const {
    return masterLogicalInterfacePortIds()[1];
  }

  VlanID ingressVlan() const {
    return getProgrammedState()
        ->getPorts()
        ->getNodeIf(ingressPort())
        ->getIngressVlan();
  }

  void applyMyMac() {
    auto config = initialConfig(*getAgentEnsemble());
    cfg::MacAndVlan myMac;
    myMac.macAddress() = kVirtualMac.toString();
    myMac.vlanID() = vlanOf(config, ingressPort());
    config.switchSettings()->myMacs() = {myMac};
    applyNewConfig(config);
  }

  void removeMyMac() {
    applyNewConfig(initialConfig(*getAgentEnsemble()));
  }

  void setupRoutes() {
    const PortDescriptor egress(egressPort());
    utility::EcmpSetupTargetedPorts6 ecmpHelper6(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    utility::EcmpSetupTargetedPorts4 ecmpHelper4(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return ecmpHelper6.resolveNextHops(in, {egress});
    });
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return ecmpHelper4.resolveNextHops(in, {egress});
    });
    auto wrapper = getSw()->getRouteUpdater();
    ecmpHelper6.programRoutes(&wrapper, {egress});
    ecmpHelper4.programRoutes(&wrapper, {egress});
  }

  void sendPkts(const folly::MacAddress& dstMac, bool isV6) {
    auto srcMac = utility::MacAddressGenerator().get(routerMac().u64HBO() + 1);
    auto srcIp = folly::IPAddress(isV6 ? "1001::1" : "10.0.0.1");
    auto dstIp = folly::IPAddress(isV6 ? "2001::1" : "200.0.0.1");
    for (int i = 0; i < kNumPkts; ++i) {
      auto pkt = utility::makeUDPTxPacket(
          getSw(), ingressVlan(), srcMac, dstMac, srcIp, dstIp, 4242, 4242);
      getSw()->sendPacketOutOfPortAsync(std::move(pkt), ingressPort());
    }
  }

  void verifyRouted(const folly::MacAddress& dstMac) {
    learnL2EntryIfPending(
        utility::MacAddressGenerator().get(routerMac().u64HBO() + 1),
        ingressPort());
    for (bool isV6 : {true, false}) {
      auto before = getLatestPortStats(egressPort());
      sendPkts(dstMac, isV6);
      WITH_RETRIES({
        auto after = getLatestPortStats(egressPort());
        EXPECT_EVENTUALLY_EQ(
            *after.outUnicastPkts_() - *before.outUnicastPkts_(), kNumPkts);
      });
    }
  }

  // With one port per interface, A is the only port in its VLAN, so packets to
  // a non router MAC are dropped. Check they went out of (and so looped back
  // into) A, then wait before checking that none were routed out of B.
  void verifyNotRouted(const folly::MacAddress& dstMac) {
    learnL2EntryIfPending(
        utility::MacAddressGenerator().get(routerMac().u64HBO() + 1),
        ingressPort());
    for (bool isV6 : {true, false}) {
      auto before = getLatestPortStats({ingressPort(), egressPort()});
      sendPkts(dstMac, isV6);
      WITH_RETRIES({
        auto after = getLatestPortStats(ingressPort());
        EXPECT_EVENTUALLY_EQ(
            *after.outUnicastPkts_() - *before[ingressPort()].outUnicastPkts_(),
            kNumPkts);
      });
      // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep_for
      std::this_thread::sleep_for(std::chrono::seconds(10));
      EXPECT_EQ(
          *getLatestPortStats(egressPort()).outUnicastPkts_(),
          *before[egressPort()].outUnicastPkts_());
    }
  }

  folly::MacAddress routerMac() const {
    return getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  }
};

TEST_F(AgentMyMacTest, routerMacAndVirtualMacRouted) {
  auto setup = [this]() {
    applyMyMac();
    setupRoutes();
  };
  auto verify = [this]() {
    verifyRouted(routerMac());
    verifyRouted(kVirtualMac);
    verifyNotRouted(kVirtualMacPlusOne);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentMyMacTest, virtualMacRoutedAfterMyMacAddedPostWarmboot) {
  auto setup = [this]() { setupRoutes(); };
  auto verify = [this]() {
    verifyRouted(routerMac());
    verifyNotRouted(kVirtualMac);
    verifyNotRouted(kVirtualMacPlusOne);
  };
  auto setupPostWarmboot = [this]() { applyMyMac(); };
  auto verifyPostWarmboot = [this]() {
    verifyRouted(routerMac());
    verifyRouted(kVirtualMac);
    verifyNotRouted(kVirtualMacPlusOne);
  };
  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

TEST_F(AgentMyMacTest, virtualMacNotRoutedAfterMyMacRemovedPostWarmboot) {
  auto setup = [this]() {
    applyMyMac();
    setupRoutes();
  };
  auto verify = [this]() {
    verifyRouted(routerMac());
    verifyRouted(kVirtualMac);
    verifyNotRouted(kVirtualMacPlusOne);
  };
  auto setupPostWarmboot = [this]() { removeMyMac(); };
  auto verifyPostWarmboot = [this]() {
    verifyRouted(routerMac());
    verifyNotRouted(kVirtualMac);
    verifyNotRouted(kVirtualMacPlusOne);
  };
  verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
}

} // namespace facebook::fboss
