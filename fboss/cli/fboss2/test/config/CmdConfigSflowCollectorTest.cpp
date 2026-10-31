/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/json.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits>

#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/commands/config/sflow_collector/CmdConfigSflowCollector.h"
#include "fboss/cli/fboss2/commands/delete/sflow_collector/CmdDeleteSflowCollector.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigSflowCollectorTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigSflowCollectorTestFixture()
      : CmdConfigTestBase(
            "fboss_sflow_collector_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "sFlowCollectors": [
      {"ip": "192.0.2.10", "port": 6343},
      {"ip": "2001:db8::1", "port": 6343}
    ]
  }
})") {}

 protected:
  static const std::vector<cfg::SflowCollector>& collectors() {
    return *ConfigSession::getInstance()
                .getAgentConfig()
                .sw()
                ->sFlowCollectors();
  }
};

TEST_F(CmdConfigSflowCollectorTestFixture, validatesAndCanonicalizesArguments) {
  {
    SflowCollectorArg arg({"172.25.165.41", "6343"});
    EXPECT_EQ(arg.getCanonicalIp(), "172.25.165.41");
    EXPECT_EQ(arg.getPort(), 6343);
  }
  {
    SflowCollectorArg arg({"2001:0db8:0:0:0:0:0:2", "32767"});
    EXPECT_EQ(arg.getCanonicalIp(), "2001:db8::2");
    EXPECT_EQ(arg.getPort(), std::numeric_limits<int16_t>::max());
  }
}

TEST_F(CmdConfigSflowCollectorTestFixture, rejectsInvalidArguments) {
  EXPECT_THROW(SflowCollectorArg({}), std::invalid_argument);
  EXPECT_THROW(SflowCollectorArg({"192.0.2.1"}), std::invalid_argument);
  EXPECT_THROW(
      SflowCollectorArg({"192.0.2.1", "6343", "extra"}), std::invalid_argument);
  EXPECT_THROW(SflowCollectorArg({"not-an-ip", "6343"}), std::invalid_argument);
  EXPECT_THROW(
      SflowCollectorArg({"192.0.2.1", "not-a-port"}), std::invalid_argument);
  EXPECT_THROW(SflowCollectorArg({"192.0.2.1", "0"}), std::invalid_argument);
  EXPECT_THROW(SflowCollectorArg({"192.0.2.1", "-1"}), std::invalid_argument);
  EXPECT_THROW(
      SflowCollectorArg({"192.0.2.1", "32768"}), std::invalid_argument);
  EXPECT_THROW(
      SflowCollectorArg({"192.0.2.1", "999999999999999999999"}),
      std::invalid_argument);
}

TEST_F(CmdConfigSflowCollectorTestFixture, addsCollectorAndPreservesExisting) {
  setupTestableConfigSession("config sflow-collector", "172.25.165.41 6343");

  CmdConfigSflowCollector cmd;
  auto result = cmd.queryClient(
      HostInfo("testhost"), SflowCollectorArg({"172.25.165.41", "6343"}));

  EXPECT_THAT(result, HasSubstr("Successfully added"));
  ASSERT_EQ(collectors().size(), 3);
  EXPECT_EQ(*collectors()[0].ip(), "192.0.2.10");
  EXPECT_EQ(*collectors()[1].ip(), "2001:db8::1");
  EXPECT_EQ(*collectors()[2].ip(), "172.25.165.41");
  EXPECT_EQ(*collectors()[2].port(), 6343);

  auto staged = folly::parseJson(readFile(getSessionConfigPath()));
  ASSERT_EQ(staged["sw"]["sFlowCollectors"].size(), 3);
  EXPECT_EQ(
      staged["sw"]["sFlowCollectors"][2]["ip"].asString(), "172.25.165.41");
  EXPECT_EQ(staged["sw"]["sFlowCollectors"][2]["port"].asInt(), 6343);
}

TEST_F(CmdConfigSflowCollectorTestFixture, identicalCollectorIsIdempotent) {
  setupTestableConfigSession(
      "config sflow-collector", "2001:0db8:0:0:0:0:0:1 6343");

  CmdConfigSflowCollector cmd;
  auto result = cmd.queryClient(
      HostInfo("testhost"),
      SflowCollectorArg({"2001:0db8:0:0:0:0:0:1", "6343"}));

  EXPECT_THAT(result, HasSubstr("already configured"));
  EXPECT_EQ(collectors().size(), 2);
}

TEST_F(CmdConfigSflowCollectorTestFixture, sameIpDifferentPortIsDistinct) {
  setupTestableConfigSession("config sflow-collector", "192.0.2.10 1234");

  CmdConfigSflowCollector cmd;
  cmd.queryClient(
      HostInfo("testhost"), SflowCollectorArg({"192.0.2.10", "1234"}));

  ASSERT_EQ(collectors().size(), 3);
  EXPECT_EQ(*collectors()[2].ip(), "192.0.2.10");
  EXPECT_EQ(*collectors()[2].port(), 1234);
}

TEST_F(CmdConfigSflowCollectorTestFixture, deletesExactCollectorOnly) {
  setupTestableConfigSession("delete sflow-collector", "192.0.2.10 6343");

  CmdDeleteSflowCollector cmd;
  auto result = cmd.queryClient(
      HostInfo("testhost"), SflowCollectorArg({"192.0.2.10", "6343"}));

  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  ASSERT_EQ(collectors().size(), 1);
  EXPECT_EQ(*collectors()[0].ip(), "2001:db8::1");
  EXPECT_EQ(*collectors()[0].port(), 6343);
}

TEST_F(CmdConfigSflowCollectorTestFixture, deleteMatchesCanonicalAddress) {
  setupTestableConfigSession(
      "delete sflow-collector", "2001:0db8:0:0:0:0:0:1 6343");

  CmdDeleteSflowCollector cmd;
  cmd.queryClient(
      HostInfo("testhost"),
      SflowCollectorArg({"2001:0db8:0:0:0:0:0:1", "6343"}));

  ASSERT_EQ(collectors().size(), 1);
  EXPECT_EQ(*collectors()[0].ip(), "192.0.2.10");
}

TEST_F(CmdConfigSflowCollectorTestFixture, deleteRemovesCanonicalDuplicates) {
  setupTestableConfigSession("delete sflow-collector", "2001:db8::1 6343");

  auto& mutableCollectors =
      *ConfigSession::getInstance().getAgentConfig().sw()->sFlowCollectors();
  cfg::SflowCollector duplicate;
  duplicate.ip() = "2001:0db8:0:0:0:0:0:1";
  duplicate.port() = 6343;
  mutableCollectors.push_back(std::move(duplicate));

  CmdDeleteSflowCollector cmd;
  cmd.queryClient(
      HostInfo("testhost"), SflowCollectorArg({"2001:db8::1", "6343"}));

  ASSERT_EQ(collectors().size(), 1);
  EXPECT_EQ(*collectors()[0].ip(), "192.0.2.10");
}

TEST_F(CmdConfigSflowCollectorTestFixture, deleteMissingCollectorFailsSafely) {
  setupTestableConfigSession("delete sflow-collector", "192.0.2.99 6343");

  CmdDeleteSflowCollector cmd;
  EXPECT_THROW(
      cmd.queryClient(
          HostInfo("testhost"), SflowCollectorArg({"192.0.2.99", "6343"})),
      FbossError);
  EXPECT_EQ(collectors().size(), 2);
}

} // namespace facebook::fboss
