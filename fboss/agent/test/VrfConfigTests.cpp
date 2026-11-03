// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vrf.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;

class VrfConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_ = createMockPlatform();
    state_ = std::make_shared<SwitchState>();
    addSwitchInfo(state_);
    config_ = testConfigA();
  }

  static cfg::VrfConfig makeVrfConfig(
      int32_t vrfId,
      const std::optional<std::string>& name = std::nullopt) {
    cfg::VrfConfig vrf;
    vrf.vrfId() = vrfId;
    if (name) {
      vrf.name() = *name;
    }
    return vrf;
  }

  std::shared_ptr<SwitchState> apply() {
    return publishAndApplyConfig(state_, &config_, platform_.get());
  }

  std::shared_ptr<SwitchState> state_;
  std::shared_ptr<Platform> platform_;
  cfg::SwitchConfig config_;
};

TEST_F(VrfConfigTest, CreateVrfs) {
  config_.vrfs() = {makeVrfConfig(1001, "red"), makeVrfConfig(1002)};
  state_ = apply();
  ASSERT_NE(state_, nullptr);

  EXPECT_EQ(state_->getVrfs()->getNode(RouterID(1001))->getName(), "red");
  EXPECT_EQ(
      state_->getVrfs()->getNode(RouterID(1002))->getName(), std::nullopt);
}

TEST_F(VrfConfigTest, RenameVrf) {
  config_.vrfs() = {makeVrfConfig(1001, "red")};
  state_ = apply();
  ASSERT_NE(state_, nullptr);

  config_.vrfs() = {makeVrfConfig(1001, "blue")};
  state_ = apply();
  ASSERT_NE(state_, nullptr);

  EXPECT_EQ(state_->getVrfs()->getNode(RouterID(1001))->getName(), "blue");
}

TEST_F(VrfConfigTest, RemoveVrf) {
  config_.vrfs() = {makeVrfConfig(1001), makeVrfConfig(1002)};
  state_ = apply();
  ASSERT_NE(state_, nullptr);

  config_.vrfs() = {makeVrfConfig(1002)};
  state_ = apply();
  ASSERT_NE(state_, nullptr);

  EXPECT_EQ(state_->getVrfs()->getNodeIf(RouterID(1001)), nullptr);
  EXPECT_NE(state_->getVrfs()->getNodeIf(RouterID(1002)), nullptr);
}

TEST_F(VrfConfigTest, NoChange) {
  config_.vrfs() = {makeVrfConfig(1001, "red")};
  state_ = apply();
  ASSERT_NE(state_, nullptr);

  EXPECT_EQ(apply(), nullptr);
}

TEST_F(VrfConfigTest, DuplicateVrfIdRejected) {
  config_.vrfs() = {makeVrfConfig(1001), makeVrfConfig(1001)};
  EXPECT_THROW(apply(), FbossError);

  config_.vrfs() = {makeVrfConfig(1001)};
  state_ = apply();
  ASSERT_NE(state_, nullptr);
  config_.vrfs() = {makeVrfConfig(1001), makeVrfConfig(1001)};
  EXPECT_THROW(apply(), FbossError);
}

TEST_F(VrfConfigTest, DefaultVrfIdRejected) {
  config_.vrfs() = {makeVrfConfig(0)};
  EXPECT_THROW(apply(), FbossError);
}

TEST_F(VrfConfigTest, NegativeVrfIdRejected) {
  config_.vrfs() = {makeVrfConfig(-1)};
  EXPECT_THROW(apply(), FbossError);
}

TEST(VrfResource, MaxVrfs) {
  // Mock ASIC supports 8 VRFs, including the default VRF
  auto config = testConfigA();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  for (int32_t id = 1; id <= 7; ++id) {
    config.vrfs().ensure().emplace_back().vrfId() = id;
  }
  sw->applyConfig("7 vrfs", config);
  EXPECT_EQ(sw->getState()->getVrfs()->numNodes(), 7);

  config.vrfs()->emplace_back().vrfId() = 8;
  EXPECT_THROW(sw->applyConfig("8 vrfs", config), FbossError);
  EXPECT_EQ(sw->getState()->getVrfs()->numNodes(), 7);
}

TEST_F(VrfConfigTest, InterfaceInVrf) {
  config_.vrfs() = {makeVrfConfig(1001)};
  config_.interfaces()[0].routerID() = 1001;
  state_ = apply();
  ASSERT_NE(state_, nullptr);

  EXPECT_EQ(
      state_->getInterfaces()
          ->getNode(InterfaceID(*config_.interfaces()[0].intfID()))
          ->getRouterID(),
      RouterID(1001));
}

TEST_F(VrfConfigTest, InterfaceInUnconfiguredVrfRejected) {
  config_.interfaces()[0].routerID() = 1001;
  EXPECT_THROW(apply(), FbossError);
}

TEST_F(VrfConfigTest, RibTablePerVrf) {
  RoutingInformationBase rib;
  config_.vrfs() = {makeVrfConfig(1001)};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get(), &rib);
  ASSERT_NE(state_, nullptr);
  auto vrfList = rib.getVrfList();
  EXPECT_NE(
      std::find(vrfList.begin(), vrfList.end(), RouterID(1001)), vrfList.end());

  config_.vrfs() = {};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get(), &rib);
  ASSERT_NE(state_, nullptr);
  vrfList = rib.getVrfList();
  EXPECT_EQ(
      std::find(vrfList.begin(), vrfList.end(), RouterID(1001)), vrfList.end());
}

namespace {
const RouterID kTestVrf{1001};

// testConfigA with interface 55 in kTestVrf
cfg::SwitchConfig vrfConfig() {
  auto config = testConfigA();
  config.vrfs().ensure().emplace_back().vrfId() = kTestVrf;
  config.interfaces()[1].routerID() = kTestVrf;
  return config;
}

cfg::SwitchConfig vrfRemovedConfig() {
  auto config = testConfigA();
  return config;
}

UnicastRoute makeRoute(const std::string& prefix, const std::string& nhop) {
  auto cidr = folly::IPAddress::createNetwork(prefix);
  UnicastRoute route;
  route.dest()->ip() = facebook::network::toBinaryAddress(cidr.first);
  route.dest()->prefixLength() = cidr.second;
  route.nextHopAddrs()->push_back(
      facebook::network::toBinaryAddress(folly::IPAddress(nhop)));
  return route;
}
} // namespace

TEST(VrfDelete, RemovesVrfAndFibWithInterfaceMove) {
  auto config = vrfConfig();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  ASSERT_NE(
      sw->getState()->getFibsInfoMap()->getFibContainerIf(kTestVrf), nullptr);

  sw->applyConfig("delete vrf", vrfRemovedConfig());

  auto state = sw->getState();
  EXPECT_EQ(state->getVrfs()->getNodeIf(kTestVrf), nullptr);

  auto ribVrfs = sw->getRib()->getVrfList();
  EXPECT_EQ(std::find(ribVrfs.begin(), ribVrfs.end(), kTestVrf), ribVrfs.end());

  EXPECT_EQ(state->getFibsInfoMap()->getFibContainerIf(kTestVrf), nullptr);

  for (const auto& [_, intfMap] : std::as_const(*state->getInterfaces())) {
    for (const auto& [id, intf] : std::as_const(*intfMap)) {
      EXPECT_NE(intf->getRouterID(), kTestVrf) << "interface " << id;
    }
  }
  auto vrf0Fib = state->getFibsInfoMap()->getFibContainer(RouterID(0));
  EXPECT_NE(
      vrf0Fib->getFibV4()->exactMatch({folly::IPAddressV4("10.0.55.0"), 24}),
      nullptr);
}

TEST(VrfDelete, RemovesNonConfigRoutes) {
  auto config = vrfConfig();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  auto updater = sw->getRouteUpdater();
  updater.addRoute(
      kTestVrf, ClientID::BGPD, makeRoute("2001:db8:100::/64", "10.0.55.10"));
  updater.program();
  auto fib = sw->getState()->getFibsInfoMap()->getFibContainerIf(kTestVrf);
  ASSERT_NE(fib, nullptr);
  ASSERT_NE(
      fib->getFibV6()->exactMatch({folly::IPAddressV6("2001:db8:100::"), 64}),
      nullptr);

  sw->applyConfig("delete vrf", vrfRemovedConfig());

  auto state = sw->getState();
  EXPECT_EQ(state->getFibsInfoMap()->getFibContainerIf(kTestVrf), nullptr);
  const auto bgpNhop =
      facebook::network::toBinaryAddress(folly::IPAddress("10.0.55.10"));
  for (const auto& [_, fibInfo] : std::as_const(*state->getFibsInfoMap())) {
    for (const auto& [id, nhop] : fibInfo->getIdToNextHopMap()->toThrift()) {
      EXPECT_NE(*nhop.address(), bgpNhop) << "next hop id " << id;
    }
  }
}
