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
#include <algorithm>
#include <filesystem>
#include <string>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"
#include "fboss/cli/fboss2/commands/config/vlan/VlanManager.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigVlanTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigVlanTestFixture()
      : CmdConfigTestBase(
            "fboss2_config_vlan_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "vlans": [
      {
        "id": 100,
        "name": "vlan100"
      }
    ],
    "interfaces": [
      {"intfID": 100, "vlanID": 100, "routerID": 0, "type": 1, "mtu": 9412}
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config vlan";
};

// "config vlan <id>" with a new id creates the VLAN and its barebone
// interface, and persists the session config to disk.
TEST_F(CmdConfigVlanTestFixture, queryClientCreatesVlan) {
  setupTestableConfigSession(cmdPrefix_, "4005");
  auto cmd = CmdConfigVlan();
  VlanId vlanId({"4005"});

  auto result = cmd.queryClient(localhost(), vlanId);

  EXPECT_THAT(result, HasSubstr("Successfully created VLAN 4005"));

  auto& swConfig =
      *ConfigSession::getInstance().config<AgentConfigDomain>().sw();
  EXPECT_NE(VlanManager::findVlan(swConfig, VlanID(4005)), nullptr);
  bool intfFound = std::any_of(
      swConfig.interfaces()->cbegin(),
      swConfig.interfaces()->cend(),
      [](const auto& intf) { return *intf.vlanID() == 4005; });
  EXPECT_TRUE(intfFound);

  // The change must be persisted to the on-disk session config.
  ASSERT_TRUE(std::filesystem::exists(getSessionConfigPath()));
  auto sessionContent = readFile(getSessionConfigPath());
  EXPECT_THAT(sessionContent, HasSubstr("Vlan4005"));
  EXPECT_THAT(sessionContent, HasSubstr("fboss4005"));
}

// "config vlan <id>" with an existing id reports it and doesn't duplicate.
TEST_F(CmdConfigVlanTestFixture, queryClientVlanAlreadyExists) {
  setupTestableConfigSession(cmdPrefix_, "100");
  auto cmd = CmdConfigVlan();
  VlanId vlanId({"100"});

  auto result = cmd.queryClient(localhost(), vlanId);

  EXPECT_THAT(result, HasSubstr("VLAN 100 already exists"));

  auto& swConfig =
      *ConfigSession::getInstance().config<AgentConfigDomain>().sw();
  int count = 0;
  for (const auto& vlan : *swConfig.vlans()) {
    if (*vlan.id() == 100) {
      ++count;
    }
  }
  EXPECT_EQ(count, 1);
}

} // namespace facebook::fboss
