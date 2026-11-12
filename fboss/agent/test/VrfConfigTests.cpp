// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/StateObserver.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/ArpTable.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/NdpTable.h"
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

TEST_F(VrfConfigTest, InterfaceMoveFlushesNeighbors) {
  config_.vrfs() = {makeVrfConfig(1001)};
  state_ = apply();
  ASSERT_NE(state_, nullptr);
  InterfaceID intfId(*config_.interfaces()[0].intfID());
  state_->getInterfaces()
      ->getNode(intfId)
      ->getArpTable()
      ->modify(intfId, &state_)
      ->addEntry(
          folly::IPAddressV4("10.0.0.10"),
          folly::MacAddress("02:00:00:00:00:01"),
          PortDescriptor(PortID(1)),
          intfId);
  state_->getInterfaces()
      ->getNode(intfId)
      ->getNdpTable()
      ->modify(intfId, &state_)
      ->addEntry(
          folly::IPAddressV6("2401:db00:2110:3001::10"),
          folly::MacAddress("02:00:00:00:00:01"),
          PortDescriptor(PortID(1)),
          intfId);

  config_.interfaces()[0].routerID() = 1001;
  state_ = apply();
  ASSERT_NE(state_, nullptr);

  auto intf = state_->getInterfaces()->getNode(intfId);
  EXPECT_EQ(intf->getRouterID(), RouterID(1001));
  EXPECT_EQ(intf->getArpTable()->size(), 0);
  EXPECT_EQ(intf->getNdpTable()->size(), 0);
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

TEST(InterfaceVrfs, SetAndRemove) {
  auto config = vrfConfig();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  const auto& resolver = *sw->getScopeResolver();

  auto state =
      setInterfaceVrfs(sw->getState(), {{InterfaceID(55), kTestVrf}}, resolver);
  EXPECT_EQ(state->getInterfaceVrfs()->getVrf(InterfaceID(55)), kTestVrf);

  state = setInterfaceVrfs(state, {{InterfaceID(55), RouterID(0)}}, resolver);
  EXPECT_EQ(state->getInterfaceVrfs()->numNodes(), 0);
}

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

namespace {
// Records every state update so tests can check the order of changes.
class StateRecorder : public StateObserver {
 public:
  explicit StateRecorder(SwSwitch* sw) : sw_(sw) {
    sw_->registerStateObserver(this, "VrfStateRecorder");
  }
  ~StateRecorder() override {
    sw_->unregisterStateObserver(this);
  }
  void stateUpdated(const StateDelta& delta) override {
    states_.wlock()->push_back(delta.newState());
  }
  // Index of the first recorded state for which pred holds, or -1
  int firstIndex(
      const std::function<bool(const std::shared_ptr<SwitchState>&)>& pred)
      const {
    auto states = states_.rlock();
    for (int i = 0; i < static_cast<int>(states->size()); ++i) {
      if (pred((*states)[i])) {
        return i;
      }
    }
    return -1;
  }

 private:
  SwSwitch* sw_;
  folly::Synchronized<std::vector<std::shared_ptr<SwitchState>>> states_;
};

bool hasRoute(
    const std::shared_ptr<SwitchState>& state,
    RouterID vrf,
    const std::string& prefix) {
  auto fib = state->getFibsInfoMap()->getFibContainerIf(vrf);
  if (!fib) {
    return false;
  }
  auto cidr = folly::IPAddress::createNetwork(prefix);
  if (cidr.first.isV4()) {
    return fib->getFibV4()->exactMatch({cidr.first.asV4(), cidr.second}) !=
        nullptr;
  }
  return fib->getFibV6()->exactMatch({cidr.first.asV6(), cidr.second}) !=
      nullptr;
}

// Egress interface of a v6 route, if it resolves to a next hop
std::optional<InterfaceID> egressIntf(
    const std::shared_ptr<SwitchState>& state,
    RouterID vrf,
    const std::string& prefix) {
  auto cidr = folly::IPAddress::createNetwork(prefix);
  auto route =
      state->getFibsInfoMap()->getFibContainerIf(vrf)->getFibV6()->exactMatch(
          {cidr.first.asV6(), cidr.second});
  if (!route) {
    return std::nullopt;
  }
  auto nhops = getNextHops(state, route->getForwardInfo());
  if (nhops.empty()) {
    return std::nullopt;
  }
  return nhops.begin()->intf();
}

RouterID intf55Vrf(const std::shared_ptr<SwitchState>& state) {
  return state->getInterfaces()->getNode(InterfaceID(55))->getRouterID();
}
} // namespace

TEST(VrfDelete, RoutesRemovedBeforeVrfAndInterfaceMove) {
  auto config = vrfConfig();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  StateRecorder recorder(sw);

  sw->applyConfig("delete vrf", vrfRemovedConfig());

  auto fibGone = recorder.firstIndex([](const auto& state) {
    return !state->getFibsInfoMap()->getFibContainerIf(kTestVrf);
  });
  auto vrfGone = recorder.firstIndex(
      [](const auto& state) { return !state->getVrfs()->getNodeIf(kTestVrf); });
  auto intfMoved = recorder.firstIndex(
      [](const auto& state) { return intf55Vrf(state) == RouterID(0); });
  auto connectedInVrf0 = recorder.firstIndex([](const auto& state) {
    return hasRoute(state, RouterID(0), "10.0.55.0/24");
  });
  ASSERT_GE(fibGone, 0);
  EXPECT_LT(fibGone, vrfGone);
  EXPECT_LT(fibGone, intfMoved);
  EXPECT_LT(intfMoved, connectedInVrf0);
}

TEST(VrfDelete, RoutesRemovedBeforeInterfaceDelete) {
  auto config = vrfConfig();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  StateRecorder recorder(sw);

  // Interface 55 and VLAN 55 are deleted together with the VRF
  auto newConfig = vrfRemovedConfig();
  newConfig.interfaces()->erase(newConfig.interfaces()->begin() + 1);
  newConfig.vlans()->erase(newConfig.vlans()->begin() + 1);
  for (auto& vlanPort : *newConfig.vlanPorts()) {
    vlanPort.vlanID() = 1;
  }
  for (auto& port : *newConfig.ports()) {
    port.ingressVlan() = 1;
  }
  sw->applyConfig("delete vrf and interface", newConfig);

  auto fibGone = recorder.firstIndex([](const auto& state) {
    return !state->getFibsInfoMap()->getFibContainerIf(kTestVrf);
  });
  auto intfGone = recorder.firstIndex([](const auto& state) {
    return !state->getInterfaces()->getNodeIf(InterfaceID(55));
  });
  ASSERT_GE(fibGone, 0);
  ASSERT_GE(intfGone, 0);
  EXPECT_LT(fibGone, intfGone);
  EXPECT_EQ(sw->getState()->getVrfs()->getNodeIf(kTestVrf), nullptr);
}

TEST(VrfMove, RoutesRemovedBeforeInterfaceMove) {
  const RouterID kOtherVrf{1002};
  auto config = vrfConfig();
  config.vrfs().ensure().emplace_back().vrfId() = kOtherVrf;
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  auto updater = sw->getRouteUpdater();
  updater.addRoute(
      kTestVrf, ClientID::BGPD, makeRoute("2001:db8:100::/64", "10.0.55.10"));
  updater.program();
  ASSERT_TRUE(hasRoute(sw->getState(), kTestVrf, "2001:db8:100::/64"));
  StateRecorder recorder(sw);

  auto newConfig = config;
  newConfig.interfaces()[1].routerID() = kOtherVrf;
  sw->applyConfig("move interface", newConfig);

  auto bgpGone = recorder.firstIndex([](const auto& state) {
    return !hasRoute(state, kTestVrf, "2001:db8:100::/64");
  });
  auto connectedGone = recorder.firstIndex([](const auto& state) {
    return !hasRoute(state, kTestVrf, "10.0.55.0/24");
  });
  auto intfMoved = recorder.firstIndex(
      [&](const auto& state) { return intf55Vrf(state) == kOtherVrf; });
  auto connectedInNewVrf = recorder.firstIndex([&](const auto& state) {
    return hasRoute(state, kOtherVrf, "10.0.55.0/24");
  });
  ASSERT_GE(bgpGone, 0);
  ASSERT_GE(connectedGone, 0);
  EXPECT_LT(bgpGone, intfMoved);
  EXPECT_LT(connectedGone, intfMoved);
  EXPECT_LT(intfMoved, connectedInNewVrf);
}

TEST(VrfMove, LinkLocalNextHopRoutesRemovedBeforeInterfaceMove) {
  auto config = testConfigA();
  config.vrfs().ensure().emplace_back().vrfId() = kTestVrf;
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  auto route = makeRoute("2001:db8:200::/64", "fe80::1");
  route.nextHopAddrs()->front().ifName() = "fboss55";
  auto updater = sw->getRouteUpdater();
  updater.addRoute(RouterID(0), ClientID::BGPD, route);
  updater.program();
  ASSERT_TRUE(hasRoute(sw->getState(), RouterID(0), "2001:db8:200::/64"));
  StateRecorder recorder(sw);

  auto newConfig = config;
  newConfig.interfaces()[1].routerID() = kTestVrf;
  sw->applyConfig("move interface out of default vrf", newConfig);

  auto bgpGone = recorder.firstIndex([](const auto& state) {
    return !hasRoute(state, RouterID(0), "2001:db8:200::/64");
  });
  auto intfMoved = recorder.firstIndex(
      [](const auto& state) { return intf55Vrf(state) == kTestVrf; });
  ASSERT_GE(bgpGone, 0);
  EXPECT_LT(bgpGone, intfMoved);
  // The interface is now in another VRF, so the route stays unresolved
  EXPECT_FALSE(hasRoute(sw->getState(), RouterID(0), "2001:db8:200::/64"));
}

TEST(VrfDelete, LinkLocalNextHopRouteAddedAfterInterfaceMove) {
  auto config = vrfConfig();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  auto route = makeRoute("2001:db8:200::/64", "fe80::1");
  route.nextHopAddrs()->front().ifName() = "fboss55";
  auto updater = sw->getRouteUpdater();
  updater.addRoute(RouterID(0), ClientID::BGPD, route);
  updater.program();
  // Interface 55 is in another VRF, so the route stays unresolved
  ASSERT_FALSE(hasRoute(sw->getState(), RouterID(0), "2001:db8:200::/64"));
  StateRecorder recorder(sw);

  // Removing the last VRF moves interface 55 to the default VRF
  sw->applyConfig("delete vrf", vrfRemovedConfig());

  auto bgpAdded = recorder.firstIndex([](const auto& state) {
    return hasRoute(state, RouterID(0), "2001:db8:200::/64");
  });
  auto intfMoved = recorder.firstIndex(
      [](const auto& state) { return intf55Vrf(state) == RouterID(0); });
  ASSERT_GE(intfMoved, 0);
  ASSERT_GE(bgpAdded, 0);
  EXPECT_GE(bgpAdded, intfMoved);
}

TEST(VrfNextHop, ResolvesInNextHopVrf) {
  auto config = vrfConfig();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  // 10.0.0.10 is only reachable in the default VRF
  auto viaDefaultVrf = makeRoute("2001:db8:200::/64", "10.0.0.10");
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.10"));
  nhop.vrf() = 0;
  viaDefaultVrf.nextHopAddrs()->clear();
  viaDefaultVrf.nextHops() = {nhop};
  auto viaOwnVrf = makeRoute("2001:db8:201::/64", "10.0.0.10");

  auto updater = sw->getRouteUpdater();
  updater.addRoute(kTestVrf, ClientID::BGPD, viaDefaultVrf);
  updater.addRoute(kTestVrf, ClientID::BGPD, viaOwnVrf);
  updater.program();

  auto state = sw->getState();
  EXPECT_EQ(egressIntf(state, kTestVrf, "2001:db8:200::/64"), InterfaceID(1));
  EXPECT_EQ(egressIntf(state, kTestVrf, "2001:db8:201::/64"), std::nullopt);

  // Config apply re-resolves every VRF; the route must stay resolved
  cfg::StaticRouteNoNextHops nullRoute;
  nullRoute.routerID() = 0;
  nullRoute.prefix() = "2001:db8:300::/64";
  config.staticRoutesToNull()->push_back(nullRoute);
  sw->applyConfig("add static route", config);
  EXPECT_EQ(
      egressIntf(sw->getState(), kTestVrf, "2001:db8:200::/64"),
      InterfaceID(1));
}

TEST(VrfNextHop, ReresolvesOnDefaultVrfChange) {
  auto config = vrfConfig();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  auto vrfRoute = makeRoute("2001:db8:200::/64", "2001:db8:500::1");
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddress("2001:db8:500::1"));
  nhop.vrf() = 0;
  vrfRoute.nextHopAddrs()->clear();
  vrfRoute.nextHops() = {nhop};
  {
    auto updater = sw->getRouteUpdater();
    updater.addRoute(kTestVrf, ClientID::BGPD, vrfRoute);
    updater.program();
  }
  EXPECT_EQ(
      egressIntf(sw->getState(), kTestVrf, "2001:db8:200::/64"), std::nullopt);

  // The next hop becomes reachable in the default VRF
  {
    auto updater = sw->getRouteUpdater();
    updater.addRoute(
        RouterID(0),
        ClientID::BGPD,
        makeRoute("2001:db8:500::/64", "10.0.0.10"));
    updater.program();
  }
  EXPECT_EQ(
      egressIntf(sw->getState(), kTestVrf, "2001:db8:200::/64"),
      InterfaceID(1));

  // And unreachable again
  {
    auto updater = sw->getRouteUpdater();
    IpPrefix prefix;
    prefix.ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("2001:db8:500::"));
    prefix.prefixLength() = 64;
    updater.delRoute(RouterID(0), prefix, ClientID::BGPD);
    updater.program();
  }
  EXPECT_EQ(
      egressIntf(sw->getState(), kTestVrf, "2001:db8:200::/64"), std::nullopt);
}

namespace {
// Route via VTEP 2001:db8:500::1, resolved in the default VRF
UnicastRoute makeVxlanRoute(
    const std::string& prefix,
    const std::string& tunnelId) {
  auto route = makeRoute(prefix, "2001:db8:500::1");
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddress("2001:db8:500::1"));
  nhop.tunnelType() = TunnelType::VXLAN;
  nhop.tunnelId() = tunnelId;
  nhop.vrf() = 0;
  route.nextHopAddrs()->clear();
  route.nextHops() = {nhop};
  route.level() = 1;
  return route;
}
} // namespace

TEST(VrfNextHop, VxlanNextHopKeepsVtep) {
  auto config = vrfConfig();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();

  {
    auto updater = sw->getRouteUpdater();
    updater.addRoute(
        RouterID(0),
        ClientID::BGPD,
        makeRoute("2001:db8:500::/64", "10.0.0.10"));
    updater.addRoute(
        kTestVrf, ClientID::BGPD, makeVxlanRoute("2001:db8:200::/64", "vx0"));
    updater.program();
  }

  auto state = sw->getState();
  auto route = state->getFibsInfoMap()
                   ->getFibContainerIf(kTestVrf)
                   ->getFibV6()
                   ->exactMatch({folly::IPAddressV6("2001:db8:200::"), 64});
  ASSERT_NE(route, nullptr);
  auto nhops = getNextHops(state, route->getForwardInfo());
  ASSERT_EQ(nhops.size(), 1);
  const auto& nhop = *nhops.begin();
  EXPECT_EQ(nhop.addr(), folly::IPAddress("2001:db8:500::1"));
  EXPECT_EQ(nhop.intfID(), std::nullopt);
  EXPECT_TRUE(nhop.resolvedInNextLevel());
  EXPECT_EQ(nhop.tunnelType(), TunnelType::VXLAN);
  EXPECT_EQ(nhop.tunnelId(), "vx0");

  // VTEP unreachable in the default VRF
  {
    auto updater = sw->getRouteUpdater();
    IpPrefix prefix;
    prefix.ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("2001:db8:500::"));
    prefix.prefixLength() = 64;
    updater.delRoute(RouterID(0), prefix, ClientID::BGPD);
    updater.program();
  }
  EXPECT_EQ(
      sw->getState()
          ->getFibsInfoMap()
          ->getFibContainerIf(kTestVrf)
          ->getFibV6()
          ->exactMatch({folly::IPAddressV6("2001:db8:200::"), 64}),
      nullptr);
}

TEST(RibToSwitchStateFunction, PreFibUpdateInSameStateUpdate) {
  auto config = vrfConfig();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  StateRecorder recorder(sw);
  auto ribToSwitchStateFunc = createRibToSwitchStateFunction(
      std::nullopt, [sw](const std::shared_ptr<SwitchState>& in) {
        return setInterfaceVrfs(
            in, {{InterfaceID(55), kTestVrf}}, *sw->getScopeResolver());
      });

  sw->getRib()->update(
      sw->getScopeResolver(),
      kTestVrf,
      ClientID::BGPD,
      AdminDistance::EBGP,
      {makeRoute("2001:db8:100::/64", "10.0.55.10")},
      {},
      false,
      "add route",
      ribToSwitchStateFunc,
      sw);

  auto routeAdded = [](const auto& state) {
    return hasRoute(state, kTestVrf, "2001:db8:100::/64");
  };
  auto intfBound = [](const auto& state) {
    return state->getInterfaceVrfs()->getVrf(InterfaceID(55)) == kTestVrf;
  };
  auto bothIndex = recorder.firstIndex(
      [&](const auto& state) { return routeAdded(state) && intfBound(state); });
  ASSERT_GE(bothIndex, 0);
  EXPECT_EQ(bothIndex, recorder.firstIndex(routeAdded));
  EXPECT_EQ(bothIndex, recorder.firstIndex(intfBound));
}
