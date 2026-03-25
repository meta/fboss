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
#include <cstddef>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlanConfig.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigVlanConfigTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigVlanConfigTestFixture()
      : CmdConfigTestBase(
            "fboss2_vlan_config_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "vlans": [
      {
        "id": 100,
        "name": "default"
      },
      {
        "id": 200,
        "name": "prod-vlan"
      }
    ]
  }
})") {}
};

// ============================================================================
// Tests for 'config vlan <id> name <value>'
// ============================================================================

TEST_F(CmdConfigVlanConfigTestFixture, setNameOnExistingVlan) {
  setupTestableConfigSession();

  VlanNameArg arg({"new-name"});
  VlanId vlanId({"100"});
  HostInfo hostInfo("testhost");

  CmdConfigVlanConfig cmd;
  std::string result = cmd.queryClient(hostInfo, vlanId, arg);

  EXPECT_THAT(result, HasSubstr("Successfully configured VLAN 100"));
  EXPECT_THAT(result, HasSubstr("name=\"new-name\""));
}

TEST_F(CmdConfigVlanConfigTestFixture, setNameUpdatesSwConfig) {
  setupTestableConfigSession();
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();

  VlanNameArg arg({"updated-name"});
  VlanId vlanId({"100"});
  HostInfo hostInfo("testhost");

  CmdConfigVlanConfig cmd;
  cmd.queryClient(hostInfo, vlanId, arg);

  auto it = std::find_if(
      swConfig.vlans()->cbegin(), swConfig.vlans()->cend(), [](const auto& v) {
        return *v.id() == 100;
      });
  ASSERT_NE(it, swConfig.vlans()->cend());
  EXPECT_EQ(*it->name(), "updated-name");
}

TEST_F(CmdConfigVlanConfigTestFixture, setNameOnSecondVlan) {
  setupTestableConfigSession();
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();

  VlanNameArg arg({"renamed"});
  VlanId vlanId({"200"});
  HostInfo hostInfo("testhost");

  CmdConfigVlanConfig cmd;
  cmd.queryClient(hostInfo, vlanId, arg);

  auto it = std::find_if(
      swConfig.vlans()->cbegin(), swConfig.vlans()->cend(), [](const auto& v) {
        return *v.id() == 200;
      });
  ASSERT_NE(it, swConfig.vlans()->cend());
  EXPECT_EQ(*it->name(), "renamed");
}

TEST_F(CmdConfigVlanConfigTestFixture, setNameOnNonExistentVlanAutoCreates) {
  setupTestableConfigSession();
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  size_t initialVlanCount = swConfig.vlans()->size();

  VlanNameArg arg({"auto-created"});
  VlanId vlanId({"999"});
  HostInfo hostInfo("testhost");

  // Capture stdout to verify "Created VLAN" message
  std::ostringstream captured;
  std::streambuf* oldCout = std::cout.rdbuf(captured.rdbuf());

  CmdConfigVlanConfig cmd;
  std::string result = cmd.queryClient(hostInfo, vlanId, arg);

  std::cout.rdbuf(oldCout);

  // VLAN should have been created
  EXPECT_EQ(swConfig.vlans()->size(), initialVlanCount + 1);
  EXPECT_THAT(captured.str(), HasSubstr("Created VLAN 999"));
  EXPECT_THAT(result, HasSubstr("Successfully configured VLAN 999"));

  // And the name should be set on the newly created VLAN
  auto it = std::find_if(
      swConfig.vlans()->cbegin(), swConfig.vlans()->cend(), [](const auto& v) {
        return *v.id() == 999;
      });
  ASSERT_NE(it, swConfig.vlans()->cend());
  EXPECT_EQ(*it->name(), "auto-created");
}

TEST_F(CmdConfigVlanConfigTestFixture, setNameDoesNotAffectOtherVlans) {
  setupTestableConfigSession();
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();

  VlanNameArg arg({"changed"});
  VlanId vlanId({"100"});
  HostInfo hostInfo("testhost");

  CmdConfigVlanConfig cmd;
  cmd.queryClient(hostInfo, vlanId, arg);

  // VLAN 200 name should remain unchanged
  auto it = std::find_if(
      swConfig.vlans()->cbegin(), swConfig.vlans()->cend(), [](const auto& v) {
        return *v.id() == 200;
      });
  ASSERT_NE(it, swConfig.vlans()->cend());
  EXPECT_EQ(*it->name(), "prod-vlan");
}

TEST_F(CmdConfigVlanConfigTestFixture, vlanNameArgGetAttrName) {
  VlanNameArg arg({"test-value"});
  EXPECT_EQ(arg.getAttrName(), "name");
}

TEST_F(CmdConfigVlanConfigTestFixture, vlanNameArgGetValue) {
  VlanNameArg arg({"my-vlan-name"});
  EXPECT_EQ(arg.getValue(), "my-vlan-name");
}

TEST_F(CmdConfigVlanConfigTestFixture, vlanNameArgRejectsMissingValue) {
  EXPECT_THROW(VlanNameArg(std::vector<std::string>{}), std::invalid_argument);
}

TEST_F(CmdConfigVlanConfigTestFixture, vlanNameArgRejectsTooManyArgs) {
  EXPECT_THROW(
      VlanNameArg(std::vector<std::string>{"a", "b"}), std::invalid_argument);
}

} // namespace facebook::fboss
