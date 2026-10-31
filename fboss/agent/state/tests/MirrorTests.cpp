// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/Mirror.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/MirrorConfigs.h"
#include "fboss/agent/test/TestUtils.h"
#include "folly/MacAddress.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>

#include <unordered_set>

namespace facebook::fboss {

class MirrorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_ = testConfigA();
    platform_ = createMockPlatform();
    state_ = testStateA();
  }

  void configureAcl(const std::string& name, uint16_t dstL4Port = 1234) {
    cfg::AclEntry aclEntry;
    auto aclCount = config_.acls()->size() + 1;
    config_.acls()->resize(aclCount);
    *config_.acls()[aclCount - 1].name() = name;
    *config_.acls()[aclCount - 1].actionType() = cfg::AclActionType::PERMIT;
    config_.acls()[aclCount - 1].l4DstPort() = dstL4Port;
  }

  void configurePortMirror(
      const std::string& mirror,
      PortID port,
      bool setSampleDestination = false) {
    int portIndex = int(port) - 1;
    config_.ports()[portIndex].ingressMirror() = mirror;
    if (setSampleDestination) {
      // Sampling to a mirror is ingress-only; egressMirror is rejected.
      config_.ports()[portIndex].sampleDest() = cfg::SampleDestination::MIRROR;
    } else {
      config_.ports()[portIndex].egressMirror() = mirror;
    }
  }

  void configureAclMirror(const std::string& name, const std::string& mirror) {
    cfg::MatchAction action;
    action.ingressMirror() = mirror;
    action.egressMirror() = mirror;

    cfg::MatchToAction mirrorAction;
    *mirrorAction.matcher() = name;
    *mirrorAction.action() = action;
    // Initialize data plane traffic policy only when uninitialized.
    if (!config_.dataPlaneTrafficPolicy()) {
      config_.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
    }
    config_.dataPlaneTrafficPolicy()->matchToAction()->push_back(mirrorAction);
  }

  void publishWithStateUpdate() {
    state_ = publishAndApplyConfig(state_, &config_, platform_.get());
    ASSERT_NE(state_, nullptr);
  }
  void publishWithFbossError() {
    EXPECT_THROW(
        publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
  }
  void publishWithNoStateUpdate() {
    auto newState = publishAndApplyConfig(state_, &config_, platform_.get());
    EXPECT_EQ(newState, nullptr);
  }
  std::shared_ptr<SwitchState> state_;
  std::shared_ptr<Platform> platform_;
  cfg::SwitchConfig config_;
  static const folly::IPAddress tunnelDestination;
  static const char* egressPortName;
  static const PortID egressPort;
  static const uint8_t dscp;
  static const TunnelUdpPorts udpPorts;
};

const folly::IPAddress MirrorTest::tunnelDestination =
    folly::IPAddress("10.0.0.1");
const char* MirrorTest::egressPortName = "port5";
const PortID MirrorTest::egressPort = PortID(5);
const uint8_t MirrorTest::dscp = 46;
const TunnelUdpPorts MirrorTest::udpPorts = {6545, 6343};

TEST_F(MirrorTest, ApplyConfigScopesPortReferencedMirrors) {
  state_ = std::make_shared<SwitchState>();
  config_.switchSettings()->switchIdToSwitchInfo() = {
      {0,
       createSwitchInfo(
           cfg::SwitchType::NPU, cfg::AsicType::ASIC_TYPE_MOCK, 0, 1, 0)},
      {1,
       createSwitchInfo(
           cfg::SwitchType::NPU, cfg::AsicType::ASIC_TYPE_MOCK, 2, 99, 1)}};
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  configurePortMirror("mirror0", PortID(1));

  publishWithStateUpdate();

  auto matcher0 = HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(0)});
  auto matcher1 = HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(1)});
  auto switch0Mirrors = state_->getMirrors()->getMapNodeIf(matcher0);
  auto switch1Mirrors = state_->getMirrors()->getMapNodeIf(matcher1);

  ASSERT_NE(nullptr, switch0Mirrors);
  EXPECT_NE(nullptr, switch0Mirrors->getNodeIf("mirror0"));
  EXPECT_TRUE(
      switch1Mirrors == nullptr ||
      switch1Mirrors->getNodeIf("mirror0") == nullptr);
}

// A mirror resolved on switch 1 that later fans out to switch 0 must not hand
// switch 0 switch 1's egress port.
TEST_F(MirrorTest, FanOutDoesNotCopyPeerResolvedEgressPort) {
  state_ = std::make_shared<SwitchState>();
  config_.switchSettings()->switchIdToSwitchInfo() = {
      {0,
       createSwitchInfo(
           cfg::SwitchType::NPU, cfg::AsicType::ASIC_TYPE_MOCK, 0, 1, 0)},
      {1,
       createSwitchInfo(
           cfg::SwitchType::NPU, cfg::AsicType::ASIC_TYPE_MOCK, 2, 99, 1)}};
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  configurePortMirror("mirror0", PortID(3));
  publishWithStateUpdate();

  const auto npu0 = HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(0)});
  const auto npu1 = HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(1)});
  auto resolvedState = state_->clone();
  auto mirrors = resolvedState->getMirrors()->modify(&resolvedState);
  auto resolved = mirrors->getMapNodeIf(npu1)->getNodeIf("mirror0")->clone();
  resolved->setEgressPortDesc(PortDescriptor(PortID(3)));
  resolved->setMirrorTunnel(MirrorTunnel(
      folly::IPAddress("10.0.0.2"),
      MirrorTest::tunnelDestination,
      folly::MacAddress("02:00:00:00:00:01"),
      folly::MacAddress("02:00:00:00:00:02")));
  mirrors->updateNode(resolved, npu1);
  resolvedState->publish();
  state_ = resolvedState;

  // Unreferenced, the mirror now goes to both switches.
  config_.ports()[2].ingressMirror().reset();
  config_.ports()[2].egressMirror().reset();
  publishWithStateUpdate();

  auto npu0Mirror =
      state_->getMirrors()->getMapNodeIf(npu0)->getNodeIf("mirror0");
  ASSERT_NE(npu0Mirror, nullptr);
  EXPECT_FALSE(npu0Mirror->isResolved());
  EXPECT_FALSE(npu0Mirror->getEgressPortDesc().has_value());
  auto npu1Mirror =
      state_->getMirrors()->getMapNodeIf(npu1)->getNodeIf("mirror0");
  ASSERT_NE(npu1Mirror, nullptr);
  EXPECT_TRUE(npu1Mirror->isResolved());
  EXPECT_EQ(npu1Mirror->getEgressPortDesc(), PortDescriptor(PortID(3)));
}

TEST_F(MirrorTest, MirrorWithPort) {
  config_.mirrors()->push_back(
      utility::getSPANMirror("mirror0", MirrorTest::egressPortName));
  publishWithStateUpdate();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getID(), "mirror0");
  EXPECT_EQ(mirror->configHasEgressPort(), true);
  auto portDesc = mirror->getEgressPortDesc();
  EXPECT_EQ(portDesc.has_value(), true);
  EXPECT_EQ(portDesc.value().phyPortID(), egressPort);
  auto dscp = mirror->getDscp();
  EXPECT_EQ(dscp, cfg::switch_config_constants::DEFAULT_MIRROR_DSCP_);
  EXPECT_FALSE(mirror->getTunnelUdpPorts().has_value());
}

TEST_F(MirrorTest, MirrorWithPortId) {
  config_.mirrors()->push_back(
      utility::getSPANMirror("mirror0", MirrorTest::egressPort));
  publishWithStateUpdate();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getID(), "mirror0");
  EXPECT_EQ(mirror->configHasEgressPort(), true);
  auto portDesc = mirror->getEgressPortDesc();
  EXPECT_EQ(portDesc.has_value(), true);
  EXPECT_EQ(portDesc.value().phyPortID(), egressPort);
  auto dscp = mirror->getDscp();
  EXPECT_EQ(dscp, cfg::switch_config_constants::DEFAULT_MIRROR_DSCP_);
}

TEST_F(MirrorTest, MirrorWithPortIdAndDscp) {
  config_.mirrors()->push_back(
      utility::getGREMirrorWithPort(
          "mirror0",
          MirrorTest::egressPort,
          folly::IPAddress("0.0.0.0"),
          std::nullopt /*src addr*/,
          MirrorTest::dscp));
  publishWithStateUpdate();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getID(), "mirror0");
  EXPECT_EQ(mirror->configHasEgressPort(), true);
  auto portDesc = mirror->getEgressPortDesc();
  EXPECT_EQ(portDesc.has_value(), true);
  EXPECT_EQ(portDesc.value().phyPortID(), egressPort);
  auto dscp = mirror->getDscp();
  EXPECT_EQ(dscp, MirrorTest::dscp);
  EXPECT_FALSE(mirror->getTunnelUdpPorts().has_value());
}

TEST_F(MirrorTest, MirrorWithIp) {
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getID(), "mirror0");
  EXPECT_EQ(mirror->configHasEgressPort(), false);
  auto portDesc = mirror->getEgressPortDesc();
  EXPECT_EQ(portDesc.has_value(), false);
  auto ip = mirror->getDestinationIp();
  EXPECT_EQ(ip.has_value(), true);
  EXPECT_EQ(ip.value(), MirrorTest::tunnelDestination);
  EXPECT_EQ(mirror->isResolved(), false);
  auto dscp = mirror->getDscp();
  EXPECT_EQ(dscp, cfg::switch_config_constants::DEFAULT_MIRROR_DSCP_);
}

TEST_F(MirrorTest, MirrorWithIpAndDscp) {
  config_.mirrors()->push_back(
      utility::getGREMirror(
          "mirror0",
          MirrorTest::tunnelDestination,
          std::nullopt /* src addr*/,
          MirrorTest::dscp));

  publishWithStateUpdate();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getID(), "mirror0");
  EXPECT_EQ(mirror->configHasEgressPort(), false);
  auto portDesc = mirror->getEgressPortDesc();
  EXPECT_EQ(portDesc.has_value(), false);
  auto ip = mirror->getDestinationIp();
  EXPECT_EQ(ip.has_value(), true);
  EXPECT_EQ(ip.value(), MirrorTest::tunnelDestination);
  EXPECT_EQ(mirror->isResolved(), false);
  auto dscp = mirror->getDscp();
  EXPECT_EQ(dscp, MirrorTest::dscp);
  EXPECT_FALSE(mirror->getTunnelUdpPorts().has_value());
}

TEST_F(MirrorTest, MirrorWithPortAndIp) {
  config_.mirrors()->push_back(
      utility::getGREMirrorWithPort(
          "mirror0",
          MirrorTest::egressPortName,
          MirrorTest::tunnelDestination));

  publishWithStateUpdate();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getID(), "mirror0");
  EXPECT_EQ(mirror->configHasEgressPort(), true);
  auto portDesc = mirror->getEgressPortDesc();
  EXPECT_EQ(portDesc.has_value(), true);
  EXPECT_EQ(portDesc.value().phyPortID(), egressPort);
  auto ip = mirror->getDestinationIp();
  EXPECT_EQ(ip.has_value(), true);
  EXPECT_EQ(ip.value(), MirrorTest::tunnelDestination);
  EXPECT_EQ(mirror->isResolved(), false);
  auto dscp = mirror->getDscp();
  EXPECT_EQ(dscp, cfg::switch_config_constants::DEFAULT_MIRROR_DSCP_);
  EXPECT_FALSE(mirror->getTunnelUdpPorts().has_value());
}

TEST_F(MirrorTest, MirrorWithPortIdAndIp) {
  config_.mirrors()->push_back(
      utility::getGREMirrorWithPort(
          "mirror0", MirrorTest::egressPort, MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getID(), "mirror0");
  EXPECT_EQ(mirror->configHasEgressPort(), true);
  auto portDesc = mirror->getEgressPortDesc();
  EXPECT_EQ(portDesc.has_value(), true);
  EXPECT_EQ(portDesc.value().phyPortID(), egressPort);
  auto ip = mirror->getDestinationIp();
  EXPECT_EQ(ip.has_value(), true);
  EXPECT_EQ(ip.value(), MirrorTest::tunnelDestination);
  EXPECT_EQ(mirror->isResolved(), false);
  auto dscp = mirror->getDscp();
  EXPECT_EQ(dscp, cfg::switch_config_constants::DEFAULT_MIRROR_DSCP_);
}

TEST_F(MirrorTest, MirrorWithPortIdAndIpAndDscp) {
  config_.mirrors()->push_back(
      utility::getGREMirrorWithPort(
          "mirror0",
          MirrorTest::egressPort,
          MirrorTest::tunnelDestination,
          std::nullopt /* src addr */,
          MirrorTest::dscp));
  publishWithStateUpdate();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getID(), "mirror0");
  EXPECT_EQ(mirror->configHasEgressPort(), true);
  auto portDesc = mirror->getEgressPortDesc();
  EXPECT_EQ(portDesc.has_value(), true);
  EXPECT_EQ(portDesc.value().phyPortID(), egressPort);
  auto ip = mirror->getDestinationIp();
  EXPECT_EQ(ip.has_value(), true);
  EXPECT_EQ(ip.value(), MirrorTest::tunnelDestination);
  EXPECT_EQ(mirror->isResolved(), false);
  auto dscp = mirror->getDscp();
  EXPECT_EQ(dscp, MirrorTest::dscp);
  EXPECT_FALSE(mirror->getTunnelUdpPorts().has_value());
}

TEST_F(MirrorTest, MirrorWithPortIdAndIpAndSflowTunnel) {
  config_.mirrors()->push_back(
      utility::getSFlowMirrorWithPort(
          "mirror0",
          MirrorTest::egressPort,
          MirrorTest::udpPorts.udpSrcPort,
          MirrorTest::udpPorts.udpDstPort,
          MirrorTest::tunnelDestination,
          std::nullopt,
          MirrorTest::dscp));
  publishWithStateUpdate();

  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getID(), "mirror0");
  EXPECT_EQ(mirror->configHasEgressPort(), true);
  auto portDesc = mirror->getEgressPortDesc();
  EXPECT_EQ(portDesc.has_value(), true);
  EXPECT_EQ(portDesc.value().phyPortID(), egressPort);
  auto ip = mirror->getDestinationIp();
  EXPECT_EQ(ip.has_value(), true);
  EXPECT_EQ(ip.value(), MirrorTest::tunnelDestination);
  EXPECT_EQ(mirror->isResolved(), false);
  auto udpPorts = mirror->getTunnelUdpPorts();
  EXPECT_TRUE(udpPorts.has_value());
  EXPECT_EQ(udpPorts.value().udpSrcPort, MirrorTest::udpPorts.udpSrcPort);
  EXPECT_EQ(udpPorts.value().udpDstPort, MirrorTest::udpPorts.udpDstPort);
}

TEST_F(MirrorTest, MirrorWithNameNoPortNoIp) {
  cfg::Mirror mirror0;
  mirror0.name() = "mirror0";
  config_.mirrors()->push_back(mirror0);
  publishWithFbossError();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_EQ(mirror, nullptr);
}

TEST_F(MirrorTest, MirrorWithNameAndDscpNoPortNoIp) {
  cfg::Mirror mirror0;
  mirror0.name() = "mirror0";
  config_.mirrors()->push_back(mirror0);
  mirror0.dscp() = MirrorTest::dscp;
  publishWithFbossError();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_EQ(mirror, nullptr);
}

TEST_F(MirrorTest, MirrorWithTunnelNoPortNoIp) {
  cfg::Mirror mirror0;
  mirror0.name() = "mirror0";
  config_.mirrors()->push_back(mirror0);
  publishWithFbossError();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
}

TEST_F(MirrorTest, MirrorWithTruncation) {
  config_.mirrors()->push_back(
      utility::getGREMirrorWithPort(
          "mirror0",
          MirrorTest::egressPort,
          MirrorTest::tunnelDestination,
          std::nullopt,
          MirrorTest::dscp,
          true));
  publishWithStateUpdate();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getTruncate(), true);
}

TEST_F(MirrorTest, MirrorWithoutTruncation) {
  config_.mirrors()->push_back(
      utility::getGREMirrorWithPort(
          "mirror0",
          MirrorTest::egressPort,
          MirrorTest::tunnelDestination,
          std::nullopt,
          MirrorTest::dscp,
          false));
  publishWithStateUpdate();
  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getTruncate(), false);
}

TEST_F(MirrorTest, SflowWithoutTruncationDisallowedOnJ3) {
  auto config =
      testConfigA(cfg::SwitchType::VOQ, cfg::AsicType::ASIC_TYPE_JERICHO3);
  config.mirrors()->push_back(
      utility::getSFlowMirrorWithPort(
          "mirror0",
          MirrorTest::egressPort,
          MirrorTest::udpPorts.udpSrcPort,
          MirrorTest::udpPorts.udpDstPort,
          MirrorTest::tunnelDestination,
          std::nullopt,
          MirrorTest::dscp,
          false /*truncate*/));
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config, platform_.get()), FbossError);
}

TEST_F(MirrorTest, AclMirror) {
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  configureAcl("acl0");
  configureAclMirror("acl0", "mirror0");
  publishWithStateUpdate();
  auto entry = state_->getAcls()->getNodeIf("acl0");
  EXPECT_NE(entry, nullptr);
  auto action = entry->getAclAction();
  ASSERT_EQ(action != nullptr, true);
  auto inMirror = action->cref<switch_state_tags::ingressMirror>();
  EXPECT_TRUE(inMirror.has_value());
  EXPECT_EQ(inMirror->cref(), "mirror0");
  auto egMirror = action->cref<switch_state_tags::egressMirror>();
  EXPECT_TRUE(egMirror.has_value());
  EXPECT_EQ(egMirror->cref(), "mirror0");
}

TEST_F(MirrorTest, PortMirror) {
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  configurePortMirror("mirror0", PortID(3));
  publishWithStateUpdate();
  auto port = state_->getPorts()->getNodeIf(PortID(3));
  EXPECT_NE(port, nullptr);
  auto inMirror = port->getIngressMirror();
  EXPECT_EQ(inMirror.has_value(), true);
  EXPECT_EQ(inMirror.value(), "mirror0");
  auto egMirror = port->getEgressMirror();
  EXPECT_EQ(egMirror.has_value(), true);
  EXPECT_EQ(egMirror.value(), "mirror0");
}

TEST_F(MirrorTest, AclWrongMirror) {
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  configureAcl("acl0");
  configureAclMirror("acl0", "mirror1");
  publishWithFbossError();
}

TEST_F(MirrorTest, PortWrongMirror) {
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  configurePortMirror("mirror1", PortID(3));
  publishWithFbossError();
}

TEST_F(MirrorTest, PortManySampleMirrors) {
  config_.mirrors()->push_back(
      utility::getSFlowMirror(
          "mirror1",
          8998,
          9889,
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.0.1"),
          MirrorTest::dscp,
          true));
  config_.mirrors()->push_back(
      utility::getSFlowMirror(
          "mirror2",
          8998,
          9889,
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.0.2"),
          MirrorTest::dscp,
          true));
  publishWithStateUpdate();
  configurePortMirror("mirror1", PortID(1), true);
  configurePortMirror("mirror1", PortID(2), true);
  publishWithStateUpdate();
  configurePortMirror("mirror2", PortID(3), true);
  publishWithFbossError();
}

TEST_F(MirrorTest, SampleDestMirrorWithEgressMirror) {
  config_.mirrors()->push_back(
      utility::getSFlowMirror(
          "mirror1",
          8998,
          9889,
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.0.1"),
          MirrorTest::dscp,
          true));
  publishWithStateUpdate();
  // Ingress sampling to the mirror is accepted.
  configurePortMirror("mirror1", PortID(1), true);
  publishWithStateUpdate();
  // Adding an egress mirror on the same port is not: it would need egress
  // sampling to a mirror destination.
  config_.ports()[0].egressMirror() = "mirror1";
  publishWithFbossError();
}

TEST_F(MirrorTest, MirrorWrongPort) {
  config_.mirrors()->push_back(
      utility::getGREMirrorWithPort(
          "mirror0", "port129", MirrorTest::tunnelDestination));
  publishWithFbossError();
}

TEST_F(MirrorTest, MirrorWrongPortId) {
  config_.mirrors()->push_back(
      utility::getGREMirrorWithPort(
          "mirror0", PortID(129), MirrorTest::tunnelDestination));
  publishWithFbossError();
}

TEST_F(MirrorTest, NoStateChange) {
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  cfg::MirrorTunnel tunnel;
  cfg::GreTunnel greTunnel;
  *greTunnel.ip() = MirrorTest::tunnelDestination.str();
  tunnel.greTunnel() = greTunnel;
  config_.mirrors()[0].destination()->tunnel() = tunnel;
  publishWithNoStateUpdate();
}

TEST_F(MirrorTest, WithStateChange) {
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  *config_.mirrors()[0]
       .destination()
       ->tunnel()
       .value()
       .greTunnel()
       .value()
       .ip() = "10.0.0.2";
  publishWithStateUpdate();

  auto mirror = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->getID(), "mirror0");
  EXPECT_EQ(mirror->configHasEgressPort(), false);
  auto portDesc = mirror->getEgressPortDesc();
  EXPECT_EQ(portDesc.has_value(), false);
  auto ip = mirror->getDestinationIp();
  EXPECT_EQ(ip.has_value(), true);
  EXPECT_EQ(ip.value(), folly::IPAddress("10.0.0.2"));
  EXPECT_EQ(mirror->isResolved(), false);
}

TEST_F(MirrorTest, AddAclAndPortToMirror) {
  std::array<std::string, 2> acls{"acl0", "acl1"};
  std::array<PortID, 2> ports{PortID(3), PortID(4)};
  uint16_t l4port = 1234;
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();

  for (int i = 0; i < 2; i++) {
    configureAcl(acls[i], l4port + i);
    configureAclMirror(acls[i], "mirror0");
    configurePortMirror("mirror0", ports[i]);
    publishWithStateUpdate();
  }

  for (int i = 0; i < 2; i++) {
    auto entry = state_->getAcls()->getNodeIf(acls[i]);
    EXPECT_NE(entry, nullptr);
    auto action = entry->getAclAction();
    ASSERT_EQ(action != nullptr, true);
    auto aclInMirror = action->cref<switch_state_tags::ingressMirror>();
    EXPECT_TRUE(aclInMirror.has_value());
    EXPECT_EQ(aclInMirror->cref(), "mirror0");
    auto aclEgMirror = action->cref<switch_state_tags::egressMirror>();
    EXPECT_TRUE(aclEgMirror.has_value());
    EXPECT_EQ(aclEgMirror->cref(), "mirror0");

    auto port = state_->getPorts()->getNodeIf(ports[i]);
    EXPECT_NE(port, nullptr);
    auto portInMirror = port->getIngressMirror();
    EXPECT_EQ(portInMirror.has_value(), true);
    EXPECT_EQ(portInMirror.value(), "mirror0");
  }
}

TEST_F(MirrorTest, DeleleteAclAndPortToMirror) {
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();

  std::array<std::string, 2> acls{"acl0", "acl1"};
  std::array<PortID, 2> ports{PortID(3), PortID(4)};
  uint16_t l4port = 1234;

  for (int i = 0; i < 2; i++) {
    configureAcl(acls[i], l4port + i);
    configureAclMirror(acls[i], "mirror0");
    configurePortMirror("mirror0", ports[i]);
  }
  publishWithStateUpdate();

  auto portIndex = int(PortID(4)) - 1;
  config_.ports()[portIndex].ingressMirror().reset();
  config_.ports()[portIndex].egressMirror().reset();

  config_.acls()->pop_back();
  config_.dataPlaneTrafficPolicy()->matchToAction()->pop_back();

  publishWithStateUpdate();

  for (int i = 0; i < 2; i++) {
    auto entry = state_->getAcls()->getNodeIf(acls[i]);
    if (i) {
      EXPECT_EQ(entry, nullptr);
      auto port = state_->getPorts()->getNodeIf(ports[i]);
      EXPECT_NE(port, nullptr);
      auto portInMirror = port->getIngressMirror();
      EXPECT_EQ(portInMirror.has_value(), false);
      auto portEgMirror = port->getEgressMirror();
      EXPECT_EQ(portEgMirror.has_value(), false);
    } else {
      EXPECT_NE(entry, nullptr);
      auto action = entry->getAclAction();
      ASSERT_EQ(action != nullptr, true);
      auto aclInMirror = action->cref<switch_state_tags::ingressMirror>();
      EXPECT_TRUE(aclInMirror.has_value());
      EXPECT_EQ(aclInMirror->cref(), "mirror0");
      auto aclEgMirror = action->cref<switch_state_tags::egressMirror>();
      EXPECT_TRUE(aclEgMirror.has_value());
      EXPECT_EQ(aclEgMirror->cref(), "mirror0");

      auto port = state_->getPorts()->getNodeIf(ports[i]);
      EXPECT_NE(port, nullptr);
      auto portInMirror = port->getIngressMirror();
      EXPECT_EQ(portInMirror.has_value(), true);
      EXPECT_EQ(portInMirror.value(), "mirror0");
      auto portEgMirror = port->getEgressMirror();
      EXPECT_EQ(portEgMirror.has_value(), true);
      EXPECT_EQ(portEgMirror.value(), "mirror0");
    }
  }
}

TEST_F(MirrorTest, AclMirrorDelete) {
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  configureAcl("acl0");
  configureAclMirror("acl0", "mirror0");
  publishWithStateUpdate();

  config_.mirrors()->pop_back();
  publishWithFbossError();
}

TEST_F(MirrorTest, PortMirrorDelete) {
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror0", MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  configurePortMirror("mirror0", PortID(3));
  publishWithStateUpdate();

  config_.mirrors()->pop_back();
  publishWithFbossError();
}

TEST_F(MirrorTest, MirrorMirrorEgressPort) {
  config_.mirrors()->push_back(
      utility::getGREMirrorWithPort(
          "mirror0", MirrorTest::egressPort, MirrorTest::tunnelDestination));
  publishWithStateUpdate();
  configurePortMirror("mirror0", MirrorTest::egressPort);
  publishWithFbossError();
}

TEST_F(MirrorTest, ToAndFromThrift) {
  config_.mirrors()->push_back(
      utility::getSPANMirror("span", MirrorTest::egressPort));

  config_.mirrors()->push_back(
      utility::getGREMirror(
          "unresolved",
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.1.10")));
  config_.mirrors()->push_back(
      utility::getGREMirror(
          "resolved",
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.1.10")));
  config_.mirrors()->push_back(
      utility::getGREMirror(
          "with_dscp", MirrorTest::tunnelDestination, std::nullopt, 3));
  config_.mirrors()->push_back(
      utility::getSFlowMirror(
          "with_tunnel_type",
          udpPorts.udpSrcPort,
          udpPorts.udpDstPort,
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.1.10"),
          MirrorTest::dscp));
  publishWithStateUpdate();
  auto span = state_->getMirrors()->getNodeIf("span");
  *span;
  auto unresolved = state_->getMirrors()->getNodeIf("unresolved");
  auto with_dscp = state_->getMirrors()->getNodeIf("with_dscp");
  auto resolved = state_->getMirrors()->getNodeIf("resolved");
  resolved->setEgressPortDesc(PortDescriptor(MirrorTest::egressPort));
  resolved->setMirrorTunnel(MirrorTunnel(
      folly::IPAddress("1.1.1.1"),
      folly::IPAddress("2.2.2.2"),
      folly::MacAddress("1:1:1:1:1:1"),
      folly::MacAddress("2:2:2:2:2:2")));
  auto withTunnelType = state_->getMirrors()->getNodeIf("with_tunnel_type");
  auto reconstructedState = SwitchState::fromThrift(state_->toThrift());

  EXPECT_EQ(
      *(reconstructedState->getMirrors()->getNodeIf("span")),
      *(state_->getMirrors()->getNodeIf("span")));

  EXPECT_EQ(
      *(reconstructedState->getMirrors()->getNodeIf("unresolved")),
      *(state_->getMirrors()->getNodeIf("unresolved")));
  EXPECT_EQ(
      *(reconstructedState->getMirrors()->getNodeIf("resolved")),
      *(state_->getMirrors()->getNodeIf("resolved")));
  EXPECT_EQ(
      *(reconstructedState->getMirrors()->getNodeIf("with_dscp")),
      *(state_->getMirrors()->getNodeIf("with_dscp")));
  EXPECT_EQ(
      *(reconstructedState->getMirrors()->getNodeIf("with_tunnel_type")),
      *(state_->getMirrors()->getNodeIf("with_tunnel_type")));
}

TEST_F(MirrorTest, GreMirrorWithSrcIP) {
  config_.mirrors()->push_back(
      utility::getGREMirror(
          "mirror0",
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.0.1"),
          MirrorTest::dscp,
          true));
  publishWithStateUpdate();
  auto mirror0 = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_EQ(mirror0->getID(), "mirror0");
  EXPECT_EQ(mirror0->getDestinationIp(), MirrorTest::tunnelDestination);
  EXPECT_EQ(mirror0->getSrcIp(), folly::IPAddress("10.0.0.1"));
  EXPECT_EQ(mirror0->getDscp(), MirrorTest::dscp);
  EXPECT_EQ(mirror0->getTruncate(), true);
  EXPECT_EQ(mirror0->getTunnelUdpPorts(), std::nullopt);
}

TEST_F(MirrorTest, SflowMirrorWithSrcIP) {
  config_.mirrors()->push_back(
      utility::getSFlowMirror(
          "mirror0",
          8998,
          9889,
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.0.1"),
          MirrorTest::dscp,
          true));
  publishWithStateUpdate();
  auto mirror0 = state_->getMirrors()->getNodeIf("mirror0");
  EXPECT_EQ(mirror0->getID(), "mirror0");
  EXPECT_EQ(mirror0->getDestinationIp(), MirrorTest::tunnelDestination);
  EXPECT_EQ(mirror0->getSrcIp(), folly::IPAddress("10.0.0.1"));
  EXPECT_EQ(mirror0->getDscp(), MirrorTest::dscp);
  EXPECT_EQ(mirror0->getTruncate(), true);
  EXPECT_TRUE(mirror0->getTunnelUdpPorts().has_value());
  EXPECT_EQ(mirror0->getTunnelUdpPorts().value().udpSrcPort, 8998);
  EXPECT_EQ(mirror0->getTunnelUdpPorts().value().udpDstPort, 9889);
}

TEST_F(MirrorTest, MirrorThrifty) {
  config_.mirrors()->push_back(
      utility::getSPANMirror("mirror0", MirrorTest::egressPort));
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror1", MirrorTest::tunnelDestination));
  config_.mirrors()->push_back(
      utility::getSFlowMirror(
          "mirror2",
          8998,
          9889,
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.0.1"),
          MirrorTest::dscp,
          true));
  publishWithStateUpdate();
  auto& mirrors = state_->getMirrors();
  auto mirrorsThrift = mirrors->toThrift();
  auto newMirrors = MultiSwitchMirrorMap::fromThrift(mirrorsThrift);
  EXPECT_EQ(mirrorsThrift, newMirrors->toThrift());
}

TEST_F(MirrorTest, Modify) {
  config_.mirrors()->push_back(
      utility::getSPANMirror("span", MirrorTest::egressPort));
  publishWithStateUpdate();
  state_->publish();
  auto oldMirrors = state_->getMirrors();
  EXPECT_TRUE(oldMirrors->isPublished());
  for (auto mnitr = oldMirrors->cbegin(); mnitr != oldMirrors->cend();
       ++mnitr) {
    EXPECT_TRUE(mnitr->second->isPublished());
  }
  auto newMirrors = state_->getMirrors()->modify(&state_);
  EXPECT_NE(newMirrors, oldMirrors.get());
}

TEST_F(MirrorTest, NumMirrors) {
  config_.mirrors()->push_back(
      utility::getSPANMirror("mirror0", MirrorTest::egressPort));
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror1", MirrorTest::tunnelDestination));
  config_.mirrors()->push_back(
      utility::getSFlowMirror(
          "mirror2",
          8998,
          9889,
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.0.1"),
          MirrorTest::dscp,
          true));
  publishWithStateUpdate();
  EXPECT_EQ(state_->getMirrors()->numNodes(), 3);
  config_.mirrors()->pop_back();
  publishWithStateUpdate();
  EXPECT_EQ(state_->getMirrors()->numNodes(), 2);
}

TEST_F(MirrorTest, MirrorMapModify) {
  config_.mirrors()->push_back(
      utility::getSPANMirror("mirror0", MirrorTest::egressPort));
  config_.mirrors()->push_back(
      utility::getGREMirror("mirror1", MirrorTest::tunnelDestination));
  config_.mirrors()->push_back(
      utility::getSFlowMirror(
          "mirror2",
          8998,
          9889,
          MirrorTest::tunnelDestination,
          folly::IPAddress("10.0.0.1"),
          MirrorTest::dscp,
          true));
  publishWithStateUpdate();
  state_->publish();
  auto oldStatePtr = state_.get();
  auto mirrors = state_->getMirrors();
  auto newMirrors = mirrors->modify(&state_);
  auto newStatePtr = state_.get();
  ASSERT_TRUE(!state_->isPublished());
  EXPECT_NE(oldStatePtr, newStatePtr);
  EXPECT_NE(mirrors.get(), newMirrors);
  // do not publish state and mirror map
  auto newMirrors2 = newMirrors->modify(&state_);
  EXPECT_EQ(state_.get(), newStatePtr);
  // no change
  EXPECT_EQ(newMirrors2, newMirrors);
}

TEST_F(MirrorTest, MultiMapClone) {
  config_.mirrors()->push_back(
      utility::getSPANMirror("mirror0", MirrorTest::egressPortName));
  publishWithStateUpdate();
  auto mirrors = state_->getMirrors();
  mirrors->publish();
  EXPECT_TRUE(mirrors->isPublished());
  for (auto iter = mirrors->cbegin(); iter != mirrors->cend(); ++iter) {
    EXPECT_TRUE(iter->second->isPublished());
  }

  auto newMirrors = mirrors->clone();
  EXPECT_TRUE(!newMirrors->isPublished());
  for (auto iter = newMirrors->cbegin(); iter != newMirrors->cend(); ++iter) {
    EXPECT_TRUE(!iter->second->isPublished());
  }
}
} // namespace facebook::fboss
