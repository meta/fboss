// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/Srv6Tunnel.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;

class Srv6TunnelConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_ = createMockPlatform();
    state_ = std::make_shared<SwitchState>();
    addSwitchInfo(state_);
    config_ = testConfigA();
  }

  cfg::Srv6Tunnel makeSrv6TunnelConfig(const std::string& id = "srv6tunnel0") {
    cfg::Srv6Tunnel tunnel;
    tunnel.srv6TunnelId() = id;
    tunnel.underlayIntfID() = 1;
    tunnel.tunnelType() = TunnelType::SRV6_ENCAP;
    tunnel.srcIp() = "2401:db00:11c:8202::1";
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

TEST_F(Srv6TunnelConfigTest, CreateSrv6Tunnel) {
  config_.srv6Tunnels() = {makeSrv6TunnelConfig()};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  auto tunnel = state_->getSrv6Tunnels()->getNodeIf("srv6tunnel0");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(tunnel->getType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(tunnel->getUnderlayIntfId(), InterfaceID(1));
  EXPECT_EQ(tunnel->getSrcIP(), folly::IPAddress("2401:db00:11c:8202::1"));
  EXPECT_EQ(tunnel->getDstIP(), std::nullopt);
  EXPECT_EQ(tunnel->getTTLMode(), cfg::TunnelMode::PIPE);
  EXPECT_EQ(tunnel->getDscpMode(), cfg::TunnelMode::PIPE);
  EXPECT_EQ(tunnel->getEcnMode(), cfg::TunnelMode::PIPE);
  EXPECT_EQ(tunnel->getTunnelTermType(), cfg::TunnelTerminationType::MP2MP);
}

TEST_F(Srv6TunnelConfigTest, CreateMultipleSrv6Tunnels) {
  config_.srv6Tunnels() = {
      makeSrv6TunnelConfig("srv6tunnel0"),
      makeSrv6TunnelConfig("srv6tunnel1"),
  };
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  EXPECT_NE(state_->getSrv6Tunnels()->getNodeIf("srv6tunnel0"), nullptr);
  EXPECT_NE(state_->getSrv6Tunnels()->getNodeIf("srv6tunnel1"), nullptr);
}

TEST_F(Srv6TunnelConfigTest, UpdateSrv6Tunnel) {
  config_.srv6Tunnels() = {makeSrv6TunnelConfig()};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  config_.srv6Tunnels()->at(0).srcIp() = "2401:db00:11c:8202::2";
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  auto tunnel = state_->getSrv6Tunnels()->getNodeIf("srv6tunnel0");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(tunnel->getSrcIP(), folly::IPAddress("2401:db00:11c:8202::2"));
}

TEST_F(Srv6TunnelConfigTest, RemoveSrv6Tunnel) {
  config_.srv6Tunnels() = {
      makeSrv6TunnelConfig("srv6tunnel0"),
      makeSrv6TunnelConfig("srv6tunnel1"),
  };
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  config_.srv6Tunnels() = {makeSrv6TunnelConfig("srv6tunnel1")};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  EXPECT_EQ(state_->getSrv6Tunnels()->getNodeIf("srv6tunnel0"), nullptr);
  EXPECT_NE(state_->getSrv6Tunnels()->getNodeIf("srv6tunnel1"), nullptr);
}

TEST_F(Srv6TunnelConfigTest, NoChangeSrv6Tunnel) {
  config_.srv6Tunnels() = {makeSrv6TunnelConfig()};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  auto unchanged = publishAndApplyConfig(state_, &config_, platform_.get());
  EXPECT_EQ(unchanged, nullptr);
}

TEST_F(Srv6TunnelConfigTest, Srv6EncapRequiresSrcIp) {
  auto tunnelCfg = makeSrv6TunnelConfig();
  tunnelCfg.srcIp().reset();
  config_.srv6Tunnels() = {tunnelCfg};
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
}

TEST_F(Srv6TunnelConfigTest, Srv6EncapRequiresUnderlayInterface) {
  auto tunnelCfg = makeSrv6TunnelConfig();
  tunnelCfg.underlayIntfID().reset();
  config_.srv6Tunnels() = {tunnelCfg};
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
}

TEST_F(Srv6TunnelConfigTest, Srv6EncapRejectsDstIp) {
  auto tunnelCfg = makeSrv6TunnelConfig();
  tunnelCfg.dstIp() = "2401:db00:11c:8202::100";
  config_.srv6Tunnels() = {tunnelCfg};
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
}

TEST_F(Srv6TunnelConfigTest, CreateSrv6TunnelMinimalConfig) {
  cfg::Srv6Tunnel tunnelCfg;
  tunnelCfg.srv6TunnelId() = "minimal";
  tunnelCfg.underlayIntfID() = 1;
  tunnelCfg.tunnelType() = TunnelType::SRV6_ENCAP;
  tunnelCfg.srcIp() = "2401:db00:11c:8202::1";
  config_.srv6Tunnels() = {tunnelCfg};

  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  auto tunnel = state_->getSrv6Tunnels()->getNodeIf("minimal");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(tunnel->getType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(tunnel->getUnderlayIntfId(), InterfaceID(1));
  EXPECT_EQ(tunnel->getSrcIP(), folly::IPAddress("2401:db00:11c:8202::1"));
  EXPECT_EQ(tunnel->getDstIP(), std::nullopt);
  EXPECT_EQ(tunnel->getTTLMode(), std::nullopt);
  EXPECT_EQ(tunnel->getDscpMode(), std::nullopt);
  EXPECT_EQ(tunnel->getEcnMode(), std::nullopt);
  EXPECT_EQ(tunnel->getTunnelTermType(), std::nullopt);
}

TEST_F(Srv6TunnelConfigTest, RejectsNonSrv6EncapTunnelType) {
  cfg::Srv6Tunnel tunnelCfg;
  tunnelCfg.srv6TunnelId() = "bad_type";
  tunnelCfg.underlayIntfID() = 1;
  tunnelCfg.tunnelType() = TunnelType::IP_IN_IP_DECAP;
  config_.srv6Tunnels() = {tunnelCfg};
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
}

TEST_F(Srv6TunnelConfigTest, CreateSrv6DecapTunnel) {
  // SRv6 decap tunnel carries only decap QoS modes; no src IP required.
  cfg::Srv6Tunnel tunnelCfg;
  tunnelCfg.srv6TunnelId() = "decap0";
  tunnelCfg.tunnelType() = TunnelType::SRV6_DECAP;
  tunnelCfg.ttlMode() = cfg::TunnelMode::UNIFORM;
  tunnelCfg.dscpMode() = cfg::TunnelMode::UNIFORM;
  tunnelCfg.ecnMode() = cfg::TunnelMode::PIPE;
  config_.srv6Tunnels() = {tunnelCfg};

  state_ = publishAndApplyConfig(state_, &config_, platform_.get());
  ASSERT_NE(state_, nullptr);

  auto tunnel = state_->getSrv6Tunnels()->getNodeIf("decap0");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(tunnel->getType(), TunnelType::SRV6_DECAP);
  EXPECT_EQ(tunnel->getUnderlayIntfId(), InterfaceID(0));
  EXPECT_EQ(tunnel->getSrcIP(), std::nullopt);
  EXPECT_EQ(tunnel->getTTLMode(), cfg::TunnelMode::UNIFORM);
  EXPECT_EQ(tunnel->getDscpMode(), cfg::TunnelMode::UNIFORM);
  EXPECT_EQ(tunnel->getEcnMode(), cfg::TunnelMode::PIPE);
}

TEST_F(Srv6TunnelConfigTest, Srv6DecapRejectsSrcIp) {
  cfg::Srv6Tunnel tunnelCfg;
  tunnelCfg.srv6TunnelId() = "decap0";
  tunnelCfg.underlayIntfID() = 1;
  tunnelCfg.tunnelType() = TunnelType::SRV6_DECAP;
  tunnelCfg.srcIp() = "2401:db00:11c:8202::1";
  config_.srv6Tunnels() = {tunnelCfg};
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
}

TEST_F(Srv6TunnelConfigTest, Srv6DecapRejectsDstIp) {
  cfg::Srv6Tunnel tunnelCfg;
  tunnelCfg.srv6TunnelId() = "decap0";
  tunnelCfg.underlayIntfID() = 1;
  tunnelCfg.tunnelType() = TunnelType::SRV6_DECAP;
  tunnelCfg.dstIp() = "2401:db00:11c:8202::100";
  config_.srv6Tunnels() = {tunnelCfg};
  EXPECT_THROW(
      publishAndApplyConfig(state_, &config_, platform_.get()), FbossError);
}
