// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/rib/MySidConfigUtils.h"

#include <set>

using namespace facebook::fboss;

namespace {

cfg::MySidConfig makeDecapAndAdjacencyConfig(
    const std::vector<std::string>& locatorPrefixes) {
  cfg::MySidConfig config;
  config.locatorPrefix() = "3001:db8::/32";
  config.locatorPrefixes() = locatorPrefixes;
  config.entries()[0x7fff].decap() = cfg::DecapMySidConfig{};
  cfg::AdjacencyMySidConfig adjConfig;
  adjConfig.portName() = "eth1/1/1";
  config.entries()[1].adjacency() = adjConfig;
  return config;
}

std::set<std::string> mySidIds(const std::vector<MySidWithNextHops>& entries) {
  std::set<std::string> ids;
  for (const auto& entry : entries) {
    ids.insert(entry.mySid->getID());
  }
  return ids;
}

const std::unordered_map<std::string, InterfaceID> kPortNameToIntfId{
    {"eth1/1/1", InterfaceID(2001)}};

} // namespace

TEST(MySidConfigUtilsTest, BuildSidAddress) {
  auto locatorAddr = folly::IPAddressV6("3001:db8::");
  // Function ID "ffff" at offset 32 bits
  auto sid = buildSidAddress(locatorAddr, 32, "ffff");
  EXPECT_EQ(sid, folly::IPAddressV6("3001:db8:ffff::"));

  // Function ID "1"
  auto sid2 = buildSidAddress(locatorAddr, 32, "1");
  EXPECT_EQ(sid2, folly::IPAddressV6("3001:db8:1::"));

  // Function ID "7fff"
  auto sid3 = buildSidAddress(locatorAddr, 32, "7fff");
  EXPECT_EQ(sid3, folly::IPAddressV6("3001:db8:7fff::"));
}

TEST(MySidConfigUtilsTest, BuildSidAddressInvalidFunctionId) {
  auto locatorAddr = folly::IPAddressV6("3001:db8::");
  EXPECT_THROW(buildSidAddress(locatorAddr, 32, "zzz"), FbossError);
}

TEST(MySidConfigUtilsTest, BuildPortNameMapPhysicalPortVlanInterface) {
  cfg::SwitchConfig config;
  cfg::Port port;
  port.logicalID() = 1;
  port.name() = "eth2/5/1";
  port.ingressVlan() = 2001;
  config.ports()->push_back(port);

  cfg::Interface intf;
  intf.intfID() = 5001;
  intf.type() = cfg::InterfaceType::VLAN;
  intf.vlanID() = 2001;
  config.interfaces()->push_back(intf);

  auto portMap = buildPortNameToInterfaceIdMap(config);
  ASSERT_EQ(portMap.count("eth2/5/1"), 1);
  EXPECT_EQ(portMap.at("eth2/5/1"), InterfaceID(5001));
}

TEST(MySidConfigUtilsTest, BuildPortNameMapPhysicalPortPortInterface) {
  // Port-routed (e.g. Chenab) — Interface has type=PORT and portID matches
  // the physical port's logicalID; ingressVlan does not map to an interface.
  cfg::SwitchConfig config;
  cfg::Port port;
  port.logicalID() = 42;
  port.name() = "eth1/1/1";
  port.ingressVlan() = 0;
  config.ports()->push_back(port);

  cfg::Interface intf;
  intf.intfID() = 6000;
  intf.type() = cfg::InterfaceType::PORT;
  intf.portID() = 42;
  config.interfaces()->push_back(intf);

  auto portMap = buildPortNameToInterfaceIdMap(config);
  ASSERT_EQ(portMap.count("eth1/1/1"), 1);
  EXPECT_EQ(portMap.at("eth1/1/1"), InterfaceID(6000));
}

TEST(MySidConfigUtilsTest, BuildPortNameMapPhysicalPortMissingNameThrows) {
  // A resolvable physical port with no name set is a config bug — throw
  // (config errors must be recoverable; CHECK would crash the agent on
  // config reload).
  cfg::SwitchConfig config;
  cfg::Port port;
  port.logicalID() = 7;
  port.ingressVlan() = 1234;
  config.ports()->push_back(port);

  cfg::Interface intf;
  intf.intfID() = 5500;
  intf.type() = cfg::InterfaceType::VLAN;
  intf.vlanID() = 1234;
  config.interfaces()->push_back(intf);

  EXPECT_THROW(buildPortNameToInterfaceIdMap(config), FbossError);
}

TEST(MySidConfigUtilsTest, BuildPortNameMapPhysicalPortNoInterface) {
  // Physical port with no matching L3 Interface — should be omitted.
  cfg::SwitchConfig config;
  cfg::Port port;
  port.logicalID() = 1;
  port.name() = "eth2/5/1";
  port.ingressVlan() = 2001;
  config.ports()->push_back(port);

  auto portMap = buildPortNameToInterfaceIdMap(config);
  EXPECT_EQ(portMap.count("eth2/5/1"), 0);
}

TEST(MySidConfigUtilsTest, BuildPortNameMapPhysicalPortPrefersPortOverVlan) {
  // Both a PORT-typed Interface (matching logicalID) and a VLAN-typed
  // Interface (matching ingressVlan) are configured for the same port.
  // PORT-typed must win — it is the more specific binding.
  cfg::SwitchConfig config;
  cfg::Port port;
  port.logicalID() = 1;
  port.name() = "eth2/5/1";
  port.ingressVlan() = 2001;
  config.ports()->push_back(port);

  cfg::Interface vlanIntf;
  vlanIntf.intfID() = 2001;
  vlanIntf.type() = cfg::InterfaceType::VLAN;
  vlanIntf.vlanID() = 2001;
  config.interfaces()->push_back(vlanIntf);

  cfg::Interface portIntf;
  portIntf.intfID() = 6000;
  portIntf.type() = cfg::InterfaceType::PORT;
  portIntf.portID() = 1;
  config.interfaces()->push_back(portIntf);

  auto portMap = buildPortNameToInterfaceIdMap(config);
  ASSERT_EQ(portMap.count("eth2/5/1"), 1);
  EXPECT_EQ(portMap.at("eth2/5/1"), InterfaceID(6000));
}

TEST(MySidConfigUtilsTest, BuildPortNameMapAggregatePortNoInterface) {
  // Aggregate port with no member ports — should be omitted (nothing to
  // inherit from).
  cfg::SwitchConfig config;
  cfg::AggregatePort aggPort;
  aggPort.key() = 301;
  aggPort.name() = "Port-Channel301";
  config.aggregatePorts()->push_back(aggPort);

  auto portMap = buildPortNameToInterfaceIdMap(config);
  EXPECT_EQ(portMap.count("Port-Channel301"), 0);
}

TEST(
    MySidConfigUtilsTest,
    BuildPortNameMapAggregatePortIgnoresInterfacePortIdMatchingKey) {
  // Even if a PORT-typed Interface happens to share a portID equal to the
  // aggregate's key, that is NOT a valid binding — Interface.portID always
  // refers to a physical Port.logicalID, never to an AggregatePort.key.
  // The aggregate must inherit from an actual member port.
  cfg::SwitchConfig config;
  cfg::AggregatePort aggPort;
  aggPort.key() = 301;
  aggPort.name() = "Port-Channel301";
  // No member ports → nothing to inherit from.
  config.aggregatePorts()->push_back(aggPort);

  cfg::Interface intf;
  intf.intfID() = 4001;
  intf.type() = cfg::InterfaceType::PORT;
  intf.portID() = 301;
  config.interfaces()->push_back(intf);

  auto portMap = buildPortNameToInterfaceIdMap(config);
  EXPECT_EQ(portMap.count("Port-Channel301"), 0);
}

TEST(MySidConfigUtilsTest, BuildPortNameMapAggregatePortViaMemberVlan) {
  // Production setup: aggregate port has no dedicated Interface and
  // inherits the L3 interface from its member port's VLAN.
  cfg::SwitchConfig config;
  cfg::Port memberPort;
  memberPort.logicalID() = 1;
  memberPort.name() = "eth1/1/1";
  memberPort.ingressVlan() = 2001;
  config.ports()->push_back(memberPort);

  cfg::AggregatePort aggPort;
  aggPort.key() = 1707;
  aggPort.name() = "Port-Channel100011";
  cfg::AggregatePortMember member;
  member.memberPortID() = 1;
  aggPort.memberPorts()->push_back(member);
  config.aggregatePorts()->push_back(aggPort);

  cfg::Interface intf;
  intf.intfID() = 2001;
  intf.type() = cfg::InterfaceType::VLAN;
  intf.vlanID() = 2001;
  config.interfaces()->push_back(intf);

  auto portMap = buildPortNameToInterfaceIdMap(config);
  ASSERT_EQ(portMap.count("Port-Channel100011"), 1);
  EXPECT_EQ(portMap.at("Port-Channel100011"), InterfaceID(2001));
  // Member port itself is also resolved.
  EXPECT_EQ(portMap.at("eth1/1/1"), InterfaceID(2001));
}

TEST(
    MySidConfigUtilsTest,
    BuildPortNameMapAggregatePortMultipleMembersSameInterface) {
  // Multiple member ports sharing the same VLAN-typed L3 Interface — the
  // aggregate inherits that interface.
  cfg::SwitchConfig config;
  for (int32_t logicalID : {1, 2, 3}) {
    cfg::Port memberPort;
    memberPort.logicalID() = logicalID;
    memberPort.name() = folly::to<std::string>("eth1/", logicalID, "/1");
    memberPort.ingressVlan() = 2001;
    config.ports()->push_back(memberPort);
  }

  cfg::AggregatePort aggPort;
  aggPort.key() = 1707;
  aggPort.name() = "Port-Channel100011";
  for (int32_t memberId : {1, 2, 3}) {
    cfg::AggregatePortMember member;
    member.memberPortID() = memberId;
    aggPort.memberPorts()->push_back(member);
  }
  config.aggregatePorts()->push_back(aggPort);

  cfg::Interface intf;
  intf.intfID() = 2001;
  intf.type() = cfg::InterfaceType::VLAN;
  intf.vlanID() = 2001;
  config.interfaces()->push_back(intf);

  auto portMap = buildPortNameToInterfaceIdMap(config);
  ASSERT_EQ(portMap.count("Port-Channel100011"), 1);
  EXPECT_EQ(portMap.at("Port-Channel100011"), InterfaceID(2001));
}

TEST(
    MySidConfigUtilsTest,
    BuildPortNameMapAggregatePortMembersOnDifferentInterfacesThrows) {
  // Misconfigured aggregate: members resolve to different L3 interfaces.
  // Throws (config errors must be recoverable; silently picking one would
  // mask the bug).
  cfg::SwitchConfig config;
  cfg::Port port1;
  port1.logicalID() = 1;
  port1.name() = "eth1/1/1";
  port1.ingressVlan() = 2001;
  config.ports()->push_back(port1);

  cfg::Port port2;
  port2.logicalID() = 2;
  port2.name() = "eth1/2/1";
  port2.ingressVlan() = 2002;
  config.ports()->push_back(port2);

  cfg::AggregatePort aggPort;
  aggPort.key() = 1707;
  aggPort.name() = "Port-Channel100011";
  for (int32_t memberId : {1, 2}) {
    cfg::AggregatePortMember member;
    member.memberPortID() = memberId;
    aggPort.memberPorts()->push_back(member);
  }
  config.aggregatePorts()->push_back(aggPort);

  cfg::Interface intf1;
  intf1.intfID() = 2001;
  intf1.type() = cfg::InterfaceType::VLAN;
  intf1.vlanID() = 2001;
  config.interfaces()->push_back(intf1);

  cfg::Interface intf2;
  intf2.intfID() = 2002;
  intf2.type() = cfg::InterfaceType::VLAN;
  intf2.vlanID() = 2002;
  config.interfaces()->push_back(intf2);

  EXPECT_THROW(buildPortNameToInterfaceIdMap(config), FbossError);
}

TEST(MySidConfigUtilsTest, BuildPortNameMapEmpty) {
  cfg::SwitchConfig config;
  auto portMap = buildPortNameToInterfaceIdMap(config);
  EXPECT_TRUE(portMap.empty());
}

TEST(MySidConfigUtilsTest, ConvertDecapConfig) {
  cfg::MySidConfig config;
  config.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entryConfig;
  cfg::DecapMySidConfig decapConfig;
  entryConfig.decap() = decapConfig;
  config.entries()[0x7fff] = entryConfig;

  auto result = convertMySidConfig(config, /*portNameToInterfaceId*/ {});
  ASSERT_EQ(result.size(), 1);
  const auto& entry = result[0];
  EXPECT_EQ(entry.mySid->getType(), MySidType::DECAPSULATE_AND_LOOKUP);
  EXPECT_TRUE(entry.nextHopSet.empty());
  EXPECT_EQ(entry.mySid->getClientId(), ClientID::STATIC_ROUTE);
  EXPECT_FALSE(entry.mySid->getAdjacencyInterfaceId().has_value());

  // Verify SID address
  auto cidr = entry.mySid->getMySid();
  EXPECT_EQ(cidr.first, folly::IPAddress("3001:db8:7fff::"));
  EXPECT_EQ(cidr.second, 48);
}

TEST(MySidConfigUtilsTest, ConvertNodeConfig) {
  cfg::MySidConfig config;
  config.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entryConfig;
  cfg::NodeMySidConfig nodeConfig;
  nodeConfig.nodeAddress() = "fc00:100::1";
  entryConfig.node() = nodeConfig;
  config.entries()[3] = entryConfig;

  auto result = convertMySidConfig(config, /*portNameToInterfaceId*/ {});
  ASSERT_EQ(result.size(), 1);
  const auto& entry = result[0];
  EXPECT_EQ(entry.mySid->getType(), MySidType::NODE_MICRO_SID);
  EXPECT_EQ(entry.mySid->getClientId(), ClientID::STATIC_ROUTE);
  EXPECT_FALSE(entry.mySid->getAdjacencyInterfaceId().has_value());

  // Verify unresolved next hops contain the node address
  ASSERT_EQ(entry.nextHopSet.size(), 1);
  auto nhop = *entry.nextHopSet.begin();
  EXPECT_EQ(nhop.addr(), folly::IPAddress("fc00:100::1"));
}

TEST(MySidConfigUtilsTest, ConvertAdjacencyConfig) {
  std::unordered_map<std::string, InterfaceID> portNameToIntfId;
  portNameToIntfId["eth1/1/1"] = InterfaceID(2001);

  cfg::MySidConfig config;
  config.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entryConfig;
  cfg::AdjacencyMySidConfig adjConfig;
  adjConfig.portName() = "eth1/1/1";
  entryConfig.adjacency() = adjConfig;
  config.entries()[1] = entryConfig;

  auto result = convertMySidConfig(config, portNameToIntfId);
  ASSERT_EQ(result.size(), 1);
  const auto& entry = result[0];
  EXPECT_EQ(entry.mySid->getType(), MySidType::ADJACENCY_MICRO_SID);
  EXPECT_EQ(entry.mySid->getClientId(), ClientID::STATIC_ROUTE);
  // No unresolved next hops — resolved later via adjacencyInterfaceId
  EXPECT_TRUE(entry.nextHopSet.empty());
  // InterfaceID should be stored on the MySid object
  ASSERT_TRUE(entry.mySid->getAdjacencyInterfaceId().has_value());
  EXPECT_EQ(entry.mySid->getAdjacencyInterfaceId().value(), 2001);
}

TEST(MySidConfigUtilsTest, ConvertAdjacencyConfigUnknownPortThrows) {
  cfg::MySidConfig config;
  config.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entryConfig;
  cfg::AdjacencyMySidConfig adjConfig;
  adjConfig.portName() = "missing-port";
  entryConfig.adjacency() = adjConfig;
  config.entries()[1] = entryConfig;

  EXPECT_THROW(
      convertMySidConfig(config, /*portNameToInterfaceId*/ {}), FbossError);
}

TEST(MySidConfigUtilsTest, ConvertMixedConfig) {
  std::unordered_map<std::string, InterfaceID> portNameToIntfId;
  portNameToIntfId["eth1/1/1"] = InterfaceID(2001);
  portNameToIntfId["Port-Channel301"] = InterfaceID(301);

  cfg::MySidConfig config;
  config.locatorPrefix() = "3001:db8::/32";

  // Decap entry
  cfg::MySidEntryConfig decapEntry;
  decapEntry.decap() = cfg::DecapMySidConfig{};
  config.entries()[0x7fff] = decapEntry;

  // Adjacency via physical port
  cfg::MySidEntryConfig adjEntry;
  cfg::AdjacencyMySidConfig adjConfig;
  adjConfig.portName() = "eth1/1/1";
  adjEntry.adjacency() = adjConfig;
  config.entries()[1] = adjEntry;

  // Adjacency via LAG
  cfg::MySidEntryConfig lagEntry;
  cfg::AdjacencyMySidConfig lagConfig;
  lagConfig.portName() = "Port-Channel301";
  lagEntry.adjacency() = lagConfig;
  config.entries()[2] = lagEntry;

  // Node entry
  cfg::MySidEntryConfig nodeEntry;
  cfg::NodeMySidConfig nodeConfig;
  nodeConfig.nodeAddress() = "fc00:200::1";
  nodeEntry.node() = nodeConfig;
  config.entries()[3] = nodeEntry;

  auto result = convertMySidConfig(config, portNameToIntfId);
  EXPECT_EQ(result.size(), 4);

  // Find entries by ID and verify properties
  std::unordered_map<std::string, std::shared_ptr<MySid>> byId;
  for (const auto& entry : result) {
    byId[entry.mySid->getID()] = entry.mySid;
  }
  // Adjacency entries should have interface IDs
  ASSERT_NE(byId.find("3001:db8:1::/48"), byId.end());
  EXPECT_EQ(byId["3001:db8:1::/48"]->getAdjacencyInterfaceId().value(), 2001);
  ASSERT_NE(byId.find("3001:db8:2::/48"), byId.end());
  EXPECT_EQ(byId["3001:db8:2::/48"]->getAdjacencyInterfaceId().value(), 301);
  // All entries should have STATIC_ROUTE clientId
  for (const auto& entry : result) {
    EXPECT_EQ(entry.mySid->getClientId(), ClientID::STATIC_ROUTE);
  }
}

TEST(MySidConfigUtilsTest, ConvertEveryEntryUnderEveryLocatorPrefix) {
  auto config =
      makeDecapAndAdjacencyConfig({"fdad:ffff::/32", "fdad:20d::/32"});

  auto result = convertMySidConfig(config, kPortNameToIntfId);

  const std::set<std::string> expected{
      "fdad:ffff:7fff::/48",
      "fdad:ffff:1::/48",
      "fdad:20d:7fff::/48",
      "fdad:20d:1::/48"};
  EXPECT_EQ(mySidIds(result), expected);
}

TEST(MySidConfigUtilsTest, LocatorPrefixesOverrideDeprecatedLocatorPrefix) {
  auto config = makeDecapAndAdjacencyConfig({"fdad:20d::/32"});

  auto result = convertMySidConfig(config, kPortNameToIntfId);

  const std::set<std::string> expected{"fdad:20d:7fff::/48", "fdad:20d:1::/48"};
  EXPECT_EQ(mySidIds(result), expected);
}

TEST(MySidConfigUtilsTest, EmptyLocatorPrefixesFallsBackToLocatorPrefix) {
  auto config = makeDecapAndAdjacencyConfig({});

  auto result = convertMySidConfig(config, kPortNameToIntfId);

  const std::set<std::string> expected{"3001:db8:7fff::/48", "3001:db8:1::/48"};
  EXPECT_EQ(mySidIds(result), expected);
}

TEST(MySidConfigUtilsTest, NoLocatorPrefixThrows) {
  cfg::MySidConfig config;
  config.entries()[0x7fff].decap() = cfg::DecapMySidConfig{};
  EXPECT_THROW(convertMySidConfig(config, kPortNameToIntfId), FbossError);
}

TEST(MySidConfigUtilsTest, DuplicateLocatorPrefixThrows) {
  auto config =
      makeDecapAndAdjacencyConfig({"fdad:ffff::/32", "fdad:ffff:0::/32"});
  EXPECT_THROW(convertMySidConfig(config, kPortNameToIntfId), FbossError);
}

TEST(MySidConfigUtilsTest, MismatchedLocatorPrefixLengthThrows) {
  auto config =
      makeDecapAndAdjacencyConfig({"fdad:ffff::/32", "fdad:20d::/40"});
  EXPECT_THROW(convertMySidConfig(config, kPortNameToIntfId), FbossError);
}
