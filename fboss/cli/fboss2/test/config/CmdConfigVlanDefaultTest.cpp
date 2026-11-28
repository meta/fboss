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

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlanDefault.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

using VlanId = CmdConfigVlanDefaultTraits::ObjectArgType;

// Fixture with a config that has a VLAN whose id matches the current default.
class CmdConfigVlanDefaultTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigVlanDefaultTestFixture()
      : CmdConfigTestBase(
            "fboss2_config_vlan_default_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "vlans": [{"id": 100, "name": "default", "routable": true}],
    "defaultVlan": 100
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config vlan";
};

// Fixture where no VLAN in the list matches the current defaultVlan id.
class CmdConfigVlanDefaultNoMatchFixture : public CmdConfigTestBase {
 public:
  CmdConfigVlanDefaultNoMatchFixture()
      : CmdConfigTestBase(
            "fboss2_config_vlan_default_nomatch_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "vlans": [{"id": 200, "name": "other-vlan"}],
    "defaultVlan": 100
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config vlan";
};

// Fixture where the target VLAN already exists but doesn't match defaultVlan.
class CmdConfigVlanDefaultTargetExistsFixture : public CmdConfigTestBase {
 public:
  CmdConfigVlanDefaultTargetExistsFixture()
      : CmdConfigTestBase(
            "fboss2_config_vlan_default_targetexists_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "vlans": [
      {"id": 200, "name": "other-vlan"},
      {"id": 300, "name": "target-vlan"}
    ],
    "defaultVlan": 100
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config vlan";
};

// Fixture with no defaultVlan field set.
class CmdConfigVlanDefaultNoFieldFixture : public CmdConfigTestBase {
 public:
  CmdConfigVlanDefaultNoFieldFixture()
      : CmdConfigTestBase(
            "fboss2_config_vlan_default_nofield_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "vlans": []
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config vlan";
};

// ============================================================================
// Tests for CmdConfigVlanDefault::queryClient
// ============================================================================

TEST_F(CmdConfigVlanDefaultTestFixture, setDefaultVlanAlreadySet) {
  setupTestableConfigSession(cmdPrefix_, "default 100");
  auto result =
      CmdConfigVlanDefault().queryClient(localhost(), VlanId({"100"}));
  EXPECT_THAT(result, HasSubstr("already set to 100"));
}

TEST_F(CmdConfigVlanDefaultTestFixture, setDefaultVlanSuccess) {
  setupTestableConfigSession(cmdPrefix_, "default 300");
  auto& swConfig =
      *ConfigSession::getInstance().config<AgentConfigDomain>().sw();

  auto result =
      CmdConfigVlanDefault().queryClient(localhost(), VlanId({"300"}));

  EXPECT_THAT(result, HasSubstr("Successfully set default VLAN to 300"));
  EXPECT_THAT(result, HasSubstr("was 100"));
  EXPECT_EQ(*swConfig.defaultVlan(), 300);

  // Since VLAN 100 has no interface, it should be removed when we change
  // the default VLAN. A new VLAN 300 is created.
  int count100 = 0;
  int count300 = 0;
  for (const auto& v : *swConfig.vlans()) {
    if (*v.id() == 100) {
      ++count100;
    } else if (*v.id() == 300) {
      ++count300;
    }
  }
  EXPECT_EQ(count100, 0); // Old VLAN removed (no interface)
  EXPECT_EQ(count300, 1); // New VLAN created
}

TEST_F(CmdConfigVlanDefaultTestFixture, setDefaultVlanUpdatesOnlyOneEntry) {
  setupTestableConfigSession(cmdPrefix_, "default 300");
  auto& swConfig =
      *ConfigSession::getInstance().config<AgentConfigDomain>().sw();

  CmdConfigVlanDefault().queryClient(localhost(), VlanId({"300"}));

  // Ensure we did not create duplicate entries. Old VLAN 100 is removed
  // since it has no interface.
  int count100 = 0;
  int count300 = 0;
  for (const auto& v : *swConfig.vlans()) {
    if (*v.id() == 100) {
      ++count100;
    }
    if (*v.id() == 300) {
      ++count300;
    }
  }
  EXPECT_EQ(count100, 0); // Old VLAN removed (no interface)
  EXPECT_EQ(count300, 1); // New VLAN created
}

TEST_F(CmdConfigVlanDefaultTestFixture, changeDefaultVlanMultipleTimes) {
  setupTestableConfigSession(cmdPrefix_, "default 200");
  auto& swConfig =
      *ConfigSession::getInstance().config<AgentConfigDomain>().sw();

  auto r1 = CmdConfigVlanDefault().queryClient(localhost(), VlanId({"200"}));
  EXPECT_THAT(r1, HasSubstr("Successfully set default VLAN to 200"));
  EXPECT_THAT(r1, HasSubstr("was 100"));
  EXPECT_EQ(*swConfig.defaultVlan(), 200);

  auto r2 = CmdConfigVlanDefault().queryClient(localhost(), VlanId({"400"}));
  EXPECT_THAT(r2, HasSubstr("Successfully set default VLAN to 400"));
  EXPECT_THAT(r2, HasSubstr("was 200"));
  EXPECT_EQ(*swConfig.defaultVlan(), 400);

  // After multiple changes, only the final VLAN 400 should remain.
  // Each previous default VLAN is removed if it has no interface.
  // 100 removed when changing to 200, 200 removed when changing to 400.
  int count100 = 0;
  int count200 = 0;
  int count400 = 0;
  for (const auto& v : *swConfig.vlans()) {
    if (*v.id() == 100) {
      ++count100;
    } else if (*v.id() == 200) {
      ++count200;
    } else if (*v.id() == 400) {
      ++count400;
    }
  }
  EXPECT_EQ(count100, 0); // Old VLAN removed (no interface)
  EXPECT_EQ(count200, 0); // Old VLAN removed (no interface)
  EXPECT_EQ(count400, 1); // Current default VLAN
}

// When no VLAN in the list matches currentDefault but the target VLAN id
// doesn't exist either, the command must create a new VLAN entry.
TEST_F(CmdConfigVlanDefaultNoMatchFixture, createsNewVlanWhenNeitherExists) {
  setupTestableConfigSession(cmdPrefix_, "default 300");
  auto& swConfig =
      *ConfigSession::getInstance().config<AgentConfigDomain>().sw();

  auto result =
      CmdConfigVlanDefault().queryClient(localhost(), VlanId({"300"}));

  EXPECT_THAT(result, HasSubstr("Successfully set default VLAN to 300"));
  EXPECT_EQ(*swConfig.defaultVlan(), 300);

  auto vitr = std::find_if(
      swConfig.vlans()->cbegin(), swConfig.vlans()->cend(), [](const auto& v) {
        return *v.id() == 300;
      });
  ASSERT_NE(vitr, swConfig.vlans()->cend());
}

// When no VLAN in the list matches currentDefault but the target VLAN already
// exists, that existing entry must be reused (not duplicated).
TEST_F(CmdConfigVlanDefaultTargetExistsFixture, reusesExistingTargetVlan) {
  setupTestableConfigSession(cmdPrefix_, "default 300");
  auto& swConfig =
      *ConfigSession::getInstance().config<AgentConfigDomain>().sw();

  auto result =
      CmdConfigVlanDefault().queryClient(localhost(), VlanId({"300"}));

  EXPECT_THAT(result, HasSubstr("Successfully set default VLAN to 300"));
  EXPECT_EQ(*swConfig.defaultVlan(), 300);

  // Exactly one VLAN with id=300.
  int count = 0;
  for (const auto& v : *swConfig.vlans()) {
    if (*v.id() == 300) {
      ++count;
    }
  }
  EXPECT_EQ(count, 1);
}

// When defaultVlan is not present in the config, the command must fall back to
// the system constant and still complete successfully.
TEST_F(CmdConfigVlanDefaultNoFieldFixture, succeedsWithNoDefaultVlanField) {
  setupTestableConfigSession(cmdPrefix_, "default 300");
  auto& swConfig =
      *ConfigSession::getInstance().config<AgentConfigDomain>().sw();

  auto result =
      CmdConfigVlanDefault().queryClient(localhost(), VlanId({"300"}));

  EXPECT_THAT(result, HasSubstr("Successfully set default VLAN to 300"));
  EXPECT_EQ(*swConfig.defaultVlan(), 300);
}

} // namespace facebook::fboss
