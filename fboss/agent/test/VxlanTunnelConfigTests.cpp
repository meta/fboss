// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/VxlanTunnel.h"
#include "fboss/agent/state/VxlanTunnelMap.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;

class VxlanTunnelConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_ = createMockPlatform();
    state_ = std::make_shared<SwitchState>();
    addSwitchInfo(state_);
    config_ = testConfigA();
  }

  cfg::VxLanTunnel makeVxlanTunnelConfig(
      const std::string& id = "vxlantunnel0") {
    cfg::VxLanTunnel tunnel;
    tunnel.vxlanTunnelId() = id;
    tunnel.underlayIntfID() = 1;
    tunnel.tunnelType() = TunnelType::VXLAN;
    tunnel.srcIp() = "1.1.1.1";
    tunnel.dstIp() = "2.2.2.2";
    tunnel.ttlMode() = cfg::TunnelMode::PIPE;
    tunnel.dscpMode() = cfg::TunnelMode::PIPE;
    tunnel.ecnMode() = cfg::TunnelMode::PIPE;
    tunnel.tunnelTermType() = cfg::TunnelTerminationType::MP2MP;
    return tunnel;
  }

  std::shared_ptr<SwitchState> state_;
  std::shared_ptr<Platform> platform_;
  cfg::SwitchConfig config_;
};

TEST_F(VxlanTunnelConfigTest, CreateVxlanTunnel) {
  config_.vxlanTunnels() = {makeVxlanTunnelConfig()};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  auto tunnel = state_->getVxlanTunnels()->getNodeIf("vxlantunnel0");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(tunnel->getType(), TunnelType::VXLAN);
  EXPECT_EQ(tunnel->getUnderlayIntfId(), InterfaceID(1));
  EXPECT_EQ(tunnel->getSrcIP(), folly::IPAddress("1.1.1.1"));
  EXPECT_EQ(tunnel->getDstIP(), folly::IPAddress("2.2.2.2"));
  EXPECT_EQ(tunnel->getTTLMode(), cfg::TunnelMode::PIPE);
  EXPECT_EQ(tunnel->getDscpMode(), cfg::TunnelMode::PIPE);
  EXPECT_EQ(tunnel->getEcnMode(), cfg::TunnelMode::PIPE);
  EXPECT_EQ(tunnel->getTunnelTermType(), cfg::TunnelTerminationType::MP2MP);
}

TEST_F(VxlanTunnelConfigTest, RejectsMultipleVxlanTunnels) {
  config_.vxlanTunnels() = {
      makeVxlanTunnelConfig("vxlantunnel0"),
      makeVxlanTunnelConfig("vxlantunnel1"),
  };
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
}

TEST_F(VxlanTunnelConfigTest, UpdateVxlanTunnel) {
  config_.vxlanTunnels() = {makeVxlanTunnelConfig()};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  config_.vxlanTunnels()->at(0).dstIp() = "3.3.3.3";
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  auto tunnel = state_->getVxlanTunnels()->getNodeIf("vxlantunnel0");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(tunnel->getDstIP(), folly::IPAddress("3.3.3.3"));
}

TEST_F(VxlanTunnelConfigTest, RemoveVxlanTunnelWithVnisRejected) {
  config_.vxlanTunnels() = {makeVxlanTunnelConfig()};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);
  // VNIs that SwSwitch::addVrfs() adds
  auto tunnel = state_->getVxlanTunnels()->getNode("vxlantunnel0")->clone();
  tunnel->setVrf2Vni(std::map<int64_t, int64_t>{{1, 2000}});
  tunnel->setVniToVrf(std::map<int64_t, int64_t>{{2000, 1}});
  auto state = state_->clone();
  state->getVxlanTunnels()->modify(&state)->updateNode(
      tunnel, HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)})));
  state->publish();

  config_.vxlanTunnels()->at(0).dstIp() = "3.3.3.3";
  auto newState = publishAndApplyConfig(state, &config_, platform_.get());
  ASSERT_NE(newState, nullptr);
  EXPECT_EQ(
      newState->getVxlanTunnels()->getNode("vxlantunnel0")->getVrf2Vni(),
      (std::map<int64_t, int64_t>{{1, 2000}}));

  config_.vxlanTunnels() = {};
  EXPECT_THROW(
      publishAndApplyConfig(newState, &config_, platform_.get()), FbossError);
}

TEST_F(VxlanTunnelConfigTest, RemoveVxlanTunnel) {
  config_.vxlanTunnels() = {makeVxlanTunnelConfig("vxlantunnel0")};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  config_.vxlanTunnels() = {};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  EXPECT_EQ(state_->getVxlanTunnels()->getNodeIf("vxlantunnel0"), nullptr);
}

TEST_F(VxlanTunnelConfigTest, NoChangeVxlanTunnel) {
  config_.vxlanTunnels() = {makeVxlanTunnelConfig()};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  auto unchanged = publishAndApplyConfig(state_, &config_, platform_.get());
  EXPECT_EQ(unchanged, nullptr);
}

TEST_F(VxlanTunnelConfigTest, VxlanRequiresSrcIp) {
  auto tunnelCfg = makeVxlanTunnelConfig();
  tunnelCfg.srcIp().reset();
  config_.vxlanTunnels() = {tunnelCfg};
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
}

TEST_F(VxlanTunnelConfigTest, VxlanRequiresDstIp) {
  auto tunnelCfg = makeVxlanTunnelConfig();
  tunnelCfg.dstIp().reset();
  config_.vxlanTunnels() = {tunnelCfg};
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
}

TEST_F(VxlanTunnelConfigTest, CreateVxlanTunnelMinimalConfig) {
  cfg::VxLanTunnel tunnelCfg;
  tunnelCfg.vxlanTunnelId() = "minimal";
  tunnelCfg.underlayIntfID() = 1;
  tunnelCfg.tunnelType() = TunnelType::VXLAN;
  tunnelCfg.srcIp() = "1.1.1.1";
  tunnelCfg.dstIp() = "2.2.2.2";
  config_.vxlanTunnels() = {tunnelCfg};

  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  auto tunnel = state_->getVxlanTunnels()->getNodeIf("minimal");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(tunnel->getType(), TunnelType::VXLAN);
  EXPECT_EQ(tunnel->getUnderlayIntfId(), InterfaceID(1));
  EXPECT_EQ(tunnel->getSrcIP(), folly::IPAddress("1.1.1.1"));
  EXPECT_EQ(tunnel->getDstIP(), folly::IPAddress("2.2.2.2"));
  EXPECT_EQ(tunnel->getTTLMode(), std::nullopt);
  EXPECT_EQ(tunnel->getDscpMode(), std::nullopt);
  EXPECT_EQ(tunnel->getEcnMode(), std::nullopt);
  EXPECT_EQ(tunnel->getTunnelTermType(), std::nullopt);
  // The default VRF maps to VNI 0
  const std::map<int64_t, int64_t> defaultVrfVni{{0, 0}};
  EXPECT_EQ(tunnel->getVrf2Vni(), defaultVrfVni);
  EXPECT_EQ(tunnel->getVniToVrf(), defaultVrfVni);
}

TEST_F(VxlanTunnelConfigTest, RejectsNonVxlanTunnelType) {
  cfg::VxLanTunnel tunnelCfg;
  tunnelCfg.vxlanTunnelId() = "bad_type";
  tunnelCfg.underlayIntfID() = 1;
  tunnelCfg.tunnelType() = TunnelType::SRV6_ENCAP;
  tunnelCfg.srcIp() = "1.1.1.1";
  tunnelCfg.dstIp() = "2.2.2.2";
  config_.vxlanTunnels() = {tunnelCfg};
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
}
