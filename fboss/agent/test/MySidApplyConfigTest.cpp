// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;

class MySidApplyConfigTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    auto config = testConfigA();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
    baseConfig_ = config;
  }

  void TearDown() override {
    FLAGS_enable_nexthop_id_manager = false;
  }

  // Apply a config with mySidConfig and verify state
  void applyConfig(cfg::SwitchConfig config) {
    sw_->applyConfig("test", config);
  }

  // Get MySid entry by key (e.g., "3001:db8:7fff::/48")
  std::shared_ptr<MySid> getMySid(const std::string& key) {
    auto mySids = sw_->getState()->getMySids();
    for (const auto& [_, mySidMap] : std::as_const(*mySids)) {
      auto node = mySidMap->getNodeIf(key);
      if (node) {
        return node;
      }
    }
    return nullptr;
  }

  size_t getMySidCount() {
    size_t count = 0;
    auto mySids = sw_->getState()->getMySids();
    for (const auto& [_, mySidMap] : std::as_const(*mySids)) {
      count += mySidMap->size();
    }
    return count;
  }

  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
  cfg::SwitchConfig baseConfig_;
};

TEST_F(MySidApplyConfigTest, AddDecapEntry) {
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entry;
  entry.decap() = cfg::DecapMySidConfig{};
  mySidConfig.entries()[0x7fff] = entry;
  config.mySidConfig() = mySidConfig;

  applyConfig(config);

  auto mySid = getMySid("3001:db8:7fff::/48");
  ASSERT_NE(mySid, nullptr);
  EXPECT_EQ(mySid->getType(), MySidType::DECAPSULATE_AND_LOOKUP);
  EXPECT_EQ(mySid->getClientId(), ClientID::STATIC_ROUTE);
}

TEST_F(MySidApplyConfigTest, AddMultipleEntries) {
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";

  cfg::MySidEntryConfig decapEntry;
  decapEntry.decap() = cfg::DecapMySidConfig{};
  mySidConfig.entries()[0x7fff] = decapEntry;

  cfg::MySidEntryConfig nodeEntry;
  cfg::NodeMySidConfig nodeConfig;
  nodeConfig.nodeAddress() = "fc00:100::1";
  nodeEntry.node() = nodeConfig;
  mySidConfig.entries()[3] = nodeEntry;

  config.mySidConfig() = mySidConfig;
  applyConfig(config);

  EXPECT_EQ(getMySidCount(), 2);
  EXPECT_NE(getMySid("3001:db8:7fff::/48"), nullptr);
  EXPECT_NE(getMySid("3001:db8:3::/48"), nullptr);
}

TEST_F(MySidApplyConfigTest, RemoveStaticEntry) {
  // First, add two entries
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";

  cfg::MySidEntryConfig entry1;
  entry1.decap() = cfg::DecapMySidConfig{};
  mySidConfig.entries()[0x7fff] = entry1;

  cfg::MySidEntryConfig entry2;
  entry2.decap() = cfg::DecapMySidConfig{};
  mySidConfig.entries()[0x7ffe] = entry2;

  config.mySidConfig() = mySidConfig;
  applyConfig(config);
  EXPECT_EQ(getMySidCount(), 2);

  // Verify both entries are STATIC after first apply
  ASSERT_NE(getMySid("3001:db8:7fff::/48"), nullptr);
  EXPECT_EQ(
      getMySid("3001:db8:7fff::/48")->getClientId(), ClientID::STATIC_ROUTE);
  ASSERT_NE(getMySid("3001:db8:7ffe::/48"), nullptr);
  EXPECT_EQ(
      getMySid("3001:db8:7ffe::/48")->getClientId(), ClientID::STATIC_ROUTE);

  // Remove one entry from config
  mySidConfig.entries()->erase(0x7ffe);
  config.mySidConfig() = mySidConfig;
  applyConfig(config);

  EXPECT_EQ(getMySidCount(), 1);
  EXPECT_NE(getMySid("3001:db8:7fff::/48"), nullptr);
  EXPECT_EQ(getMySid("3001:db8:7ffe::/48"), nullptr);
}

TEST_F(MySidApplyConfigTest, RemoveAllStaticEntries) {
  // Add entries
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entry;
  entry.decap() = cfg::DecapMySidConfig{};
  mySidConfig.entries()[0x7fff] = entry;
  config.mySidConfig() = mySidConfig;
  applyConfig(config);
  EXPECT_EQ(getMySidCount(), 1);

  // Remove mySidConfig entirely
  config.mySidConfig().reset();
  applyConfig(config);
  EXPECT_EQ(getMySidCount(), 0);
}

TEST_F(MySidApplyConfigTest, DynamicEntriesPreservedOnConfigChange) {
  // Add a dynamic entry via ThriftHandler
  ThriftHandler handler(sw_);
  auto entries = std::make_unique<std::vector<MySidEntry>>();
  MySidEntry dynamicEntry;
  dynamicEntry.type() = MySidType::BINDING_MICRO_SID;
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddress("2001:db8::1"));
  prefix.prefixLength() = 64;
  dynamicEntry.mySid() = prefix;
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::ff"));
  nhop.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::10"))};
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop.tunnelId() = "tunnel1";
  dynamicEntry.nextHops() = {nhop};
  entries->push_back(dynamicEntry);
  handler.addMySidEntries(std::move(entries));

  EXPECT_NE(getMySid("2001:db8::1/64"), nullptr);
  // Dynamic entries have DYNAMIC ownership by default
  EXPECT_EQ(getMySid("2001:db8::1/64")->getClientId(), ClientID::TE_AGENT);

  // Now apply config with a STATIC entry
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig staticEntry;
  staticEntry.decap() = cfg::DecapMySidConfig{};
  mySidConfig.entries()[0x7fff] = staticEntry;
  config.mySidConfig() = mySidConfig;
  applyConfig(config);

  // Both should exist
  EXPECT_NE(getMySid("3001:db8:7fff::/48"), nullptr);
  EXPECT_NE(getMySid("2001:db8::1/64"), nullptr);
  EXPECT_EQ(
      getMySid("3001:db8:7fff::/48")->getClientId(), ClientID::STATIC_ROUTE);
  EXPECT_EQ(getMySid("2001:db8::1/64")->getClientId(), ClientID::TE_AGENT);

  // Remove mySidConfig — only STATIC should be removed
  config.mySidConfig().reset();
  applyConfig(config);

  EXPECT_EQ(getMySid("3001:db8:7fff::/48"), nullptr);
  // Dynamic entry still exists
  EXPECT_NE(getMySid("2001:db8::1/64"), nullptr);
}

TEST_F(MySidApplyConfigTest, UpdateExistingEntry) {
  // Add a decap entry
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entry;
  entry.decap() = cfg::DecapMySidConfig{};
  mySidConfig.entries()[1] = entry;
  config.mySidConfig() = mySidConfig;
  applyConfig(config);

  auto mySid = getMySid("3001:db8:1::/48");
  ASSERT_NE(mySid, nullptr);
  EXPECT_EQ(mySid->getType(), MySidType::DECAPSULATE_AND_LOOKUP);

  // Change from decap to node type
  cfg::MySidEntryConfig nodeEntry;
  cfg::NodeMySidConfig nodeConfig;
  nodeConfig.nodeAddress() = "fc00:100::1";
  nodeEntry.node() = nodeConfig;
  mySidConfig.entries()[1] = nodeEntry;
  config.mySidConfig() = mySidConfig;
  applyConfig(config);

  mySid = getMySid("3001:db8:1::/48");
  ASSERT_NE(mySid, nullptr);
  EXPECT_EQ(mySid->getType(), MySidType::NODE_MICRO_SID);
  EXPECT_EQ(mySid->getClientId(), ClientID::STATIC_ROUTE);
}

TEST_F(MySidApplyConfigTest, LocatorPrefixChange) {
  // Add entry with locator 3001:db8::/32
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entry;
  entry.decap() = cfg::DecapMySidConfig{};
  mySidConfig.entries()[0x7fff] = entry;
  config.mySidConfig() = mySidConfig;
  applyConfig(config);

  EXPECT_NE(getMySid("3001:db8:7fff::/48"), nullptr);

  // Change locator to 3002:db8::/32
  mySidConfig.locatorPrefix() = "3002:db8::/32";
  config.mySidConfig() = mySidConfig;
  applyConfig(config);

  // Old SID deleted, new SID created
  EXPECT_EQ(getMySid("3001:db8:7fff::/48"), nullptr);
  EXPECT_NE(getMySid("3002:db8:7fff::/48"), nullptr);
  EXPECT_EQ(
      getMySid("3002:db8:7fff::/48")->getClientId(), ClientID::STATIC_ROUTE);
}

TEST_F(MySidApplyConfigTest, InvalidPortNameThrows) {
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entry;
  cfg::AdjacencyMySidConfig adjConfig;
  adjConfig.portName() = "nonexistent_port";
  entry.adjacency() = adjConfig;
  mySidConfig.entries()[1] = entry;
  config.mySidConfig() = mySidConfig;

  // Config with invalid port name should throw during applyConfig
  EXPECT_THROW(applyConfig(config), FbossError);
  // No MySid entries should have been added
  EXPECT_EQ(getMySidCount(), 0);
}

TEST_F(MySidApplyConfigTest, AdjacencyEntryHasInterfaceId) {
  // Set ingressVlan on port1 so resolvePortNameToInterfaceId works
  auto config = baseConfig_;
  config.ports()[0].ingressVlan() = 1;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "3001:db8::/32";
  cfg::MySidEntryConfig entry;
  cfg::AdjacencyMySidConfig adjConfig;
  adjConfig.portName() = "port1";
  entry.adjacency() = adjConfig;
  mySidConfig.entries()[1] = entry;
  config.mySidConfig() = mySidConfig;
  applyConfig(config);

  auto mySid = getMySid("3001:db8:1::/48");
  ASSERT_NE(mySid, nullptr);
  EXPECT_EQ(mySid->getType(), MySidType::ADJACENCY_MICRO_SID);
  EXPECT_EQ(mySid->getClientId(), ClientID::STATIC_ROUTE);
  ASSERT_TRUE(mySid->getAdjacencyInterfaceId().has_value());
  EXPECT_EQ(mySid->getAdjacencyInterfaceId().value(), 1);
}

TEST_F(MySidApplyConfigTest, AddAndRemoveSecondLocatorPrefix) {
  auto config = baseConfig_;
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = "fdad:ffff::/32";
  mySidConfig.locatorPrefixes() = {"fdad:ffff::/32"};
  mySidConfig.entries()[0x7fff].decap() = cfg::DecapMySidConfig{};
  config.mySidConfig() = mySidConfig;
  applyConfig(config);
  auto legacySid = getMySid("fdad:ffff:7fff::/48");
  ASSERT_NE(legacySid, nullptr);

  mySidConfig.locatorPrefixes() = {"fdad:ffff::/32", "fdad:20d::/32"};
  config.mySidConfig() = mySidConfig;
  applyConfig(config);
  EXPECT_EQ(getMySidCount(), 2);
  EXPECT_EQ(getMySid("fdad:ffff:7fff::/48"), legacySid);
  EXPECT_NE(getMySid("fdad:20d:7fff::/48"), nullptr);

  mySidConfig.locatorPrefixes() = {"fdad:ffff::/32"};
  config.mySidConfig() = mySidConfig;
  applyConfig(config);
  EXPECT_EQ(getMySidCount(), 1);
  EXPECT_EQ(getMySid("fdad:ffff:7fff::/48"), legacySid);
  EXPECT_EQ(getMySid("fdad:20d:7fff::/48"), nullptr);
}
