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
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vrf.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;

namespace {
VrfEntry makeVrfEntry(
    RouterID vrf,
    const std::optional<std::string>& name = std::nullopt) {
  VrfEntry entry;
  entry.vrfId() = vrf;
  if (name) {
    entry.name() = *name;
  }
  return entry;
}
} // namespace

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

TEST_F(VrfConfigTest, VrfNotInConfigKept) {
  config_.vrfs() = {makeVrfConfig(1001), makeVrfConfig(1002)};
  state_ = apply();
  ASSERT_NE(state_, nullptr);

  config_.vrfs() = {makeVrfConfig(1002)};
  EXPECT_EQ(apply(), nullptr);
  EXPECT_NE(state_->getVrfs()->getNodeIf(RouterID(1001)), nullptr);
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
  std::vector<VrfEntry> vrfs;
  for (int32_t id = 1; id <= 7; ++id) {
    vrfs.push_back(makeVrfEntry(RouterID(id)));
  }
  sw->addVrfs(vrfs);
  EXPECT_EQ(sw->getState()->getVrfs()->numNodes(), 7);

  EXPECT_THROW(sw->addVrfs({makeVrfEntry(RouterID(8))}), FbossError);
  EXPECT_EQ(sw->getState()->getVrfs()->numNodes(), 7);
}

TEST_F(VrfConfigTest, InterfaceVrfFromConfigRejected) {
  config_.vrfs() = {makeVrfConfig(1001)};
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
}

TEST_F(VrfConfigTest, RibTableOfVrfNotInConfigKept) {
  RoutingInformationBase rib;
  config_.vrfs() = {makeVrfConfig(1001)};
  state_ = publishAndApplyConfig(state_, &config_, platform_.get(), &rib);
  ASSERT_NE(state_, nullptr);

  config_.vrfs() = {};
  publishAndApplyConfig(state_, &config_, platform_.get(), &rib);
  auto vrfList = rib.getVrfList();
  EXPECT_NE(
      std::find(vrfList.begin(), vrfList.end(), RouterID(1001)), vrfList.end());
}

namespace {
const RouterID kTestVrf{1001};

// Test handle with kTestVrf added; all interfaces in VRF 0
std::unique_ptr<HwTestHandle> createVrfTestHandle(cfg::SwitchConfig* config) {
  auto handle = createTestHandle(config);
  handle->getSw()->addVrfs({makeVrfEntry(kTestVrf)});
  return handle;
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
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  const auto& resolver = *sw->getScopeResolver();

  auto state =
      setInterfaceVrfs(sw->getState(), {{InterfaceID(55), kTestVrf}}, resolver);
  EXPECT_EQ(state->getInterfaceVrfs()->getVrf(InterfaceID(55)), kTestVrf);

  state = setInterfaceVrfs(state, {{InterfaceID(55), RouterID(0)}}, resolver);
  EXPECT_EQ(state->getInterfaceVrfs()->numNodes(), 0);
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
  return state->getInterfaceVrfs()->getVrf(InterfaceID(55));
}
} // namespace

TEST(VrfNextHop, ResolvesInNextHopVrf) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});
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
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});
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
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});

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
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});
  StateRecorder recorder(sw);
  auto ribToSwitchStateFunc = createRibToSwitchStateFunction(
      std::nullopt, [sw](const std::shared_ptr<SwitchState>& in) {
        return setInterfaceVrfs(
            in, {{InterfaceID(1), kTestVrf}}, *sw->getScopeResolver());
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
    return state->getInterfaceVrfs()->getVrf(InterfaceID(1)) == kTestVrf;
  };
  auto bothIndex = recorder.firstIndex(
      [&](const auto& state) { return routeAdded(state) && intfBound(state); });
  ASSERT_GE(bothIndex, 0);
  EXPECT_EQ(bothIndex, recorder.firstIndex(routeAdded));
  EXPECT_EQ(bothIndex, recorder.firstIndex(intfBound));
}

TEST(UpdateInterfaceVrfs, RoutesFollowInterface) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  {
    auto updater = sw->getRouteUpdater();
    updater.addRoute(
        RouterID(0),
        ClientID::BGPD,
        makeRoute("2001:db8:100::/64", "10.0.55.10"));
    updater.addRoute(
        kTestVrf, ClientID::BGPD, makeRoute("2001:db8:200::/64", "10.0.55.10"));
    updater.program();
  }
  ASSERT_EQ(
      egressIntf(sw->getState(), RouterID(0), "2001:db8:100::/64"),
      InterfaceID(55));
  StateRecorder recorder(sw);

  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});

  auto state = sw->getState();
  EXPECT_EQ(intf55Vrf(state), kTestVrf);
  EXPECT_FALSE(hasRoute(state, RouterID(0), "10.0.55.0/24"));
  // VRF 0 no longer reaches the next hop through interface 55
  EXPECT_EQ(egressIntf(state, RouterID(0), "2001:db8:100::/64"), std::nullopt);
  EXPECT_TRUE(hasRoute(state, kTestVrf, "10.0.55.0/24"));
  EXPECT_EQ(egressIntf(state, kTestVrf, "2001:db8:200::/64"), InterfaceID(55));
  // The interface moves with its routes in one state update
  auto moved = [](const auto& state) { return intf55Vrf(state) == kTestVrf; };
  auto routed = [](const auto& state) {
    return hasRoute(state, kTestVrf, "2001:db8:200::/64");
  };
  EXPECT_EQ(recorder.firstIndex(moved), recorder.firstIndex(routed));
}

TEST(UpdateInterfaceVrfs, LinkLocalNextHopRouteFollowsInterface) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  auto route = makeRoute("2001:db8:200::/64", "fe80::1");
  route.nextHopAddrs()->front().ifName() = "fboss55";
  auto updater = sw->getRouteUpdater();
  updater.addRoute(RouterID(0), ClientID::BGPD, route);
  updater.program();
  ASSERT_TRUE(hasRoute(sw->getState(), RouterID(0), "2001:db8:200::/64"));
  StateRecorder recorder(sw);

  // Interface 55 leaves the default VRF, so the route goes unresolved in the
  // same state update
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});
  auto bgpGone = recorder.firstIndex([](const auto& state) {
    return !hasRoute(state, RouterID(0), "2001:db8:200::/64");
  });
  auto intfMoved = recorder.firstIndex(
      [](const auto& state) { return intf55Vrf(state) == kTestVrf; });
  ASSERT_GE(bgpGone, 0);
  EXPECT_EQ(bgpGone, intfMoved);

  // Moving it back resolves the route again
  sw->updateInterfaceVrfs({{InterfaceID(55), RouterID(0)}});
  EXPECT_TRUE(hasRoute(sw->getState(), RouterID(0), "2001:db8:200::/64"));
}

TEST(UpdateInterfaceVrfs, AddressMapFollowsInterfaceVrfMap) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  const auto& addrToIntf = sw->getAddrToLocalIntfMap();
  const std::pair<RouterID, folly::IPAddress> vrf0Addr{
      RouterID(0), folly::IPAddress("10.0.55.1")};
  const std::pair<RouterID, folly::IPAddress> testVrfAddr{
      kTestVrf, folly::IPAddress("10.0.55.1")};
  ASSERT_NE(addrToIntf.find(vrf0Addr), addrToIntf.end());

  // Only the interface VRF map changes
  sw->updateStateBlocking(
      "bind interface", [sw](const std::shared_ptr<SwitchState>& in) {
        return setInterfaceVrfs(
            in, {{InterfaceID(55), kTestVrf}}, *sw->getScopeResolver());
      });

  EXPECT_EQ(addrToIntf.find(vrf0Addr), addrToIntf.end());
  auto it = addrToIntf.find(testVrfAddr);
  ASSERT_NE(it, addrToIntf.end());
  EXPECT_EQ(it->second, InterfaceID(55));
}

TEST(UpdateInterfaceVrfs, FlushesNeighbors) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateStateBlocking(
      "add neighbor", [](const std::shared_ptr<SwitchState>& in) {
        auto state = in->clone();
        auto intf = state->getInterfaces()->getNode(InterfaceID(55));
        intf->getArpTable()
            ->modify(InterfaceID(55), &state)
            ->addEntry(
                folly::IPAddressV4("10.0.55.10"),
                folly::MacAddress("02:00:00:00:00:01"),
                PortDescriptor(PortID(11)),
                InterfaceID(55));
        return state;
      });
  ASSERT_EQ(
      sw->getState()
          ->getInterfaces()
          ->getNode(InterfaceID(55))
          ->getArpTable()
          ->size(),
      1);

  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});

  EXPECT_EQ(
      sw->getState()
          ->getInterfaces()
          ->getNode(InterfaceID(55))
          ->getArpTable()
          ->size(),
      0);
}

TEST(UpdateInterfaceVrfs, MoveBackToDefaultVrf) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});
  sw->updateInterfaceVrfs({{InterfaceID(55), RouterID(0)}});

  auto state = sw->getState();
  EXPECT_EQ(intf55Vrf(state), RouterID(0));
  EXPECT_EQ(state->getInterfaceVrfs()->numNodes(), 0);
  EXPECT_TRUE(hasRoute(state, RouterID(0), "10.0.55.0/24"));
  EXPECT_FALSE(hasRoute(state, kTestVrf, "10.0.55.0/24"));
}

TEST(UpdateInterfaceVrfs, Rejected) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  EXPECT_THROW(
      sw->updateInterfaceVrfs({{InterfaceID(1234), kTestVrf}}), FbossError);
  EXPECT_THROW(
      sw->updateInterfaceVrfs({{InterfaceID(55), RouterID(2002)}}), FbossError);
  EXPECT_EQ(intf55Vrf(sw->getState()), RouterID(0));
}

TEST(UpdateInterfaceVrfs, ConfigApplyKeepsBinding) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});
  // A neighbor learned in the new VRF
  sw->updateStateBlocking(
      "add neighbor", [](const std::shared_ptr<SwitchState>& in) {
        auto state = in->clone();
        auto intf = state->getInterfaces()->getNode(InterfaceID(55));
        intf->getArpTable()
            ->modify(InterfaceID(55), &state)
            ->addEntry(
                folly::IPAddressV4("10.0.55.10"),
                folly::MacAddress("02:00:00:00:00:01"),
                PortDescriptor(PortID(11)),
                InterfaceID(55));
        return state;
      });

  cfg::StaticRouteNoNextHops nullRoute;
  nullRoute.routerID() = 0;
  nullRoute.prefix() = "2001:db8:300::/64";
  config.staticRoutesToNull()->push_back(nullRoute);
  sw->applyConfig("add static route", config);

  auto state = sw->getState();
  EXPECT_EQ(intf55Vrf(state), kTestVrf);
  EXPECT_TRUE(hasRoute(state, kTestVrf, "10.0.55.0/24"));
  EXPECT_FALSE(hasRoute(state, RouterID(0), "10.0.55.0/24"));
  // Config apply leaves the moved interface alone
  EXPECT_EQ(
      state->getInterfaces()->getNode(InterfaceID(55))->getArpTable()->size(),
      1);
}

TEST(UpdateInterfaceVrfs, ConfigApplyKeepsInterfaceOutOfDefaultVrf) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});
  auto route = makeRoute("2001:db8:200::/64", "fe80::1");
  route.nextHopAddrs()->front().ifName() = "fboss55";
  auto updater = sw->getRouteUpdater();
  updater.addRoute(RouterID(0), ClientID::BGPD, route);
  updater.program();
  ASSERT_FALSE(hasRoute(sw->getState(), RouterID(0), "2001:db8:200::/64"));

  cfg::StaticRouteNoNextHops nullRoute;
  nullRoute.routerID() = 0;
  nullRoute.prefix() = "2001:db8:300::/64";
  config.staticRoutesToNull()->push_back(nullRoute);
  sw->applyConfig("add static route", config);

  // Interface 55 is still not usable in the default VRF
  EXPECT_FALSE(hasRoute(sw->getState(), RouterID(0), "2001:db8:200::/64"));
  EXPECT_TRUE(hasRoute(sw->getState(), RouterID(0), "2001:db8:300::/64"));
}

TEST(UpdateInterfaceVrfs, ConfigChangesAddressesOfBoundInterface) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});

  // 10.0.55.1/24 becomes 10.0.56.1/24
  config.interfaces()[1].ipAddresses()[0] = "10.0.56.1/24";
  sw->applyConfig("change interface address", config);

  auto state = sw->getState();
  EXPECT_EQ(intf55Vrf(state), kTestVrf);
  EXPECT_TRUE(hasRoute(state, kTestVrf, "10.0.56.0/24"));
  EXPECT_FALSE(hasRoute(state, kTestVrf, "10.0.55.0/24"));
  EXPECT_FALSE(hasRoute(state, RouterID(0), "10.0.56.0/24"));
  EXPECT_FALSE(hasRoute(state, RouterID(0), "10.0.55.0/24"));
  EXPECT_TRUE(hasRoute(state, kTestVrf, "192.168.55.0/24"));
  const auto& addrToIntf = sw->getAddrToLocalIntfMap();
  auto it = addrToIntf.find({kTestVrf, folly::IPAddress("10.0.56.1")});
  ASSERT_NE(it, addrToIntf.end());
  EXPECT_EQ(it->second, InterfaceID(55));
  EXPECT_EQ(
      addrToIntf.find({kTestVrf, folly::IPAddress("10.0.55.1")}),
      addrToIntf.end());
}

TEST(UpdateInterfaceVrfs, DuplicateSubnetRejected) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});
  // Interface 55 takes interface 1's subnet, which is in VRF 0
  config.interfaces()[1].ipAddresses()[0] = "10.0.0.2/24";
  sw->applyConfig("same subnet in another VRF", config);

  EXPECT_THROW(
      sw->updateInterfaceVrfs({{InterfaceID(55), RouterID(0)}}), FbossError);
  auto state = sw->getState();
  EXPECT_EQ(intf55Vrf(state), kTestVrf);
  EXPECT_TRUE(hasRoute(state, kTestVrf, "10.0.0.0/24"));
  EXPECT_TRUE(hasRoute(state, RouterID(0), "10.0.0.0/24"));
}

TEST(UpdateInterfaceVrfs, DuplicateSubnetInOneUpdateRejected) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->addVrfs({makeVrfEntry(RouterID(1002))});
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});
  config.interfaces()[1].ipAddresses()[0] = "10.0.0.2/24";
  sw->applyConfig("same subnet in another VRF", config);

  // Interfaces 1 and 55 with the same subnet move to one VRF together
  EXPECT_THROW(
      sw->updateInterfaceVrfs(
          {{InterfaceID(1), RouterID(1002)},
           {InterfaceID(55), RouterID(1002)}}),
      FbossError);
  auto state = sw->getState();
  EXPECT_EQ(state->getInterfaceVrfs()->getVrf(InterfaceID(1)), RouterID(0));
  EXPECT_EQ(intf55Vrf(state), kTestVrf);
  EXPECT_FALSE(hasRoute(state, RouterID(1002), "10.0.0.0/24"));
}

TEST(UpdateInterfaceVrfs, RemovingBoundInterfaceRejected) {
  auto config = testConfigA();
  auto handle = createVrfTestHandle(&config);
  auto sw = handle->getSw();
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});

  // Interface 55 and VLAN 55 are deleted
  config.interfaces()->erase(config.interfaces()->begin() + 1);
  config.vlans()->erase(config.vlans()->begin() + 1);
  for (auto& vlanPort : *config.vlanPorts()) {
    vlanPort.vlanID() = 1;
  }
  for (auto& port : *config.ports()) {
    port.ingressVlan() = 1;
  }
  try {
    sw->applyConfig("delete interface", config);
    FAIL() << "Removing a bound interface must be rejected";
  } catch (const FbossError& e) {
    EXPECT_NE(std::string(e.what()).find("bound to VRF"), std::string::npos)
        << e.what();
  }
  EXPECT_NE(
      sw->getState()->getInterfaces()->getNodeIf(InterfaceID(55)), nullptr);
}

TEST(SwSwitchVrfs, AddAndDelete) {
  auto config = testConfigA();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  sw->addVrfs({makeVrfEntry(kTestVrf, "blue")});
  auto vrf = sw->getState()->getVrfs()->getNodeIf(kTestVrf);
  ASSERT_NE(vrf, nullptr);
  EXPECT_EQ(vrf->getName(), "blue");
  EXPECT_NE(
      sw->getState()->getFibsInfoMap()->getFibContainerIf(kTestVrf), nullptr);

  sw->deleteVrfs({kTestVrf});
  EXPECT_EQ(sw->getState()->getVrfs()->getNodeIf(kTestVrf), nullptr);
  EXPECT_EQ(
      sw->getState()->getFibsInfoMap()->getFibContainerIf(kTestVrf), nullptr);
}

TEST(SwSwitchVrfs, InterfaceInAddedVrf) {
  auto config = testConfigA();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  sw->addVrfs({makeVrfEntry(kTestVrf, "blue")});
  sw->updateInterfaceVrfs({{InterfaceID(55), kTestVrf}});
  EXPECT_TRUE(hasRoute(sw->getState(), kTestVrf, "10.0.55.0/24"));

  EXPECT_THROW(sw->deleteVrfs({kTestVrf}), FbossError);
  EXPECT_NE(sw->getState()->getVrfs()->getNodeIf(kTestVrf), nullptr);
}

TEST(SwSwitchVrfs, Rejected) {
  auto config = testConfigA();
  auto handle = createTestHandle(&config);
  auto sw = handle->getSw();
  sw->addVrfs({makeVrfEntry(kTestVrf, "blue")});
  EXPECT_THROW(sw->addVrfs({makeVrfEntry(kTestVrf, "red")}), FbossError);
  EXPECT_THROW(sw->deleteVrfs({RouterID(3003)}), FbossError);
  EXPECT_EQ(sw->getState()->getVrfs()->getNodeIf(kTestVrf)->getName(), "blue");
}
