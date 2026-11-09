/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <stdexcept>
#include <string>

#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/commands/delete/traffic_counter/CmdDeleteTrafficCounter.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdDeleteTrafficCounterTestFixture : public CmdConfigTestBase {
 public:
  // trafficCounters mirror the ttld-* counters (types [PACKETS, BYTES]) seen
  // on production devices; one is attached via cpuTrafficPolicy and one via
  // dataPlaneTrafficPolicy so the referenced-counter refusal has realistic
  // referrers.
  CmdDeleteTrafficCounterTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_traffic_counter_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "trafficCounters": [
      {"name": "ttld-prod-private", "types": [0, 1]},
      {"name": "ttld-prod-public", "types": [0, 1]},
      {"name": "ttld-interconnect", "types": [0, 1]}
    ],
    "cpuTrafficPolicy": {
      "trafficPolicy": {
        "matchToAction": [
          {
            "matcher": "cpuPolicing-high-NetworkControl",
            "action": {"counter": "ttld-prod-private"}
          }
        ]
      }
    },
    "dataPlaneTrafficPolicy": {
      "matchToAction": [
        {
          "matcher": "ttld-acl-public",
          "action": {"counter": "ttld-prod-public"}
        }
      ]
    }
  }
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession("delete traffic-counter", "ttld-interconnect");
  }

  static bool hasCounter(const std::string& name) {
    const auto& counters =
        *ConfigSession::getInstance().getAgentConfig().sw()->trafficCounters();
    return std::any_of(
        counters.cbegin(), counters.cend(), [&name](const auto& counter) {
          return *counter.name() == name;
        });
  }

  static size_t counterCount() {
    return ConfigSession::getInstance()
        .getAgentConfig()
        .sw()
        ->trafficCounters()
        ->size();
  }
};

// Argument validation

TEST_F(CmdDeleteTrafficCounterTestFixture, argValid) {
  TrafficCounterNameArg arg({"ttld-interconnect"});
  EXPECT_EQ(arg.getName(), "ttld-interconnect");
}

TEST_F(CmdDeleteTrafficCounterTestFixture, argWrongArity) {
  EXPECT_THROW(TrafficCounterNameArg({}), std::invalid_argument);
  EXPECT_THROW(TrafficCounterNameArg({"a", "b"}), std::invalid_argument);
}

TEST_F(CmdDeleteTrafficCounterTestFixture, argEmptyName) {
  EXPECT_THROW(TrafficCounterNameArg({""}), std::invalid_argument);
}

// queryClient

TEST_F(CmdDeleteTrafficCounterTestFixture, deleteUnreferencedCounter) {
  ASSERT_TRUE(hasCounter("ttld-interconnect"));

  auto cmd = CmdDeleteTrafficCounter();
  auto result = cmd.queryClient(
      localhost(), TrafficCounterNameArg({"ttld-interconnect"}));

  EXPECT_THAT(result, HasSubstr("deleted traffic counter"));
  EXPECT_THAT(result, HasSubstr("ttld-interconnect"));

  EXPECT_FALSE(hasCounter("ttld-interconnect"));
  // The other counters are untouched.
  EXPECT_EQ(counterCount(), 2);
  EXPECT_TRUE(hasCounter("ttld-prod-private"));
  EXPECT_TRUE(hasCounter("ttld-prod-public"));
}

TEST_F(
    CmdDeleteTrafficCounterTestFixture,
    deleteCpuPolicyReferencedCounterRefused) {
  auto cmd = CmdDeleteTrafficCounter();
  try {
    cmd.queryClient(localhost(), TrafficCounterNameArg({"ttld-prod-private"}));
    FAIL() << "Expected FbossError";
  } catch (const FbossError& e) {
    EXPECT_THAT(std::string(e.what()), HasSubstr("still referenced"));
    EXPECT_THAT(std::string(e.what()), HasSubstr("cpuTrafficPolicy"));
    EXPECT_THAT(
        std::string(e.what()), HasSubstr("cpuPolicing-high-NetworkControl"));
  }
  EXPECT_TRUE(hasCounter("ttld-prod-private"));
}

TEST_F(
    CmdDeleteTrafficCounterTestFixture,
    deleteDataPlanePolicyReferencedCounterRefused) {
  auto cmd = CmdDeleteTrafficCounter();
  try {
    cmd.queryClient(localhost(), TrafficCounterNameArg({"ttld-prod-public"}));
    FAIL() << "Expected FbossError";
  } catch (const FbossError& e) {
    EXPECT_THAT(std::string(e.what()), HasSubstr("dataPlaneTrafficPolicy"));
    EXPECT_THAT(std::string(e.what()), HasSubstr("ttld-acl-public"));
  }
  EXPECT_TRUE(hasCounter("ttld-prod-public"));
}

TEST_F(CmdDeleteTrafficCounterTestFixture, deleteAbsentCounterIsNoop) {
  const auto sessionBefore = sessionConfigText();
  auto cmd = CmdDeleteTrafficCounter();
  auto result =
      cmd.queryClient(localhost(), TrafficCounterNameArg({"no-such-counter"}));
  EXPECT_THAT(result, HasSubstr("not found, nothing to delete"));
  EXPECT_EQ(counterCount(), 3);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

} // namespace facebook::fboss
