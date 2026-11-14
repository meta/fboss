/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/commands/config/traffic_counter/CmdConfigTrafficCounter.h"
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

  auto& counters =
      *ConfigSession::getInstance().getAgentConfig().sw()->trafficCounters();
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

  auto& counters =
      *ConfigSession::getInstance().getAgentConfig().sw()->trafficCounters();
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

  auto& counters =
      *ConfigSession::getInstance().getAgentConfig().sw()->trafficCounters();
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

  auto& counters =
      *ConfigSession::getInstance().getAgentConfig().sw()->trafficCounters();
  ASSERT_EQ(counters.size(), 1);
}

TEST_F(CmdConfigTrafficCounterTestFixture, noAsicInConfigNoWarning) {
  setupTestableConfigSession(cmdPrefix_, "stat_both PACKETS,BYTES");
  CmdConfigTrafficCounter cmd;
  HostInfo hostInfo("testhost");
  TrafficCounterArg arg({"stat_both", "PACKETS,BYTES"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, Not(HasSubstr("Warning")));
}

// Seed config with an ASIC whose ACL_BYTE_COUNTER support is given by asicType.
class CmdConfigTrafficCounterAsicTestFixture : public CmdConfigTestBase {
 public:
  explicit CmdConfigTrafficCounterAsicTestFixture(int asicType)
      : CmdConfigTestBase(
            "fboss_traffic_counter_asic_test_%%%%-%%%%-%%%%-%%%%",
            fmt::format(
                R"({{
  "sw": {{
    "switchSettings": {{
      "switchIdToSwitchInfo": {{
        "0": {{
          "switchType": 0,
          "asicType": {},
          "switchIndex": 0,
          "portIdRange": {{"minimum": 0, "maximum": 2047}}
        }}
      }}
    }},
    "trafficCounters": [
      {{"name": "existing_counter", "types": [0]}}
    ]
  }}
}})",
                asicType)) {}

 protected:
  const std::string cmdPrefix_ = "config traffic-counter";
};

// ASIC_TYPE_G202X does not support ACL byte counters.
class CmdConfigTrafficCounterG202xTestFixture
    : public CmdConfigTrafficCounterAsicTestFixture {
 public:
  CmdConfigTrafficCounterG202xTestFixture()
      : CmdConfigTrafficCounterAsicTestFixture(
            static_cast<int>(cfg::AsicType::ASIC_TYPE_G202X)) {}
};

// ASIC_TYPE_TOMAHAWK5 supports ACL byte counters.
class CmdConfigTrafficCounterTomahawk5TestFixture
    : public CmdConfigTrafficCounterAsicTestFixture {
 public:
  CmdConfigTrafficCounterTomahawk5TestFixture()
      : CmdConfigTrafficCounterAsicTestFixture(
            static_cast<int>(cfg::AsicType::ASIC_TYPE_TOMAHAWK5)) {}
};

TEST_F(CmdConfigTrafficCounterG202xTestFixture, createBytesWarns) {
  setupTestableConfigSession(cmdPrefix_, "stat_both PACKETS,BYTES");
  CmdConfigTrafficCounter cmd;
  HostInfo hostInfo("testhost");
  TrafficCounterArg arg({"stat_both", "PACKETS,BYTES"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("Successfully created"));
  EXPECT_THAT(
      result,
      HasSubstr(
          "Warning: ASIC G202X does not support ACL byte counters; "
          "'stat_both.bytes' will always report 0"));

  // The warning does not block the change.
  auto& counters =
      *ConfigSession::getInstance().getAgentConfig().sw()->trafficCounters();
  ASSERT_EQ(counters.size(), 2);
  EXPECT_EQ(
      *counters[1].types(),
      (std::vector{cfg::CounterType::PACKETS, cfg::CounterType::BYTES}));
}

TEST_F(CmdConfigTrafficCounterG202xTestFixture, updateBytesWarns) {
  setupTestableConfigSession(cmdPrefix_, "existing_counter PACKETS,BYTES");
  CmdConfigTrafficCounter cmd;
  HostInfo hostInfo("testhost");
  TrafficCounterArg arg({"existing_counter", "PACKETS,BYTES"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("Successfully updated"));
  EXPECT_THAT(result, HasSubstr("'existing_counter.bytes'"));
  EXPECT_THAT(result, HasSubstr("Warning"));
}

TEST_F(CmdConfigTrafficCounterG202xTestFixture, packetsOnlyNoWarning) {
  setupTestableConfigSession(cmdPrefix_, "new_counter PACKETS");
  CmdConfigTrafficCounter cmd;
  HostInfo hostInfo("testhost");
  TrafficCounterArg arg({"new_counter", "PACKETS"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, Not(HasSubstr("Warning")));
}

TEST_F(CmdConfigTrafficCounterTomahawk5TestFixture, bytesSupportedNoWarning) {
  setupTestableConfigSession(cmdPrefix_, "stat_both PACKETS,BYTES");
  CmdConfigTrafficCounter cmd;
  HostInfo hostInfo("testhost");
  TrafficCounterArg arg({"stat_both", "PACKETS,BYTES"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("Successfully created"));
  EXPECT_THAT(result, Not(HasSubstr("Warning")));
}

} // namespace facebook::fboss
