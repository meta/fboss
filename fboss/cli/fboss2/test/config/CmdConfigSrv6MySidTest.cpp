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

#include <fmt/format.h>

#include <iostream>
#include <map>
#include <string>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/srv6/my_sid/CmdConfigSrv6MySid.h"
#include "fboss/cli/fboss2/commands/config/srv6/my_sid/entry/CmdConfigSrv6MySidEntry.h"
#include "fboss/cli/fboss2/commands/config/srv6/utils/Srv6MySidCliUtils.h"
#include "fboss/cli/fboss2/commands/delete/srv6/my_sid/CmdDeleteSrv6MySid.h"
#include "fboss/cli/fboss2/commands/delete/srv6/my_sid/entry/CmdDeleteSrv6MySidEntry.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {
const std::string kLocatorPrefix = "fdad:ffff::/32";
const std::string kOtherLocatorPrefix = "fd00:abcd::/32";

const std::map<std::string, std::string> kSrv6MySidTestDescriptions = {
    {"locatorPrefixArg_valid",
     "Valid IPv6 /32 locator prefix is accepted by LocatorPrefixArg"},
    {"locatorPrefixArg_notIPv6", "Rejects non-IPv6 prefix (192.168.1.0/24)"},
    {"locatorPrefixArg_invalid", "Rejects malformed prefix string"},
    {"locatorPrefixArg_unsupportedLength",
     "Rejects IPv6 locator prefixes other than /32"},
    {"locatorPrefixArg_hostBitsSet",
     "Rejects a locator prefix with non-zero host bits instead of masking it"},
    {"locatorPrefixArg_missing", "Rejects missing locator prefix argument"},
    {"mySidInit_createsConfig",
     "config srv6 my-sid <prefix> creates empty mySidConfig in session"},
    {"mySidInit_rejectsSecondInit",
     "Second config srv6 my-sid init for same prefix is rejected"},
    {"mySidEntry_validAdjacency",
     "Parses entry adjacency args (fn, is-v6, port-name)"},
    {"mySidEntry_validNode", "Parses entry node args (fn, node-address)"},
    {"mySidEntry_validDecap", "Parses entry decap args (fn only)"},
    {"mySidEntry_noLocatorThrows",
     "entry before my-sid init fails with runtime_error"},
    {"mySidEntry_invalidEntry",
     "Rejects function values 0, 99999, and non-numeric abc"},
    {"mySidEntry_unknownType", "Rejects unknown entry type 'binding'"},
    {"mySidEntry_invalidIsV6",
     "Rejects non-boolean is-v6 value on adjacency entry"},
    {"mySidEntry_portNameOnNode", "Rejects port-name on node-type entry"},
    {"mySidEntry_nodeAddressOnAdjacency",
     "Rejects node-address on adjacency-type entry"},
    {"mySidEntry_insertsEntry",
     "entry adjacency stages entry in session mySidConfig"},
    {"mySidEntry_prefixMismatch",
     "entry with wrong locator prefix is rejected"},
    {"mySidEntry_upserts",
     "Re-adding same function ID overwrites prior entry with warning"},
    {"mySidDeleteEntry_removesEntry",
     "delete srv6 my-sid <prefix> entry <fn> removes entry"},
    {"mySidDeleteEntry_noOpWhenMissing",
     "delete entry for missing function ID returns informational message"},
    {"mySidDeleteEntry_noOpWhenNoConfig",
     "delete entry with no mySidConfig returns informational message"},
    {"deleteMySid_removesEntireConfig",
     "delete srv6 my-sid <prefix> removes entire mySidConfig block"},
    {"deleteMySid_wrongPrefix",
     "delete srv6 my-sid with mismatched prefix is rejected"},
    {"deleteMySid_noConfig",
     "delete srv6 my-sid when nothing configured returns informational message"},
};

class Srv6MySidTestLogListener : public ::testing::EmptyTestEventListener {
 public:
  void OnTestStart(const ::testing::TestInfo& testInfo) override {
    if (!isSrv6MySidTest(testInfo)) {
      return;
    }
    const std::string testName = testInfo.name();
    std::cout << "[TEST] " << testInfo.test_suite_name() << "." << testName;
    const auto description = kSrv6MySidTestDescriptions.find(testName);
    if (description != kSrv6MySidTestDescriptions.end()) {
      std::cout << " | " << description->second;
    }
    std::cout << std::endl;
  }

  void OnTestEnd(const ::testing::TestInfo& testInfo) override {
    if (!isSrv6MySidTest(testInfo)) {
      return;
    }
    const auto* result = testInfo.result();
    std::cout << "[RESULT] " << testInfo.test_suite_name() << "."
              << testInfo.name() << " => "
              << (result->Passed() ? "PASS" : "FAIL") << " ("
              << result->elapsed_time() << " ms)" << std::endl;
  }

 private:
  static bool isSrv6MySidTest(const ::testing::TestInfo& testInfo) {
    return std::string(testInfo.test_suite_name()) ==
        "CmdConfigSrv6MySidTestFixture";
  }
};

struct RegisterSrv6MySidTestLogListener {
  RegisterSrv6MySidTestLogListener() {
    ::testing::UnitTest::GetInstance()->listeners().Append(
        new Srv6MySidTestLogListener());
  }
};

const RegisterSrv6MySidTestLogListener registerSrv6MySidTestLogListener;
} // namespace

class CmdConfigSrv6MySidTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigSrv6MySidTestFixture()
      : CmdConfigTestBase(
            "fboss2_config_srv6_mysid_test_%%%%-%%%%-%%%%-%%%%",
            R"({"sw": {}})") {}

 protected:
  void initMySidConfig() {
    setupTestableConfigSession("config srv6 my-sid", kLocatorPrefix);
    CmdConfigSrv6MySid initCmd;
    HostInfo hostInfo("testhost");
    LocatorPrefixArg prefix({kLocatorPrefix});
    initCmd.queryClient(hostInfo, prefix);
  }

  HostInfo hostInfo_{"testhost"};
};

TEST_F(CmdConfigSrv6MySidTestFixture, locatorPrefixArg_valid) {
  LocatorPrefixArg arg({kLocatorPrefix});
  EXPECT_EQ(arg.getPrefix(), kLocatorPrefix);
}

TEST_F(CmdConfigSrv6MySidTestFixture, locatorPrefixArg_notIPv6) {
  EXPECT_THROW(LocatorPrefixArg({"192.168.1.0/24"}), std::invalid_argument);
}

TEST_F(CmdConfigSrv6MySidTestFixture, locatorPrefixArg_invalid) {
  EXPECT_THROW(LocatorPrefixArg({"not-a-prefix"}), std::invalid_argument);
}

TEST_F(CmdConfigSrv6MySidTestFixture, locatorPrefixArg_unsupportedLength) {
  EXPECT_THROW(LocatorPrefixArg({"3001:db8::/33"}), std::invalid_argument);
  EXPECT_THROW(LocatorPrefixArg({"3001:db8::/120"}), std::invalid_argument);
}

TEST_F(CmdConfigSrv6MySidTestFixture, locatorPrefixArg_hostBitsSet) {
  EXPECT_THROW(
      LocatorPrefixArg({"fdad:ffff:dead:beef::/32"}), std::invalid_argument);
}

TEST_F(CmdConfigSrv6MySidTestFixture, locatorPrefixArg_missing) {
  EXPECT_THROW(LocatorPrefixArg({}), std::invalid_argument);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidInit_createsConfig) {
  setupTestableConfigSession("config srv6 my-sid", kLocatorPrefix);
  CmdConfigSrv6MySid cmd;
  LocatorPrefixArg prefix({kLocatorPrefix});

  auto result = cmd.queryClient(hostInfo_, prefix);
  EXPECT_THAT(result, HasSubstr("Successfully initialized"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  ASSERT_TRUE(config.sw()->mySidConfig().has_value());
  EXPECT_EQ(*config.sw()->mySidConfig()->locatorPrefix(), kLocatorPrefix);
  EXPECT_TRUE(config.sw()->mySidConfig()->entries()->empty());
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidInit_rejectsSecondInit) {
  initMySidConfig();
  CmdConfigSrv6MySid cmd;
  LocatorPrefixArg prefix({kLocatorPrefix});

  EXPECT_THROW(cmd.queryClient(hostInfo_, prefix), std::runtime_error);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_validAdjacency) {
  MySidEntryArg arg(
      {"10188",
       "type",
       "adjacency",
       "is-v6",
       "true",
       "port-name",
       "Port-Channel190412"});
  EXPECT_EQ(arg.getFunctionValue(), 10188);
  EXPECT_EQ(arg.getType(), MySidConfigEntryType::ADJACENCY);
  EXPECT_TRUE(arg.isV6());
  EXPECT_EQ(arg.getPortName(), "Port-Channel190412");
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_validNode) {
  MySidEntryArg arg({"20001", "type", "node", "node-address", "2001:db8::1"});
  EXPECT_EQ(arg.getFunctionValue(), 20001);
  EXPECT_EQ(arg.getType(), MySidConfigEntryType::NODE);
  EXPECT_EQ(arg.getNodeAddress(), "2001:db8::1");
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_validDecap) {
  MySidEntryArg arg({"32767", "type", "decap"});
  EXPECT_EQ(arg.getFunctionValue(), 32767);
  EXPECT_EQ(arg.getType(), MySidConfigEntryType::DECAP);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_noLocatorThrows) {
  setupTestableConfigSession(
      "config srv6 my-sid entry",
      fmt::format(
          "{} 10188 type adjacency is-v6 true port-name Port-Channel190412",
          kLocatorPrefix));
  CmdConfigSrv6MySidEntry cmd;
  LocatorPrefixArg prefix({kLocatorPrefix});
  MySidEntryArg entryArg(
      {"10188",
       "type",
       "adjacency",
       "is-v6",
       "true",
       "port-name",
       "Port-Channel190412"});

  EXPECT_THROW(
      cmd.queryClient(hostInfo_, prefix, entryArg), std::runtime_error);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_invalidEntry) {
  EXPECT_THROW(MySidEntryArg({"0", "type", "decap"}), std::invalid_argument);
  EXPECT_THROW(
      MySidEntryArg({"99999", "type", "decap"}), std::invalid_argument);
  EXPECT_THROW(MySidEntryArg({"abc", "type", "decap"}), std::invalid_argument);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_unknownType) {
  EXPECT_THROW(MySidEntryArg({"1", "type", "binding"}), std::invalid_argument);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_invalidIsV6) {
  EXPECT_THROW(
      MySidEntryArg(
          {"1",
           "type",
           "adjacency",
           "is-v6",
           "maybe",
           "port-name",
           "Port-Channel1"}),
      std::invalid_argument);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_portNameOnNode) {
  EXPECT_THROW(
      MySidEntryArg({"1", "type", "node", "port-name", "Port-Channel1"}),
      std::invalid_argument);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_nodeAddressOnAdjacency) {
  EXPECT_THROW(
      MySidEntryArg(
          {"1",
           "type",
           "adjacency",
           "is-v6",
           "true",
           "node-address",
           "2001:db8::1"}),
      std::invalid_argument);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_insertsEntry) {
  initMySidConfig();
  CmdConfigSrv6MySidEntry cmd;
  LocatorPrefixArg prefix({kLocatorPrefix});
  MySidEntryArg entryArg(
      {"10188",
       "type",
       "adjacency",
       "is-v6",
       "true",
       "port-name",
       "Port-Channel190412"});

  auto result = cmd.queryClient(hostInfo_, prefix, entryArg);
  EXPECT_THAT(result, HasSubstr("Successfully added adjacency SID"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  ASSERT_TRUE(config.sw()->mySidConfig().has_value());
  EXPECT_EQ(config.sw()->mySidConfig()->entries()->count(10188), 1);
  EXPECT_TRUE(
      config.sw()->mySidConfig()->entries()->at(10188).adjacency().has_value());
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_prefixMismatch) {
  initMySidConfig();
  CmdConfigSrv6MySidEntry cmd;
  LocatorPrefixArg prefix({kOtherLocatorPrefix});
  MySidEntryArg entryArg(
      {"10188",
       "type",
       "adjacency",
       "is-v6",
       "true",
       "port-name",
       "Port-Channel190412"});

  EXPECT_THROW(
      cmd.queryClient(hostInfo_, prefix, entryArg), std::runtime_error);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidEntry_upserts) {
  initMySidConfig();
  CmdConfigSrv6MySidEntry cmd;
  LocatorPrefixArg prefix({kLocatorPrefix});
  MySidEntryArg firstArg({"10188", "type", "decap"});
  MySidEntryArg secondArg(
      {"10188",
       "type",
       "adjacency",
       "is-v6",
       "true",
       "port-name",
       "Port-Channel190412"});

  cmd.queryClient(hostInfo_, prefix, firstArg);
  auto result = cmd.queryClient(hostInfo_, prefix, secondArg);
  EXPECT_THAT(result, HasSubstr("Warning: MySID entry 10188 overwritten"));
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidDeleteEntry_removesEntry) {
  initMySidConfig();
  CmdConfigSrv6MySidEntry entryCmd;
  LocatorPrefixArg prefix({kLocatorPrefix});
  MySidEntryArg entryArg({"32767", "type", "decap"});
  entryCmd.queryClient(hostInfo_, prefix, entryArg);

  CmdDeleteSrv6MySidEntry deleteCmd;
  MySidDeleteEntryArg deleteArg({"32767"});
  auto result = deleteCmd.queryClient(hostInfo_, prefix, deleteArg);
  EXPECT_THAT(result, HasSubstr("Successfully deleted MySID entry 32767"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(config.sw()->mySidConfig()->entries()->count(32767), 0);
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidDeleteEntry_noOpWhenMissing) {
  initMySidConfig();
  CmdDeleteSrv6MySidEntry deleteCmd;
  LocatorPrefixArg prefix({kLocatorPrefix});
  MySidDeleteEntryArg deleteArg({"10188"});

  auto result = deleteCmd.queryClient(hostInfo_, prefix, deleteArg);
  EXPECT_THAT(result, HasSubstr("does not exist"));
}

TEST_F(CmdConfigSrv6MySidTestFixture, mySidDeleteEntry_noOpWhenNoConfig) {
  setupTestableConfigSession(
      "delete srv6 my-sid entry", fmt::format("{} 10188", kLocatorPrefix));
  CmdDeleteSrv6MySidEntry deleteCmd;
  LocatorPrefixArg prefix({kLocatorPrefix});
  MySidDeleteEntryArg deleteArg({"10188"});

  auto result = deleteCmd.queryClient(hostInfo_, prefix, deleteArg);
  EXPECT_THAT(result, HasSubstr("no mySidConfig configured"));
}

TEST_F(CmdConfigSrv6MySidTestFixture, deleteMySid_removesEntireConfig) {
  initMySidConfig();
  CmdDeleteSrv6MySid deleteCmd;
  LocatorPrefixArg prefix({kLocatorPrefix});

  auto result = deleteCmd.queryClient(hostInfo_, prefix);
  EXPECT_THAT(
      result, HasSubstr("Successfully deleted SRv6 MySID configuration"));
  EXPECT_THAT(result, HasSubstr(kLocatorPrefix));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_FALSE(config.sw()->mySidConfig().has_value());
}

TEST_F(CmdConfigSrv6MySidTestFixture, deleteMySid_wrongPrefix) {
  initMySidConfig();
  CmdDeleteSrv6MySid deleteCmd;
  LocatorPrefixArg prefix({kOtherLocatorPrefix});

  EXPECT_THROW(deleteCmd.queryClient(hostInfo_, prefix), std::runtime_error);
}

TEST_F(CmdConfigSrv6MySidTestFixture, deleteMySid_noConfig) {
  setupTestableConfigSession("delete srv6 my-sid", kLocatorPrefix);
  CmdDeleteSrv6MySid deleteCmd;
  LocatorPrefixArg prefix({kLocatorPrefix});

  auto result = deleteCmd.queryClient(hostInfo_, prefix);
  EXPECT_THAT(result, HasSubstr("nothing configured"));
}

} // namespace facebook::fboss
