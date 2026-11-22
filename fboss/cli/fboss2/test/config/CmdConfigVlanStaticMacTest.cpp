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
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"
#include "fboss/cli/fboss2/commands/config/vlan/static_mac/add/CmdConfigVlanStaticMacAdd.h"
#include "fboss/cli/fboss2/commands/config/vlan/static_mac/delete/CmdConfigVlanStaticMacDelete.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigVlanStaticMacTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigVlanStaticMacTestFixture()
      : CmdConfigTestBase(
            "fboss2_config_vlan_static_mac_test_%%%%-%%%%-%%%%-%%%%", // unique_path
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 100
      },
      {
        "logicalID": 2,
        "name": "eth1/2/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 100
      }
    ],
    "vlans": [
      {
        "id": 100,
        "name": "default"
      },
      {
        "id": 200,
        "name": "test-vlan"
      }
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config vlan";
};

// ============================================================================
// Tests for VlanId validation (from CmdConfigVlan.h)
// ============================================================================

TEST_F(CmdConfigVlanStaticMacTestFixture, vlanIdValidMin) {
  VlanId vlanId({"1"});
  EXPECT_EQ(vlanId.getVlanId(), 1);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, vlanIdValidMax) {
  VlanId vlanId({"4094"});
  EXPECT_EQ(vlanId.getVlanId(), 4094);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, vlanIdValidMid) {
  VlanId vlanId({"100"});
  EXPECT_EQ(vlanId.getVlanId(), 100);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, vlanIdZeroInvalid) {
  EXPECT_THROW(VlanId({"0"}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, vlanIdTooHighInvalid) {
  EXPECT_THROW(VlanId({"4095"}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, vlanIdNegativeInvalid) {
  EXPECT_THROW(VlanId({"-1"}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, vlanIdNonNumericInvalid) {
  EXPECT_THROW(VlanId({"abc"}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, vlanIdEmptyInvalid) {
  EXPECT_THROW(VlanId({}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, vlanIdMultipleValuesInvalid) {
  EXPECT_THROW(VlanId({"100", "200"}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, vlanIdOutOfRangeErrorMessage) {
  try {
    auto unused = VlanId({"9999"});
    (void)unused;
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    std::string errorMsg = e.what();
    EXPECT_THAT(errorMsg, HasSubstr("VLAN ID must be between 1 and 4094"));
    EXPECT_THAT(errorMsg, HasSubstr("9999"));
  }
}

// ============================================================================
// Tests for MacAndPortArg validation (from CmdConfigVlanStaticMacAdd.h)
// ============================================================================

TEST_F(CmdConfigVlanStaticMacTestFixture, macAndPortValidArgs) {
  MacAndPortArg args({"00:11:22:33:44:55", "eth1/1/1"});
  EXPECT_EQ(args.getMacAddress(), "00:11:22:33:44:55");
  EXPECT_EQ(args.getPortName(), "eth1/1/1");
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAndPortValidUpperCaseMac) {
  MacAndPortArg args({"AA:BB:CC:DD:EE:FF", "eth1/2/1"});
  EXPECT_EQ(args.getMacAddress(), "AA:BB:CC:DD:EE:FF");
  EXPECT_EQ(args.getPortName(), "eth1/2/1");
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAndPortMissingPort) {
  EXPECT_THROW(MacAndPortArg({"00:11:22:33:44:55"}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAndPortTooManyArgs) {
  EXPECT_THROW(
      MacAndPortArg({"00:11:22:33:44:55", "eth1/1/1", "extra"}),
      std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAndPortEmptyArgs) {
  EXPECT_THROW(MacAndPortArg({}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAndPortInvalidMacFormat) {
  EXPECT_THROW(
      MacAndPortArg({"invalid-mac", "eth1/1/1"}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAndPortInvalidMacErrorMessage) {
  try {
    auto unused = MacAndPortArg({"not-a-mac", "eth1/1/1"});
    (void)unused;
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    std::string errorMsg = e.what();
    EXPECT_THAT(errorMsg, HasSubstr("Invalid MAC address format"));
    EXPECT_THAT(errorMsg, HasSubstr("not-a-mac"));
  }
}

// ============================================================================
// Tests for MacAddressArg validation (from CmdConfigVlanStaticMacDelete.h)
// ============================================================================

TEST_F(CmdConfigVlanStaticMacTestFixture, macAddressArgValid) {
  MacAddressArg arg({"00:11:22:33:44:55"});
  EXPECT_EQ(arg.getMacAddress(), "00:11:22:33:44:55");
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAddressArgValidUpperCase) {
  MacAddressArg arg({"AA:BB:CC:DD:EE:FF"});
  EXPECT_EQ(arg.getMacAddress(), "AA:BB:CC:DD:EE:FF");
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAddressArgEmpty) {
  EXPECT_THROW(MacAddressArg({}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAddressArgTooManyArgs) {
  EXPECT_THROW(
      MacAddressArg({"00:11:22:33:44:55", "extra"}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAddressArgInvalidFormat) {
  EXPECT_THROW(MacAddressArg({"invalid"}), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, macAddressArgInvalidErrorMessage) {
  try {
    auto unused = MacAddressArg({"bad-mac"});
    (void)unused;
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    std::string errorMsg = e.what();
    EXPECT_THAT(errorMsg, HasSubstr("Invalid MAC address format"));
    EXPECT_THAT(errorMsg, HasSubstr("bad-mac"));
  }
}

// ============================================================================
// Tests for CmdConfigVlanStaticMacAdd::queryClient
// ============================================================================

TEST_F(CmdConfigVlanStaticMacTestFixture, addStaticMacSuccess) {
  setupTestableConfigSession(
      cmdPrefix_, "100 static-mac add 00:11:22:33:44:55 eth1/1/1");
  auto cmd = CmdConfigVlanStaticMacAdd();
  VlanId vlanId({"100"});
  MacAndPortArg macAndPort({"00:11:22:33:44:55", "eth1/1/1"});

  auto result = cmd.queryClient(localhost(), vlanId, macAndPort);

  EXPECT_THAT(result, HasSubstr("Successfully added static MAC entry"));
  EXPECT_THAT(result, HasSubstr("00:11:22:33:44:55"));
  EXPECT_THAT(result, HasSubstr("VLAN 100"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));

  // Verify the entry was added to config
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  ASSERT_TRUE(swConfig.staticMacAddrs().has_value());
  const auto& staticMacAddrs = *swConfig.staticMacAddrs();
  ASSERT_EQ(staticMacAddrs.size(), 1);
  EXPECT_EQ(*staticMacAddrs.at(0).vlanID(), 100);
  EXPECT_EQ(*staticMacAddrs.at(0).macAddress(), "00:11:22:33:44:55");
}

TEST_F(CmdConfigVlanStaticMacTestFixture, addStaticMacVlanNotFoundAutoCreates) {
  setupTestableConfigSession(
      cmdPrefix_, "999 static-mac add 00:11:22:33:44:55 eth1/1/1");
  auto cmd = CmdConfigVlanStaticMacAdd();
  VlanId vlanId({"999"}); // VLAN doesn't exist yet - should be auto-created
  MacAndPortArg macAndPort({"00:11:22:33:44:55", "eth1/1/1"});

  // Should succeed by auto-creating VLAN 999
  auto result = cmd.queryClient(localhost(), vlanId, macAndPort);
  EXPECT_THAT(result, HasSubstr("Successfully added static MAC entry"));
  EXPECT_THAT(result, HasSubstr("00:11:22:33:44:55"));
  EXPECT_THAT(result, HasSubstr("VLAN 999"));

  // Verify VLAN was created
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto vlanIt = std::find_if(
      swConfig.vlans()->cbegin(),
      swConfig.vlans()->cend(),
      [](const auto& vlan) { return *vlan.id() == 999; });
  EXPECT_NE(vlanIt, swConfig.vlans()->cend());
}

TEST_F(CmdConfigVlanStaticMacTestFixture, addStaticMacPortNotFound) {
  setupTestableConfigSession(
      cmdPrefix_, "100 static-mac add 00:11:22:33:44:55 eth99/99/99");
  auto cmd = CmdConfigVlanStaticMacAdd();
  VlanId vlanId({"100"});
  MacAndPortArg macAndPort(
      {"00:11:22:33:44:55", "eth99/99/99"}); // Port doesn't exist

  EXPECT_THROW(
      cmd.queryClient(localhost(), vlanId, macAndPort), std::invalid_argument);
}

TEST_F(CmdConfigVlanStaticMacTestFixture, addStaticMacDuplicateEntry) {
  setupTestableConfigSession(
      cmdPrefix_, "100 static-mac add 00:11:22:33:44:55 eth1/1/1");
  auto cmd = CmdConfigVlanStaticMacAdd();
  VlanId vlanId({"100"});
  MacAndPortArg macAndPort({"00:11:22:33:44:55", "eth1/1/1"});

  // First add should succeed
  auto result = cmd.queryClient(localhost(), vlanId, macAndPort);
  EXPECT_THAT(result, HasSubstr("Successfully added"));

  // Second add of same MAC/VLAN should succeed (idempotent)
  auto result2 = cmd.queryClient(localhost(), vlanId, macAndPort);
  EXPECT_THAT(result2, HasSubstr("already exists"));
}

// ============================================================================
// Tests for CmdConfigVlanStaticMacDelete::queryClient
// ============================================================================

TEST_F(CmdConfigVlanStaticMacTestFixture, deleteStaticMacSuccess) {
  setupTestableConfigSession(
      cmdPrefix_, "100 static-mac add 00:11:22:33:44:55 eth1/1/1");
  // First add a static MAC entry
  auto addCmd = CmdConfigVlanStaticMacAdd();
  VlanId vlanId({"100"});
  MacAndPortArg macAndPort({"00:11:22:33:44:55", "eth1/1/1"});
  addCmd.queryClient(localhost(), vlanId, macAndPort);

  // Verify it was added
  {
    auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
    auto& swConfig = *config.sw();
    ASSERT_TRUE(swConfig.staticMacAddrs().has_value());
    ASSERT_EQ(swConfig.staticMacAddrs()->size(), 1);
  }

  // Now delete it
  setupTestableConfigSession(
      cmdPrefix_, "100 static-mac delete 00:11:22:33:44:55 eth1/1/1");
  auto deleteCmd = CmdConfigVlanStaticMacDelete();
  MacAddressArg macArg({"00:11:22:33:44:55"});
  auto result = deleteCmd.queryClient(localhost(), vlanId, macArg);

  EXPECT_THAT(result, HasSubstr("Successfully deleted static MAC entry"));
  EXPECT_THAT(result, HasSubstr("00:11:22:33:44:55"));
  EXPECT_THAT(result, HasSubstr("VLAN 100"));

  // Verify it was removed
  {
    auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
    auto& swConfig = *config.sw();
    EXPECT_TRUE(
        !swConfig.staticMacAddrs().has_value() ||
        swConfig.staticMacAddrs()->empty());
  }
}

TEST_F(CmdConfigVlanStaticMacTestFixture, deleteStaticMacVlanNotFound) {
  setupTestableConfigSession(
      cmdPrefix_, "999 static-mac delete 00:11:22:33:44:55");
  auto cmd = CmdConfigVlanStaticMacDelete();
  VlanId vlanId({"999"}); // VLAN doesn't exist
  MacAddressArg macArg({"00:11:22:33:44:55"});

  // Should succeed idempotently (no VLAN created for a delete)
  auto result = cmd.queryClient(localhost(), vlanId, macArg);
  EXPECT_THAT(result, HasSubstr("does not exist"));

  // Verify VLAN was NOT created
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  auto vlanIt = std::find_if(
      swConfig.vlans()->cbegin(),
      swConfig.vlans()->cend(),
      [](const auto& vlan) { return *vlan.id() == 999; });
  EXPECT_EQ(vlanIt, swConfig.vlans()->cend());
}

TEST_F(CmdConfigVlanStaticMacTestFixture, deleteStaticMacEntryNotFound) {
  setupTestableConfigSession(
      cmdPrefix_, "100 static-mac delete AA:BB:CC:DD:EE:FF");
  // First add a different entry so staticMacAddrs is not empty
  auto addCmd = CmdConfigVlanStaticMacAdd();
  VlanId vlanId({"100"});
  MacAndPortArg macAndPort({"AA:BB:CC:DD:EE:FF", "eth1/1/1"});
  addCmd.queryClient(localhost(), vlanId, macAndPort);

  auto cmd = CmdConfigVlanStaticMacDelete();
  MacAddressArg macArg({"00:11:22:33:44:55"}); // Entry doesn't exist

  // Should succeed (idempotent)
  auto result = cmd.queryClient(localhost(), vlanId, macArg);
  EXPECT_THAT(result, HasSubstr("does not exist"));
}

TEST_F(CmdConfigVlanStaticMacTestFixture, deleteStaticMacNoEntriesConfigured) {
  setupTestableConfigSession(
      cmdPrefix_, "100 static-mac delete 00:11:22:33:44:55");
  auto cmd = CmdConfigVlanStaticMacDelete();
  VlanId vlanId({"100"});
  MacAddressArg macArg({"00:11:22:33:44:55"});

  // Should succeed (idempotent)
  auto result = cmd.queryClient(localhost(), vlanId, macArg);
  EXPECT_THAT(result, HasSubstr("does not exist"));
}

} // namespace facebook::fboss
