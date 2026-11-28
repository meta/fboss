/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <boost/filesystem/operations.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"
#include "fboss/cli/fboss2/commands/config/vlan/port/tagging_mode/CmdConfigVlanPortTaggingMode.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/session/Git.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "fboss/cli/fboss2/test/TestableConfigSession.h"
#include "fboss/cli/fboss2/utils/PortMap.h" // NOLINT(misc-include-cleaner)

namespace fs = std::filesystem;

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigVlanPortTaggingModeTestFixture : public CmdHandlerTestBase {
 public:
  void SetUp() override {
    CmdHandlerTestBase::SetUp();

    // Create unique test directories
    auto tempBase = fs::temp_directory_path();
    auto uniquePath = boost::filesystem::unique_path(
        "fboss_tagging_mode_test_%%%%-%%%%-%%%%-%%%%");
    testHomeDir_ = tempBase / (uniquePath.string() + "_home");
    testEtcDir_ = tempBase / (uniquePath.string() + "_etc");

    std::error_code ec;
    if (fs::exists(testHomeDir_)) {
      fs::remove_all(testHomeDir_, ec);
    }
    if (fs::exists(testEtcDir_)) {
      fs::remove_all(testEtcDir_, ec);
    }

    // Create test directories
    fs::create_directories(testHomeDir_);
    systemConfigDir_ = testEtcDir_ / "coop";
    fs::create_directories(systemConfigDir_ / "cli");

    // NOLINTNEXTLINE(concurrency-mt-unsafe) - acceptable in unit tests
    setenv("HOME", testHomeDir_.c_str(), 1);
    // NOLINTNEXTLINE(concurrency-mt-unsafe) - acceptable in unit tests
    setenv("USER", "testuser", 1);

    // Create a test system config file at cli/agent.conf
    fs::path cliConfigPath = systemConfigDir_ / "cli" / "agent.conf";
    createTestConfig(cliConfigPath, R"({
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
    ],
    "vlanPorts": [
      {
        "vlanID": 100,
        "logicalPort": 1,
        "spanningTreeState": 2,
        "emitTags": false
      },
      {
        "vlanID": 100,
        "logicalPort": 2,
        "spanningTreeState": 2,
        "emitTags": false
      }
    ]
  }
})");

    // Create symlink at /etc/coop/agent.conf -> cli/agent.conf
    fs::create_symlink("cli/agent.conf", systemConfigDir_ / "agent.conf");

    // Initialize Git repository and create initial commit
    Git git(systemConfigDir_.string());
    git.init();
    git.commit({cliConfigPath.string()}, "Initial commit");

    // Initialize the ConfigSession singleton for all tests
    sessionConfigDir_ = testHomeDir_ / ".fboss2";
    TestableConfigSession::setInstance(
        std::make_unique<TestableConfigSession>(
            sessionConfigDir_.string(), systemConfigDir_.string()));
  }

  void TearDown() override {
    // Reset the singleton to ensure tests don't interfere with each other
    TestableConfigSession::setInstance(nullptr);
    std::error_code ec;
    if (fs::exists(testHomeDir_)) {
      fs::remove_all(testHomeDir_, ec);
    }
    if (fs::exists(testEtcDir_)) {
      fs::remove_all(testEtcDir_, ec);
    }
    CmdHandlerTestBase::TearDown();
  }

 protected:
  void createTestConfig(const fs::path& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
    file.close();
  }

  fs::path testHomeDir_;
  fs::path testEtcDir_;
  fs::path systemConfigDir_;
  fs::path sessionConfigDir_;
};

// ============================================================================
// Tests for TaggingModeArg validation
// ============================================================================

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, taggingModeTaggedValid) {
  TaggingModeArg arg({"tagged"});
  EXPECT_TRUE(arg.getEmitTags());
  EXPECT_FALSE(arg.getEmitPriorityTags());
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, taggingModeUntaggedValid) {
  TaggingModeArg arg({"untagged"});
  EXPECT_FALSE(arg.getEmitTags());
  EXPECT_FALSE(arg.getEmitPriorityTags());
}

TEST_F(
    CmdConfigVlanPortTaggingModeTestFixture,
    taggingModePriorityTaggedValid) {
  TaggingModeArg arg({"priority-tagged"});
  EXPECT_FALSE(arg.getEmitTags());
  EXPECT_TRUE(arg.getEmitPriorityTags());
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, taggingModeTaggedUpperCase) {
  TaggingModeArg arg({"TAGGED"});
  EXPECT_TRUE(arg.getEmitTags());
  EXPECT_FALSE(arg.getEmitPriorityTags());
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, taggingModeUntaggedUpperCase) {
  TaggingModeArg arg({"UNTAGGED"});
  EXPECT_FALSE(arg.getEmitTags());
  EXPECT_FALSE(arg.getEmitPriorityTags());
}

TEST_F(
    CmdConfigVlanPortTaggingModeTestFixture,
    taggingModePriorityTaggedUpperCase) {
  TaggingModeArg arg({"PRIORITY-TAGGED"});
  EXPECT_FALSE(arg.getEmitTags());
  EXPECT_TRUE(arg.getEmitPriorityTags());
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, taggingModeMixedCase) {
  TaggingModeArg arg({"TaGgEd"});
  EXPECT_TRUE(arg.getEmitTags());
  EXPECT_FALSE(arg.getEmitPriorityTags());
}

TEST_F(
    CmdConfigVlanPortTaggingModeTestFixture,
    taggingModePriorityTaggedMixedCase) {
  TaggingModeArg arg({"PrIoRiTy-TaGgEd"});
  EXPECT_FALSE(arg.getEmitTags());
  EXPECT_TRUE(arg.getEmitPriorityTags());
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, taggingModeEmptyInvalid) {
  EXPECT_THROW(TaggingModeArg({}), std::invalid_argument);
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, taggingModeTooManyArgsInvalid) {
  EXPECT_THROW(TaggingModeArg({"tagged", "extra"}), std::invalid_argument);
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, taggingModeInvalidValue) {
  EXPECT_THROW(TaggingModeArg({"invalid"}), std::invalid_argument);
}

TEST_F(
    CmdConfigVlanPortTaggingModeTestFixture,
    taggingModeInvalidErrorMessage) {
  try {
    auto unused = TaggingModeArg({"bad-mode"});
    (void)unused;
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    std::string errorMsg = e.what();
    EXPECT_THAT(errorMsg, HasSubstr("Invalid tagging mode"));
    EXPECT_THAT(errorMsg, HasSubstr("bad-mode"));
    EXPECT_THAT(errorMsg, HasSubstr("tagged"));
    EXPECT_THAT(errorMsg, HasSubstr("untagged"));
    EXPECT_THAT(errorMsg, HasSubstr("priority-tagged"));
  }
}

// ============================================================================
// Tests for CmdConfigVlanPortTaggingMode::queryClient
// ============================================================================

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, setTaggingModeTaggedSuccess) {
  auto cmd = CmdConfigVlanPortTaggingMode();
  VlanId vlanId({"100"});
  utils::PortList portList({"eth1/1/1"});
  TaggingModeArg taggingMode({"tagged"});

  auto result = cmd.queryClient(localhost(), vlanId, portList, taggingMode);

  EXPECT_THAT(result, HasSubstr("Successfully set port"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("tagged"));
  EXPECT_THAT(result, HasSubstr("VLAN 100"));

  // Verify the emitTags was updated in config
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  bool found = false;
  for (const auto& vlanPort : *swConfig.vlanPorts()) {
    if (*vlanPort.vlanID() == 100 && *vlanPort.logicalPort() == 1) {
      EXPECT_TRUE(*vlanPort.emitTags());
      EXPECT_FALSE(*vlanPort.emitPriorityTags());
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, setTaggingModeUntaggedSuccess) {
  // First set to tagged
  auto cmd = CmdConfigVlanPortTaggingMode();
  VlanId vlanId({"100"});
  utils::PortList portList({"eth1/1/1"});
  TaggingModeArg taggedMode({"tagged"});
  cmd.queryClient(localhost(), vlanId, portList, taggedMode);

  // Now set to untagged
  TaggingModeArg untaggedMode({"untagged"});
  auto result = cmd.queryClient(localhost(), vlanId, portList, untaggedMode);

  EXPECT_THAT(result, HasSubstr("Successfully set port"));
  EXPECT_THAT(result, HasSubstr("untagged"));

  // Verify the emitTags was updated in config
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  for (const auto& vlanPort : *swConfig.vlanPorts()) {
    if (*vlanPort.vlanID() == 100 && *vlanPort.logicalPort() == 1) {
      EXPECT_FALSE(*vlanPort.emitTags());
      EXPECT_FALSE(*vlanPort.emitPriorityTags());
      break;
    }
  }
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, setTaggingModeVlanNotFound) {
  auto cmd = CmdConfigVlanPortTaggingMode();
  VlanId vlanId({"999"}); // VLAN doesn't exist
  utils::PortList portList({"eth1/1/1"});
  TaggingModeArg taggingMode({"tagged"});

  EXPECT_THROW(
      cmd.queryClient(localhost(), vlanId, portList, taggingMode),
      std::invalid_argument);
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, setTaggingModePortNotFound) {
  auto cmd = CmdConfigVlanPortTaggingMode();
  VlanId vlanId({"100"});
  utils::PortList portList({"eth99/99/99"}); // Port doesn't exist
  TaggingModeArg taggingMode({"tagged"});

  EXPECT_THROW(
      cmd.queryClient(localhost(), vlanId, portList, taggingMode),
      std::invalid_argument);
}

TEST_F(
    CmdConfigVlanPortTaggingModeTestFixture,
    setTaggingModePortNotMemberOfVlan) {
  auto cmd = CmdConfigVlanPortTaggingMode();
  VlanId vlanId({"200"}); // VLAN exists but port is not a member
  utils::PortList portList({"eth1/1/1"});
  TaggingModeArg taggingMode({"tagged"});

  EXPECT_THROW(
      cmd.queryClient(localhost(), vlanId, portList, taggingMode),
      std::invalid_argument);
}

TEST_F(CmdConfigVlanPortTaggingModeTestFixture, setTaggingModeMultiplePorts) {
  auto cmd = CmdConfigVlanPortTaggingMode();
  VlanId vlanId({"100"});
  utils::PortList portList({"eth1/1/1", "eth1/2/1"});
  TaggingModeArg taggingMode({"tagged"});

  auto result = cmd.queryClient(localhost(), vlanId, portList, taggingMode);

  EXPECT_THAT(result, HasSubstr("Successfully set 2 ports"));
  EXPECT_THAT(result, HasSubstr("tagged"));
  EXPECT_THAT(result, HasSubstr("VLAN 100"));

  // Verify both ports were updated
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  int updatedCount = 0;
  for (const auto& vlanPort : *swConfig.vlanPorts()) {
    if (*vlanPort.vlanID() == 100) {
      EXPECT_TRUE(*vlanPort.emitTags());
      EXPECT_FALSE(*vlanPort.emitPriorityTags());
      updatedCount++;
    }
  }
  EXPECT_EQ(updatedCount, 2);
}

TEST_F(
    CmdConfigVlanPortTaggingModeTestFixture,
    setTaggingModePriorityTaggedSuccess) {
  auto cmd = CmdConfigVlanPortTaggingMode();
  VlanId vlanId({"100"});
  utils::PortList portList({"eth1/1/1"});
  TaggingModeArg taggingMode({"priority-tagged"});

  auto result = cmd.queryClient(localhost(), vlanId, portList, taggingMode);

  EXPECT_THAT(result, HasSubstr("Successfully set port"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("priority-tagged"));
  EXPECT_THAT(result, HasSubstr("VLAN 100"));

  // Verify the emitPriorityTags was updated in config
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  bool found = false;
  for (const auto& vlanPort : *swConfig.vlanPorts()) {
    if (*vlanPort.vlanID() == 100 && *vlanPort.logicalPort() == 1) {
      EXPECT_FALSE(*vlanPort.emitTags());
      EXPECT_TRUE(*vlanPort.emitPriorityTags());
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

} // namespace facebook::fboss
