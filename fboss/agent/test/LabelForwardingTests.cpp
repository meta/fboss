// Copyright 2004-present Facebook. All Rights Reserved.

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/LabelForwardingUtils.h"
#include "fboss/agent/test/TestUtils.h"

using namespace ::testing;

namespace facebook::fboss {

class LabelForwardingTest : public ::testing::TestWithParam<bool> {
 public:
  void SetUp() override {
    // Setup a default state object
    auto config = testConfigA();
    handle = createTestHandle(&config);
    sw = handle->getSw();
    thriftHandler = std::make_unique<ThriftHandler>(sw);
    ON_CALL(*getMockHw(sw), isValidStateUpdate(_))
        .WillByDefault(testing::Return(true));
    sw->initialConfigApplied(std::chrono::steady_clock::now());
  }

  std::vector<folly::IPAddress> getDirectlyConnectedNexthops() {
    return {
        folly::IPAddress("10.0.0.101"), // intfID 1
        folly::IPAddress("2401:db00:2110:3001::0101"), // intfID 1
        folly::IPAddress("10.0.55.101"), // intfID 55
        folly::IPAddress("2401:db00:2110:3055::0101"), // intfID 55
    };
  }

  void TearDown() override {
    waitForStateUpdates(this->sw);
  }
  // Members are destroyed in reverse order: the handler must go before sw.
  std::unique_ptr<HwTestHandle> handle{nullptr};
  SwSwitch* sw{nullptr};
  std::unique_ptr<ThriftHandler> thriftHandler{nullptr};
};

TEST_P(LabelForwardingTest, addMplsRoutes) {
  FLAGS_mpls_rib = GetParam();
  std::array<ClientID, 2> clients{
      ClientID::OPENR,
      ClientID::BGPD,
  };
  std::array<std::vector<MplsRoute>, 2> routes{
      util::getTestRoutes(0, 4),
      util::getTestRoutes(4, 4),
  };

  for (auto i = 0; i < 2; i++) {
    this->thriftHandler->addMplsRoutes(
        static_cast<int>(clients[i]),
        std::make_unique<std::vector<MplsRoute>>(routes[i]));
  }

  waitForStateUpdates(this->sw);
  auto state = this->sw->getState();
  auto labelFib = state->getLabelForwardingInformationBase();

  for (auto i = 0; i < 2; i++) {
    for (const auto& route : routes[i]) {
      const auto& labelFibEntry = labelFib->getNode(*route.topLabel());
      auto labelFibEntryForClient =
          labelFibEntry->getEntryForClient(clients[i]);

      EXPECT_NE(nullptr, labelFibEntryForClient);
      EXPECT_EQ(
          util::toRouteNextHopSet(*route.nextHops()),
          getMplsClientNextHops(state, *labelFibEntryForClient));
    }
  }
}

TEST_P(LabelForwardingTest, modifyMplsRoutes) {
  FLAGS_mpls_rib = GetParam();
  std::array<ClientID, 2> clients{
      ClientID::OPENR,
      ClientID::BGPD,
  };
  std::array<std::vector<MplsRoute>, 2> routes{
      util::getTestRoutes(0, 4),
      util::getTestRoutes(4, 4),
  };

  for (auto i = 0; i < 2; i++) {
    this->thriftHandler->addMplsRoutes(
        static_cast<int>(clients[i]),
        std::make_unique<std::vector<MplsRoute>>(routes[i]));
  }

  auto verifyMplsRoutes = [&]() {
    waitForStateUpdates(this->sw);
    auto state = this->sw->getState();
    auto labelFib = state->getLabelForwardingInformationBase();

    for (auto i = 0; i < 2; i++) {
      for (const auto& route : routes[i]) {
        const auto& labelFibEntry = labelFib->getNode(*route.topLabel());
        const auto labelFibEntryForClient =
            labelFibEntry->getEntryForClient(clients[i]);

        EXPECT_NE(nullptr, labelFibEntryForClient);
        EXPECT_EQ(
            util::toRouteNextHopSet(*route.nextHops()),
            getMplsClientNextHops(state, *labelFibEntryForClient));
      }
    }
  };

  verifyMplsRoutes();

  // modify the old routes
  for (auto i = 0; i < 2; i++) {
    for (auto& route : routes[i]) {
      util::modifyMplsRoute(route);
    }
  }

  // update the old routes in RIB
  for (auto i = 0; i < 2; i++) {
    this->thriftHandler->addMplsRoutes(
        static_cast<int>(clients[i]),
        std::make_unique<std::vector<MplsRoute>>(routes[i]));
  }
  verifyMplsRoutes();
}
TEST_F(LabelForwardingTest, addMplsRecursiveRoutes) {
  FLAGS_mpls_rib = true;
  auto clientAdmin =
      this->sw->clientIdToAdminDistance(static_cast<int>(ClientID::OPENR));
  MplsRoute mplsRoute;
  mplsRoute.topLabel() = 10010;

  std::vector<folly::IPAddress> connectedIps{
      folly::IPAddress("10.0.0.101"),
      folly::IPAddress("10.0.55.101"),
  };
  std::vector<folly::IPAddress> recursiveIps{
      folly::IPAddress("11.0.0.1"),
      folly::IPAddress("11.0.1.1"),
  };

  for (int i = 0; i < 2; i++) {
    this->thriftHandler->addUnicastRoute(
        static_cast<int>(ClientID::OPENR),
        util::makeUnicastRoute(
            recursiveIps[i].str(), 24, connectedIps[i].str(), clientAdmin));
  }

  for (auto i = 0; i < 2; i++) {
    NextHopThrift nexthop;

    nexthop.mplsAction() = MplsAction();
    if (i % 2) {
      nexthop.mplsAction()->action() = MplsActionCode::PHP;
    } else {
      nexthop.mplsAction()->action() = MplsActionCode::SWAP;
      nexthop.mplsAction()->swapLabel() = 10011;
    }
    nexthop.address()->addr()->append(
        reinterpret_cast<const char*>(recursiveIps[i].bytes()),
        folly::IPAddressV4::byteCount());
    mplsRoute.nextHops()->emplace_back(std::move(nexthop));
  }

  auto routes = std::make_unique<std::vector<MplsRoute>>();
  routes->push_back(mplsRoute);
  this->thriftHandler->addMplsRoutes(
      static_cast<int>(ClientID::OPENR), std::move(routes));
  waitForStateUpdates(this->sw);
  auto state = this->sw->getState();
  auto labelFib = state->getLabelForwardingInformationBase();
  const auto& labelFibEntry = labelFib->getNode(*mplsRoute.topLabel());
  EXPECT_NE(nullptr, labelFibEntry);
  auto nexthops = getMplsNextHops(state, labelFibEntry->getForwardInfo());
  EXPECT_EQ(nexthops.size(), 2);
  auto idx = 0;
  for (const auto& nhop : nexthops) {
    EXPECT_EQ(nhop.addr(), connectedIps[idx++]);
  }
}

TEST_P(LabelForwardingTest, deleteMplsRoutes) {
  FLAGS_mpls_rib = GetParam();
  std::array<ClientID, 2> clients{
      ClientID::OPENR,
      ClientID::BGPD,
  };
  std::array<std::vector<MplsRoute>, 2> routes{
      util::getTestRoutes(0, 4),
      util::getTestRoutes(4, 4),
  };

  for (auto i = 0; i < 2; i++) {
    this->thriftHandler->addMplsRoutes(
        static_cast<int>(clients[i]),
        std::make_unique<std::vector<MplsRoute>>(routes[i]));
  }

  waitForStateUpdates(this->sw);
  ;

  std::array<std::vector<MplsLabel>, 2> routesToRemove;
  std::array<std::vector<MplsRoute>, 2> routesToRetain;

  for (auto i = 0; i < 2; i++) {
    for (const auto& route : routes[i]) {
      if (*route.topLabel() % 2) {
        routesToRemove[i].push_back(*route.topLabel());
      }
    }
  }
  for (auto i = 0; i < 2; i++) {
    this->thriftHandler->deleteMplsRoutes(
        static_cast<int>(clients[i]),
        std::make_unique<std::vector<MplsLabel>>(routesToRemove[i]));
  }

  auto state = this->sw->getState();
  auto labelFib = state->getLabelForwardingInformationBase();

  for (auto i = 0; i < 2; i++) {
    for (const auto& label : routesToRemove[i]) {
      const auto labelFibEntry = labelFib->getNodeIf(label);
      EXPECT_EQ(nullptr, labelFibEntry);
    }
    for (const auto& route : routesToRetain[i]) {
      const auto& labelFibEntry = labelFib->getNode(*route.topLabel());
      const auto labelFibEntryForClient =
          labelFibEntry->getEntryForClient(clients[i]);
      EXPECT_NE(nullptr, labelFibEntryForClient);
      EXPECT_EQ(
          util::toRouteNextHopSet(*route.nextHops()),
          getMplsClientNextHops(state, *labelFibEntryForClient));
    }
  }
}

TEST_P(LabelForwardingTest, syncMplsFib) {
  FLAGS_mpls_rib = GetParam();
  std::array<ClientID, 2> clients{
      ClientID::OPENR,
      ClientID::BGPD,
  };
  std::array<std::vector<MplsRoute>, 2> routes{
      util::getTestRoutes(0, 4),
      util::getTestRoutes(4, 4),
  };

  for (auto i = 0; i < 2; i++) {
    this->thriftHandler->addMplsRoutes(
        static_cast<int>(clients[i]),
        std::make_unique<std::vector<MplsRoute>>(routes[i]));
  }

  waitForStateUpdates(this->sw);

  auto moreOpenrRoutes = util::getTestRoutes(8, 4);
  this->thriftHandler->addMplsRoutes(
      static_cast<int>(clients[0]),
      std::make_unique<std::vector<MplsRoute>>(moreOpenrRoutes));

  waitForStateUpdates(this->sw);

  routes[0].insert(
      std::end(routes[0]),
      std::begin(moreOpenrRoutes),
      std::end(moreOpenrRoutes));

  auto state = this->sw->getState();
  auto labelFib = state->getLabelForwardingInformationBase();

  for (auto i = 0; i < 2; i++) {
    for (const auto& route : routes[i]) {
      const auto& labelFibEntry = labelFib->getNode(*route.topLabel());
      const auto labelFibEntryForClient =
          labelFibEntry->getEntryForClient(clients[i]);

      EXPECT_NE(nullptr, labelFibEntryForClient);
      EXPECT_EQ(
          util::toRouteNextHopSet(*route.nextHops()),
          getMplsClientNextHops(state, *labelFibEntryForClient));
    }
  }

  this->thriftHandler->syncMplsFib(
      static_cast<int>(clients[0]),
      std::make_unique<std::vector<MplsRoute>>(
          std::begin(routes[0]), std::begin(routes[0]) + 4));

  waitForStateUpdates(this->sw);

  state = this->sw->getState();
  labelFib = state->getLabelForwardingInformationBase();

  for (auto i = 0; i < 8; i++) {
    if (i < 4) {
      const auto& labelFibEntry = labelFib->getNode(*routes[0][i].topLabel());
      const auto labelFibEntryForClient =
          labelFibEntry->getEntryForClient(clients[0]);

      EXPECT_NE(nullptr, labelFibEntryForClient);
      EXPECT_EQ(
          util::toRouteNextHopSet(*routes[0][i].nextHops()),
          getMplsClientNextHops(state, *labelFibEntryForClient));
    } else {
      auto labelFibEntry = labelFib->getNodeIf(*routes[0][i].topLabel());
      EXPECT_EQ(nullptr, labelFibEntry);
    }
  }
}

TEST_P(LabelForwardingTest, getMplsRouteTableByClient) {
  FLAGS_mpls_rib = GetParam();
  std::array<ClientID, 2> clients{
      ClientID::OPENR,
      ClientID::BGPD,
  };
  std::array<std::vector<MplsRoute>, 2> inRoutes{
      util::getTestRoutes(0, 4),
      util::getTestRoutes(4, 4),
  };

  auto sortByLabel = [](const MplsRoute& route1, const MplsRoute& route2) {
    return *route1.topLabel() < *route2.topLabel();
  };

  for (auto i = 0; i < 2; i++) {
    std::sort(inRoutes[i].begin(), inRoutes[i].end(), sortByLabel);

    this->thriftHandler->addMplsRoutes(
        static_cast<int>(clients[i]),
        std::make_unique<std::vector<MplsRoute>>(inRoutes[i]));
  }

  std::array<std::vector<MplsRoute>, 2> outRoutes;

  for (auto i = 0; i < 2; i++) {
    this->thriftHandler->getMplsRouteTableByClient(
        outRoutes[i], static_cast<int>(clients[i]));
    std::sort(outRoutes[i].begin(), outRoutes[i].end(), sortByLabel);
  }

  for (auto i = 0; i < 2; i++) {
    auto in = inRoutes[i].begin();
    auto out = outRoutes[i].begin();
    for (; in != inRoutes[i].end() && out != outRoutes[i].end(); in++, out++) {
      EXPECT_EQ(*in->topLabel(), *out->topLabel());
      EXPECT_EQ(
          util::toRouteNextHopSet(*in->nextHops()),
          util::toRouteNextHopSet(*out->nextHops()));
      EXPECT_EQ(
          in->adminDistance().value_or({}), out->adminDistance().value_or({}));
    }
  }
}

// Aborted under mpls_rib=false before the MPLS resolvers were gated.
TEST_P(LabelForwardingTest, getMplsRouteDetails) {
  FLAGS_mpls_rib = GetParam();
  auto inRoutes = util::getTestRoutes(0, 1);
  const auto& inRoute = inRoutes.front();
  this->thriftHandler->addMplsRoutes(
      static_cast<int>(ClientID::OPENR),
      std::make_unique<std::vector<MplsRoute>>(inRoutes));

  MplsRouteDetails details;
  this->thriftHandler->getMplsRouteDetails(details, *inRoute.topLabel());

  EXPECT_EQ(*details.topLabel(), *inRoute.topLabel());
  EXPECT_EQ(
      util::toRouteNextHopSet(*details.nextHops()),
      util::toRouteNextHopSet(*inRoute.nextHops()));

  bool foundClient = false;
  for (const auto& clientNhops : *details.nextHopMulti()) {
    if (*clientNhops.clientId() != static_cast<int>(ClientID::OPENR)) {
      continue;
    }
    EXPECT_EQ(
        util::toRouteNextHopSet(*clientNhops.nextHops()),
        util::toRouteNextHopSet(*inRoute.nextHops()));
    foundClient = true;
  }
  EXPECT_TRUE(foundClient) << "OPENR entry missing from nextHopMulti";
}

TEST_P(LabelForwardingTest, unresolvedNextHops) {
  FLAGS_mpls_rib = GetParam();
  std::vector<MplsRoute> mplsRoutes;
  std::vector<MplsActionCode> actions{
      MplsActionCode::SWAP,
      MplsActionCode::PHP,
      MplsActionCode::PUSH,
  };
  std::vector<MplsLabel> labels{9001, 9002, 9003};
  mplsRoutes.resize(3);
  for (auto i = 0; i < 3; i++) {
    *mplsRoutes[i].topLabel() = labels[i];
    auto ips = this->getDirectlyConnectedNexthops();
    for (auto ip : ips) {
      NextHopThrift nexthop;
      if (ip.isV4()) {
        nexthop.address()->addr()->append(
            reinterpret_cast<const char*>(ip.bytes()),
            folly::IPAddressV4::byteCount());
      } else {
        nexthop.address()->addr()->append(
            reinterpret_cast<const char*>(ip.bytes()),
            folly::IPAddressV6::byteCount());
      }
      nexthop.mplsAction() = MplsAction();
      *nexthop.mplsAction()->action() = actions[i];
      if (actions[i] == MplsActionCode::SWAP) {
        nexthop.mplsAction()->swapLabel() = 9781;
      } else if (actions[i] == MplsActionCode::PUSH) {
        nexthop.mplsAction()->pushLabels() = MplsLabelStack();
        nexthop.mplsAction()->pushLabels()->push_back(9781);
      }
      mplsRoutes[i].nextHops()->emplace_back(nexthop);
    }
  }

  this->thriftHandler->addMplsRoutes(
      static_cast<int>(ClientID::OPENR),
      std::make_unique<std::vector<MplsRoute>>(mplsRoutes));

  waitForStateUpdates(this->sw);
  auto state = this->sw->getState();
  auto labelFib = state->getLabelForwardingInformationBase();

  for (auto i = 0; i < 3; i++) {
    const auto& labelFibEntry = labelFib->getNode(labels[i]);
    const auto labelFibEntryForClient =
        labelFibEntry->getEntryForClient(ClientID::OPENR);

    EXPECT_NE(nullptr, labelFibEntryForClient);
    auto clientNexthops = getMplsClientNextHops(state, *labelFibEntryForClient);
    EXPECT_EQ(clientNexthops.size(), 4);
    auto nexthops = getMplsNextHops(state, labelFibEntry->getForwardInfo());
    for (auto nexthop : nexthops) {
      // no unresolved next hops , all are resolved
      EXPECT_TRUE(nexthop.isResolved());
      EXPECT_TRUE(
          nexthop.intf() == InterfaceID(1) ||
          nexthop.intf() == InterfaceID(55));
      auto addr = nexthop.addr();
      if (nexthop.intf() == InterfaceID(1)) {
        if (addr.isV4()) {
          EXPECT_EQ(addr, folly::IPAddress("10.0.0.101"));
        } else {
          EXPECT_EQ(addr, folly::IPAddress("2401:db00:2110:3001::0101"));
        }
      } else if (nexthop.intf() == InterfaceID(55)) {
        if (addr.isV4()) {
          EXPECT_EQ(addr, folly::IPAddress("10.0.55.101"));
        } else {
          EXPECT_EQ(addr, folly::IPAddress("2401:db00:2110:3055::0101"));
        }
      }
      auto action = nexthop.labelForwardingAction();
      EXPECT_EQ(action->type(), actions[i]);
      if (action->type() == MplsActionCode::SWAP) {
        EXPECT_EQ(action->swapWith().value(), 9781);
      } else if (action->type() == MplsActionCode::PUSH) {
        EXPECT_EQ(action->pushStack().value(), std::vector<MplsLabel>{9781});
      }
    }
  }
}

TEST_P(LabelForwardingTest, invalidUnresolvedNextHops) {
  FLAGS_mpls_rib = GetParam();
  MplsRoute mplsRoute;
  *mplsRoute.topLabel() = 10010;
  std::vector<folly::IPAddress> ips{
      folly::IPAddress("10.0.0.101"),
      folly::IPAddress("10.0.55.101"),
      folly::IPAddress("101.101.101.1"), // invalid
  };
  for (auto i = 0; i < 3; i++) {
    NextHopThrift nexthop;

    nexthop.mplsAction() = MplsAction();
    *nexthop.mplsAction()->action() = MplsActionCode::PHP;
    nexthop.address()->addr()->append(
        reinterpret_cast<const char*>(ips[i].bytes()),
        folly::IPAddressV4::byteCount());
    mplsRoute.nextHops()->emplace_back(nexthop);
  }

  auto routes = std::make_unique<std::vector<MplsRoute>>();
  routes->push_back(mplsRoute);
  // With RIB, the route will get resolved over 2 valid nhs.
  if (FLAGS_mpls_rib) {
    this->thriftHandler->addMplsRoutes(
        static_cast<int>(ClientID::OPENR), std::move(routes));
    waitForStateUpdates(this->sw);
    auto state = this->sw->getState();
    auto labelFib = state->getLabelForwardingInformationBase();
    const auto& labelFibEntry = labelFib->getNode(*mplsRoute.topLabel());
    EXPECT_NE(nullptr, labelFibEntry);
    auto nexthops = getMplsNextHops(state, labelFibEntry->getForwardInfo());
    EXPECT_EQ(nexthops.size(), 2);
  } else {
    EXPECT_THROW(
        this->thriftHandler->addMplsRoutes(
            static_cast<int>(ClientID::OPENR), std::move(routes)),
        FbossError);
  }
}

TEST_F(LabelForwardingTest, nextHopWithInterfaceAddress) {
  MplsRoute mplsRoute0;
  *mplsRoute0.topLabel() = 10010;
  std::vector<folly::IPAddress> ips0{
      folly::IPAddress("10.0.0.101"),
      folly::IPAddress("10.0.55.101"),
      folly::IPAddress("fe80:db00::101"), // invalid, link local address
  };
  for (auto i = 0; i < 3; i++) {
    NextHopThrift nexthop;

    nexthop.mplsAction() = MplsAction();
    *nexthop.mplsAction()->action() = MplsActionCode::PHP;
    if (ips0[i].isV6()) {
      nexthop.address()->addr()->append(
          reinterpret_cast<const char*>(ips0[i].bytes()),
          folly::IPAddressV6::byteCount());
    } else {
      nexthop.address()->addr()->append(
          reinterpret_cast<const char*>(ips0[i].bytes()),
          folly::IPAddressV4::byteCount());
    }
    mplsRoute0.nextHops()->emplace_back(nexthop);
  }

  auto routes1 = std::make_unique<std::vector<MplsRoute>>();
  routes1->push_back(mplsRoute0);
  EXPECT_THROW(
      this->thriftHandler->addMplsRoutes(
          static_cast<int>(ClientID::OPENR), std::move(routes1)),
      FbossError);
}

TEST_P(LabelForwardingTest, popAndLookUp) {
  FLAGS_mpls_rib = GetParam();
  MplsRoute mplsRoute0;
  *mplsRoute0.topLabel() = 10010;
  folly::IPAddress ips0{"::"};
  NextHopThrift nexthop;

  nexthop.mplsAction() = MplsAction();
  *nexthop.mplsAction()->action() = MplsActionCode::POP_AND_LOOKUP;
  nexthop.address()->addr()->append(
      reinterpret_cast<const char*>(ips0.bytes()),
      folly::IPAddressV6::byteCount());

  mplsRoute0.nextHops()->emplace_back(nexthop);
  auto routes0 = std::make_unique<std::vector<MplsRoute>>();
  routes0->push_back(mplsRoute0);
  EXPECT_NO_THROW(this->thriftHandler->addMplsRoutes(
      static_cast<int>(ClientID::OPENR), std::move(routes0)));
}

TEST_F(LabelForwardingTest, popAndLookUpInvalid) {
  MplsRoute mplsRoute0;
  *mplsRoute0.topLabel() = 10010;

  folly::IPAddress ips0{"::"};
  NextHopThrift nexthop0;
  nexthop0.mplsAction() = MplsAction();
  *nexthop0.mplsAction()->action() = MplsActionCode::POP_AND_LOOKUP;
  nexthop0.address()->addr()->append(
      reinterpret_cast<const char*>(ips0.bytes()),
      folly::IPAddressV6::byteCount());
  mplsRoute0.nextHops()->emplace_back(nexthop0);

  folly::IPAddress ips1{"::1"};
  NextHopThrift nexthop1;
  nexthop1.mplsAction() = MplsAction();
  *nexthop1.mplsAction()->action() = MplsActionCode::POP_AND_LOOKUP;
  nexthop1.address()->addr()->append(
      reinterpret_cast<const char*>(ips1.bytes()),
      folly::IPAddressV6::byteCount());
  mplsRoute0.nextHops()->emplace_back(nexthop1);

  auto routes0 = std::make_unique<std::vector<MplsRoute>>();
  routes0->push_back(mplsRoute0);
  EXPECT_THROW(
      this->thriftHandler->addMplsRoutes(
          static_cast<int>(ClientID::OPENR), std::move(routes0)),
      FbossError);
}

INSTANTIATE_TEST_CASE_P(
    LabelForwardingTest,
    LabelForwardingTest,
    ::testing::Bool());

} // namespace facebook::fboss
