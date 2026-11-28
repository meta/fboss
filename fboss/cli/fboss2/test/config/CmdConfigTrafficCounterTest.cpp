/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/traffic_counter/CmdConfigTrafficCounter.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/PortMap.h" // NOLINT(misc-include-cleaner)

using namespace ::testing;

namespace facebook::fboss {

// Seed config carries a pre-existing traffic counter so we can exercise both
// create-new and update-existing code paths.
class CmdConfigTrafficCounterTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigTrafficCounterTestFixture()
      : CmdConfigTestBase(
            "fboss_traffic_counter_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "trafficCounters": [
      {"name": "existing_counter", "types": [0]}
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config traffic-counter";
};

// ==============================================================================
// TrafficCounterArg Validation Tests
// ==============================================================================

TEST_F(CmdConfigTrafficCounterTestFixture, argValidation) {
  // Valid: name + single type
  {
    TrafficCounterArg arg({"my_counter", "PACKETS"});
    EXPECT_EQ(arg.getName(), "my_counter");
    EXPECT_EQ(arg.getTypes(), std::vector{cfg::CounterType::PACKETS});
  }

  // Valid: name + bytes
  {
    TrafficCounterArg arg({"c1", "BYTES"});
    EXPECT_EQ(arg.getTypes(), std::vector{cfg::CounterType::BYTES});
  }

  // Valid: name + both types spelled out
  {
    TrafficCounterArg arg({"c1", "PACKETS,BYTES"});
    EXPECT_EQ(
        arg.getTypes(),
        (std::vector{cfg::CounterType::PACKETS, cfg::CounterType::BYTES}));
  }

  // Valid: types are stored in a fixed order, whatever order they were typed
  {
    TrafficCounterArg arg({"c1", "BYTES,PACKETS"});
    EXPECT_EQ(
        arg.getTypes(),
        (std::vector{cfg::CounterType::PACKETS, cfg::CounterType::BYTES}));
  }

  // Valid: types are case-insensitive
  {
    TrafficCounterArg arg({"c1", "packets,bytes"});
    EXPECT_EQ(
        arg.getTypes(),
        (std::vector{cfg::CounterType::PACKETS, cfg::CounterType::BYTES}));
  }

  // Valid: a repeated type collapses to one entry
  {
    TrafficCounterArg arg({"c1", "BYTES,BYTES"});
    EXPECT_EQ(arg.getTypes(), std::vector{cfg::CounterType::BYTES});
  }

  // Invalid: empty
  EXPECT_THROW(TrafficCounterArg({}), std::invalid_argument);

  // Invalid: name without types
  EXPECT_THROW(TrafficCounterArg({"c1"}), std::invalid_argument);

  // Invalid: too many args
  EXPECT_THROW(
      TrafficCounterArg({"c1", "PACKETS", "extra"}), std::invalid_argument);

  // Invalid: empty name
  EXPECT_THROW(TrafficCounterArg({"", "PACKETS"}), std::invalid_argument);

  // Invalid: unknown type
  EXPECT_THROW(TrafficCounterArg({"c1", "KILOBYTES"}), std::invalid_argument);

  // Invalid: unknown type mixed in with a known one
  EXPECT_THROW(
      TrafficCounterArg({"c1", "PACKETS,KILOBYTES"}), std::invalid_argument);

  // Invalid: trailing separator leaves an empty type
  EXPECT_THROW(TrafficCounterArg({"c1", "PACKETS,"}), std::invalid_argument);

  // Invalid: 'both' is no longer a type; the types must be listed explicitly
  EXPECT_THROW(TrafficCounterArg({"c1", "both"}), std::invalid_argument);
}

// ==============================================================================
// Command Execution Tests
// ==============================================================================

TEST_F(CmdConfigTrafficCounterTestFixture, createNewCounterSingleType) {
  setupTestableConfigSession(cmdPrefix_, "new_counter PACKETS");
  CmdConfigTrafficCounter cmd;
  HostInfo hostInfo("testhost");
  TrafficCounterArg arg({"new_counter", "PACKETS"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("new_counter"));
  EXPECT_THAT(result, HasSubstr("PACKETS"));

  auto& counters = *ConfigSession::getInstance()
                        .config<AgentConfigDomain>()
                        .sw()
                        ->trafficCounters();
  ASSERT_EQ(counters.size(), 2);
  EXPECT_EQ(*counters[1].name(), "new_counter");
  EXPECT_EQ(*counters[1].types(), std::vector{cfg::CounterType::PACKETS});
}

TEST_F(CmdConfigTrafficCounterTestFixture, createNewCounterBothTypes) {
  setupTestableConfigSession(cmdPrefix_, "stat_both PACKETS,BYTES");
  CmdConfigTrafficCounter cmd;
  HostInfo hostInfo("testhost");
  TrafficCounterArg arg({"stat_both", "PACKETS,BYTES"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("PACKETS,BYTES"));

  auto& counters = *ConfigSession::getInstance()
                        .config<AgentConfigDomain>()
                        .sw()
                        ->trafficCounters();
  ASSERT_EQ(counters.size(), 2);
  EXPECT_EQ(*counters[1].name(), "stat_both");
  EXPECT_EQ(
      *counters[1].types(),
      (std::vector{cfg::CounterType::PACKETS, cfg::CounterType::BYTES}));
}

TEST_F(CmdConfigTrafficCounterTestFixture, updateExistingCounter) {
  setupTestableConfigSession(cmdPrefix_, "existing_counter BYTES");
  CmdConfigTrafficCounter cmd;
  HostInfo hostInfo("testhost");
  TrafficCounterArg arg({"existing_counter", "BYTES"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("updated"));
  EXPECT_THAT(result, HasSubstr("BYTES"));

  auto& counters = *ConfigSession::getInstance()
                        .config<AgentConfigDomain>()
                        .sw()
                        ->trafficCounters();
  ASSERT_EQ(counters.size(), 1);
  EXPECT_EQ(*counters[0].name(), "existing_counter");
  EXPECT_EQ(*counters[0].types(), std::vector{cfg::CounterType::BYTES});
}

TEST_F(CmdConfigTrafficCounterTestFixture, alreadyConfigured) {
  // Seed already has existing_counter with type PACKETS
  setupTestableConfigSession(cmdPrefix_, "existing_counter PACKETS");
  CmdConfigTrafficCounter cmd;
  HostInfo hostInfo("testhost");
  TrafficCounterArg arg({"existing_counter", "PACKETS"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("already"));

  auto& counters = *ConfigSession::getInstance()
                        .config<AgentConfigDomain>()
                        .sw()
                        ->trafficCounters();
  ASSERT_EQ(counters.size(), 1);
}

} // namespace facebook::fboss
