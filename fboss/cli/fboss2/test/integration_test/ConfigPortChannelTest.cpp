// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "folly/String.h"
#include "folly/json/dynamic.h"

namespace facebook::fboss {

/*
 * Integration tests for:
 *   config port-channel <name> [description <string>|minimum-links <count>]
 *   config port-channel <name> member add|remove <interface> [...]
 *   config port-channel <name> member <interface> priority <n>
 *   config port-channel <name> member <interface> lacp rate|mode|hold-timer
 *   delete port-channel <name> [description|minimum-links]
 *   delete port-channel <name> member ...
 *
 * All port-channel changes are HITLESS: the agent applies the aggregate-port
 * delta live through SaiLagManager (add/change/removeLag).
 */
class ConfigPortChannelTest : public Fboss2IntegrationTest {
 protected:
  static constexpr auto kPortChannelName = "port-channel900";

  void TearDown() override {
    // Best-effort cleanup so a failed run does not leave the test
    // port-channel behind for the next one.
    if (findAggregatePort(kPortChannelName).has_value()) {
      auto result = runCli({"delete", "port-channel", kPortChannelName});
      if (result.exitCode == 0) {
        commitConfig();
        waitForAgentReady();
      }
    }
    // Move a re-homed member back to its original VLAN (warm boot commit,
    // like the switchport command that moved it).
    if (rehomedPort_.has_value()) {
      auto result = runCli(
          {"config",
           "interface",
           rehomedPort_->first,
           "switchport",
           "access",
           "vlan",
           std::to_string(rehomedPort_->second)});
      if (result.exitCode == 0) {
        commitConfig();
        waitForAgentReady();
      }
    }
    Fboss2IntegrationTest::TearDown();
  }

  /* ingressVlan of a port from the running config. */
  int64_t ingressVlanForName(const std::string& portName) const {
    auto config = getRunningConfig();
    for (const auto& port : config["sw"]["ports"]) {
      if (port["name"].asString() == portName) {
        return port["ingressVlan"].asInt();
      }
    }
    throw std::runtime_error("Port not found in running config: " + portName);
  }

  /*
   * Members of a port-channel must share one VLAN; on a DUT with per-port
   * VLANs, move `port` into `vlanId` (switchport access, warm boot commit).
   */
  void rehomePortToVlan(const std::string& port, int64_t vlanId) {
    int64_t originalVlan = ingressVlanForName(port);
    runCliExpectSuccess(
        {"config",
         "interface",
         port,
         "switchport",
         "access",
         "vlan",
         std::to_string(vlanId)});
    rehomedPort_ = std::make_pair(port, originalVlan);
    commitAndWait();
  }

  /* The aggregatePorts entry with the given name, if present. */
  std::optional<folly::dynamic> findAggregatePort(
      const std::string& name) const {
    auto config = getRunningConfig();
    auto* sw = config.get_ptr("sw");
    if (!sw) {
      return std::nullopt;
    }
    auto* aggregatePorts = sw->get_ptr("aggregatePorts");
    if (!aggregatePorts || !aggregatePorts->isArray()) {
      return std::nullopt;
    }
    for (const auto& aggPort : *aggregatePorts) {
      if (aggPort["name"].asString() == name) {
        return aggPort;
      }
    }
    return std::nullopt;
  }

  /* Logical port ID for a port name from the running config. */
  int64_t portIdForName(const std::string& portName) const {
    auto config = getRunningConfig();
    for (const auto& port : config["sw"]["ports"]) {
      if (port["name"].asString() == portName) {
        return port["logicalID"].asInt();
      }
    }
    throw std::runtime_error("Port not found in running config: " + portName);
  }

  /* The memberPorts entry for the given port, if present. */
  static std::optional<folly::dynamic> findMember(
      const folly::dynamic& aggPort,
      int64_t portId) {
    for (const auto& member : aggPort["memberPorts"]) {
      if (member["memberPortID"].asInt() == portId) {
        return member;
      }
    }
    return std::nullopt;
  }

  /* Interface ports that are not a member of any existing aggregate port. */
  std::vector<std::string> pickFreeMemberPorts(size_t count) {
    auto config = getRunningConfig();
    std::vector<int64_t> takenPortIds;
    auto* sw = config.get_ptr("sw");
    if (auto* aggregatePorts = sw ? sw->get_ptr("aggregatePorts") : nullptr) {
      for (const auto& aggPort : *aggregatePorts) {
        for (const auto& member : aggPort["memberPorts"]) {
          takenPortIds.push_back(member["memberPortID"].asInt());
        }
      }
    }

    // Enabled INTERFACE_PORTs from the running config, in config order.
    std::vector<std::string> picked;
    for (const auto& port : config["sw"]["ports"]) {
      if (picked.size() == count) {
        break;
      }
      if (port.getDefault("portType", 0).asInt() !=
              static_cast<int64_t>(cfg::PortType::INTERFACE_PORT) ||
          port["state"].asInt() !=
              static_cast<int64_t>(cfg::PortState::ENABLED)) {
        continue;
      }
      int64_t portId = port["logicalID"].asInt();
      if (std::find(takenPortIds.begin(), takenPortIds.end(), portId) ==
          takenPortIds.end()) {
        picked.push_back(port["name"].asString());
      }
    }
    return picked;
  }

  void runCliExpectSuccess(const std::vector<std::string>& args) {
    auto result = runCli(args);
    ASSERT_EQ(result.exitCode, 0)
        << "CLI failed: " << folly::join(" ", args) << "\n"
        << result.stderr;
  }

  void commitAndWait() {
    commitConfig();
    waitForAgentReady();
  }

  // (port, original ingress VLAN) of the member moved into the other
  // member's VLAN, restored in TearDown.
  std::optional<std::pair<std::string, int64_t>> rehomedPort_;
};

TEST_F(ConfigPortChannelTest, CreateModifyAndDeletePortChannel) {
  XLOG(INFO) << "[Step 1] Pick two interface ports that are not aggregate "
                "port members";
  auto memberPorts = pickFreeMemberPorts(2);
  if (memberPorts.size() < 2) {
    GTEST_SKIP() << "Fewer than 2 free INTERFACE_PORTs available - skipping";
  }
  const std::string& port1 = memberPorts[0];
  const std::string& port2 = memberPorts[1];
  XLOG(INFO) << "Using member ports " << port1 << " and " << port2;

  int64_t vlan1 = ingressVlanForName(port1);
  ASSERT_NE(vlan1, 0) << port1 << " has no ingress VLAN";
  if (ingressVlanForName(port2) != vlan1) {
    XLOG(INFO) << "[Step 1b] Move " << port2 << " into VLAN " << vlan1
               << " so both members share one VLAN";
    rehomePortToVlan(port2, vlan1);
    ASSERT_EQ(ingressVlanForName(port2), vlan1);
  }

  XLOG(INFO) << "[Step 2] Create the port-channel by adding both members";
  runCliExpectSuccess(
      {"config",
       "port-channel",
       kPortChannelName,
       "member",
       "add",
       port1,
       port2});
  commitAndWait();

  auto aggPort = findAggregatePort(kPortChannelName);
  ASSERT_TRUE(aggPort.has_value())
      << "aggregatePorts entry missing after commit";
  EXPECT_EQ((*aggPort)["key"].asInt(), 900);
  ASSERT_EQ((*aggPort)["memberPorts"].size(), 2);

  int64_t port1Id = portIdForName(port1);
  int64_t port2Id = portIdForName(port2);
  ASSERT_TRUE(findMember(*aggPort, port1Id).has_value());
  ASSERT_TRUE(findMember(*aggPort, port2Id).has_value());

  XLOG(INFO) << "[Step 3] Set description and minimum-links";
  runCliExpectSuccess(
      {"config",
       "port-channel",
       kPortChannelName,
       "description",
       "fboss2-integration-test",
       "minimum-links",
       "1"});
  commitAndWait();

  aggPort = findAggregatePort(kPortChannelName);
  ASSERT_TRUE(aggPort.has_value());
  EXPECT_EQ((*aggPort)["description"].asString(), "fboss2-integration-test");
  EXPECT_EQ((*aggPort)["minimumCapacity"]["linkCount"].asInt(), 1);

  XLOG(INFO) << "[Step 4] Set per-member LACP attributes on " << port1;
  runCliExpectSuccess(
      {"config",
       "port-channel",
       kPortChannelName,
       "member",
       port1,
       "priority",
       "1000"});
  runCliExpectSuccess(
      {"config",
       "port-channel",
       kPortChannelName,
       "member",
       port1,
       "lacp",
       "rate",
       "slow"});
  runCliExpectSuccess(
      {"config",
       "port-channel",
       kPortChannelName,
       "member",
       port1,
       "lacp",
       "mode",
       "passive"});
  runCliExpectSuccess(
      {"config",
       "port-channel",
       kPortChannelName,
       "member",
       port1,
       "lacp",
       "hold-timer",
       "5"});
  commitAndWait();

  aggPort = findAggregatePort(kPortChannelName);
  ASSERT_TRUE(aggPort.has_value());
  auto member1 = findMember(*aggPort, port1Id);
  ASSERT_TRUE(member1.has_value());
  EXPECT_EQ((*member1)["priority"].asInt(), 1000);
  EXPECT_EQ((*member1)["rate"].asInt(), 0); // SLOW
  EXPECT_EQ((*member1)["activity"].asInt(), 0); // PASSIVE
  EXPECT_EQ((*member1)["holdTimerMultiplier"].asInt(), 5);

  XLOG(INFO) << "[Step 5] Reset the member attributes via delete";
  runCliExpectSuccess(
      {"delete",
       "port-channel",
       kPortChannelName,
       "member",
       port1,
       "priority"});
  runCliExpectSuccess(
      {"delete",
       "port-channel",
       kPortChannelName,
       "member",
       port1,
       "lacp",
       "rate"});
  runCliExpectSuccess(
      {"delete",
       "port-channel",
       kPortChannelName,
       "member",
       port1,
       "lacp",
       "mode"});
  runCliExpectSuccess(
      {"delete",
       "port-channel",
       kPortChannelName,
       "member",
       port1,
       "lacp",
       "hold-timer"});
  commitAndWait();

  aggPort = findAggregatePort(kPortChannelName);
  ASSERT_TRUE(aggPort.has_value());
  member1 = findMember(*aggPort, port1Id);
  ASSERT_TRUE(member1.has_value());
  EXPECT_EQ((*member1)["priority"].asInt(), 32768);
  EXPECT_EQ((*member1)["rate"].asInt(), 1); // FAST
  EXPECT_EQ((*member1)["activity"].asInt(), 1); // ACTIVE
  EXPECT_EQ((*member1)["holdTimerMultiplier"].asInt(), 3);

  XLOG(INFO) << "[Step 6] Reset description and minimum-links via delete";
  runCliExpectSuccess(
      {"delete",
       "port-channel",
       kPortChannelName,
       "description",
       "minimum-links"});
  commitAndWait();

  aggPort = findAggregatePort(kPortChannelName);
  ASSERT_TRUE(aggPort.has_value());
  EXPECT_EQ((*aggPort)["description"].asString(), "");
  EXPECT_EQ((*aggPort)["minimumCapacity"]["linkPercentage"].asDouble(), 1.0);

  XLOG(INFO) << "[Step 7] Remove one member";
  runCliExpectSuccess(
      {"config", "port-channel", kPortChannelName, "member", "remove", port2});
  commitAndWait();

  aggPort = findAggregatePort(kPortChannelName);
  ASSERT_TRUE(aggPort.has_value());
  ASSERT_EQ((*aggPort)["memberPorts"].size(), 1);
  EXPECT_TRUE(findMember(*aggPort, port1Id).has_value());

  XLOG(INFO) << "[Step 8] Removing the last member must fail with guidance";
  auto result = runCli(
      {"config", "port-channel", kPortChannelName, "member", "remove", port1});
  EXPECT_NE(result.exitCode, 0);

  XLOG(INFO) << "[Step 9] Delete the port-channel";
  runCliExpectSuccess({"delete", "port-channel", kPortChannelName});
  commitAndWait();

  EXPECT_FALSE(findAggregatePort(kPortChannelName).has_value());
}

} // namespace facebook::fboss
