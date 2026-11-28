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
#include "fboss/cli/fboss2/commands/config/arp/CmdConfigArp.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigArpTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigArpTestFixture()
      : CmdConfigTestBase(
            "fboss_arp_config_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "arpTimeoutSeconds": 60,
    "arpAgerInterval": 5,
    "maxNeighborProbes": 300,
    "staleEntryInterval": 10
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config arp";
};

// =============================================================
// ArpConfigArgs validation tests
// =============================================================

TEST_F(CmdConfigArpTestFixture, argValidation_valid) {
  ArpConfigArgs a({"timeout", "45"});
  EXPECT_EQ(a.getAttribute(), "timeout");
  EXPECT_EQ(a.getValue(), 45);

  ArpConfigArgs b({"age-interval", "3"});
  EXPECT_EQ(b.getAttribute(), "age-interval");
  EXPECT_EQ(b.getValue(), 3);

  ArpConfigArgs c({"max-probes", "1000"});
  EXPECT_EQ(c.getAttribute(), "max-probes");
  EXPECT_EQ(c.getValue(), 1000);

  ArpConfigArgs d({"stale-interval", "0"});
  EXPECT_EQ(d.getAttribute(), "stale-interval");
  EXPECT_EQ(d.getValue(), 0);
}

TEST_F(CmdConfigArpTestFixture, argValidation_badArity) {
  EXPECT_THROW(ArpConfigArgs({}), std::invalid_argument);
  EXPECT_THROW(ArpConfigArgs({"timeout"}), std::invalid_argument);
  EXPECT_THROW(
      ArpConfigArgs({"timeout", "45", "extra"}), std::invalid_argument);
}

TEST_F(CmdConfigArpTestFixture, argValidation_unknownAttr) {
  EXPECT_THROW(ArpConfigArgs({"unknown", "10"}), std::invalid_argument);
  // refresh is intentionally NOT in the valid set — deferred (NOS-5734)
  EXPECT_THROW(ArpConfigArgs({"refresh", "20"}), std::invalid_argument);
  // Case-sensitive: "Timeout" not accepted
  EXPECT_THROW(ArpConfigArgs({"Timeout", "10"}), std::invalid_argument);
}

TEST_F(CmdConfigArpTestFixture, argValidation_nonInteger) {
  EXPECT_THROW(ArpConfigArgs({"timeout", "abc"}), std::invalid_argument);
  EXPECT_THROW(ArpConfigArgs({"timeout", "3.14"}), std::invalid_argument);
  EXPECT_THROW(ArpConfigArgs({"timeout", ""}), std::invalid_argument);
}

TEST_F(CmdConfigArpTestFixture, argValidation_negative) {
  EXPECT_THROW(ArpConfigArgs({"timeout", "-1"}), std::invalid_argument);
  EXPECT_THROW(ArpConfigArgs({"age-interval", "-5"}), std::invalid_argument);
}

// =============================================================
// queryClient() tests — one per attribute
// =============================================================

TEST_F(CmdConfigArpTestFixture, setTimeout) {
  setupTestableConfigSession(cmdPrefix_, "timeout 120");
  CmdConfigArp cmd;
  HostInfo hostInfo("testhost");
  ArpConfigArgs args({"timeout", "120"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("timeout"));
  EXPECT_THAT(result, HasSubstr("120"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->arpTimeoutSeconds(), 120);
}

TEST_F(CmdConfigArpTestFixture, setAgeInterval) {
  setupTestableConfigSession(cmdPrefix_, "age-interval 7");
  CmdConfigArp cmd;
  HostInfo hostInfo("testhost");
  ArpConfigArgs args({"age-interval", "7"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("age-interval"));
  EXPECT_THAT(result, HasSubstr("7"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->arpAgerInterval(), 7);
}

TEST_F(CmdConfigArpTestFixture, setMaxProbes) {
  setupTestableConfigSession(cmdPrefix_, "max-probes 500");
  CmdConfigArp cmd;
  HostInfo hostInfo("testhost");
  ArpConfigArgs args({"max-probes", "500"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("max-probes"));
  EXPECT_THAT(result, HasSubstr("500"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->maxNeighborProbes(), 500);
}

TEST_F(CmdConfigArpTestFixture, setStaleInterval) {
  setupTestableConfigSession(cmdPrefix_, "stale-interval 15");
  CmdConfigArp cmd;
  HostInfo hostInfo("testhost");
  ArpConfigArgs args({"stale-interval", "15"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("stale-interval"));
  EXPECT_THAT(result, HasSubstr("15"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->staleEntryInterval(), 15);
}

TEST_F(CmdConfigArpTestFixture, idempotentSameValue) {
  // Setting the same value twice should succeed without error.
  setupTestableConfigSession(cmdPrefix_, "timeout 90");
  CmdConfigArp cmd;
  HostInfo hostInfo("testhost");
  ArpConfigArgs args({"timeout", "90"});

  cmd.queryClient(hostInfo, args);
  cmd.queryClient(hostInfo, args);

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->arpTimeoutSeconds(), 90);
}

} // namespace facebook::fboss
