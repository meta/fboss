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
#include "fboss/cli/fboss2/commands/config/switch/admin_distance/CmdConfigAdminDistance.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed mirrors the production clientIdToAdminDistance map shipped on Meta
// devices: every routing client is mapped (BGP=20, static=1, interface/
// linklocal/remote=0, local-BGP/OpenR=10, internal=255). The CLI mutates a
// single entry, so the test asserts the other entries survive untouched.
class CmdConfigAdminDistanceTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigAdminDistanceTestFixture()
      : CmdConfigTestBase(
            "fboss_admin_distance_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "clientIdToAdminDistance": {
      "0": 20,
      "1": 1,
      "2": 0,
      "3": 0,
      "4": 0,
      "700": 255,
      "786": 10
    }
  }
})") {}

 protected:
  const std::string setPrefix_ = "config switch admin-distance";
};

// ==============================================================================
// AdminDistanceArg Validation Tests
// ==============================================================================

TEST_F(CmdConfigAdminDistanceTestFixture, argValidation) {
  // Valid values
  EXPECT_EQ(AdminDistanceArg({"0", "20"}).getClientId(), 0);
  EXPECT_EQ(AdminDistanceArg({"0", "20"}).getDistance(), 20);
  EXPECT_EQ(AdminDistanceArg({"786", "10"}).getClientId(), 786);
  EXPECT_EQ(AdminDistanceArg({"700", "255"}).getDistance(), 255);

  // Invalid: empty
  EXPECT_THROW(AdminDistanceArg({}), std::invalid_argument);

  // Invalid: wrong arity
  EXPECT_THROW(AdminDistanceArg({"0"}), std::invalid_argument);
  EXPECT_THROW(AdminDistanceArg({"0", "20", "30"}), std::invalid_argument);

  // Invalid: non-integer
  EXPECT_THROW(AdminDistanceArg({"bgp", "20"}), std::invalid_argument);
  EXPECT_THROW(AdminDistanceArg({"0", "twenty"}), std::invalid_argument);
  EXPECT_THROW(AdminDistanceArg({"0", "20.5"}), std::invalid_argument);

  // Invalid: negative client-id
  EXPECT_THROW(AdminDistanceArg({"-1", "20"}), std::invalid_argument);

  // Invalid: distance out of [0, 255]
  EXPECT_THROW(AdminDistanceArg({"0", "-1"}), std::invalid_argument);
  EXPECT_THROW(AdminDistanceArg({"0", "256"}), std::invalid_argument);

  // Invalid: forbidden client IDs whose admin distance is hardcoded in agent.
  // STATIC_ROUTE(1)           -> AdminDistance::STATIC_ROUTE
  // INTERFACE_ROUTE(2)        -> AdminDistance::DIRECTLY_CONNECTED
  // LINKLOCAL_ROUTE(3)        -> AdminDistance::DIRECTLY_CONNECTED
  // REMOTE_INTERFACE_ROUTE(4) -> AdminDistance::DIRECTLY_CONNECTED
  EXPECT_THROW(AdminDistanceArg({"1", "5"}), std::invalid_argument);
  EXPECT_THROW(AdminDistanceArg({"2", "5"}), std::invalid_argument);
  EXPECT_THROW(AdminDistanceArg({"3", "5"}), std::invalid_argument);
  EXPECT_THROW(AdminDistanceArg({"4", "5"}), std::invalid_argument);
}

// ==============================================================================
// Set Command Tests
// ==============================================================================

TEST_F(CmdConfigAdminDistanceTestFixture, updateExistingClient) {
  setupTestableConfigSession(setPrefix_, "0 30");
  CmdConfigAdminDistance cmd;
  HostInfo hostInfo("testhost");
  AdminDistanceArg arg({"0", "30"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("30"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  const auto& map = *config.sw()->clientIdToAdminDistance();
  EXPECT_EQ(map.at(0), 30);

  // Other entries must survive untouched.
  EXPECT_EQ(map.at(1), 1);
  EXPECT_EQ(map.at(2), 0);
  EXPECT_EQ(map.at(3), 0);
  EXPECT_EQ(map.at(4), 0);
  EXPECT_EQ(map.at(700), 255);
  EXPECT_EQ(map.at(786), 10);
  EXPECT_EQ(map.size(), 7);
}

TEST_F(CmdConfigAdminDistanceTestFixture, addNewClient) {
  setupTestableConfigSession(setPrefix_, "800 2");
  CmdConfigAdminDistance cmd;
  HostInfo hostInfo("testhost");
  AdminDistanceArg arg({"800", "2"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("800"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  const auto& map = *config.sw()->clientIdToAdminDistance();
  EXPECT_EQ(map.at(800), 2);
  // All original entries preserved; new key added.
  EXPECT_EQ(map.at(0), 20);
  EXPECT_EQ(map.at(1), 1);
  EXPECT_EQ(map.at(2), 0);
  EXPECT_EQ(map.at(3), 0);
  EXPECT_EQ(map.at(4), 0);
  EXPECT_EQ(map.at(700), 255);
  EXPECT_EQ(map.at(786), 10);
  EXPECT_EQ(map.size(), 8);
}

TEST_F(CmdConfigAdminDistanceTestFixture, alreadySet) {
  // Seed has client-id 786 -> 10; setting to 10 is a no-op.
  setupTestableConfigSession(setPrefix_, "786 10");
  CmdConfigAdminDistance cmd;
  HostInfo hostInfo("testhost");
  AdminDistanceArg arg({"786", "10"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("already"));
}

} // namespace facebook::fboss
