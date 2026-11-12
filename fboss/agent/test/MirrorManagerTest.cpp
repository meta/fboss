/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/MirrorManager.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;

namespace facebook::fboss {

namespace {
const std::string kMirrorName = "mirror";
constexpr AdminDistance DISTANCE = AdminDistance::STATIC_ROUTE;
const PortID kMirrorEgressPort{5};

template <typename AddressT>
struct MirrorManagerTestParams {
  using AddrT = AddressT;

  AddrT mirrorDestination;
  AddrT mirrorSource;
  std::array<AddrT, 2> neighborIPs;
  std::array<MacAddress, 2> neighborMACs;
  std::array<PortID, 2> neighborPorts;
  std::array<InterfaceID, 2> interfaces;
  RoutePrefix<AddrT> longerPrefix;
  RoutePrefix<AddrT> shorterPrefix;

  MirrorManagerTestParams(
      const AddrT& mirrorDestination,
      const AddrT& mirrorSource,
      std::array<AddrT, 2>&& neighborIPs,
      std::array<MacAddress, 2>&& neighborMACs,
      std::array<PortID, 2>&& neighborPorts,
      std::array<InterfaceID, 2>&& interfaces,
      const RoutePrefix<AddrT>& longerPrefix,
      const RoutePrefix<AddrT>& shorterPrefix)
      : mirrorDestination(mirrorDestination),
        mirrorSource(mirrorSource),
        neighborIPs(std::move(neighborIPs)),
        neighborMACs(std::move(neighborMACs)),
        neighborPorts(std::move(neighborPorts)),
        interfaces(std::move(interfaces)),
        longerPrefix(longerPrefix),
        shorterPrefix(shorterPrefix) {}

  const UnresolvedNextHop nextHop(int neighborIndex) const {
    return UnresolvedNextHop(neighborIPs[neighborIndex % 2], NextHopWeight(80));
  }
};

template <typename AddressT>
MirrorManagerTestParams<AddressT> getParams() {
  if constexpr (std::is_same_v<AddressT, folly::IPAddressV4>) {
    return MirrorManagerTestParams<AddressT>(
        IPAddressV4("10.0.10.101"),
        IPAddressV4("10.0.11.101"),
        {IPAddressV4("10.0.0.111"), IPAddressV4("10.0.55.111")},
        {MacAddress("10:0:0:0:1:11"), MacAddress("10:0:0:55:1:11")},
        {PortID(6), PortID(7)},
        {InterfaceID(1), InterfaceID(55)},
        {IPAddressV4("10.0.10.100"), 31},
        {IPAddressV4("10.0.0.0"), 16});
  } else {
    return MirrorManagerTestParams<AddressT>(
        IPAddressV6("2401:db00:2110:10::1001"),
        IPAddressV6("2401:db00:2110:11::1001"),
        {IPAddressV6("2401:db00:2110:3001::0111"),
         IPAddressV6("2401:db00:2110:3055::0111")},
        {MacAddress("10:0:0:0:1:11"), MacAddress("10:0:0:55:1:11")},
        {PortID(6), PortID(7)},
        {InterfaceID(1), InterfaceID(55)},
        {IPAddressV6("2401:db00:2110:10::1000"), 127},
        {IPAddressV6("2401:db00:2110:10::0000"), 64});
  }
}
} // namespace

template <typename AddrType>
struct IpAddrT {
  using IPAddrT = AddrType;
};

using TestTypes =
    ::testing::Types<IpAddrT<folly::IPAddressV4>, IpAddrT<folly::IPAddressV6>>;

template <typename IpAddrT>
class MirrorManagerTest : public ::testing::Test {
 public:
  using Func = folly::Function<void()>;
  using StateUpdateFn = SwSwitch::StateUpdateFn;
  using AddrT = typename IpAddrT::IPAddrT;

  void SetUp() override {
    auto config = testConfigA();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
  }

  MirrorManagerTestParams<AddrT> getParamsHelper() {
    return getParams<AddrT>();
  }

  void verifyStateUpdate(Func func) {
    waitForStateUpdates(sw_);
    runInUpdateEventBaseAndWait(std::move(func));
  }

  void TearDown() override {
    schedulePendingTestStateUpdates();
  }

  void updateState(folly::StringPiece name, StateUpdateFn func) {
    sw_->updateStateBlocking(name, func);
  }

  std::shared_ptr<SwitchState> addNewMirror(
      const std::shared_ptr<SwitchState>& state,
      std::shared_ptr<Mirror> mirror) {
    auto newState = state->clone();
    auto mirrors = newState->getMirrors()->modify(&newState);
    mirrors->addNode(mirror, sw_->getScopeResolver()->scope(mirror));
    return newState;
  }
  std::shared_ptr<SwitchState> updateMirror(
      const std::shared_ptr<SwitchState>& state,
      std::shared_ptr<Mirror> mirror) {
    auto newState = state->clone();
    auto mirrors = newState->getMirrors()->modify(&newState);
    mirrors->updateNode(mirror, sw_->getScopeResolver()->scope(mirror));
    return newState;
  }

  std::shared_ptr<SwitchState> addNeighborToIntfTable(
      const std::shared_ptr<SwitchState>& state,
      InterfaceID interfaceId,
      const AddrT& ip,
      const MacAddress mac,
      PortID port) {
    auto newState = state->isPublished() ? state->clone() : state;
    Interface* intf = newState->getInterfaces()->getNodeIf(interfaceId).get();
    auto* neighborTable =
        intf->template getNeighborEntryTable<AddrT>().get()->modify(
            &intf, &newState);
    neighborTable->addEntry(ip, mac, PortDescriptor(port), interfaceId);
    return newState;
  }

  std::shared_ptr<SwitchState> addNeighbor(
      const std::shared_ptr<SwitchState>& state,
      InterfaceID interfaceId,
      const AddrT& ip,
      const MacAddress mac,
      PortID port) {
    return addNeighborToIntfTable(state, interfaceId, ip, mac, port);
  }

  std::shared_ptr<SwitchState> delNeighborFromIntfTable(
      const std::shared_ptr<SwitchState>& state,
      InterfaceID interfaceId,
      const AddrT& ip) {
    auto newState = state->isPublished() ? state->clone() : state;
    Interface* intf = newState->getInterfaces()->getNodeIf(interfaceId).get();
    auto* neighborTable =
        intf->template getNeighborEntryTable<AddrT>().get()->modify(
            &intf, &newState);
    neighborTable->removeEntry(ip);
    return newState;
  }

  std::shared_ptr<SwitchState> delNeighbor(
      const std::shared_ptr<SwitchState>& state,
      InterfaceID interfaceId,
      const AddrT& ip) {
    return delNeighborFromIntfTable(state, interfaceId, ip);
  }

  void addRoute(const RoutePrefix<AddrT>& prefix, RouteNextHopSet nexthops) {
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(
        RouterID(0),
        prefix.network(),
        prefix.mask(),
        ClientID(1000),
        RouteNextHopEntry(nexthops, DISTANCE));
    updater.program();
  }
  void delRoute(const RoutePrefix<AddrT>& prefix) {
    auto updater = sw_->getRouteUpdater();
    updater.delRoute(
        RouterID(0), prefix.network(), prefix.mask(), ClientID(1000));
    updater.program();
  }

  std::shared_ptr<SwitchState> addSpanMirror(
      const std::shared_ptr<SwitchState>& state,
      const std::string& name,
      PortID egressPort) {
    auto mirror = std::make_shared<Mirror>(
        name,
        std::make_optional<PortDescriptor>(egressPort),
        std::optional<IPAddress>());
    return addNewMirror(state, mirror);
  }

  std::shared_ptr<SwitchState> addErspanMirror(
      const std::shared_ptr<SwitchState>& state,
      const std::string& name,
      AddrT remoteIp) {
    auto mirror = std::make_shared<Mirror>(
        name,
        std::optional<PortDescriptor>(),
        std::make_optional<IPAddress>(remoteIp));
    return addNewMirror(state, mirror);
  }

  std::shared_ptr<SwitchState> addErspanMirror(
      const std::shared_ptr<SwitchState>& state,
      const std::string& name,
      AddrT remoteIp,
      PortID egressPort) {
    auto mirror = std::make_shared<Mirror>(
        name,
        std::make_optional<PortDescriptor>(PortID(egressPort)),
        std::make_optional<IPAddress>(remoteIp));
    return addNewMirror(state, mirror);
  }

  std::shared_ptr<SwitchState> addSflowMirror(
      const std::shared_ptr<SwitchState>& state,
      const std::string& name,
      AddrT remoteIp,
      uint16_t srcPort,
      uint16_t dstPort) {
    auto mirror = std::make_shared<Mirror>(
        name,
        std::optional<PortDescriptor>(),
        std::make_optional<IPAddress>(remoteIp),
        std::optional<IPAddress>(),
        std::make_optional<TunnelUdpPorts>(srcPort, dstPort));
    return addNewMirror(state, mirror);
  }

  void resolveNeighbor(
      IPAddress ipAddress,
      MacAddress macAddress,
      InterfaceID interfaceID,
      PortID portID,
      bool wait = true) {
    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      sw_->getNeighborUpdater()->receivedArpMineForIntf(
          interfaceID,
          ipAddress.asV4(),
          macAddress,
          PortDescriptor(portID),
          ArpOpCode::ARP_OP_REPLY);

    } else {
      sw_->getNeighborUpdater()->receivedNdpMineForIntf(
          interfaceID,
          ipAddress.asV6(),
          macAddress,
          PortDescriptor(portID),
          ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_SOLICITATION,
          0);
    }
    if (wait) {
      sw_->getNeighborUpdater()->waitForPendingUpdates();
      waitForBackgroundThread(sw_);
      waitForStateUpdates(sw_);
      sw_->getNeighborUpdater()->waitForPendingUpdates();
      waitForStateUpdates(sw_);
    }
  }

 protected:
  void runInUpdateEventBaseAndWait(Func func) {
    auto* evb = sw_->getUpdateEvb();
    evb->runInFbossEventBaseThreadAndWait(std::move(func));
  }

  void schedulePendingTestStateUpdates() {
    runInUpdateEventBaseAndWait([]() {});
  }

  std::unique_ptr<HwTestHandle> handle_;
  SwSwitch* sw_;
};

TYPED_TEST_SUITE(MirrorManagerTest, TestTypes);

TYPED_TEST(MirrorManagerTest, CanNotUpdateMirrors) {
  auto params = this->getParamsHelper();

  this->updateState(
      "CanNotUpdateMirrors",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addErspanMirror(
            state, kMirrorName, params.mirrorDestination);
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->isResolved());
  });
}

TYPED_TEST(MirrorManagerTest, ResolveNoMirrorWithoutRoutes) {
  auto params = this->getParamsHelper();
  this->updateState(
      "ResolveNoMirrorWithoutRoutes",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState =
            this->addErspanMirror(state, kMirrorName, params.mirrorDestination);
        return this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->isResolved());
  });
}

TYPED_TEST(MirrorManagerTest, ResolveNoMirrorWithoutArpEntry) {
  auto params = this->getParamsHelper();
  this->updateState(
      "ResolveNoMirrorWithoutArpEntry",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addErspanMirror(
            state, kMirrorName, params.mirrorDestination);
      });
  RouteNextHopSet nextHops = {params.nextHop(0), params.nextHop(1)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->isResolved());
  });
}

TYPED_TEST(MirrorManagerTest, LocalMirrorAlreadyResolved) {
  this->updateState(
      "LocalMirrorAlreadyResolved",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addSpanMirror(state, kMirrorName, kMirrorEgressPort);
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
  });
}

TYPED_TEST(MirrorManagerTest, ResolveMirrorWithoutEgressPort) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "ResolveMirrorWithoutEgressPort",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState =
            this->addErspanMirror(state, kMirrorName, params.mirrorDestination);
        return this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
      });
  RouteNextHopSet nextHops = {params.nextHop(0)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    ASSERT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[0]);
    const auto& interface =
        state->getInterfaces()->getNodeIf(params.interfaces[0]);
    const auto& addr = interface->getAddressToReach(params.neighborIPs[0]);
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ(tunnel.srcIp, addr->first);
    EXPECT_EQ(tunnel.srcMac, interface->getMac());
  });
}

TYPED_TEST(MirrorManagerTest, ResolveMirrorWithEgressPort) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "ResolveMirrorWithEgressPort",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addErspanMirror(
            state,
            kMirrorName,
            params.mirrorDestination,
            params.neighborPorts[1]);
        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[1],
            params.neighborIPs[1],
            params.neighborMACs[1],
            params.neighborPorts[1]);
        return updatedState;
      });

  RouteNextHopSet nextHops = {params.nextHop(0), params.nextHop(1)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    ASSERT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[1]);
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[1]);

    const auto& interface =
        state->getInterfaces()->getNodeIf(params.interfaces[1]);
    const auto& addr = interface->getAddressToReach(params.neighborIPs[1]);

    ASSERT_TRUE(addr.has_value());

    EXPECT_EQ(tunnel.srcIp, addr->first);
    EXPECT_EQ(tunnel.srcMac, MacAddress(interface->getMac()));
  });
}

TYPED_TEST(MirrorManagerTest, ResolveNoMirrorWithEgressPort) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "ResolveNoMirrorWithEgressPort",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addErspanMirror(
            state, kMirrorName, params.mirrorDestination, PortID(9));
        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        updatedState = this->addNeighbor(
            updatedState,
            InterfaceID(55),
            params.neighborIPs[1],
            params.neighborMACs[1],
            params.neighborPorts[1]);

        return updatedState;
      });
  RouteNextHopSet nextHops = {params.nextHop(0), params.nextHop(1)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    EXPECT_EQ(mirror->getEgressPortDesc().value().phyPortID(), PortID(9));
    EXPECT_FALSE(mirror->getMirrorTunnel().has_value());
  });
}

TYPED_TEST(MirrorManagerTest, ResolveMirrorWithDirectlyConnectedRoute) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "ResolveMirrorWithDirectlyConnectedRoute",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState =
            this->addErspanMirror(state, kMirrorName, params.neighborIPs[0]);
        return this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
      });
  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.neighborIPs[0]));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[0]);

    const auto& interface =
        state->getInterfaces()->getNodeIf(params.interfaces[0]);
    const auto& addr = interface->getAddressToReach(params.neighborIPs[0]);

    ASSERT_TRUE(addr.has_value());

    EXPECT_EQ(tunnel.srcIp, addr->first);
    EXPECT_EQ(tunnel.srcMac, interface->getMac());
  });
}

TYPED_TEST(MirrorManagerTest, ResolveNoMirrorWithDirectlyConnectedRoute) {
  const auto params = this->getParamsHelper();
  this->updateState(
      "ResolveNoMirrorWithDirectlyConnectedRoute",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addErspanMirror(state, kMirrorName, params.neighborIPs[0]);
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    EXPECT_NE(state, nullptr);
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->isResolved());
    EXPECT_FALSE(mirror->getEgressPortDesc().has_value());
    ASSERT_FALSE(mirror->getMirrorTunnel().has_value());
  });
}

TYPED_TEST(MirrorManagerTest, UpdateMirrorOnRouteDelete) {
  const auto params = this->getParamsHelper();
  this->updateState(
      "UpdateMirrorOnRouteDelete: addNode",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState =
            this->addErspanMirror(state, kMirrorName, params.mirrorDestination);
        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[1],
            params.neighborIPs[1],
            params.neighborMACs[1],
            params.neighborPorts[1]);
        return updatedState;
      });

  RouteNextHopSet nextHopsLonger = {params.nextHop(0)};
  this->addRoute(params.longerPrefix, nextHopsLonger);
  RouteNextHopSet nextHopsShorter = {params.nextHop(1)};
  this->addRoute(params.shorterPrefix, nextHopsShorter);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[0]);

    const auto& interface0 =
        state->getInterfaces()->getNodeIf(params.interfaces[0]);
    const auto& addr = interface0->getAddressToReach(params.neighborIPs[0]);

    ASSERT_TRUE(addr.has_value());

    EXPECT_EQ(tunnel.srcIp, addr->first);
    EXPECT_EQ(tunnel.srcMac, interface0->getMac());
  });

  this->delRoute(params.longerPrefix);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[1]);
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[1]);

    const auto& interface1 =
        state->getInterfaces()->getNodeIf(params.interfaces[1]);
    const auto& addr = interface1->getAddressToReach(params.neighborIPs[1]);

    ASSERT_TRUE(addr.has_value());

    EXPECT_EQ(tunnel.srcIp, addr->first);
    EXPECT_EQ(tunnel.srcMac, interface1->getMac());
  });
}

TYPED_TEST(MirrorManagerTest, UpdateMirrorOnRouteAdd) {
  const auto params = this->getParamsHelper();
  this->updateState(
      "UpdateMirrorOnRouteAdd: addNode",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState =
            this->addErspanMirror(state, kMirrorName, params.mirrorDestination);
        return this->addNeighbor(
            updatedState,
            InterfaceID(55),
            params.neighborIPs[1],
            params.neighborMACs[1],
            params.neighborPorts[1]);
      });

  RouteNextHopSet nextHopsShorter = {params.nextHop(1)};
  this->addRoute(params.shorterPrefix, nextHopsShorter);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[1]);
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[1]);

    const auto& interface1 =
        state->getInterfaces()->getNodeIf(params.interfaces[1]);
    const auto& addr = interface1->getAddressToReach(params.neighborIPs[1]);

    ASSERT_TRUE(addr.has_value());

    EXPECT_EQ(tunnel.srcIp, addr->first);
    EXPECT_EQ(tunnel.srcMac, interface1->getMac());
  });

  this->updateState(
      "UpdateMirrorOnRouteAdd: addRoute",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->addNeighbor(
            state,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
      });

  RouteNextHopSet nextHopsLonger = {params.nextHop(0)};
  this->addRoute(params.longerPrefix, nextHopsLonger);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[0]);

    const auto& interface0 =
        state->getInterfaces()->getNodeIf(params.interfaces[0]);
    const auto& addr = interface0->getAddressToReach(params.neighborIPs[0]);

    ASSERT_TRUE(addr.has_value());

    EXPECT_EQ(tunnel.srcIp, addr->first);
    EXPECT_EQ(tunnel.srcMac, interface0->getMac());
  });
}

TYPED_TEST(MirrorManagerTest, UpdateNoMirrorWithEgressPortOnRouteDel) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "UpdateNoMirrorWithEgressPortOnRouteDel",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addErspanMirror(
            state,
            kMirrorName,
            params.mirrorDestination,
            params.neighborPorts[0]);
        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        updatedState = this->addNeighbor(
            updatedState,
            InterfaceID(55),
            params.neighborIPs[1],
            params.neighborMACs[1],
            params.neighborPorts[1]);
        return updatedState;
      });

  RouteNextHopSet nextHopsLonger = {params.nextHop(0), params.nextHop(1)};
  this->addRoute(params.longerPrefix, nextHopsLonger);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[0]);

    const auto& interface =
        state->getInterfaces()->getNodeIf(params.interfaces[0]);
    const auto& addr = interface->getAddressToReach(params.neighborIPs[0]);

    ASSERT_TRUE(addr.has_value());

    EXPECT_EQ(tunnel.srcIp, addr->first);
    EXPECT_EQ(tunnel.srcMac, interface->getMac());
  });

  this->delRoute(params.longerPrefix);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    EXPECT_EQ(
        mirror->getEgressPortDesc().value().phyPortID(),
        params.neighborPorts[0]);
    EXPECT_FALSE(mirror->getMirrorTunnel().has_value());
  });
}

TYPED_TEST(MirrorManagerTest, UpdateNoMirrorWithEgressPortOnRouteAdd) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "UpdateNoMirrorWithEgressPortOnRouteAdd",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addErspanMirror(
            state,
            kMirrorName,
            params.mirrorDestination,
            params.neighborPorts[0]);

        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[1]);
        return this->addNeighbor(
            updatedState,
            InterfaceID(55),
            params.neighborIPs[1],
            params.neighborMACs[1],
            params.neighborPorts[1]);
      });

  RouteNextHopSet nextHopsLonger = {params.nextHop(0), params.nextHop(1)};
  this->addRoute(params.longerPrefix, nextHopsLonger);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
    EXPECT_FALSE(mirror->getMirrorTunnel().has_value());
  });
}

TYPED_TEST(MirrorManagerTest, UpdateMirrorOnNeighborChange) {
  const auto params = this->getParamsHelper();
  this->updateState(
      "UpdateMirrorOnNeighborChange: addNode",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState =
            this->addErspanMirror(state, kMirrorName, params.mirrorDestination);
        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        return this->addNeighbor(
            updatedState,
            params.interfaces[1],
            params.neighborIPs[1],
            params.neighborMACs[1],
            params.neighborPorts[1]);
      });

  RouteNextHopSet nextHopsLonger = {params.nextHop(0)};
  this->addRoute(params.longerPrefix, nextHopsLonger);
  RouteNextHopSet nextHopsShorter = {params.nextHop(1)};
  this->addRoute(params.shorterPrefix, nextHopsShorter);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[0]);

    const auto& interface0 =
        state->getInterfaces()->getNodeIf(params.interfaces[0]);
    const auto& addr = interface0->getAddressToReach(params.neighborIPs[0]);

    ASSERT_TRUE(addr.has_value());

    EXPECT_EQ(tunnel.srcIp, addr->first);
    EXPECT_EQ(tunnel.srcMac, interface0->getMac());
  });

  this->updateState(
      "UpdateMirrorOnNeighborChange: delNeighbor",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->delNeighbor(
            state, params.interfaces[0], params.neighborIPs[0]);
        updatedState = this->delNeighbor(
            updatedState, params.interfaces[1], params.neighborIPs[1]);

        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[1]);
        return this->addNeighbor(
            updatedState,
            params.interfaces[1],
            params.neighborIPs[1],
            params.neighborMACs[1],
            params.neighborPorts[0]);
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    EXPECT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[1]);
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[0]);

    const auto& interface1 =
        state->getInterfaces()->getNodeIf(params.interfaces[0]);
    const auto& addr = interface1->getAddressToReach(params.neighborIPs[0]);

    ASSERT_TRUE(addr.has_value());

    EXPECT_EQ(tunnel.srcIp, addr->first);
    EXPECT_EQ(tunnel.srcMac, interface1->getMac());
  });
}

TYPED_TEST(MirrorManagerTest, EmptyDelta) {
  const auto params = this->getParamsHelper();

  const auto oldState = this->sw_->getState();

  this->resolveNeighbor(
      params.neighborIPs[0],
      params.neighborMACs[0],
      params.interfaces[0],
      params.neighborPorts[0]);
  this->resolveNeighbor(
      params.neighborIPs[1],
      params.neighborMACs[1],
      params.interfaces[1],
      params.neighborPorts[1]);

  this->verifyStateUpdate([=, this]() {
    const auto newState = this->sw_->getState();
    StateDelta delta(oldState, newState);
    EXPECT_FALSE(DeltaFunctions::isEmpty(delta.getVlansDelta()));
  });
}

// test for gre src ip resolved
TYPED_TEST(MirrorManagerTest, GreMirrorWithSrcIp) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "GreMirrorWithSrcIp",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addNeighbor(
            state,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        updatedState = this->addErspanMirror(
            updatedState, kMirrorName, params.mirrorDestination);

        auto oldMirror = updatedState->getMirrors()->getNodeIf(kMirrorName);
        auto mirror = std::make_shared<Mirror>(
            kMirrorName,
            oldMirror->getEgressPortDesc(),
            oldMirror->getDestinationIp(),
            std::make_optional<folly::IPAddress>(params.mirrorSource));
        return this->updateMirror(updatedState, mirror);
      });

  RouteNextHopSet nextHops = {params.nextHop(0)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[0]);
    const auto& interface =
        state->getInterfaces()->getNodeIf(params.interfaces[0]);
    EXPECT_EQ(tunnel.srcIp, folly::IPAddress(params.mirrorSource));
    EXPECT_EQ(tunnel.srcMac, interface->getMac());
    EXPECT_FALSE(tunnel.udpPorts.has_value());
  });
}

TYPED_TEST(MirrorManagerTest, SflowMirrorWithSrcIp) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "GreMirrorWithSrcIp",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addNeighbor(
            state,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        updatedState = this->addSflowMirror(
            updatedState, kMirrorName, params.mirrorDestination, 10101, 20202);

        auto oldMirror = updatedState->getMirrors()->getNodeIf(kMirrorName);
        auto mirror = std::make_shared<Mirror>(
            kMirrorName,
            oldMirror->getEgressPortDesc(),
            oldMirror->getDestinationIp(),
            std::make_optional<folly::IPAddress>(params.mirrorSource),
            oldMirror->getTunnelUdpPorts());
        return this->updateMirror(updatedState, mirror);
      });

  RouteNextHopSet nextHops = {params.nextHop(0)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[0]);
    const auto& interface =
        state->getInterfaces()->getNodeIf(params.interfaces[0]);
    EXPECT_EQ(tunnel.srcIp, folly::IPAddress(params.mirrorSource));
    EXPECT_EQ(tunnel.srcMac, interface->getMac());
    EXPECT_TRUE(tunnel.udpPorts.has_value());
    EXPECT_EQ(tunnel.udpPorts->udpSrcPort, 10101);
    EXPECT_EQ(tunnel.udpPorts->udpDstPort, 20202);
  });
}

TYPED_TEST(MirrorManagerTest, ResolveMirrorOnMirrorUpdate) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "add sflowMirror", [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = state->clone();
        updatedState = this->addSflowMirror(
            updatedState, kMirrorName, params.mirrorDestination, 10101, 20202);
        return updatedState;
      });

  this->updateState(
      "resolve sflowMirror",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addNeighbor(
            state,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        return updatedState;
      });

  RouteNextHopSet nextHops = {params.nextHop(0)};
  this->addRoute(params.longerPrefix, nextHops);

  this->updateState(
      "update sflow mirror config",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = state->clone();
        auto oldMirror = state->getMirrors()->getNodeIf(kMirrorName);

        // unresolve and update with non-resolving affecting update,
        // mirror so mirror manager resolves it again.
        auto mirror = std::make_shared<Mirror>(
            oldMirror->getID(),
            std::optional<PortDescriptor>(),
            oldMirror->getDestinationIp(),
            std::optional<IPAddress>(),
            oldMirror->getTunnelUdpPorts());
        mirror->setTruncate(true);
        return this->updateMirror(updatedState, mirror);
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value());
    auto tunnel = mirror->getMirrorTunnel().value();
    EXPECT_EQ(tunnel.dstIp, IPAddress(params.mirrorDestination));
    EXPECT_EQ(tunnel.dstMac, params.neighborMACs[0]);
    const auto& interface =
        state->getInterfaces()->getNodeIf(params.interfaces[0]);
    EXPECT_EQ(tunnel.srcMac, interface->getMac());
    EXPECT_TRUE(tunnel.udpPorts.has_value());
    EXPECT_EQ(tunnel.udpPorts->udpSrcPort, 10101);
    EXPECT_EQ(tunnel.udpPorts->udpDstPort, 20202);
    EXPECT_TRUE(mirror->getTruncate());
  });
}

TYPED_TEST(MirrorManagerTest, ConfigHasEgressPort) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "add sflowMirror", [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addSflowMirror(
            state, kMirrorName, params.mirrorDestination, 10101, 20202);
        return updatedState;
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->configHasEgressPort());
  });

  this->updateState(
      "resolve sflowMirror",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addNeighbor(
            state,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        return updatedState;
      });

  RouteNextHopSet nextHops = {params.nextHop(0)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->configHasEgressPort());
    EXPECT_TRUE(mirror->isResolved());
  });

  this->updateState(
      "remove neighbor and route",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        return this->delNeighbor(
            state, params.interfaces[0], params.neighborIPs[0]);
      });
  this->delRoute(params.longerPrefix);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->configHasEgressPort());
    EXPECT_FALSE(mirror->isResolved());
  });
}

TYPED_TEST(MirrorManagerTest, NeighborUpdates) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "GreMirrorWithOutPort-0",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addSflowMirror(
            state, kMirrorName, params.mirrorDestination, 10101, 20202);
        return updatedState;
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->isResolved());
  });

  this->updateState(
      "resolve sflowMirror-0",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addNeighbor(
            state,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[1],
            params.neighborIPs[1],
            params.neighborMACs[1],
            params.neighborPorts[1]);
        return updatedState;
      });

  RouteNextHopSet nextHops = {params.nextHop(0), params.nextHop(1)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    ASSERT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
  });

  this->updateState(
      "remove nbr 0", [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->delNeighbor(
            state, params.interfaces[0], params.neighborIPs[0]);
        return updatedState;
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    ASSERT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value();
    EXPECT_EQ(egressPort, params.neighborPorts[1]);
  });

  this->updateState(
      "remove nbr 1,add nbr 0",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->delNeighbor(
            state, params.interfaces[1], params.neighborIPs[1]);
        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        return updatedState;
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    ASSERT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
  });
}

TYPED_TEST(MirrorManagerTest, UpdateRoute) {
  const auto params = this->getParamsHelper();

  this->updateState(
      "add mirror", [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addSflowMirror(
            state, kMirrorName, params.mirrorDestination, 10101, 20202);
        return updatedState;
      });

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->isResolved());
  });

  this->updateState(
      "resolve mirror", [=, this](const std::shared_ptr<SwitchState>& state) {
        auto updatedState = this->addNeighbor(
            state,
            params.interfaces[0],
            params.neighborIPs[0],
            params.neighborMACs[0],
            params.neighborPorts[0]);
        updatedState = this->addNeighbor(
            updatedState,
            params.interfaces[1],
            params.neighborIPs[1],
            params.neighborMACs[1],
            params.neighborPorts[1]);
        return updatedState;
      });

  RouteNextHopSet nextHops = {params.nextHop(0), params.nextHop(1)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_TRUE(mirror->isResolved());
    EXPECT_FALSE(mirror->configHasEgressPort());
    ASSERT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
  });

  this->delRoute(params.longerPrefix);
  nextHops = {params.nextHop(1)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->configHasEgressPort());
    EXPECT_TRUE(mirror->isResolved());
    ASSERT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[1]);
  });

  this->delRoute(params.longerPrefix);
  nextHops = {params.nextHop(0)};
  this->addRoute(params.longerPrefix, nextHops);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto mirror = state->getMirrors()->getNodeIf(kMirrorName);
    EXPECT_NE(mirror, nullptr);
    EXPECT_FALSE(mirror->configHasEgressPort());
    EXPECT_TRUE(mirror->isResolved());
    ASSERT_TRUE(mirror->getEgressPortDesc().has_value());
    auto egressPort = mirror->getEgressPortDesc().value().phyPortID();
    EXPECT_EQ(egressPort, params.neighborPorts[0]);
  });
}
// Port 6 is on switch 0, port 7 on switch 1.
constexpr int64_t kNpu0PortIdMax = 6;

template <typename IpAddrT>
class MirrorManagerMultiNpuTest : public MirrorManagerTest<IpAddrT> {
 public:
  using AddrT = typename IpAddrT::IPAddrT;

  void SetUp() override {
    // Two switches need MULTI_SWITCH run mode.
    savedMultiSwitch_ = FLAGS_multi_switch;
    FLAGS_multi_switch = true;

    cfg::AgentConfig agentConfig;
    agentConfig.sw() = testConfigA();
    agentConfig.sw()->switchSettings()->switchIdToSwitchInfo() = {
        {0,
         createSwitchInfo(
             cfg::SwitchType::NPU,
             cfg::AsicType::ASIC_TYPE_MOCK,
             cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
             kNpu0PortIdMax,
             0 /* switchIndex */)},
        {1,
         createSwitchInfo(
             cfg::SwitchType::NPU,
             cfg::AsicType::ASIC_TYPE_MOCK,
             kNpu0PortIdMax + 1,
             cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
             1 /* switchIndex */)},
    };
    this->handle_ = createTestHandle(&agentConfig.sw().value());
    this->sw_ = this->handle_->getSw();
    this->sw_->setConfig(std::make_unique<AgentConfig>(std::move(agentConfig)));
    waitForStateUpdates(this->sw_);
  }

  void TearDown() override {
    MirrorManagerTest<IpAddrT>::TearDown();
    // Stop here; ~SwSwitch has no HwSwitch for the second switch.
    this->sw_->stop(false, false);
    FLAGS_multi_switch = savedMultiSwitch_;
  }

 protected:
  const MirrorManagerTestParams<AddrT> params_ = getParams<AddrT>();

  static HwSwitchMatcher npu(int switchId) {
    return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(switchId)}));
  }

  // Tunnel mirror on both NPUs, plus a route with one nexthop per NPU.
  void addTunnelMirrorToBothNpus() {
    this->updateState(
        "addTunnelMirrorToBothNpus",
        [this](const std::shared_ptr<SwitchState>& state) {
          auto newState = state->clone();
          auto mirrors = newState->getMirrors()->modify(&newState);
          for (int switchId : {0, 1}) {
            auto mirror = std::make_shared<Mirror>(
                kMirrorName,
                std::optional<PortDescriptor>(),
                std::make_optional<IPAddress>(params_.mirrorDestination));
            mirror->setSwitchId(SwitchID(switchId));
            mirrors->addNode(mirror, npu(switchId));
          }
          return newState;
        });
    this->addRoute(
        params_.shorterPrefix, {params_.nextHop(0), params_.nextHop(1)});
  }

  void addNeighbor(int index) {
    this->updateState(
        "addNeighbor",
        [this, index](const std::shared_ptr<SwitchState>& state) {
          return this->addNeighborToIntfTable(
              state,
              params_.interfaces[index],
              params_.neighborIPs[index],
              params_.neighborMACs[index],
              params_.neighborPorts[index]);
        });
  }

  // Must run on the update thread, inside verifyStateUpdate().
  void expectNpuMirror(int switchId, std::optional<int> resolvedNeighborIndex) {
    auto mirrorMap =
        this->sw_->getState()->getMirrors()->getMapNodeIf(npu(switchId));
    ASSERT_NE(mirrorMap, nullptr) << "switch " << switchId;
    auto mirror = mirrorMap->getNodeIf(kMirrorName);
    ASSERT_NE(mirror, nullptr) << "switch " << switchId;
    if (!resolvedNeighborIndex.has_value()) {
      EXPECT_FALSE(mirror->isResolved()) << "switch " << switchId;
      return;
    }
    EXPECT_TRUE(mirror->isResolved()) << "switch " << switchId;
    ASSERT_TRUE(mirror->getEgressPortDesc().has_value())
        << "switch " << switchId;
    EXPECT_EQ(
        mirror->getEgressPortDesc()->phyPortID(),
        params_.neighborPorts[*resolvedNeighborIndex])
        << "switch " << switchId;
    ASSERT_TRUE(mirror->getMirrorTunnel().has_value()) << "switch " << switchId;
    EXPECT_EQ(
        mirror->getMirrorTunnel()->dstMac,
        params_.neighborMACs[*resolvedNeighborIndex])
        << "switch " << switchId;
  }

 private:
  bool savedMultiSwitch_{false};
};

TYPED_TEST_SUITE(MirrorManagerMultiNpuTest, TestTypes);

// Each NPU resolves to an egress port it owns.
TYPED_TEST(MirrorManagerMultiNpuTest, EachNpuResolvesItsOwnEgressPort) {
  this->addTunnelMirrorToBothNpus();
  this->addNeighbor(0);
  this->addNeighbor(1);

  this->verifyStateUpdate([this]() {
    this->expectNpuMirror(0, 0);
    this->expectNpuMirror(1, 1);
  });
}

// No local nexthop: switch 0's copy stays unresolved.
TYPED_TEST(MirrorManagerMultiNpuTest, NpuStaysUnresolvedWithoutLocalNexthop) {
  this->addTunnelMirrorToBothNpus();
  this->addNeighbor(1);

  this->verifyStateUpdate([this]() {
    this->expectNpuMirror(0, std::nullopt);
    this->expectNpuMirror(1, 1);
  });
}

} // namespace facebook::fboss
