// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/delete/interface/pfc_config/CmdDeleteInterfacePfcConfig.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

using namespace ::testing;

namespace facebook::fboss {

// eth1/1/1 carries a full pfc struct (watchdog + duration flags), eth1/2/1 a
// minimal one, eth1/3/1 has no pfc at all.
class CmdDeleteInterfacePfcConfigTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteInterfacePfcConfigTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_intf_pfc_config_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1, "name": "eth1/1/1", "state": 2, "speed": 100000,
        "pfc": {
          "tx": true, "rx": true, "portPgConfigName": "pg-a",
          "watchdog": {"detectionTimeMsecs": 150, "recoveryTimeMsecs": 1000, "recoveryAction": 0},
          "txPfcDurationEnable": true, "rxPfcDurationEnable": true
        }
      },
      {
        "logicalID": 2, "name": "eth1/2/1", "state": 2, "speed": 100000,
        "pfc": {"tx": true, "rx": false, "portPgConfigName": "pg-a"}
      },
      {"logicalID": 3, "name": "eth1/3/1", "state": 2, "speed": 100000}
    ],
    "portPgConfigs": {"pg-a": [{"id": 0, "bufferPoolName": "pool"}]},
    "bufferPoolConfigs": {"pool": {"sharedBytes": 1000}}
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete interface";
  const PfcConfigDeleteAttrs kNoAttrs{std::vector<std::string>{}};

  const cfg::Port& port(const std::string& name) {
    for (const auto& p :
         *ConfigSession::getInstance().getAgentConfig().sw()->ports()) {
      if (p.name().has_value() && *p.name() == name) {
        return p;
      }
    }
    throw std::runtime_error("No such port in seed config: " + name);
  }
};

TEST_F(CmdDeleteInterfacePfcConfigTestFixture, attrValidation) {
  EXPECT_NO_THROW(PfcConfigDeleteAttrs(std::vector<std::string>{}));
  EXPECT_NO_THROW(PfcConfigDeleteAttrs(
      std::vector<std::string>{"watchdog", "RX-DURATION"}));
  EXPECT_THROW(
      PfcConfigDeleteAttrs(std::vector<std::string>{"rx"}),
      std::invalid_argument);
  EXPECT_THROW(
      PfcConfigDeleteAttrs(std::vector<std::string>{"priority-group-policy"}),
      std::invalid_argument);
}

TEST_F(CmdDeleteInterfacePfcConfigTestFixture, deletesWholeStruct) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 pfc-config");
  auto cmd = CmdDeleteInterfacePfcConfig();
  utils::InterfaceList interfaces({"eth1/1/1"});

  ASSERT_TRUE(port("eth1/1/1").pfc().has_value());
  auto result = cmd.queryClient(localhost(), interfaces, kNoAttrs);

  EXPECT_THAT(result, HasSubstr("Deleted pfc-config"));
  EXPECT_FALSE(port("eth1/1/1").pfc().has_value());
  // Sibling and the policy definition untouched.
  EXPECT_TRUE(port("eth1/2/1").pfc().has_value());
  EXPECT_TRUE(
      ConfigSession::getInstance()
          .getAgentConfig()
          .sw()
          ->portPgConfigs()
          ->count("pg-a"));
}

TEST_F(CmdDeleteInterfacePfcConfigTestFixture, resetsOnlyNamedFields) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 pfc-config watchdog rx-duration");
  auto cmd = CmdDeleteInterfacePfcConfig();
  utils::InterfaceList interfaces({"eth1/1/1"});

  auto result = cmd.queryClient(
      localhost(),
      interfaces,
      PfcConfigDeleteAttrs(
          std::vector<std::string>{"watchdog", "rx-duration"}));

  EXPECT_THAT(result, HasSubstr("Reset pfc-config watchdog, rx-duration"));
  const auto& pfc = *port("eth1/1/1").pfc();
  EXPECT_FALSE(pfc.watchdog().has_value());
  EXPECT_FALSE(pfc.rxPfcDurationEnable().has_value());
  // Everything else survives.
  EXPECT_TRUE(pfc.txPfcDurationEnable().has_value());
  EXPECT_TRUE(*pfc.tx());
  EXPECT_TRUE(*pfc.rx());
  EXPECT_EQ(*pfc.portPgConfigName(), "pg-a");
}

TEST_F(CmdDeleteInterfacePfcConfigTestFixture, noPfcIsANoOp) {
  setupTestableConfigSession(cmdPrefix_, "eth1/3/1 pfc-config");
  auto cmd = CmdDeleteInterfacePfcConfig();
  utils::InterfaceList interfaces({"eth1/3/1"});

  EXPECT_THAT(
      cmd.queryClient(localhost(), interfaces, kNoAttrs),
      HasSubstr("No pfc-config set"));
}

TEST_F(CmdDeleteInterfacePfcConfigTestFixture, mixedSelectionNamesUpdated) {
  setupTestableConfigSession(cmdPrefix_, "eth1/2/1,eth1/3/1 pfc-config");
  auto cmd = CmdDeleteInterfacePfcConfig();
  utils::InterfaceList interfaces({"eth1/2/1", "eth1/3/1"});

  auto result = cmd.queryClient(localhost(), interfaces, kNoAttrs);

  EXPECT_THAT(result, HasSubstr("eth1/2/1"));
  EXPECT_THAT(result, Not(HasSubstr("eth1/3/1")));
  EXPECT_FALSE(port("eth1/2/1").pfc().has_value());
}

TEST_F(CmdDeleteInterfacePfcConfigTestFixture, emptyInterfaceListThrows) {
  setupTestableConfigSession(cmdPrefix_, "pfc-config");
  auto cmd = CmdDeleteInterfacePfcConfig();
  utils::InterfaceList interfaces({});

  EXPECT_THROW(
      cmd.queryClient(localhost(), interfaces, kNoAttrs),
      std::invalid_argument);
}

} // namespace facebook::fboss
