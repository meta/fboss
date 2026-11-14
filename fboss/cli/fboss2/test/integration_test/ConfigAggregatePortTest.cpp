// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "folly/String.h"
#include "folly/json/dynamic.h"

namespace facebook::fboss {

class ConfigAggregatePortTest : public Fboss2IntegrationTest {
 protected:
  struct AggregatePortIdentity {
    std::string name;
    int key{0};
  };

  static constexpr auto kTestDescription = "fboss2-integration-test";
  static constexpr int kTestAggregatePortKeyMin = 900;
  static constexpr int kTestAggregatePortKeyMax = 999;

  void TearDown() override {
    if (aggregatePortMayExist_) {
      try {
        discardSession();
        auto aggregatePort = findAggregatePort();
        if (aggregatePort.has_value() && isOwnedAggregatePort(*aggregatePort)) {
          auto result =
              runCli({"delete", "aggregate-port", aggregatePort_.name});
          if (result.exitCode == 0) {
            commitConfig();
            waitForAgentReady();
          } else {
            ADD_FAILURE() << "Failed to stage cleanup for aggregate port '"
                          << aggregatePort_.name << "': " << result.stderr;
          }
        }
      } catch (const std::exception& ex) {
        ADD_FAILURE() << "Failed to clean up aggregate port '"
                      << aggregatePort_.name << "': " << ex.what();
      }
    }
    Fboss2IntegrationTest::TearDown();
  }

  std::optional<folly::dynamic> findAggregatePort() const {
    if (aggregatePort_.name.empty()) {
      return std::nullopt;
    }
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
      if (aggregatePort["name"].asString() == aggregatePort_.name) {
        return aggregatePort;
      }
    }
    return std::nullopt;
  }

  std::optional<AggregatePortIdentity> pickUnusedAggregatePort() const {
    auto config = getRunningConfig();
    std::set<int64_t> configuredKeys;
    std::set<std::string> configuredNames;
    auto* sw = config.get_ptr("sw");
    if (auto* aggregatePorts = sw ? sw->get_ptr("aggregatePorts") : nullptr) {
      for (const auto& aggregatePort : *aggregatePorts) {
        configuredKeys.insert(aggregatePort["key"].asInt());
        configuredNames.insert(aggregatePort["name"].asString());
      }
    }

    for (int key = kTestAggregatePortKeyMin; key <= kTestAggregatePortKeyMax;
         ++key) {
      auto name = fmt::format("Port-Channel{}", key);
      if (!configuredKeys.count(key) && !configuredNames.count(name)) {
        return AggregatePortIdentity{std::move(name), key};
      }
    }
    return std::nullopt;
  }

  bool isOwnedAggregatePort(const folly::dynamic& aggregatePort) const {
    return aggregatePort["key"].asInt() == aggregatePort_.key &&
        aggregatePort["description"].asString() == kTestDescription;
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

  testing::AssertionResult runCliSuccessfully(
      const std::vector<std::string>& args) const {
    auto result = runCli(args);
    if (result.exitCode != 0) {
      return testing::AssertionFailure()
          << "CLI failed: " << folly::join(" ", args) << "\n"
          << result.stderr;
    }
    return testing::AssertionSuccess();
  }

  void commitAndWait() {
    commitConfig();
    waitForAgentReady();
  }

  AggregatePortIdentity aggregatePort_;
  bool aggregatePortMayExist_{false};
};

TEST_F(ConfigAggregatePortTest, CreateModifyAndDeleteAggregatePort) {
  auto memberPort = pickFreeMemberPort();
  if (!memberPort.has_value()) {
    GTEST_SKIP() << "No free INTERFACE_PORT is available";
  }

  auto aggregatePort = pickUnusedAggregatePort();
  if (!aggregatePort.has_value()) {
    GTEST_SKIP() << "No aggregate-port key is available in ["
                 << kTestAggregatePortKeyMin << ", " << kTestAggregatePortKeyMax
                 << "]";
  }
  aggregatePort_ = std::move(*aggregatePort);

  XLOG(INFO) << "[Step 1] Stage aggregate port " << aggregatePort_.name;
  ASSERT_TRUE(runCliSuccessfully(
      {"config",
       "aggregate-port",
       aggregatePort_.name,
       "key",
       std::to_string(aggregatePort_.key),
       "description",
       kTestDescription,
       "minimum-capacity-link-count",
       "1",
       "minimum-capacity-to-up-link-count",
       "1"}));
  aggregatePortMayExist_ = true;

  XLOG(INFO) << "[Step 2] Stage member " << *memberPort;
  ASSERT_TRUE(runCliSuccessfully(
      {"config",
       "aggregate-port",
       aggregatePort_.name,
       "member",
       *memberPort,
       "activity",
       "passive",
       "rate",
       "slow",
       "priority",
       "1000",
       "hold-timer-multiplier",
       "5"}));
  commitAndWait();

  auto runningAggregatePort = findAggregatePort();
  ASSERT_TRUE(runningAggregatePort.has_value());
  EXPECT_EQ((*runningAggregatePort)["key"].asInt(), aggregatePort_.key);
  EXPECT_EQ(
      (*runningAggregatePort)["description"].asString(), kTestDescription);
  EXPECT_EQ((*runningAggregatePort)["minimumCapacity"]["linkCount"].asInt(), 1);
  EXPECT_EQ(
      (*runningAggregatePort)["minimumCapacityToUp"]["linkCount"].asInt(), 1);
  ASSERT_EQ((*runningAggregatePort)["memberPorts"].size(), 1);
  const auto& member = (*runningAggregatePort)["memberPorts"][0];
  EXPECT_EQ(member["memberPortID"].asInt(), portIdForName(*memberPort));
  EXPECT_EQ(member["activity"].asInt(), 0);
  EXPECT_EQ(member["rate"].asInt(), 0);
  EXPECT_EQ(member["priority"].asInt(), 1000);
  EXPECT_EQ(member["holdTimerMultiplier"].asInt(), 5);

  XLOG(INFO) << "[Step 3] Remove minimum-capacity-to-up";
  ASSERT_TRUE(runCliSuccessfully(
      {"delete",
       "aggregate-port",
       aggregatePort_.name,
       "minimum-capacity-to-up"}));
  commitAndWait();

  runningAggregatePort = findAggregatePort();
  ASSERT_TRUE(runningAggregatePort.has_value());
  EXPECT_EQ(runningAggregatePort->count("minimumCapacityToUp"), 0);

  XLOG(INFO) << "[Step 4] Stage member removal " << *memberPort;
  ASSERT_TRUE(runCliSuccessfully(
      {"delete",
       "aggregate-port",
       aggregatePort_.name,
       "member",
       *memberPort}));

  XLOG(INFO) << "[Step 5] Stage aggregate-port deletion";
  ASSERT_TRUE(
      runCliSuccessfully({"delete", "aggregate-port", aggregatePort_.name}));
  commitAndWait();
  aggregatePortMayExist_ = false;
  EXPECT_FALSE(findAggregatePort().has_value());

  XLOG(INFO) << "[Step 6] Verify all delete forms are idempotent";
  ASSERT_TRUE(
      runCliSuccessfully({"delete", "aggregate-port", aggregatePort_.name}));
  ASSERT_TRUE(runCliSuccessfully(
      {"delete",
       "aggregate-port",
       aggregatePort_.name,
       "member",
       *memberPort}));
  ASSERT_TRUE(runCliSuccessfully(
      {"delete",
       "aggregate-port",
       aggregatePort_.name,
       "minimum-capacity-to-up"}));
  EXPECT_FALSE(findAggregatePort().has_value());
}

} // namespace facebook::fboss
