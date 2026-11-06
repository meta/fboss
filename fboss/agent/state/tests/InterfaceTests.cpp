/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/NodeMapDelta.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;
using folly::IPAddress;
using folly::MacAddress;
using std::make_shared;
using std::shared_ptr;
using ::testing::Return;

namespace {
void validateSerialization(const std::shared_ptr<InterfaceMap>& node) {
  if (!node) {
    return;
  }
  auto nodeBack = std::make_shared<InterfaceMap>(node->toThrift());
  EXPECT_EQ(node->toThrift(), nodeBack->toThrift());
}

template <cfg::InterfaceType intfType>
struct TestType {
  static constexpr cfg::InterfaceType type = intfType;
};
using TestVlanIntf = TestType<cfg::InterfaceType::VLAN>;
using TestPortIntf = TestType<cfg::InterfaceType::PORT>;
} // namespace

template <typename Type>
class InterfaceTest : public ::testing::Test {
 private:
  cfg::SwitchConfig genConfigWithLLs(
      std::set<std::string> intfLinkLocals,
      std::optional<std::string> raRouterAddr) {
    cfg::SwitchConfig config;
    config.vlans()->resize(2);
    *config.vlans()[0].id() = 1;
    *config.vlans()[1].id() = 2;
    config.interfaces()->resize(2);
    auto* intfConfig = &config.interfaces()[0];
    *intfConfig->intfID() = 1;
    *intfConfig->vlanID() = 1;
    *intfConfig->routerID() = 1;
    config.vrfs().ensure().emplace_back().vrfId() = 1;
    intfConfig->ipAddresses()->resize(4 + intfLinkLocals.size());
    auto idx = 0;
    intfConfig->ipAddresses()[idx++] = "10.1.1.1/24";
    intfConfig->ipAddresses()[idx++] = "20.1.1.2/24";
    intfConfig->ipAddresses()[idx++] = "::22:33:44/120";
    intfConfig->ipAddresses()[idx++] = "::11:11:11/120";
    for (const auto& intfAddr : intfLinkLocals) {
      intfConfig->ipAddresses()[idx++] = intfAddr;
    }
    if (raRouterAddr) {
      intfConfig->ndp() = cfg::NdpConfig{};
      intfConfig->ndp()->routerAddress() = *raRouterAddr;
    }

    intfConfig = &config.interfaces()[1];
    *intfConfig->intfID() = 2;
    *intfConfig->vlanID() = 2;
    *intfConfig->routerID() = 2;
    config.vrfs().ensure().emplace_back().vrfId() = 2;
    intfConfig->ipAddresses()->resize(4);
    intfConfig->ipAddresses()[0] = "10.1.1.1/24";
    intfConfig->ipAddresses()[1] = "20.1.1.2/24";
    intfConfig->ipAddresses()[2] = "::22:33:44/120";
    intfConfig->ipAddresses()[3] = "::11:11:11/120";
    return config;
  }

  cfg::SwitchConfig genPortRouterInterfaceConfigWithLLs(
      std::set<std::string> intfLinkLocals,
      std::optional<std::string> raRouterAddr) {
    cfg::SwitchConfig config;

    /* add router port interfaces along side vlan interfaces */
    config.ports()->resize(2);
    preparedMockPortConfig(config.ports()[0], 1);
    preparedMockPortConfig(config.ports()[1], 2);

    for (auto port : *config.ports()) {
      cfg::Interface portIntf{};
      portIntf.intfID() = *port.logicalID();
      portIntf.type() = cfg::InterfaceType::PORT;
      portIntf.portID() = *port.logicalID();
      portIntf.routerID() = *port.logicalID();
      config.vrfs().ensure().emplace_back().vrfId() = *port.logicalID();
      portIntf.name() = folly::to<std::string>("port-", *port.logicalID());
      portIntf.ipAddresses()->emplace_back("10.1.1.1/24");
      portIntf.ipAddresses()->emplace_back("20.1.1.2/24");
      portIntf.ipAddresses()->emplace_back("::22:33:44/120");
      portIntf.ipAddresses()->emplace_back("::11:11:11/120");

      if (raRouterAddr) {
        portIntf.ndp() = cfg::NdpConfig{};
        portIntf.ndp()->routerAddress() = *raRouterAddr;
      }
      for (const auto& intfAddr : intfLinkLocals) {
        portIntf.ipAddresses()->push_back(intfAddr);
      }
      config.interfaces()->push_back(portIntf);
    }
    return config;
  }

 protected:
  static auto constexpr interfaceType = Type::type;
  std::shared_ptr<SwitchState> setup(
      std::set<std::string> intfLinkLocals,
      std::optional<std::string> raRouterAddr) {
    platform_ = createMockPlatform();
    auto config = interfaceType == cfg::InterfaceType::VLAN
        ? genConfigWithLLs(intfLinkLocals, raRouterAddr)
        : genPortRouterInterfaceConfigWithLLs(intfLinkLocals, raRouterAddr);
    return publishAndApplyConfig(
        std::make_shared<SwitchState>(), &config, platform_.get());
  }

 private:
  std::unique_ptr<Platform> platform_;
};

using InterfaceTypes = ::testing::Types<
    TestType<cfg::InterfaceType::VLAN>,
    TestType<cfg::InterfaceType::PORT>>;

TYPED_TEST_SUITE(InterfaceTest, InterfaceTypes);
TYPED_TEST(InterfaceTest, addrToReach) {
  auto state = this->setup({"fe80::face:b00c/64"}, std::nullopt);
  ASSERT_NE(nullptr, state);
  const auto& intfs = state->getInterfaces();
  const auto& intf1 = intfs->getNode(InterfaceID(1));
  const auto& intf2 = intfs->getNode(InterfaceID(2));

  validateThriftStructNodeSerialization(*intf1);
  validateThriftStructNodeSerialization(*intf2);

  EXPECT_TRUE(intf1->hasAddress(IPAddress("10.1.1.1")));
  EXPECT_FALSE(intf1->hasAddress(IPAddress("10.1.2.1")));
  EXPECT_TRUE(intf2->hasAddress(IPAddress("::11:11:11")));
  EXPECT_FALSE(intf2->hasAddress(IPAddress("::11:11:12")));

  auto intf = intfs->getIntfToReach(RouterID(1), IPAddress("20.1.1.100"));
  ASSERT_TRUE(intf != nullptr);
  EXPECT_EQ(intf1, intf);
  auto addr = intf->getAddressToReach(IPAddress("20.1.1.100"));
  EXPECT_EQ(IPAddress("20.1.1.2"), addr->first);
  EXPECT_EQ(24, addr->second);

  intf = intfs->getIntfToReach(RouterID(2), IPAddress("::22:33:4f"));
  ASSERT_TRUE(intf != nullptr);
  EXPECT_EQ(intf2, intf);
  addr = intf->getAddressToReach(IPAddress("::22:33:4f"));
  EXPECT_EQ(IPAddress("::22:33:44"), addr->first);
  EXPECT_EQ(120, addr->second);

  intf = intfs->getIntfToReach(RouterID(2), IPAddress("::22:34:5f"));
  ASSERT_TRUE(intf == nullptr);
  // Assert the to reach LL we always use fe80::face:b00c address
  // This is not required by NDP RFC (we could use any of the in
  // subnet addresses), but using fe80::face:b00c is done to
  // preserve longstanding behavior. See S289408 for details.
  EXPECT_EQ(
      IPAddress("fe80::face:b00c"),
      intf1->getAddressToReach(IPAddress("fe80::9a03:9bff:fe7d:656a"))->first);
}

TYPED_TEST(InterfaceTest, addrToReachBackendNw) {
  auto state = this->setup(
      {"fe80::face:b00b/64", "fe80::be:face:b00c/64"}, std::nullopt);
  const auto& intf1 = state->getInterfaces()->getNode(InterfaceID(1));
  EXPECT_EQ(
      IPAddress("fe80::face:b00b"),
      intf1->getAddressToReach(IPAddress("fe80::9a03:9bff:fe7d:656a"))->first);
}

TYPED_TEST(InterfaceTest, addrToReachBackendNwNewConfig) {
  auto platform = createMockPlatform();
  auto state = this->setup({"fe80::be:face:b00c/64"}, std::nullopt);
  const auto& intf1 = state->getInterfaces()->getNode(InterfaceID(1));
  EXPECT_EQ(
      IPAddress("fe80::be:face:b00c"),
      intf1->getAddressToReach(IPAddress("fe80::9a03:9bff:fe7d:656a"))->first);
}

TYPED_TEST(InterfaceTest, addrToReachWithRouterAddrConfigured) {
  auto state = this->setup({"fe80::face:b00c/64"}, "fe80::face:b00c");
  const auto& intf1 = state->getInterfaces()->getNode(InterfaceID(1));
  EXPECT_EQ(
      MockPlatform::getMockLinkLocalIp6(),
      intf1->getAddressToReach(IPAddress("fe80::9a03:9bff:fe7d:656a"))->first);
}

TYPED_TEST(InterfaceTest, addrToReachBackendRouterAddrConfigured) {
  auto state = this->setup(
      {"fe80::face:b00b/64", "fe80::be:face:b00c/64"}, "fe80::face:b00b");
  const auto& intf1 = state->getInterfaces()->getNode(InterfaceID(1));
  EXPECT_EQ(
      MockPlatform::getMockLinkLocalIp6(),
      intf1->getAddressToReach(IPAddress("fe80::9a03:9bff:fe7d:656a"))->first);
}

TYPED_TEST(InterfaceTest, addrToReachBackendNewConfigRouterAddrConfigured) {
  auto state = this->setup({"fe80::be:face:b00c/64"}, "fe80::be:face:b00c");
  const auto& intf1 = state->getInterfaces()->getNode(InterfaceID(1));
  EXPECT_EQ(
      MockPlatform::getMockLinkLocalIp6(),
      intf1->getAddressToReach(IPAddress("fe80::9a03:9bff:fe7d:656a"))->first);
}

TYPED_TEST(InterfaceTest, getSetArpTable) {
  auto state = this->setup({}, std::nullopt);
  state::NeighborEntries arpTable;
  state::NeighborEntryFields arp;
  arp.ipaddress() = "10.1.1.100";
  arp.mac() = "02:00:00:00:00:01";
  cfg::PortDescriptor port;
  port.portId() = 1;
  port.portType() = cfg::PortDescriptorType::Physical;
  arp.portId() = port;
  arp.interfaceId() = 1;
  arp.state() = state::NeighborState::Reachable;
  arpTable.insert({*arp.ipaddress(), arp});
  auto intf1 = state->getInterfaces()->getNode(InterfaceID(1))->clone();
  intf1->setArpTable(arpTable);
  EXPECT_EQ(arpTable, intf1->getArpTable()->toThrift());
  EXPECT_EQ(
      arpTable,
      intf1->template getNeighborEntryTable<folly::IPAddressV4>()->toThrift());
  EXPECT_NE(
      arpTable,
      intf1->template getNeighborEntryTable<folly::IPAddressV6>()->toThrift());
}

TYPED_TEST(InterfaceTest, getSetNdpTable) {
  auto state = this->setup({}, std::nullopt);
  state::NeighborEntries ndpTable;
  state::NeighborEntryFields ndp;
  ndp.ipaddress() = "::22:33:4f";
  ndp.mac() = "02:00:00:00:00:01";
  cfg::PortDescriptor port;
  port.portId() = 1;
  port.portType() = cfg::PortDescriptorType::Physical;
  ndp.portId() = port;
  ndp.interfaceId() = 1;
  ndp.state() = state::NeighborState::Reachable;
  ndpTable.insert({*ndp.ipaddress(), ndp});
  auto intf1 = state->getInterfaces()->getNode(InterfaceID(1))->clone();
  intf1->setNdpTable(ndpTable);
  EXPECT_EQ(ndpTable, intf1->getNdpTable()->toThrift());
  EXPECT_EQ(
      ndpTable,
      intf1->template getNeighborEntryTable<folly::IPAddressV6>()->toThrift());
  EXPECT_NE(
      ndpTable,
      intf1->template getNeighborEntryTable<folly::IPAddressV4>()->toThrift());
}

TEST(Interface, Modify) {
  {
    // NPU
    auto state = std::make_shared<SwitchState>();
    auto platform = createMockPlatform();
    cfg::SwitchConfig config = testConfigA();
    auto stateV1 = publishAndApplyConfig(state, &config, platform.get());
    stateV1->publish();
    auto origIntfMap = utility::getFirstMap(stateV1->getInterfaces());
    auto origIntf = origIntfMap->cbegin()->second;
    auto origIntfs = stateV1->getInterfaces();
    auto newIntf = origIntf->modify(&stateV1);
    EXPECT_NE(origIntf.get(), newIntf);
    EXPECT_NE(origIntfMap, utility::getFirstMap(stateV1->getInterfaces()));
    EXPECT_NE(origIntfs, stateV1->getInterfaces());
  }
  {
    // VOQ
    auto state = std::make_shared<SwitchState>();
    addSwitchInfo(state, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
    auto platform = createMockPlatform();
    cfg::SwitchConfig config = testConfigA(cfg::SwitchType::VOQ);
    auto stateV1 = publishAndApplyConfig(state, &config, platform.get());
    stateV1->publish();
    auto origIntfMap = utility::getFirstMap(stateV1->getInterfaces());
    auto origIntf = origIntfMap->cbegin()->second;
    auto origIntfs = stateV1->getInterfaces();
    auto newIntf = origIntf->modify(&stateV1);
    EXPECT_NE(origIntf.get(), newIntf);
    EXPECT_NE(origIntfMap, utility::getFirstMap(stateV1->getInterfaces()));
    EXPECT_NE(origIntfs, stateV1->getInterfaces());
  }
}

TEST(Interface, RemoteInterfaceModify) {
  auto state = std::make_shared<SwitchState>();
  auto platform = createMockPlatform();
  addSwitchInfo(state, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
  cfg::SwitchConfig config = testConfigA(cfg::SwitchType::VOQ);
  auto stateV1 = publishAndApplyConfig(state, &config, platform.get());
  auto remoteSysPorts = stateV1->getRemoteSystemPorts()->modify(&stateV1);

  HwSwitchMatcher scope(std::unordered_set<SwitchID>({SwitchID{1}}));
  auto sysPort1 = makeSysPort("olympic", 1001, 100);
  sysPort1->setScope(cfg::Scope::GLOBAL);
  remoteSysPorts->addNode(sysPort1, scope);
  auto remoteInterfaces = stateV1->getRemoteInterfaces()->modify(&stateV1);
  InterfaceID remoteIntfId(1001);
  auto rif = std::make_shared<Interface>(
      remoteIntfId,
      RouterID(0),
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece("1001"),
      folly::MacAddress{},
      9000,
      false,
      false,
      cfg::InterfaceType::SYSTEM_PORT);
  rif->setScope(cfg::Scope::GLOBAL);

  remoteInterfaces->addNode(rif, scope);
  stateV1->publish();
  auto origIntfMap = stateV1->getRemoteInterfaces()->getMapNodeIf(scope);
  auto origIntf = stateV1->getRemoteInterfaces()->getNode(remoteIntfId);
  auto origIntfs = stateV1->getRemoteInterfaces();
  auto newIntf = origIntf->modify(&stateV1);
  EXPECT_NE(origIntf.get(), newIntf);
  EXPECT_NE(origIntfMap, stateV1->getRemoteInterfaces()->getMapNodeIf(scope));
  EXPECT_NE(origIntfs, stateV1->getRemoteInterfaces());
}

TEST(Interface, applyConfig) {
  auto platform = createMockPlatform();
  cfg::SwitchConfig config;
  config.vlans()->resize(1);
  *config.vlans()[0].id() = 1;
  config.vlans()[0].intfID() = 1;
  config.interfaces()->resize(1);
  auto* intfConfig = &config.interfaces()[0];
  *intfConfig->intfID() = 1;
  *intfConfig->vlanID() = 1;
  *intfConfig->routerID() = 0;
  intfConfig->mac() = "00:02:00:11:22:33";
  intfConfig->dhcpRelayAddressV4() = "30.1.1.1";
  intfConfig->dhcpRelayAddressV6() = "2a03:2880:10:1f07:face:b00c:0:0";
  intfConfig->dhcpRelayOverridesV4() = {};
  (*intfConfig->dhcpRelayOverridesV4())["02:00:00:00:00:02"] = "1.2.3.4";
  intfConfig->dhcpRelayOverridesV6() = {};
  (*intfConfig->dhcpRelayOverridesV6())["02:00:00:00:00:02"] =
      "2a03:2880:10:1f07:face:b00c:0:0";

  InterfaceID id(1);
  shared_ptr<SwitchState> oldState;
  shared_ptr<SwitchState> state;
  shared_ptr<Interface> oldInterface;
  shared_ptr<Interface> interface;
  auto updateState = [&]() {
    oldState = state;
    oldInterface = interface;
    state = publishAndApplyConfig(oldState, &config, platform.get());
    EXPECT_NE(oldState, state);
    ASSERT_NE(nullptr, state);
    interface = state->getInterfaces()->getNode(id);
    EXPECT_NE(oldInterface, interface);
    ASSERT_NE(nullptr, interface);
  };

  state = make_shared<SwitchState>();
  updateState();
  NodeID nodeID = interface->getNodeID();
  EXPECT_EQ(0, interface->getGeneration());
  EXPECT_EQ(VlanID(1), interface->getVlanID());
  EXPECT_EQ(RouterID(0), interface->getRouterID());
  EXPECT_EQ("Interface 1", interface->getName());
  EXPECT_EQ(MacAddress("00:02:00:11:22:33"), interface->getMac());
  EXPECT_EQ(1, interface->getAddresses()->size()); // 1 ipv6 link local address
  EXPECT_EQ(0, interface->routerAdvertisementSeconds());
  auto vlan1 = state->getVlans()->getNodeIf(VlanID(1));
  EXPECT_EQ(InterfaceID(1), vlan1->getInterfaceID());
  EXPECT_EQ(folly::IPAddressV4("30.1.1.1"), interface->getDhcpV4Relay());
  EXPECT_EQ(
      folly::IPAddressV6("2a03:2880:10:1f07:face:b00c:0:0"),
      interface->getDhcpV6Relay());

  auto map4 = interface->getDhcpV4RelayOverrides();
  EXPECT_EQ(
      folly::IPAddressV4("1.2.3.4"),
      folly::IPAddressV4(map4[folly::MacAddress("02:00:00:00:00:02")]));
  auto map6 = interface->getDhcpV6RelayOverrides();
  EXPECT_EQ(
      folly::IPAddressV6("2a03:2880:10:1f07:face:b00c:0:0"),
      folly::IPAddressV6(map6[folly::MacAddress("02:00:00:00:00:02")]));

  // same configuration cause nothing changed
  EXPECT_EQ(nullptr, publishAndApplyConfig(state, &config, platform.get()));

  // Change VlanID for intf + create new intf for existing vlan
  config.vlans()->resize(2);
  *config.vlans()[1].id() = 2;
  config.vlans()[1].intfID() = 2;
  *intfConfig->vlanID() = 2;
  config.interfaces()->resize(2);
  *config.interfaces()[0].intfID() = 2;
  *config.interfaces()[0].vlanID() = 2;
  *config.interfaces()[1].intfID() = 1;
  *config.interfaces()[1].vlanID() = 1;
  *config.interfaces()[1].routerID() = 0;
  MacAddress intf2Mac("02:01:02:ab:cd:78");
  config.interfaces()[1].mac() = intf2Mac.toString();
  updateState();
  EXPECT_EQ(nodeID, interface->getNodeID());
  EXPECT_EQ(1, interface->getGeneration());
  EXPECT_EQ(VlanID(1), interface->getVlanID());
  EXPECT_EQ(RouterID(0), interface->getRouterID());
  EXPECT_EQ(oldInterface->getName(), interface->getName());
  EXPECT_EQ(intf2Mac, interface->getMac());
  auto vlan2 = state->getVlans()->getNodeIf(VlanID(2));
  auto newvlan1 = state->getVlans()->getNodeIf(VlanID(1));
  EXPECT_EQ(InterfaceID(2), vlan2->getInterfaceID());
  EXPECT_EQ(InterfaceID(1), newvlan1->getInterfaceID());

  // routerID change
  *config.interfaces()[1].routerID() = 1;
  config.vrfs().ensure().emplace_back().vrfId() = 1;
  updateState();
  EXPECT_EQ(nodeID, interface->getNodeID());
  EXPECT_EQ(oldInterface->getGeneration() + 1, interface->getGeneration());
  EXPECT_EQ(VlanID(1), interface->getVlanID());
  EXPECT_EQ(RouterID(1), interface->getRouterID());
  EXPECT_EQ(InterfaceID(1), interface->getID());
  EXPECT_EQ(oldInterface->getName(), interface->getName());
  EXPECT_EQ(oldInterface->getMac(), interface->getMac());
  EXPECT_EQ(oldInterface->getAddressesCopy(), interface->getAddressesCopy());

  // MAC address change
  config.interfaces()[1].mac() = "00:02:00:12:34:56";
  updateState();
  EXPECT_EQ(oldInterface->getGeneration() + 1, interface->getGeneration());
  EXPECT_EQ(VlanID(1), interface->getVlanID());
  EXPECT_EQ(RouterID(1), interface->getRouterID());
  EXPECT_EQ(oldInterface->getName(), interface->getName());
  EXPECT_EQ(MacAddress("00:02:00:12:34:56"), interface->getMac());
  // Use the platform supplied MAC
  config.interfaces()[1].mac().reset();
  updateState();
  EXPECT_EQ(nodeID, interface->getNodeID());
  EXPECT_EQ(oldInterface->getGeneration() + 1, interface->getGeneration());
  EXPECT_EQ(oldInterface->getVlanID(), interface->getVlanID());
  EXPECT_EQ(oldInterface->getRouterID(), interface->getRouterID());
  EXPECT_EQ(oldInterface->getName(), interface->getName());
  EXPECT_EQ(platform->getLocalMac(), interface->getMac());
  // Interface will be updated based on new MAC Address
  EXPECT_NE(oldInterface->getAddresses(), interface->getAddresses());

  // IP addresses change
  config.interfaces()[1].ipAddresses()->resize(4);
  config.interfaces()[1].ipAddresses()[0] = "10.1.1.1/24";
  config.interfaces()[1].ipAddresses()[1] = "20.1.1.2/24";
  config.interfaces()[1].ipAddresses()[2] = "::22:33:44/120";
  config.interfaces()[1].ipAddresses()[3] = "::11:11:11/120";
  updateState();
  EXPECT_EQ(nodeID, interface->getNodeID());
  EXPECT_EQ(oldInterface->getGeneration() + 1, interface->getGeneration());
  EXPECT_EQ(VlanID(1), interface->getVlanID());
  EXPECT_EQ(RouterID(1), interface->getRouterID());
  EXPECT_EQ(oldInterface->getName(), interface->getName());
  EXPECT_EQ(oldInterface->getMac(), interface->getMac());
  // Link-local addrs will be added automatically
  EXPECT_EQ(5, interface->getAddresses()->size());

  // change the order of IP address shall not change the interface
  config.interfaces()[1].ipAddresses()[0] = "10.1.1.1/24";
  config.interfaces()[1].ipAddresses()[1] = "::22:33:44/120";
  config.interfaces()[1].ipAddresses()[2] = "20.1.1.2/24";
  config.interfaces()[1].ipAddresses()[3] = "::11:11:11/120";
  EXPECT_EQ(nullptr, publishAndApplyConfig(state, &config, platform.get()));

  // duplicate IP addresses causes throw
  config.interfaces()[1].ipAddresses()[1] =
      config.interfaces()[1].ipAddresses()[0];
  EXPECT_THROW(
      publishAndApplyConfig(state, &config, platform.get()), FbossError);
  // Should still throw even if the mask is different
  config.interfaces()[1].ipAddresses()[1] = "10.1.1.1/16";
  EXPECT_THROW(
      publishAndApplyConfig(state, &config, platform.get()), FbossError);
  config.interfaces()[1].ipAddresses()[1] = "::22:33:44/120";

  // Name change
  config.interfaces()[1].name() = "myintf";
  updateState();
  EXPECT_EQ(nodeID, interface->getNodeID());
  EXPECT_EQ(oldInterface->getGeneration() + 1, interface->getGeneration());
  EXPECT_EQ("myintf", interface->getName());
  EXPECT_EQ(oldInterface->getVlanID(), interface->getVlanID());
  EXPECT_EQ(oldInterface->getRouterID(), interface->getRouterID());
  EXPECT_EQ(oldInterface->getMac(), interface->getMac());
  EXPECT_EQ(oldInterface->getAddressesCopy(), interface->getAddressesCopy());
  // Reset the name back to it's default value
  config.interfaces()[1].name().reset();
  updateState();
  EXPECT_EQ(nodeID, interface->getNodeID());
  EXPECT_EQ(oldInterface->getGeneration() + 1, interface->getGeneration());
  EXPECT_EQ("Interface 1", interface->getName());
  EXPECT_EQ(oldInterface->getVlanID(), interface->getVlanID());
  EXPECT_EQ(oldInterface->getRouterID(), interface->getRouterID());
  EXPECT_EQ(oldInterface->getMac(), interface->getMac());
  EXPECT_EQ(oldInterface->getAddressesCopy(), interface->getAddressesCopy());
  EXPECT_EQ(
      oldInterface->getNdpConfig()->toThrift(),
      interface->getNdpConfig()->toThrift());

  // Change the NDP configuration
  config.interfaces()[1].ndp() = cfg::NdpConfig();
  *config.interfaces()[1].ndp()->routerAdvertisementSeconds() = 4;
  updateState();
  EXPECT_EQ(nodeID, interface->getNodeID());
  EXPECT_EQ(oldInterface->getGeneration() + 1, interface->getGeneration());
  EXPECT_EQ(oldInterface->getName(), interface->getName());
  EXPECT_EQ(oldInterface->getVlanID(), interface->getVlanID());
  EXPECT_EQ(oldInterface->getRouterID(), interface->getRouterID());
  EXPECT_EQ(oldInterface->getMac(), interface->getMac());
  EXPECT_EQ(
      oldInterface->getAddresses()->toThrift(),
      interface->getAddresses()->toThrift());
  EXPECT_NE(oldInterface->getNdpConfig(), interface->getNdpConfig());
  EXPECT_EQ(4, interface->routerAdvertisementSeconds());
  // Update the RA interval to 31 seconds
  *config.interfaces()[1].ndp()->routerAdvertisementSeconds() = 30;
  updateState();
  EXPECT_EQ(nodeID, interface->getNodeID());
  EXPECT_EQ(oldInterface->getGeneration() + 1, interface->getGeneration());
  EXPECT_NE(oldInterface->getNdpConfig(), interface->getNdpConfig());
  EXPECT_EQ(30, interface->routerAdvertisementSeconds());
  // Drop the NDP configuration
  config.interfaces()[1].ndp().reset();
  updateState();
  EXPECT_EQ(nodeID, interface->getNodeID());
  EXPECT_EQ(oldInterface->getGeneration() + 1, interface->getGeneration());
  EXPECT_NE(oldInterface->getNdpConfig(), interface->getNdpConfig());
  EXPECT_EQ(0, interface->routerAdvertisementSeconds());

  // Change DHCP relay configuration
  config.interfaces()[1].dhcpRelayAddressV4() = "30.1.1.2";
  config.interfaces()[1].dhcpRelayAddressV6() =
      "2a03:2880:10:1f07:face:b00c:0:2";
  updateState();
  EXPECT_EQ(folly::IPAddressV4("30.1.1.2"), interface->getDhcpV4Relay());
  EXPECT_EQ(
      folly::IPAddressV6("2a03:2880:10:1f07:face:b00c:0:2"),
      interface->getDhcpV6Relay());

  // Change DHCP relay override configuration
  config.interfaces()[1].dhcpRelayOverridesV4() = {};
  (*config.interfaces()[1].dhcpRelayOverridesV4())["02:00:00:00:00:02"] =
      "1.2.3.5";
  config.interfaces()[1].dhcpRelayOverridesV6() = {};
  (*config.interfaces()[1].dhcpRelayOverridesV6())["02:00:00:00:00:02"] =
      "2a03:2880:10:1f07:face:b00c:0:2";
  updateState();

  auto map44 = interface->getDhcpV4RelayOverrides();
  EXPECT_EQ(
      folly::IPAddressV4("1.2.3.5"),
      folly::IPAddressV4(map44[folly::MacAddress("02:00:00:00:00:02")]));
  auto map66 = interface->getDhcpV6RelayOverrides();
  EXPECT_EQ(
      folly::IPAddressV6("2a03:2880:10:1f07:face:b00c:0:2"),
      folly::IPAddressV6(map66[folly::MacAddress("02:00:00:00:00:02")]));

  // Changing the ID creates a new interface
  *config.interfaces()[0].intfID() = 2;
  config.interfaces()[0].name() = "newName";
  id = InterfaceID(2);
  updateState();
  // The generation number for the new interface will be 0
  EXPECT_NE(nodeID, interface->getNodeID());
  EXPECT_EQ(VlanID(2), interface->getVlanID());
  EXPECT_EQ(RouterID(0), interface->getRouterID());
  EXPECT_EQ("newName", interface->getName());
  EXPECT_EQ(MacAddress("00:02:00:11:22:33"), interface->getMac());
  validateThriftStructNodeSerialization(*interface);
}

/*
 * Test that forEachChanged(StateDelta::getIntfsDelta(), ...) invokes the
 * callback for the specified list of changed interfaces.
 */
void checkChangedIntfs(
    const shared_ptr<MultiSwitchInterfaceMap>& oldIntfs,
    const shared_ptr<MultiSwitchInterfaceMap>& newIntfs,
    const std::set<uint16_t> changedIDs,
    const std::set<uint16_t> addedIDs,
    const std::set<uint16_t> removedIDs) {
  auto oldState = make_shared<SwitchState>();
  auto newState = make_shared<SwitchState>();
  oldState->resetIntfs(oldIntfs);
  newState->resetIntfs(newIntfs);

  std::set<uint16_t> foundChanged;
  std::set<uint16_t> foundAdded;
  std::set<uint16_t> foundRemoved;
  StateDelta delta(oldState, newState);
  DeltaFunctions::forEachChanged(
      delta.getIntfsDelta(),
      [&](const shared_ptr<Interface>& oldIntf,
          const shared_ptr<Interface>& newIntf) {
        EXPECT_EQ(oldIntf->getID(), newIntf->getID());
        EXPECT_NE(oldIntf, newIntf);

        auto ret = foundChanged.insert(oldIntf->getID());
        EXPECT_TRUE(ret.second);
      },
      [&](const shared_ptr<Interface>& intf) {
        auto ret = foundAdded.insert(intf->getID());
        EXPECT_TRUE(ret.second);
      },
      [&](const shared_ptr<Interface>& intf) {
        auto ret = foundRemoved.insert(intf->getID());
        EXPECT_TRUE(ret.second);
      });

  EXPECT_EQ(changedIDs, foundChanged);
  EXPECT_EQ(addedIDs, foundAdded);
  EXPECT_EQ(removedIDs, foundRemoved);

  validateSerialization(utility::getFirstMap(oldIntfs));
  validateSerialization(utility::getFirstMap(newIntfs));
}

TEST(InterfaceMap, Modify) {
  {
    auto state = std::make_shared<SwitchState>();
    auto origIntfs = state->getInterfaces();
    EXPECT_EQ(origIntfs.get(), origIntfs->modify(&state));
    state->publish();
    EXPECT_NE(origIntfs.get(), origIntfs->modify(&state));
    EXPECT_NE(origIntfs.get(), state->getInterfaces().get());
  }
  {
    // Remote sys ports modify
    auto state = std::make_shared<SwitchState>();
    auto origRemoteIntfs = state->getRemoteInterfaces();
    EXPECT_EQ(origRemoteIntfs.get(), origRemoteIntfs->modify(&state));
    state->publish();
    EXPECT_NE(origRemoteIntfs.get(), origRemoteIntfs->modify(&state));
    EXPECT_NE(origRemoteIntfs.get(), state->getRemoteInterfaces().get());
  }
}
TEST(InterfaceMap, applyConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto intfsV0 = stateV0->getInterfaces();

  cfg::SwitchConfig config;
  config.vlans()->resize(2);
  *config.vlans()[0].id() = 1;
  config.vlans()[0].intfID() = 1;
  *config.vlans()[1].id() = 2;
  config.vlans()[1].intfID() = 2;
  config.interfaces()->resize(2);
  *config.interfaces()[0].intfID() = 1;
  *config.interfaces()[0].vlanID() = 1;
  config.interfaces()[0].mac() = "00:00:00:00:00:11";
  *config.interfaces()[1].intfID() = 2;
  *config.interfaces()[1].vlanID() = 2;
  config.interfaces()[1].mac() = "00:00:00:00:00:22";

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto intfsV1 = stateV1->getInterfaces();
  EXPECT_NE(intfsV1, intfsV0);
  EXPECT_EQ(2, intfsV1->numNodes());

  // verify interface intfID==1
  auto intf1 = intfsV1->getNode(InterfaceID(1));
  auto vlan1 = stateV1->getVlans()->getNodeIf(intf1->getVlanID());
  ASSERT_NE(nullptr, intf1);
  EXPECT_EQ(VlanID(1), intf1->getVlanID());
  EXPECT_EQ("00:00:00:00:00:11", intf1->getMac().toString());
  EXPECT_EQ(vlan1->getInterfaceID(), intf1->getID());
  checkChangedIntfs(intfsV0, intfsV1, {}, {1, 2}, {});

  // getInterface() should throw on a non-existent interface
  EXPECT_THROW(intfsV1->getNode(InterfaceID(99)), FbossError);
  // getInterfaceIf() should return nullptr on a non-existent interface
  EXPECT_EQ(nullptr, intfsV1->getNodeIf(InterfaceID(99)));

  // applying the same configure results in no change
  EXPECT_EQ(nullptr, publishAndApplyConfig(stateV1, &config, platform.get()));

  // adding some IP addresses
  config.interfaces()[1].ipAddresses()->resize(2);
  config.interfaces()[1].ipAddresses()[0] = "192.168.1.1/16";
  config.interfaces()[1].ipAddresses()[1] = "::1/48";
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  auto intfsV2 = stateV2->getInterfaces();
  EXPECT_NE(intfsV1, intfsV2);
  EXPECT_EQ(2, intfsV2->numNodes());
  auto intf2 = intfsV2->getNode(InterfaceID(2));
  EXPECT_EQ(3, intf2->getAddresses()->size()); // v6 link-local is added

  checkChangedIntfs(intfsV1, intfsV2, {2}, {}, {});

  // add a new interface and change 1
  config.vlans()->resize(3);
  *config.vlans()[2].id() = 3;
  config.vlans()[2].intfID() = 3;
  config.interfaces()[0].mac() = "00:00:00:00:00:33";
  config.interfaces()->resize(3);
  *config.interfaces()[2].intfID() = 3;
  *config.interfaces()[2].vlanID() = 3;
  config.interfaces()[2].mac() = "00:00:00:00:00:55";
  config.vlans()[2].intfID() = 3;

  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  ASSERT_NE(nullptr, stateV3);
  auto intfsV3 = stateV3->getInterfaces();
  EXPECT_NE(intfsV2, intfsV3);
  EXPECT_EQ(3, intfsV3->numNodes());
  auto intf3 = intfsV3->getNode(InterfaceID(3));
  EXPECT_EQ(1, intf3->getAddresses()->size());
  EXPECT_EQ(
      config.interfaces()[2].mac().value_or({}), intf3->getMac().toString());
  // intf 1 should not be there anymroe
  auto vlan3 = stateV3->getVlans()->getNodeIf(intf3->getVlanID());
  EXPECT_EQ(vlan3->getInterfaceID(), intf3->getID());
  auto newvlan1 = stateV3->getVlans()->getNodeIf(VlanID(1));

  checkChangedIntfs(intfsV2, intfsV3, {1}, {3}, {});

  // change the MTU
  config.interfaces()[0].mtu() = 1337;
  EXPECT_EQ(1500, intfsV3->getNode(InterfaceID(3))->getMtu());
  auto stateV4 = publishAndApplyConfig(stateV3, &config, platform.get());
  ASSERT_NE(nullptr, stateV4);
  auto intfsV4 = stateV4->getInterfaces();
  EXPECT_NE(intfsV3, intfsV4);
  EXPECT_EQ(1337, intfsV4->getNode(InterfaceID(1))->getMtu());
}

TEST(Interface, getLocalInterfacesBySwitchId) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
  auto config = testConfigA(cfg::SwitchType::VOQ);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto localSwitchId = kVoqSwitchIdBegin;
  auto myRif = stateV1->getInterfaces(SwitchID(localSwitchId));
  EXPECT_EQ(myRif->size(), stateV1->getInterfaces()->numNodes());
  // No remote sys ports
  EXPECT_EQ(stateV1->getInterfaces(SwitchID(localSwitchId + 1))->size(), 0);
}

TEST(Interface, getRemoteInterfacesBySwitchId) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
  auto config = testConfigA(cfg::SwitchType::VOQ);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto localSwitchId = kVoqSwitchIdBegin;
  auto myRif = stateV1->getInterfaces(SwitchID(localSwitchId));
  EXPECT_EQ(myRif->size(), stateV1->getInterfaces()->numNodes());
  int64_t remoteSwitchId = 100;
  auto sysPort1 = makeSysPort("olympic", 1001, remoteSwitchId);
  auto stateV2 = stateV1->clone();
  auto remoteSysPorts = stateV2->getRemoteSystemPorts()->modify(&stateV2);
  remoteSysPorts->addNode(
      sysPort1,
      HwSwitchMatcher(
          std::unordered_set<SwitchID>({SwitchID{kVoqSwitchIdBegin}})));
  auto remoteInterfaces = stateV2->getRemoteInterfaces()->modify(&stateV2);
  auto rif = std::make_shared<Interface>(
      InterfaceID(1001),
      RouterID(0),
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece("1001"),
      folly::MacAddress{},
      9000,
      false,
      false,
      cfg::InterfaceType::SYSTEM_PORT);

  remoteInterfaces->addNode(
      rif,
      HwSwitchMatcher(
          std::unordered_set<SwitchID>(
              {static_cast<SwitchID>(static_cast<uint16_t>(remoteSwitchId))})));

  EXPECT_EQ(stateV2->getInterfaces(SwitchID(remoteSwitchId))->size(), 1);
}

TEST(Interface, getInterfaceSysPortIDVoqSwitch) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
  auto config = testConfigA(cfg::SwitchType::VOQ);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto multiIntfs = stateV1->getInterfaces();
  auto intf = multiIntfs->cbegin()->second->cbegin()->second;
  EXPECT_TRUE(intf->getSystemPortID().has_value());
  EXPECT_EQ(
      static_cast<int64_t>(intf->getID()),
      static_cast<int64_t>(intf->getSystemPortID().value()));
}

TEST(Interface, getInterfaceSysPortID) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  auto config = testConfigA();
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto multiIntfs = stateV1->getInterfaces();
  auto intf = multiIntfs->cbegin()->second->cbegin()->second;
  EXPECT_FALSE(intf->getSystemPortID().has_value());
}

TEST(Interface, getInterfaceSysPortRangeVoqSwitch) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
  auto config = testConfigA(cfg::SwitchType::VOQ);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto multiIntfs = stateV1->getInterfaces();
  auto intf = multiIntfs->cbegin()->second->cbegin()->second;
  EXPECT_FALSE(stateV1->getAssociatedSystemPortRangesIf(intf->getID())
                   .systemPortRanges()
                   ->empty());
}

TEST(Interface, getInterfaceSysPortRange) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  auto config = testConfigA();
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto multiIntfs = stateV1->getInterfaces();
  auto intf = multiIntfs->cbegin()->second->cbegin()->second;
  EXPECT_TRUE(stateV1->getAssociatedSystemPortRangesIf(intf->getID())
                  .systemPortRanges()
                  ->empty());
}

TEST(Interface, getInterfacePortsVoqSwitch) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
  auto config = testConfigA(cfg::SwitchType::VOQ);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto multiIntfs = stateV1->getInterfaces();
  auto intf = multiIntfs->cbegin()->second->cbegin()->second;
  EXPECT_EQ(getPortsForInterface(intf->getID(), stateV1).size(), 1);
}

TEST(Interface, getVlanInterfacePorts) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  auto config = testConfigA();
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto multiIntfs = stateV1->getInterfaces();
  auto intf = multiIntfs->cbegin()->second->cbegin()->second;
  EXPECT_EQ(getPortsForInterface(intf->getID(), stateV1).size(), 11);
}

TEST(Interface, getPortInterfacePorts) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  auto config = testConfigAWithPortInterfaces();
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto multiIntfs = stateV1->getInterfaces();
  auto intf = multiIntfs->cbegin()->second->cbegin()->second;
  EXPECT_EQ(getPortsForInterface(intf->getID(), stateV1).size(), 1);
}

namespace {
std::shared_ptr<Interface> makePortTypeInterface() {
  return std::make_shared<Interface>(
      InterfaceID(1),
      RouterID(0),
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece("1"),
      MacAddress("00:02:00:00:00:01"),
      9000,
      false,
      false,
      cfg::InterfaceType::PORT);
}
} // namespace

TEST(Interface, portAndAggregatePortIdUnsetByDefault) {
  auto intf = makePortTypeInterface();
  EXPECT_EQ(intf->getPortIDf(), std::nullopt);
  EXPECT_EQ(intf->getAggregatePortIDf(), std::nullopt);
}

TEST(Interface, setPortID) {
  auto intf = makePortTypeInterface();
  intf->setPortID(PortID(7));
  EXPECT_EQ(intf->getPortIDf(), PortID(7));
  EXPECT_EQ(intf->getPortID(), PortID(7));
  // A port binding leaves no aggregate port binding behind.
  EXPECT_EQ(intf->getAggregatePortIDf(), std::nullopt);
}

TEST(Interface, setAggregatePortID) {
  auto intf = makePortTypeInterface();
  intf->setAggregatePortID(AggregatePortID(11));
  EXPECT_EQ(intf->getAggregatePortIDf(), AggregatePortID(11));
  EXPECT_EQ(intf->getAggregatePortID(), AggregatePortID(11));
  // An aggregate port binding leaves no port binding behind.
  EXPECT_EQ(intf->getPortIDf(), std::nullopt);
}

TEST(Interface, portAndAggregatePortIdAreMutuallyExclusive) {
  auto intf = makePortTypeInterface();

  intf->setPortID(PortID(7));
  intf->setAggregatePortID(AggregatePortID(11));
  EXPECT_EQ(intf->getAggregatePortIDf(), AggregatePortID(11));
  EXPECT_EQ(intf->getPortIDf(), std::nullopt);

  // and back the other way
  intf->setPortID(PortID(9));
  EXPECT_EQ(intf->getPortIDf(), PortID(9));
  EXPECT_EQ(intf->getAggregatePortIDf(), std::nullopt);
}

TEST(Interface, getPortIDDiesWhenBoundToAggregatePort) {
  auto intf = makePortTypeInterface();
  intf->setAggregatePortID(AggregatePortID(11));
  // getPortID() must fail loudly rather than return a stale or empty port.
  EXPECT_DEATH({ intf->getPortID(); }, "not bound to a physical port");
}

TEST(Interface, getAggregatePortIDDiesWhenBoundToPort) {
  auto intf = makePortTypeInterface();
  intf->setPortID(PortID(7));
  EXPECT_DEATH(
      { intf->getAggregatePortID(); }, "not bound to an aggregate port");
}

TEST(Interface, portAndAggregatePortIdInThrift) {
  auto portIntf = makePortTypeInterface();
  portIntf->setPortID(PortID(7));
  EXPECT_EQ(portIntf->toThrift().portId(), 7);
  EXPECT_FALSE(portIntf->toThrift().aggregatePortId().has_value());

  auto aggIntf = makePortTypeInterface();
  aggIntf->setAggregatePortID(AggregatePortID(11));
  EXPECT_EQ(aggIntf->toThrift().aggregatePortId(), 11);
  EXPECT_FALSE(aggIntf->toThrift().portId().has_value());
}

TEST(Interface, portAndAggregatePortIdSurviveThriftRoundTrip) {
  auto portIntf = makePortTypeInterface();
  portIntf->setPortID(PortID(7));
  auto aggIntf = makePortTypeInterface();
  aggIntf->setAggregatePortID(AggregatePortID(11));

  // Seed each target with the opposite binding, so the round trip has to
  // replace the binding rather than merely leave it alone.
  auto portRoundTripped = makePortTypeInterface();
  portRoundTripped->setAggregatePortID(AggregatePortID(11));
  portRoundTripped->fromThrift(portIntf->toThrift());
  EXPECT_EQ(portRoundTripped->getPortIDf(), PortID(7));
  EXPECT_EQ(portRoundTripped->getAggregatePortIDf(), std::nullopt);
  EXPECT_EQ(portRoundTripped->toThrift(), portIntf->toThrift());

  auto aggRoundTripped = makePortTypeInterface();
  aggRoundTripped->setPortID(PortID(7));
  aggRoundTripped->fromThrift(aggIntf->toThrift());
  EXPECT_EQ(aggRoundTripped->getAggregatePortIDf(), AggregatePortID(11));
  EXPECT_EQ(aggRoundTripped->getPortIDf(), std::nullopt);
  EXPECT_EQ(aggRoundTripped->toThrift(), aggIntf->toThrift());
}

namespace {
constexpr int32_t kAggIntfID = kAggregatePortInterfaceID;
constexpr int32_t kAggPortKey = kAggregatePortKey;
} // namespace

TEST(Interface, applyConfigWithAggregatePortInterface) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  auto config = testConfigAWithAggregatePortInterface();
  auto state = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, state);

  // The interface is bound to the aggregate port, not to a physical port.
  auto intf = state->getInterfaces()->getNode(InterfaceID(kAggIntfID));
  EXPECT_EQ(intf->getType(), cfg::InterfaceType::PORT);
  EXPECT_EQ(intf->getAggregatePortIDf(), AggregatePortID(kAggPortKey));
  EXPECT_EQ(intf->getPortIDf(), std::nullopt);

  // The aggregate port resolves to that interface ...
  auto aggPort =
      state->getAggregatePorts()->getNode(AggregatePortID(kAggPortKey));
  ASSERT_EQ(aggPort->getInterfaceIDs()->size(), 1);
  EXPECT_EQ(aggPort->getInterfaceIDs()->at(0)->cref(), kAggIntfID);
  EXPECT_EQ(
      state->getInterfaceIDForPort(
          PortDescriptor(AggregatePortID(kAggPortKey))),
      InterfaceID(kAggIntfID));

  // ... and so does every one of its member ports.
  std::set<PortID> memberPorts;
  for (const auto& subport : aggPort->sortedSubports()) {
    memberPorts.insert(subport.portID);
    auto port = state->getPorts()->getNode(subport.portID);
    EXPECT_EQ(port->getInterfaceID(), InterfaceID(kAggIntfID));
  }
  EXPECT_EQ(memberPorts.size(), 2);

  // Ports outside the aggregate keep their own port router interface.
  for (const auto& [_, portMap] : std::as_const(*state->getPorts())) {
    for (const auto& [_, port] : std::as_const(*portMap)) {
      if (memberPorts.count(port->getID())) {
        continue;
      }
      EXPECT_NE(port->getInterfaceID(), InterfaceID(kAggIntfID));
    }
  }
}

TEST(Interface, aggregatePortInterfaceRejectsSharedMemberPort) {
  auto platform = createMockPlatform();
  auto config = testConfigAWithAggregatePortInterface();

  // A second aggregate port sharing a member with the first, each with its own
  // router interface, binds that member port twice.
  auto sharedMember =
      *config.aggregatePorts()[0].memberPorts()[0].memberPortID();
  cfg::AggregatePortMember member;
  member.memberPortID() = sharedMember;
  cfg::AggregatePort aggPort2;
  aggPort2.key() = kAggPortKey + 1;
  aggPort2.name() = "agg2";
  aggPort2.memberPorts()->push_back(member);
  config.aggregatePorts()->push_back(aggPort2);

  auto aggIntf2 = config.interfaces()->back();
  aggIntf2.intfID() = kAggIntfID + 1;
  aggIntf2.aggregatePortID() = kAggPortKey + 1;
  aggIntf2.name() = "fbossAgg2";
  aggIntf2.ipAddresses()[0] = "2601:db00:2110:3101::1/64";
  aggIntf2.ipAddresses()[1] = "100.0.101.1/24";
  config.interfaces()->push_back(aggIntf2);

  EXPECT_THROW(
      publishAndApplyConfig(
          std::make_shared<SwitchState>(), &config, platform.get()),
      FbossError);
}

TEST(Interface, aggregatePortInterfaceNoChangeOnReapply) {
  auto platform = createMockPlatform();
  auto config = testConfigAWithAggregatePortInterface();
  auto stateV1 = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  stateV1->publish();

  // Reapplying the same config is a no op, i.e. the aggregate port binding
  // participates in the no change check rather than forcing an update.
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_EQ(nullptr, stateV2);
}

TEST(Interface, aggregatePortInterfaceUpdatePreservesBinding) {
  auto platform = createMockPlatform();
  auto config = testConfigAWithAggregatePortInterface();
  auto stateV1 = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  stateV1->publish();

  // Change something unrelated so the interface takes the update path rather
  // than being created, and confirm the binding survives it.
  config.interfaces()->back().mtu() = 1500;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);

  auto intf = stateV2->getInterfaces()->getNode(InterfaceID(kAggIntfID));
  EXPECT_EQ(intf->getMtu(), 1500);
  EXPECT_EQ(intf->getAggregatePortIDf(), AggregatePortID(kAggPortKey));
  EXPECT_EQ(intf->getPortIDf(), std::nullopt);
}

namespace {
struct IntfDelta {
  std::set<uint16_t> changed;
  std::set<uint16_t> added;
  std::set<uint16_t> removed;
};

IntfDelta collectIntfDelta(
    const shared_ptr<SwitchState>& oldState,
    const shared_ptr<SwitchState>& newState) {
  IntfDelta out;
  StateDelta delta(oldState, newState);
  DeltaFunctions::forEachChanged(
      delta.getIntfsDelta(),
      [&](const shared_ptr<Interface>& oldIntf,
          const shared_ptr<Interface>& newIntf) {
        EXPECT_EQ(oldIntf->getID(), newIntf->getID());
        out.changed.insert(oldIntf->getID());
      },
      [&](const shared_ptr<Interface>& intf) {
        out.added.insert(intf->getID());
      },
      [&](const shared_ptr<Interface>& intf) {
        out.removed.insert(intf->getID());
      });
  return out;
}

// Replace the port interfaces of the first four ports with two aggregate
// ports, AP1 over the first two and AP2 over the next two. aggIntfIDs gives
// the interface id to use for each aggregate; reusing two of the original
// interface ids exercises rebinding rather than replacement.
cfg::SwitchConfig aggregatePortConfigOverFirstFourPorts(
    const std::array<int32_t, 2>& aggIntfIDs) {
  auto config = testConfigAWithPortInterfaces();
  std::array<int32_t, 4> ports{
      *config.ports()[0].logicalID(),
      *config.ports()[1].logicalID(),
      *config.ports()[2].logicalID(),
      *config.ports()[3].logicalID()};

  auto& intfs = *config.interfaces();
  intfs.erase(
      std::remove_if(
          intfs.begin(),
          intfs.end(),
          [&ports](const auto& intf) {
            return intf.portID().has_value() &&
                std::find(ports.begin(), ports.end(), *intf.portID()) !=
                ports.end();
          }),
      intfs.end());

  for (int i = 0; i < 2; ++i) {
    cfg::AggregatePort aggPort;
    aggPort.key() = kAggPortKey + i;
    aggPort.name() = fmt::format("agg{}", i);
    for (int j = 0; j < 2; ++j) {
      cfg::AggregatePortMember member;
      member.memberPortID() = ports[2 * i + j];
      aggPort.memberPorts()->push_back(member);
    }
    config.aggregatePorts()->push_back(aggPort);

    cfg::Interface aggIntf;
    aggIntf.intfID() = aggIntfIDs[i];
    aggIntf.vlanID() = 0;
    aggIntf.aggregatePortID() = kAggPortKey + i;
    aggIntf.routerID() = 0;
    aggIntf.type() = cfg::InterfaceType::PORT;
    aggIntf.name() = fmt::format("fbossAgg{}", i);
    aggIntf.mtu() = 9000;
    aggIntf.mac() = fmt::format("00:02:00:00:00:{:02x}", 0x80 + i);
    aggIntf.ipAddresses()->resize(2);
    aggIntf.ipAddresses()[0] = fmt::format("2601:db00:2110:32{:02x}::1/64", i);
    aggIntf.ipAddresses()[1] = fmt::format("100.0.{}.1/24", 200 + i);
    intfs.push_back(aggIntf);
  }
  return config;
}
} // namespace

TEST(Interface, portRifsReplacedByAggregatePortRifs) {
  auto platform = createMockPlatform();
  auto portConfig = testConfigAWithPortInterfaces();
  auto stateV1 = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &portConfig, platform.get());
  ASSERT_NE(nullptr, stateV1);
  stateV1->publish();

  std::array<int32_t, 4> ports{
      *portConfig.ports()[0].logicalID(),
      *portConfig.ports()[1].logicalID(),
      *portConfig.ports()[2].logicalID(),
      *portConfig.ports()[3].logicalID()};

  // Brand new interface ids for the aggregate rifs, so the four port rifs are
  // replaced outright rather than rebound.
  auto aggConfig = aggregatePortConfigOverFirstFourPorts({6500, 6501});
  auto stateV2 = publishAndApplyConfig(stateV1, &aggConfig, platform.get());
  ASSERT_NE(nullptr, stateV2);

  auto delta = collectIntfDelta(stateV1, stateV2);
  std::set<uint16_t> expectedRemoved;
  for (auto port : ports) {
    expectedRemoved.insert(6000 + port);
  }
  EXPECT_EQ(delta.removed, expectedRemoved);
  EXPECT_EQ(delta.added, (std::set<uint16_t>{6500, 6501}));
  EXPECT_TRUE(delta.changed.empty());

  // Each member port now resolves to its aggregate's interface.
  for (int i = 0; i < 2; ++i) {
    auto intfID = InterfaceID(6500 + i);
    for (int j = 0; j < 2; ++j) {
      auto port = stateV2->getPorts()->getNode(PortID(ports[2 * i + j]));
      EXPECT_EQ(port->getInterfaceID(), intfID);
    }
  }
}

TEST(Interface, portRifsReboundToAggregatePortRifs) {
  auto platform = createMockPlatform();
  auto portConfig = testConfigAWithPortInterfaces();
  auto stateV1 = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &portConfig, platform.get());
  ASSERT_NE(nullptr, stateV1);
  stateV1->publish();

  std::array<int32_t, 4> ports{
      *portConfig.ports()[0].logicalID(),
      *portConfig.ports()[1].logicalID(),
      *portConfig.ports()[2].logicalID(),
      *portConfig.ports()[3].logicalID()};
  int32_t r1 = 6000 + ports[0];
  int32_t r2 = 6000 + ports[1];
  int32_t r3 = 6000 + ports[2];
  int32_t r4 = 6000 + ports[3];

  // Reuse r1 and r2 as the aggregate rifs. They are rebound from a physical
  // port to an aggregate port, so they must show up as changed, while r3 and
  // r4 disappear.
  auto aggConfig = aggregatePortConfigOverFirstFourPorts({r1, r2});
  auto stateV2 = publishAndApplyConfig(stateV1, &aggConfig, platform.get());
  ASSERT_NE(nullptr, stateV2);

  auto delta = collectIntfDelta(stateV1, stateV2);
  EXPECT_EQ(delta.changed, (std::set<uint16_t>{uint16_t(r1), uint16_t(r2)}));
  EXPECT_EQ(delta.removed, (std::set<uint16_t>{uint16_t(r3), uint16_t(r4)}));
  EXPECT_TRUE(delta.added.empty());

  // The rebound interfaces now carry an aggregate port binding, not a port
  // binding.
  for (int i = 0; i < 2; ++i) {
    auto intf =
        stateV2->getInterfaces()->getNode(InterfaceID(i == 0 ? r1 : r2));
    EXPECT_EQ(intf->getAggregatePortIDf(), AggregatePortID(kAggPortKey + i));
    EXPECT_EQ(intf->getPortIDf(), std::nullopt);
  }

  // Both members of each aggregate resolve to the rebound interface.
  for (int i = 0; i < 2; ++i) {
    auto intfID = InterfaceID(i == 0 ? r1 : r2);
    for (int j = 0; j < 2; ++j) {
      auto port = stateV2->getPorts()->getNode(PortID(ports[2 * i + j]));
      EXPECT_EQ(port->getInterfaceID(), intfID);
    }
  }
}

TEST(Interface, aggregatePortRifsReplacedByPortRifs) {
  auto platform = createMockPlatform();
  auto aggConfig = aggregatePortConfigOverFirstFourPorts({6500, 6501});
  auto stateV1 = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &aggConfig, platform.get());
  ASSERT_NE(nullptr, stateV1);
  stateV1->publish();

  // Unlink the aggregate ports: go back to one port rif per port.
  auto portConfig = testConfigAWithPortInterfaces();
  auto stateV2 = publishAndApplyConfig(stateV1, &portConfig, platform.get());
  ASSERT_NE(nullptr, stateV2);

  std::array<int32_t, 4> ports{
      *portConfig.ports()[0].logicalID(),
      *portConfig.ports()[1].logicalID(),
      *portConfig.ports()[2].logicalID(),
      *portConfig.ports()[3].logicalID()};

  auto delta = collectIntfDelta(stateV1, stateV2);
  EXPECT_EQ(delta.removed, (std::set<uint16_t>{6500, 6501}));
  std::set<uint16_t> expectedAdded;
  for (auto port : ports) {
    expectedAdded.insert(6000 + port);
  }
  EXPECT_EQ(delta.added, expectedAdded);
  EXPECT_TRUE(delta.changed.empty());

  // Every port is back on its own port rif, and no aggregate ports remain.
  for (auto port : ports) {
    auto swPort = stateV2->getPorts()->getNode(PortID(port));
    EXPECT_EQ(swPort->getInterfaceID(), InterfaceID(6000 + port));
    auto intf = stateV2->getInterfaces()->getNode(InterfaceID(6000 + port));
    EXPECT_EQ(intf->getPortIDf(), PortID(port));
    EXPECT_EQ(intf->getAggregatePortIDf(), std::nullopt);
  }
  EXPECT_EQ(stateV2->getAggregatePorts()->numNodes(), 0);
}

TEST(Interface, aggregatePortRifsReboundToPortRifs) {
  auto platform = createMockPlatform();
  auto portConfig = testConfigAWithPortInterfaces();
  std::array<int32_t, 4> ports{
      *portConfig.ports()[0].logicalID(),
      *portConfig.ports()[1].logicalID(),
      *portConfig.ports()[2].logicalID(),
      *portConfig.ports()[3].logicalID()};
  int32_t r1 = 6000 + ports[0];
  int32_t r2 = 6000 + ports[1];
  int32_t r3 = 6000 + ports[2];
  int32_t r4 = 6000 + ports[3];

  auto aggConfig = aggregatePortConfigOverFirstFourPorts({r1, r2});
  auto stateV1 = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &aggConfig, platform.get());
  ASSERT_NE(nullptr, stateV1);
  stateV1->publish();

  // Unlinking back to port rifs rebinds r1 and r2 from aggregate to port, and
  // brings r3 and r4 back.
  auto stateV2 = publishAndApplyConfig(stateV1, &portConfig, platform.get());
  ASSERT_NE(nullptr, stateV2);

  auto delta = collectIntfDelta(stateV1, stateV2);
  EXPECT_EQ(delta.changed, (std::set<uint16_t>{uint16_t(r1), uint16_t(r2)}));
  EXPECT_EQ(delta.added, (std::set<uint16_t>{uint16_t(r3), uint16_t(r4)}));
  EXPECT_TRUE(delta.removed.empty());

  for (auto port : ports) {
    auto intf = stateV2->getInterfaces()->getNode(InterfaceID(6000 + port));
    EXPECT_EQ(intf->getPortIDf(), PortID(port));
    EXPECT_EQ(intf->getAggregatePortIDf(), std::nullopt);
  }
}

TEST(Interface, aggregatePortInterfaceRejectsEmptyAggregatePort) {
  auto platform = createMockPlatform();
  auto config = testConfigAWithAggregatePortInterface();
  // An aggregate port with no members binds no ports, so an interface over it
  // is meaningless.
  config.aggregatePorts()[0].memberPorts()->clear();
  EXPECT_THROW(
      publishAndApplyConfig(
          std::make_shared<SwitchState>(), &config, platform.get()),
      FbossError);
}

TEST(Interface, vlanInterfaceRejectsPortBindings) {
  auto platform = createMockPlatform();

  auto withAggPort = testConfigA(cfg::SwitchType::NPU);
  withAggPort.interfaces()[0].aggregatePortID() = kAggPortKey;
  EXPECT_THROW(
      publishAndApplyConfig(
          std::make_shared<SwitchState>(), &withAggPort, platform.get()),
      FbossError);

  auto withPort = testConfigA(cfg::SwitchType::NPU);
  withPort.interfaces()[0].portID() = 1;
  EXPECT_THROW(
      publishAndApplyConfig(
          std::make_shared<SwitchState>(), &withPort, platform.get()),
      FbossError);
}

TEST(Interface, aggregatePortInterfaceRejectsMemberPortWithOwnInterface) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  auto config = testConfigAWithAggregatePortInterface();

  // A member port of an aggregate port cannot also carry a standalone router
  // interface of its own.
  cfg::Interface memberIntf;
  memberIntf.intfID() = 6999;
  memberIntf.vlanID() = 0;
  memberIntf.portID() =
      *config.aggregatePorts()[0].memberPorts()[0].memberPortID();
  memberIntf.routerID() = 0;
  memberIntf.type() = cfg::InterfaceType::PORT;
  memberIntf.name() = "fboss6999";
  memberIntf.mtu() = 9000;
  memberIntf.mac() = "00:02:00:00:00:77";
  config.interfaces()->push_back(memberIntf);

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(Interface, aggregatePortInterfaceRejectsUnknownAggregatePort) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  auto config = testConfigAWithAggregatePortInterface();
  config.interfaces()->back().aggregatePortID() = 99;
  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(Interface, portInterfaceRequiresExactlyOneBinding) {
  auto platform = createMockPlatform();

  // Both bindings set.
  auto bothSet = testConfigAWithAggregatePortInterface();
  bothSet.interfaces()->back().portID() =
      *bothSet.aggregatePorts()[0].memberPorts()[0].memberPortID();
  EXPECT_THROW(
      publishAndApplyConfig(
          std::make_shared<SwitchState>(), &bothSet, platform.get()),
      FbossError);

  // Neither binding set.
  auto neitherSet = testConfigAWithAggregatePortInterface();
  neitherSet.interfaces()->back().aggregatePortID().reset();
  EXPECT_THROW(
      publishAndApplyConfig(
          std::make_shared<SwitchState>(), &neitherSet, platform.get()),
      FbossError);
}

TEST(Interface, modify) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  auto config = testConfigA();
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto multiIntfs = stateV1->getInterfaces();
  auto intf = multiIntfs->cbegin()->second->cbegin()->second;
  auto intfModified = intf->modify(&stateV1);
  EXPECT_EQ(intf.get(), intfModified);
  auto scope = multiIntfs->getNodeAndScope(intf->getID()).second;
  auto intfMap = multiIntfs->getMapNodeIf(scope);
  multiIntfs->publish();
  intfMap->publish();
  intf->publish();
  intfModified = intf->modify(&stateV1);
  EXPECT_NE(intf.get(), intfModified);
  EXPECT_NE(stateV1->getInterfaces()->getNode(intf->getID()), intf);
  EXPECT_NE(stateV1->getInterfaces()->getMapNodeIf(scope), intfMap);
  EXPECT_NE(stateV1->getInterfaces(), multiIntfs);
  auto oldMtu = intfModified->getMtu();
  auto newMtu = oldMtu + 1000;
  intfModified->setMtu(newMtu);
  EXPECT_EQ(stateV1->getInterfaces()->getNode(intf->getID())->getMtu(), newMtu);
}

TEST(Interface, getAllNodes) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  auto config = testConfigA();
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_EQ(
      *stateV1->getInterfaces()->getAllNodes(),
      *utility::getFirstMap(stateV1->getInterfaces()));
}

TEST(Interface, getRemoteInterfaceType) {
  auto platform = createMockPlatform();
  auto stateV0 = std::make_shared<SwitchState>();
  addSwitchInfo(stateV0, cfg::SwitchType::VOQ, kVoqSwitchIdBegin /* switchId*/);
  auto config = testConfigA(cfg::SwitchType::VOQ);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);

  config.dsfNodes()->insert({5, makeDsfNodeCfg(5)});
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);

  auto verifyRemoteInterfaceTypeHelper =
      [](const std::shared_ptr<MultiSwitchInterfaceMap>& intfs,
         const std::optional<RemoteInterfaceType>& expectedType) {
        EXPECT_GT(intfs->size(), 0);
        for (const auto& [_, intfMap] : std::as_const(*intfs)) {
          for (const auto& [_, intf] : std::as_const(*intfMap)) {
            EXPECT_EQ(intf->getRemoteInterfaceType(), expectedType);
          }
        }
      };

  // Local interfaces don't have remoteInterfaceType
  verifyRemoteInterfaceTypeHelper(
      stateV1->getInterfaces(),
      std::optional<RemoteInterfaceType>(std::nullopt));

  // Only statically programmed remote interfaces should be present given the
  // config applied.
  verifyRemoteInterfaceTypeHelper(
      stateV2->getRemoteInterfaces(),
      std::optional<RemoteInterfaceType>(RemoteInterfaceType::STATIC_ENTRY));
}
