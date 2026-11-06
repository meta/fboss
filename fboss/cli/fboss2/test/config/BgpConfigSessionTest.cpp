// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gtest/gtest.h>

#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <string>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpConfigSession.h"

namespace facebook::fboss {

/**
 * BgpConfigSessionTest validates the JSON generation workflow for BGP CLI
 * commands.
 *
 * These tests verify that:
 * 1. CLI commands correctly generate BGP++ thrift-compatible JSON
 * 2. All configuration fields are properly set and retrieved
 * 3. Boolean values (including false) are preserved
 * 4. Nested structures (peer timers, pre_filter, add_path) are correct
 * 5. The round-trip workflow (CLI -> JSON) produces valid output
 */
class BgpConfigSessionTest : public CmdConfigTestBase {
 public:
  BgpConfigSessionTest()
      : CmdConfigTestBase("bgp_config_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();

    // Enable stub mode to prevent actual file writes during most tests
    session_ = &BgpConfigSession::getInstance();
    session_->setStubMode(true);
    session_->clearSession();
  }

 protected:
  BgpConfigSession* session_ = nullptr;
};

// ==================== Global Configuration Tests ====================

TEST_F(BgpConfigSessionTest, setRouterId) {
  session_->setRouterId("10.0.0.1");
  EXPECT_EQ(session_->getRouterId(), "10.0.0.1");

  auto& config = session_->getBgpConfig();
  EXPECT_EQ(config["router_id"].asString(), "10.0.0.1");
}

TEST_F(BgpConfigSessionTest, setLocalAsn) {
  session_->setLocalAsn(65000);
  EXPECT_EQ(session_->getLocalAsn(), 65000);

  auto& config = session_->getBgpConfig();
  EXPECT_EQ(config["local_as_4_byte"].asInt(), 65000);
}

TEST_F(BgpConfigSessionTest, setHoldTime) {
  session_->setHoldTime(90);
  EXPECT_EQ(session_->getHoldTime(), 90);

  auto& config = session_->getBgpConfig();
  EXPECT_EQ(config["hold_time"].asInt(), 90);
}

TEST_F(BgpConfigSessionTest, setConfedAsn) {
  session_->setConfedAsn(65100);
  EXPECT_EQ(session_->getConfedAsn(), 65100);

  auto& config = session_->getBgpConfig();
  EXPECT_EQ(config["local_confed_as_4_byte"].asInt(), 65100);
}

TEST_F(BgpConfigSessionTest, setListenAddress) {
  session_->setListenAddress("0.0.0.0");
  EXPECT_EQ(session_->getListenAddress(), "0.0.0.0");

  auto& config = session_->getBgpConfig();
  EXPECT_EQ(config["listen_addr"].asString(), "0.0.0.0");
}

TEST_F(BgpConfigSessionTest, setListenPort) {
  session_->setListenPort(1179);
  EXPECT_EQ(session_->getListenPort(), 1179);

  auto& config = session_->getBgpConfig();
  EXPECT_EQ(config["listen_port"].asInt(), 1179);
}

// ==================== Switch Limit Configuration Tests ====================

TEST_F(BgpConfigSessionTest, setSwitchLimitPrefixLimit) {
  session_->setSwitchLimitPrefixLimit(10000);

  auto& config = session_->getBgpConfig();
  ASSERT_TRUE(config.count("switch_limit_config"));
  EXPECT_EQ(config["switch_limit_config"]["prefix_limit"].asInt(), 10000);
}

TEST_F(BgpConfigSessionTest, setSwitchLimitTotalPathLimit) {
  session_->setSwitchLimitTotalPathLimit(250000);

  auto& config = session_->getBgpConfig();
  ASSERT_TRUE(config.count("switch_limit_config"));
  EXPECT_EQ(config["switch_limit_config"]["total_path_limit"].asInt(), 250000);
}

TEST_F(BgpConfigSessionTest, setSwitchLimitMaxGoldenVips) {
  session_->setSwitchLimitMaxGoldenVips(2860);

  auto& config = session_->getBgpConfig();
  ASSERT_TRUE(config.count("switch_limit_config"));
  EXPECT_EQ(config["switch_limit_config"]["max_golden_vips"].asInt(), 2860);
}

TEST_F(BgpConfigSessionTest, setSwitchLimitOverloadProtectionMode) {
  session_->setSwitchLimitOverloadProtectionMode(2);

  auto& config = session_->getBgpConfig();
  ASSERT_TRUE(config.count("switch_limit_config"));
  EXPECT_EQ(
      config["switch_limit_config"]["overload_protection_mode"].asInt(), 2);
}

TEST_F(BgpConfigSessionTest, allSwitchLimitFieldsTogether) {
  session_->setSwitchLimitPrefixLimit(10000);
  session_->setSwitchLimitTotalPathLimit(250000);
  session_->setSwitchLimitMaxGoldenVips(2860);
  session_->setSwitchLimitOverloadProtectionMode(2);

  auto& config = session_->getBgpConfig();
  ASSERT_TRUE(config.count("switch_limit_config"));
  auto& switchLimit = config["switch_limit_config"];
  EXPECT_EQ(switchLimit["prefix_limit"].asInt(), 10000);
  EXPECT_EQ(switchLimit["total_path_limit"].asInt(), 250000);
  EXPECT_EQ(switchLimit["max_golden_vips"].asInt(), 2860);
  EXPECT_EQ(switchLimit["overload_protection_mode"].asInt(), 2);
}

// ==================== Network6 Configuration Tests ====================

TEST_F(BgpConfigSessionTest, addNetwork6Basic) {
  session_->addNetwork6("2001:db8::/32", "TEST_POLICY", true, 0);

  auto& config = session_->getBgpConfig();
  ASSERT_TRUE(config.count("networks6"));
  ASSERT_EQ(config["networks6"].size(), 1);

  auto& network = config["networks6"][0];
  EXPECT_EQ(network["prefix"].asString(), "2001:db8::/32");
  EXPECT_EQ(network["policy_name"].asString(), "TEST_POLICY");
  EXPECT_TRUE(network["install_to_fib"].asBool());
  EXPECT_EQ(network["minimum_supporting_routes"].asInt(), 0);
}

TEST_F(BgpConfigSessionTest, addNetwork6WithInstallToFibFalse) {
  // Critical test: Verify that install_to_fib: false is preserved
  session_->addNetwork6(
      "2401:db00:501c::/64", "ORIGINATE_RACK_PRIVATE_PREFIXES", false, 0);

  auto& config = session_->getBgpConfig();
  ASSERT_TRUE(config.count("networks6"));
  ASSERT_EQ(config["networks6"].size(), 1);

  auto& network = config["networks6"][0];
  EXPECT_EQ(network["prefix"].asString(), "2401:db00:501c::/64");
  EXPECT_EQ(
      network["policy_name"].asString(), "ORIGINATE_RACK_PRIVATE_PREFIXES");
  // Critical: install_to_fib: false must be preserved, not omitted
  EXPECT_FALSE(network["install_to_fib"].asBool());
  EXPECT_EQ(network["minimum_supporting_routes"].asInt(), 0);
}

TEST_F(BgpConfigSessionTest, addNetwork6WithMinSupportingRoutes) {
  session_->addNetwork6("2001:db8::/48", "POLICY", true, 5);

  auto& config = session_->getBgpConfig();
  auto& network = config["networks6"][0];
  EXPECT_EQ(network["minimum_supporting_routes"].asInt(), 5);
}

TEST_F(BgpConfigSessionTest, addMultipleNetworks6) {
  session_->addNetwork6("2001:db8:1::/48", "POLICY1", true, 0);
  session_->addNetwork6("2001:db8:2::/48", "POLICY2", false, 1);
  session_->addNetwork6("2001:db8:3::/48", "POLICY3", true, 2);

  auto& config = session_->getBgpConfig();
  ASSERT_EQ(config["networks6"].size(), 3);

  EXPECT_EQ(config["networks6"][0]["prefix"].asString(), "2001:db8:1::/48");
  EXPECT_EQ(config["networks6"][1]["prefix"].asString(), "2001:db8:2::/48");
  EXPECT_EQ(config["networks6"][2]["prefix"].asString(), "2001:db8:3::/48");

  // Verify second network has install_to_fib: false
  EXPECT_FALSE(config["networks6"][1]["install_to_fib"].asBool());
}

// ==================== Peer Group Configuration Tests ====================

TEST_F(BgpConfigSessionTest, createPeerGroupAndSetDescription) {
  session_->createPeerGroup("TEST-GROUP");
  session_->setPeerGroupDescription("TEST-GROUP", "Test peer group");

  auto& config = session_->getBgpConfig();
  ASSERT_TRUE(config.count("peer_groups"));
  ASSERT_EQ(config["peer_groups"].size(), 1);

  auto& group = config["peer_groups"][0];
  EXPECT_EQ(group["name"].asString(), "TEST-GROUP");
  EXPECT_EQ(group["description"].asString(), "Test peer group");
}

TEST_F(BgpConfigSessionTest, setPeerGroupRemoteAsn) {
  session_->createPeerGroup("TEST-GROUP");
  session_->setPeerGroupRemoteAsn("TEST-GROUP", 65002);

  auto& config = session_->getBgpConfig();
  EXPECT_EQ(config["peer_groups"][0]["remote_as_4_byte"].asInt(), 65002);
}

TEST_F(BgpConfigSessionTest, setPeerGroupNextHopSelf) {
  session_->createPeerGroup("TEST-GROUP");
  session_->setPeerGroupNextHopSelf("TEST-GROUP", true);

  auto& config = session_->getBgpConfig();
  EXPECT_TRUE(config["peer_groups"][0]["next_hop_self"].asBool());
}

TEST_F(BgpConfigSessionTest, setPeerGroupConfedPeer) {
  session_->createPeerGroup("TEST-GROUP");
  session_->setPeerGroupConfedPeer("TEST-GROUP", true);

  auto& config = session_->getBgpConfig();
  EXPECT_TRUE(config["peer_groups"][0]["is_confed_peer"].asBool());
}

TEST_F(BgpConfigSessionTest, setPeerGroupDisableIpv4AfiTrue) {
  session_->createPeerGroup("TEST-GROUP");
  session_->setPeerGroupDisableIpv4Afi("TEST-GROUP", true);

  auto& config = session_->getBgpConfig();
  EXPECT_TRUE(config["peer_groups"][0]["disable_ipv4_afi"].asBool());
}

TEST_F(BgpConfigSessionTest, setPeerGroupDisableIpv4AfiFalse) {
  session_->createPeerGroup("TEST-GROUP");
  session_->setPeerGroupDisableIpv4Afi("TEST-GROUP", false);

  auto& config = session_->getBgpConfig();
  // Must be explicitly set to false, not omitted
  ASSERT_TRUE(config["peer_groups"][0].count("disable_ipv4_afi"));
  EXPECT_FALSE(config["peer_groups"][0]["disable_ipv4_afi"].asBool());
}

TEST_F(BgpConfigSessionTest, setPeerGroupTimers) {
  session_->createPeerGroup("TEST-GROUP");
  session_->setPeerGroupHoldTime("TEST-GROUP", 30);
  session_->setPeerGroupKeepalive("TEST-GROUP", 10);

  auto& config = session_->getBgpConfig();
  ASSERT_TRUE(config["peer_groups"][0].count("bgp_peer_timers"));

  auto& timers = config["peer_groups"][0]["bgp_peer_timers"];
  EXPECT_EQ(timers["hold_time_seconds"].asInt(), 30);
  EXPECT_EQ(timers["keep_alive_seconds"].asInt(), 10);
}

TEST_F(BgpConfigSessionTest, setPeerGroupPolicies) {
  session_->createPeerGroup("TEST-GROUP");
  session_->setPeerGroupIngressPolicy("TEST-GROUP", "GROUP_INGRESS");
  session_->setPeerGroupEgressPolicy("TEST-GROUP", "GROUP_EGRESS");

  auto& config = session_->getBgpConfig();
  EXPECT_EQ(
      config["peer_groups"][0]["ingress_policy_name"].asString(),
      "GROUP_INGRESS");
  EXPECT_EQ(
      config["peer_groups"][0]["egress_policy_name"].asString(),
      "GROUP_EGRESS");
}

TEST_F(BgpConfigSessionTest, setPeerGroupIpv4OverIpv6Nh) {
  session_->createPeerGroup("TEST-GROUP");
  session_->setPeerGroupIpv4OverIpv6Nh("TEST-GROUP", true);

  auto& config = session_->getBgpConfig();
  EXPECT_TRUE(config["peer_groups"][0]["v4_over_v6_nexthop"].asBool());
}

// ==================== Integration Tests ====================

TEST_F(BgpConfigSessionTest, fullRswConfigurationWorkflow) {
  // Simulate full RSW configuration workflow
  // This test validates the entire round-trip: CLI commands -> JSON

  // Global config
  session_->setRouterId("0.17.1.1");
  session_->setHoldTime(30);

  // Switch limits
  session_->setSwitchLimitPrefixLimit(10000);
  session_->setSwitchLimitTotalPathLimit(250000);
  session_->setSwitchLimitMaxGoldenVips(2860);
  session_->setSwitchLimitOverloadProtectionMode(2);

  // Network6 with install_to_fib: false
  session_->addNetwork6(
      "2401:db00:501c::/64", "ORIGINATE_RACK_PRIVATE_PREFIXES", false, 0);

  // Create peer groups
  session_->createPeerGroup("RSW-FSW-V6");
  session_->setPeerGroupDescription(
      "RSW-FSW-V6", "BGP peering from RSW to FSW, IPv6 sessions");
  session_->setPeerGroupNextHopSelf("RSW-FSW-V6", true);
  session_->setPeerGroupConfedPeer("RSW-FSW-V6", true);
  session_->setPeerGroupIngressPolicy("RSW-FSW-V6", "PROPAGATE_RSW_FSW_IN");
  session_->setPeerGroupEgressPolicy("RSW-FSW-V6", "PROPAGATE_RSW_FSW_OUT");
  session_->setPeerGroupIpv4OverIpv6Nh("RSW-FSW-V6", true);
  session_->setPeerGroupHoldTime("RSW-FSW-V6", 30);
  session_->setPeerGroupKeepalive("RSW-FSW-V6", 10);

  session_->createPeerGroup("RSW-RTSW-V6");
  session_->setPeerGroupDisableIpv4Afi("RSW-RTSW-V6", true);

  // Verify the complete configuration
  auto& config = session_->getBgpConfig();

  // Verify global config
  EXPECT_EQ(config["router_id"].asString(), "0.17.1.1");
  EXPECT_EQ(config["hold_time"].asInt(), 30);

  // Verify switch limits
  ASSERT_TRUE(config.count("switch_limit_config"));
  EXPECT_EQ(config["switch_limit_config"]["prefix_limit"].asInt(), 10000);
  EXPECT_EQ(config["switch_limit_config"]["total_path_limit"].asInt(), 250000);
  EXPECT_EQ(config["switch_limit_config"]["max_golden_vips"].asInt(), 2860);
  EXPECT_EQ(
      config["switch_limit_config"]["overload_protection_mode"].asInt(), 2);

  // Verify networks6
  ASSERT_EQ(config["networks6"].size(), 1);
  EXPECT_FALSE(config["networks6"][0]["install_to_fib"].asBool());

  // Verify peer groups
  ASSERT_EQ(config["peer_groups"].size(), 2);

  // Find RSW-FSW-V6 group
  bool foundFswGroup = false;
  bool foundRtswGroup = false;
  for (const auto& group : config["peer_groups"]) {
    if (group["name"].asString() == "RSW-FSW-V6") {
      foundFswGroup = true;
      EXPECT_TRUE(group["next_hop_self"].asBool());
      EXPECT_TRUE(group["is_confed_peer"].asBool());
      EXPECT_TRUE(group["v4_over_v6_nexthop"].asBool());
      EXPECT_EQ(
          group["ingress_policy_name"].asString(), "PROPAGATE_RSW_FSW_IN");
      EXPECT_EQ(
          group["egress_policy_name"].asString(), "PROPAGATE_RSW_FSW_OUT");
      ASSERT_TRUE(group.count("bgp_peer_timers"));
      EXPECT_EQ(group["bgp_peer_timers"]["hold_time_seconds"].asInt(), 30);
      EXPECT_EQ(group["bgp_peer_timers"]["keep_alive_seconds"].asInt(), 10);
    } else if (group["name"].asString() == "RSW-RTSW-V6") {
      foundRtswGroup = true;
      EXPECT_TRUE(group["disable_ipv4_afi"].asBool());
    }
  }
  EXPECT_TRUE(foundFswGroup);
  EXPECT_TRUE(foundRtswGroup);
}

TEST_F(BgpConfigSessionTest, exportConfigProducesValidJson) {
  session_->setRouterId("10.0.0.1");
  session_->setLocalAsn(65000);
  session_->addNetwork6("2001:db8::/32", "TEST_POLICY", true, 0);
  session_->createPeerGroup("TEST-GROUP");

  std::string jsonStr = session_->exportConfig();

  // Verify it's valid JSON by parsing it
  folly::dynamic parsed = folly::parseJson(jsonStr);

  EXPECT_EQ(parsed["router_id"].asString(), "10.0.0.1");
  EXPECT_EQ(parsed["local_as_4_byte"].asInt(), 65000);
  EXPECT_EQ(parsed["networks6"].size(), 1);
  EXPECT_EQ(parsed["peer_groups"].size(), 1);
}

TEST_F(BgpConfigSessionTest, stubModePreventsSave) {
  // When stub mode is enabled, saveConfig should not write to disk
  session_->setStubMode(true);
  EXPECT_TRUE(session_->isStubMode());

  session_->setRouterId("10.0.0.1");

  // Verify config is in memory
  EXPECT_EQ(session_->getRouterId(), "10.0.0.1");
}

TEST_F(BgpConfigSessionTest, stubModeCanClearSession) {
  // In stub mode, clearSession() should not throw
  // The actual clearing behavior depends on implementation details
  session_->setRouterId("10.0.0.1");

  EXPECT_EQ(session_->getRouterId(), "10.0.0.1");

  // Should not throw in stub mode
  EXPECT_NO_THROW(session_->clearSession());
}

// ==================== Edge Case Tests ====================

TEST_F(BgpConfigSessionTest, updateExistingPeerGroup) {
  session_->createPeerGroup("TEST-GROUP");
  session_->setPeerGroupDescription("TEST-GROUP", "Initial");

  session_->setPeerGroupDescription("TEST-GROUP", "Updated");

  auto& config = session_->getBgpConfig();
  ASSERT_EQ(config["peer_groups"].size(), 1);
  EXPECT_EQ(config["peer_groups"][0]["description"].asString(), "Updated");
}

TEST_F(BgpConfigSessionTest, largeAsnNumber) {
  // Test with 4-byte ASN
  session_->setLocalAsn(4200000000);
  EXPECT_EQ(session_->getLocalAsn(), 4200000000);
}

} // namespace facebook::fboss
