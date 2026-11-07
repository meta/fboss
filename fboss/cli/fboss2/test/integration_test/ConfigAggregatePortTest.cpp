// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "folly/String.h"
#include "folly/json/dynamic.h"

namespace facebook::fboss {

class ConfigAggregatePortTest : public Fboss2IntegrationTest {
 protected:
  static constexpr auto kAggregatePortName = "Port-Channel900";
  static constexpr auto kAggregatePortKey = 900;

  void TearDown() override {
    if (findAggregatePort().has_value()) {
      auto result = runCli({"delete", "aggregate-port", kAggregatePortName});
      if (result.exitCode == 0) {
        commitConfig();
        waitForAgentReady();
      }
    }
    Fboss2IntegrationTest::TearDown();
  }

  std::optional<folly::dynamic> findAggregatePort() const {
    auto config = getRunningConfig();
    auto* sw = config.get_ptr("sw");
    if (!sw) {
      return std::nullopt;
    }
    auto* aggregatePorts = sw->get_ptr("aggregatePorts");
    if (!aggregatePorts || !aggregatePorts->isArray()) {
      return std::nullopt;
    }
    for (const auto& aggregatePort : *aggregatePorts) {
      if (aggregatePort["name"].asString() == kAggregatePortName) {
        return aggregatePort;
      }
    }
    return std::nullopt;
  }

  int64_t portIdForName(const std::string& portName) const {
    auto config = getRunningConfig();
    for (const auto& port : config["sw"]["ports"]) {
      if (port["name"].asString() == portName) {
        return port["logicalID"].asInt();
      }
    }
    throw std::runtime_error(
        fmt::format("Port not found in running config: {}", portName));
  }

  std::optional<std::string> pickFreeMemberPort() const {
    auto config = getRunningConfig();
    std::vector<int64_t> unavailablePortIds;
    auto* sw = config.get_ptr("sw");
    if (auto* aggregatePorts = sw ? sw->get_ptr("aggregatePorts") : nullptr) {
      for (const auto& aggregatePort : *aggregatePorts) {
        for (const auto& member : aggregatePort["memberPorts"]) {
          unavailablePortIds.push_back(member["memberPortID"].asInt());
        }
      }
    }
    if (auto* interfaces = sw ? sw->get_ptr("interfaces") : nullptr) {
      for (const auto& interface : *interfaces) {
        if (auto* portId = interface.get_ptr("portID")) {
          unavailablePortIds.push_back(portId->asInt());
        }
      }
    }

    for (const auto& port : config["sw"]["ports"]) {
      if (port.getDefault("portType", 0).asInt() !=
              static_cast<int64_t>(cfg::PortType::INTERFACE_PORT) ||
          port["state"].asInt() !=
              static_cast<int64_t>(cfg::PortState::ENABLED)) {
        continue;
      }
      auto portId = port["logicalID"].asInt();
      if (std::find(
              unavailablePortIds.begin(), unavailablePortIds.end(), portId) ==
          unavailablePortIds.end()) {
        return port["name"].asString();
      }
    }
    return std::nullopt;
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
};

TEST_F(ConfigAggregatePortTest, CreateModifyAndDeleteAggregatePort) {
  auto memberPort = pickFreeMemberPort();
  if (!memberPort.has_value()) {
    GTEST_SKIP() << "No free INTERFACE_PORT is available";
  }

  XLOG(INFO) << "[Step 1] Create aggregate port " << kAggregatePortName;
  runCliExpectSuccess(
      {"config",
       "aggregate-port",
       kAggregatePortName,
       "key",
       std::to_string(kAggregatePortKey),
       "description",
       "fboss2-integration-test",
       "minimum-capacity-link-count",
       "1",
       "minimum-capacity-to-up-link-count",
       "1"});
  commitAndWait();

  auto aggregatePort = findAggregatePort();
  ASSERT_TRUE(aggregatePort.has_value());
  EXPECT_EQ((*aggregatePort)["key"].asInt(), kAggregatePortKey);
  EXPECT_EQ(
      (*aggregatePort)["description"].asString(), "fboss2-integration-test");
  EXPECT_EQ((*aggregatePort)["minimumCapacity"]["linkCount"].asInt(), 1);
  EXPECT_EQ((*aggregatePort)["minimumCapacityToUp"]["linkCount"].asInt(), 1);

  XLOG(INFO) << "[Step 2] Add member " << *memberPort;
  runCliExpectSuccess(
      {"config",
       "aggregate-port",
       kAggregatePortName,
       "member",
       *memberPort,
       "activity",
       "passive",
       "rate",
       "slow",
       "priority",
       "1000",
       "hold-timer-multiplier",
       "5"});
  commitAndWait();

  aggregatePort = findAggregatePort();
  ASSERT_TRUE(aggregatePort.has_value());
  ASSERT_EQ((*aggregatePort)["memberPorts"].size(), 1);
  const auto& member = (*aggregatePort)["memberPorts"][0];
  EXPECT_EQ(member["memberPortID"].asInt(), portIdForName(*memberPort));
  EXPECT_EQ(member["activity"].asInt(), 0);
  EXPECT_EQ(member["rate"].asInt(), 0);
  EXPECT_EQ(member["priority"].asInt(), 1000);
  EXPECT_EQ(member["holdTimerMultiplier"].asInt(), 5);

  XLOG(INFO) << "[Step 3] Remove minimum-capacity-to-up";
  runCliExpectSuccess(
      {"delete",
       "aggregate-port",
       kAggregatePortName,
       "minimum-capacity-to-up"});
  commitAndWait();

  aggregatePort = findAggregatePort();
  ASSERT_TRUE(aggregatePort.has_value());
  EXPECT_EQ(aggregatePort->count("minimumCapacityToUp"), 0);

  XLOG(INFO) << "[Step 4] Remove member " << *memberPort;
  runCliExpectSuccess(
      {"delete", "aggregate-port", kAggregatePortName, "member", *memberPort});
  commitAndWait();

  aggregatePort = findAggregatePort();
  ASSERT_TRUE(aggregatePort.has_value());
  EXPECT_TRUE((*aggregatePort)["memberPorts"].empty());

  XLOG(INFO) << "[Step 5] Delete the aggregate port";
  runCliExpectSuccess({"delete", "aggregate-port", kAggregatePortName});
  commitAndWait();
  EXPECT_FALSE(findAggregatePort().has_value());

  XLOG(INFO) << "[Step 6] Verify all delete forms are idempotent";
  runCliExpectSuccess({"delete", "aggregate-port", kAggregatePortName});
  runCliExpectSuccess(
      {"delete", "aggregate-port", kAggregatePortName, "member", *memberPort});
  runCliExpectSuccess(
      {"delete",
       "aggregate-port",
       kAggregatePortName,
       "minimum-capacity-to-up"});
  EXPECT_FALSE(findAggregatePort().has_value());
}

} // namespace facebook::fboss
