// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/VxlanTunnel.h"
#include "folly/IPAddressV6.h"

using namespace facebook::fboss;

namespace {
HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(10)}};
}

std::shared_ptr<VxlanTunnel> makeVxlanTunnel(
    const std::string& tunnelId = "vxlantunnel0") {
  auto tunnel = std::make_shared<VxlanTunnel>(tunnelId);
  tunnel->setType(TunnelType::VXLAN);
  tunnel->setUnderlayIntfId(InterfaceID(42));
  tunnel->setTTLMode(cfg::TunnelMode::PIPE);
  tunnel->setDscpMode(cfg::TunnelMode::PIPE);
  tunnel->setEcnMode(cfg::TunnelMode::PIPE);
  tunnel->setTunnelTermType(cfg::TunnelTerminationType::MP2MP);
  tunnel->setSrcIP(folly::IPAddressV6("2401:db00:11c:8202::1"));
  tunnel->setDstIP(folly::IPAddressV6("2401:db00:11c:8202::100"));
  tunnel->setVrf2Vni(std::map<int64_t, int64_t>{{10, 10010}, {20, 10020}});
  tunnel->setVniToVrf(std::map<int64_t, int64_t>{{10010, 10}, {10020, 20}});
  return tunnel;
}
} // namespace

TEST(VxlanTunnel, GettersReturnSetValues) {
  auto tunnel = makeVxlanTunnel();
  EXPECT_EQ(tunnel->getID(), "vxlantunnel0");
  EXPECT_EQ(tunnel->getType(), TunnelType::VXLAN);
  EXPECT_EQ(tunnel->getUnderlayIntfId(), InterfaceID(42));
  EXPECT_EQ(tunnel->getTTLMode(), cfg::TunnelMode::PIPE);
  EXPECT_EQ(tunnel->getDscpMode(), cfg::TunnelMode::PIPE);
  EXPECT_EQ(tunnel->getEcnMode(), cfg::TunnelMode::PIPE);
  EXPECT_EQ(tunnel->getTunnelTermType(), cfg::TunnelTerminationType::MP2MP);
  EXPECT_EQ(tunnel->getSrcIP(), folly::IPAddressV6("2401:db00:11c:8202::1"));
  EXPECT_EQ(tunnel->getDstIP(), folly::IPAddressV6("2401:db00:11c:8202::100"));
  EXPECT_EQ(
      tunnel->getVrf2Vni(),
      (std::map<int64_t, int64_t>{{10, 10010}, {20, 10020}}));
  EXPECT_EQ(
      tunnel->getVniToVrf(),
      (std::map<int64_t, int64_t>{{10010, 10}, {10020, 20}}));
}

TEST(VxlanTunnel, DefaultOptionalFieldsAreNullopt) {
  const std::string id{"bare"};
  auto tunnel = std::make_shared<VxlanTunnel>(id);
  EXPECT_EQ(tunnel->getID(), "bare");
  EXPECT_EQ(tunnel->getSrcIP(), std::nullopt);
  EXPECT_EQ(tunnel->getDstIP(), std::nullopt);
  EXPECT_EQ(tunnel->getTTLMode(), std::nullopt);
  EXPECT_EQ(tunnel->getDscpMode(), std::nullopt);
  EXPECT_EQ(tunnel->getEcnMode(), std::nullopt);
  EXPECT_EQ(tunnel->getTunnelTermType(), std::nullopt);
  EXPECT_EQ(tunnel->getVrf2Vni(), std::nullopt);
  EXPECT_EQ(tunnel->getVniToVrf(), std::nullopt);
}

TEST(VxlanTunnel, ResetOptionalFields) {
  auto tunnel = makeVxlanTunnel();

  tunnel->setSrcIP(std::nullopt);
  EXPECT_EQ(tunnel->getSrcIP(), std::nullopt);

  tunnel->setDstIP(std::nullopt);
  EXPECT_EQ(tunnel->getDstIP(), std::nullopt);

  tunnel->setTTLMode(std::nullopt);
  EXPECT_EQ(tunnel->getTTLMode(), std::nullopt);

  tunnel->setDscpMode(std::nullopt);
  EXPECT_EQ(tunnel->getDscpMode(), std::nullopt);

  tunnel->setEcnMode(std::nullopt);
  EXPECT_EQ(tunnel->getEcnMode(), std::nullopt);

  tunnel->setTunnelTermType(std::nullopt);
  EXPECT_EQ(tunnel->getTunnelTermType(), std::nullopt);

  tunnel->setVrf2Vni(std::nullopt);
  EXPECT_EQ(tunnel->getVrf2Vni(), std::nullopt);

  tunnel->setVniToVrf(std::nullopt);
  EXPECT_EQ(tunnel->getVniToVrf(), std::nullopt);
}

TEST(VxlanTunnel, ModifyFields) {
  auto tunnel = makeVxlanTunnel();

  tunnel->setUnderlayIntfId(InterfaceID(99));
  EXPECT_EQ(tunnel->getUnderlayIntfId(), InterfaceID(99));

  tunnel->setType(TunnelType::IP_IN_IP_DECAP);
  EXPECT_EQ(tunnel->getType(), TunnelType::IP_IN_IP_DECAP);

  tunnel->setTTLMode(cfg::TunnelMode::UNIFORM);
  EXPECT_EQ(tunnel->getTTLMode(), cfg::TunnelMode::UNIFORM);

  tunnel->setDscpMode(cfg::TunnelMode::UNIFORM);
  EXPECT_EQ(tunnel->getDscpMode(), cfg::TunnelMode::UNIFORM);

  tunnel->setEcnMode(cfg::TunnelMode::UNIFORM);
  EXPECT_EQ(tunnel->getEcnMode(), cfg::TunnelMode::UNIFORM);

  tunnel->setTunnelTermType(cfg::TunnelTerminationType::P2MP);
  EXPECT_EQ(tunnel->getTunnelTermType(), cfg::TunnelTerminationType::P2MP);

  auto newSrc = folly::IPAddressV6("2401:db00:11c:8202::2");
  tunnel->setSrcIP(newSrc);
  EXPECT_EQ(tunnel->getSrcIP(), newSrc);

  auto newDst = folly::IPAddressV6("2401:db00:11c:8202::200");
  tunnel->setDstIP(newDst);
  EXPECT_EQ(tunnel->getDstIP(), newDst);

  tunnel->setVrf2Vni(std::map<int64_t, int64_t>{{30, 10030}});
  EXPECT_EQ(tunnel->getVrf2Vni(), (std::map<int64_t, int64_t>{{30, 10030}}));
  tunnel->setVniToVrf(std::map<int64_t, int64_t>{{10030, 30}});
  EXPECT_EQ(tunnel->getVniToVrf(), (std::map<int64_t, int64_t>{{10030, 30}}));
}

TEST(VxlanTunnel, Inequality) {
  auto tunnel0 = makeVxlanTunnel("vxlantunnel0");
  auto tunnel1 = makeVxlanTunnel("vxlantunnel1");
  EXPECT_FALSE(*tunnel0 == *tunnel1);
}

TEST(VxlanTunnel, SerDeserWithOptionalFieldsUnset) {
  const std::string id{"minimal"};
  auto tunnel = std::make_shared<VxlanTunnel>(id);
  tunnel->setUnderlayIntfId(InterfaceID(1));

  auto serialized = tunnel->toThrift();
  auto tunnelBack = std::make_shared<VxlanTunnel>(serialized);
  EXPECT_TRUE(*tunnel == *tunnelBack);
  EXPECT_EQ(tunnelBack->getSrcIP(), std::nullopt);
  EXPECT_EQ(tunnelBack->getDstIP(), std::nullopt);
  EXPECT_EQ(tunnelBack->getTTLMode(), std::nullopt);
  EXPECT_EQ(tunnelBack->getVrf2Vni(), std::nullopt);
  EXPECT_EQ(tunnelBack->getVniToVrf(), std::nullopt);
}

TEST(VxlanTunnel, SerDeserVxlanTunnel) {
  auto tunnel = makeVxlanTunnel();
  auto serialized = tunnel->toThrift();
  auto tunnelBack = std::make_shared<VxlanTunnel>(serialized);
  EXPECT_TRUE(*tunnel == *tunnelBack);
}

TEST(VxlanTunnel, SerDeserSwitchState) {
  auto state = std::make_shared<SwitchState>();

  auto tunnel0 = makeVxlanTunnel("vxlantunnel0");
  auto tunnel1 = makeVxlanTunnel("vxlantunnel1");
  auto tunnels = state->getVxlanTunnels()->modify(&state);

  tunnels->addNode(tunnel0, scope());
  tunnels->addNode(tunnel1, scope());

  auto serialized = state->toThrift();
  auto stateBack = SwitchState::fromThrift(serialized);

  for (const auto& tunnelID : {"vxlantunnel0", "vxlantunnel1"}) {
    EXPECT_TRUE(
        *state->getVxlanTunnels()->getNode(tunnelID) ==
        *stateBack->getVxlanTunnels()->getNode(tunnelID));
  }
}

TEST(VxlanTunnel, AddRemove) {
  auto state = std::make_shared<SwitchState>();

  auto tunnel0 = makeVxlanTunnel("vxlantunnel0");
  auto tunnel1 = makeVxlanTunnel("vxlantunnel1");

  auto tunnels = state->getVxlanTunnels()->modify(&state);
  tunnels->addNode(tunnel0, scope());
  tunnels->addNode(tunnel1, scope());
  tunnels->removeNode("vxlantunnel0");
  EXPECT_EQ(state->getVxlanTunnels()->getNodeIf("vxlantunnel0"), nullptr);
  EXPECT_NE(state->getVxlanTunnels()->getNodeIf("vxlantunnel1"), nullptr);
}
