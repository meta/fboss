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
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/delete/arp/CmdDeleteArp.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed uses non-default values for every ARP timer so each reset is
// observable (thrift defaults: timeout 60, ager 5, probes 300, stale 10).
class CmdDeleteArpTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteArpTestFixture()
      : CmdConfigTestBase(
            "fboss_arp_delete_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "arpTimeoutSeconds": 120,
    "arpAgerInterval": 7,
    "maxNeighborProbes": 500,
    "staleEntryInterval": 20
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete arp";
};

// =============================================================
// ArpDeleteAttrs validation tests
// =============================================================

TEST_F(CmdDeleteArpTestFixture, argValidation_valid) {
  ArpDeleteAttrs single({"timeout"});
  EXPECT_THAT(single.getAttributes(), ElementsAre("timeout"));

  ArpDeleteAttrs multi(
      {"timeout", "age-interval", "max-probes", "stale-interval"});
  EXPECT_THAT(
      multi.getAttributes(),
      ElementsAre("timeout", "age-interval", "max-probes", "stale-interval"));
}

TEST_F(CmdDeleteArpTestFixture, argValidation_empty) {
  EXPECT_THROW(ArpDeleteAttrs({}), std::invalid_argument);
}

TEST_F(CmdDeleteArpTestFixture, argValidation_unknownAttr) {
  EXPECT_THROW(ArpDeleteAttrs({"unknown"}), std::invalid_argument);
  // refresh is intentionally NOT in the valid set — deferred (see
  // CmdConfigArp.h: arpRefreshSeconds is not applied by the agent)
  EXPECT_THROW(ArpDeleteAttrs({"refresh"}), std::invalid_argument);
  // Case-sensitive: "Timeout" not accepted
  EXPECT_THROW(ArpDeleteAttrs({"Timeout"}), std::invalid_argument);
  // One bad attr poisons the whole list
  EXPECT_THROW(ArpDeleteAttrs({"timeout", "bogus"}), std::invalid_argument);
}

TEST_F(CmdDeleteArpTestFixture, argValidation_duplicate) {
  EXPECT_THROW(ArpDeleteAttrs({"timeout", "timeout"}), std::invalid_argument);
}

// =============================================================
// queryClient() tests — one per attribute, plus all-at-once
// =============================================================

TEST_F(CmdDeleteArpTestFixture, resetTimeout) {
  setupTestableConfigSession(cmdPrefix_, "timeout");
  CmdDeleteArp cmd;
  HostInfo hostInfo("testhost");
  ArpDeleteAttrs args({"timeout"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("timeout"));
  EXPECT_THAT(result, HasSubstr("60"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->arpTimeoutSeconds(), 60);
  // Other attrs untouched
  EXPECT_EQ(*config.sw()->arpAgerInterval(), 7);
  EXPECT_EQ(*config.sw()->maxNeighborProbes(), 500);
  EXPECT_EQ(*config.sw()->staleEntryInterval(), 20);
}

TEST_F(CmdDeleteArpTestFixture, resetAgeInterval) {
  setupTestableConfigSession(cmdPrefix_, "age-interval");
  CmdDeleteArp cmd;
  HostInfo hostInfo("testhost");
  ArpDeleteAttrs args({"age-interval"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("age-interval"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->arpAgerInterval(), 5);
  EXPECT_EQ(*config.sw()->arpTimeoutSeconds(), 120);
}

TEST_F(CmdDeleteArpTestFixture, resetMaxProbes) {
  setupTestableConfigSession(cmdPrefix_, "max-probes");
  CmdDeleteArp cmd;
  HostInfo hostInfo("testhost");
  ArpDeleteAttrs args({"max-probes"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("max-probes"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->maxNeighborProbes(), 300);
  EXPECT_EQ(*config.sw()->staleEntryInterval(), 20);
}

TEST_F(CmdDeleteArpTestFixture, resetStaleInterval) {
  setupTestableConfigSession(cmdPrefix_, "stale-interval");
  CmdDeleteArp cmd;
  HostInfo hostInfo("testhost");
  ArpDeleteAttrs args({"stale-interval"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("stale-interval"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->staleEntryInterval(), 10);
  EXPECT_EQ(*config.sw()->maxNeighborProbes(), 500);
}

TEST_F(CmdDeleteArpTestFixture, resetAllAttrs) {
  setupTestableConfigSession(
      cmdPrefix_, "timeout age-interval max-probes stale-interval");
  CmdDeleteArp cmd;
  HostInfo hostInfo("testhost");
  ArpDeleteAttrs args(
      {"timeout", "age-interval", "max-probes", "stale-interval"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("timeout"));
  EXPECT_THAT(result, HasSubstr("age-interval"));
  EXPECT_THAT(result, HasSubstr("max-probes"));
  EXPECT_THAT(result, HasSubstr("stale-interval"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->arpTimeoutSeconds(), 60);
  EXPECT_EQ(*config.sw()->arpAgerInterval(), 5);
  EXPECT_EQ(*config.sw()->maxNeighborProbes(), 300);
  EXPECT_EQ(*config.sw()->staleEntryInterval(), 10);
}

TEST_F(CmdDeleteArpTestFixture, idempotentOnDefaults) {
  // Resetting an attr already at its default succeeds without error.
  setupTestableConfigSession(cmdPrefix_, "timeout");
  CmdDeleteArp cmd;
  HostInfo hostInfo("testhost");
  ArpDeleteAttrs args({"timeout"});

  cmd.queryClient(hostInfo, args);
  cmd.queryClient(hostInfo, args);

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->arpTimeoutSeconds(), 60);
}

} // namespace facebook::fboss
