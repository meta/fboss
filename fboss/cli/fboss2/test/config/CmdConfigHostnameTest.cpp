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
#include "fboss/cli/fboss2/commands/config/switch/hostname/CmdConfigHostname.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigHostnameTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigHostnameTestFixture()
      : CmdConfigTestBase(
            "fboss_hostname_config_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "hostname": "rsw001.p001.m002.qzr1.example.com"
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config hostname";
};

class CmdConfigHostnameAbsentTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigHostnameAbsentTestFixture()
      : CmdConfigTestBase(
            "fboss_hostname_absent_config_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {}
})") {}

 protected:
  const std::string cmdPrefix_ = "config hostname";
};

// ==============================================================================
// HostnameConfigArgs validation
// ==============================================================================

TEST_F(CmdConfigHostnameTestFixture, argValidation_valid) {
  // Valid: simple hostname
  HostnameConfigArgs a({"myswitch"});
  EXPECT_EQ(a.getName(), "myswitch");
}

TEST_F(CmdConfigHostnameTestFixture, argValidation_fqdn) {
  // Valid: FQDN
  HostnameConfigArgs a({"rsw001.p001.m002.qzr1.example.com"});
  EXPECT_EQ(a.getName(), "rsw001.p001.m002.qzr1.example.com");
}

TEST_F(CmdConfigHostnameTestFixture, argValidation_empty) {
  EXPECT_THROW(HostnameConfigArgs({}), std::invalid_argument);
}

TEST_F(CmdConfigHostnameTestFixture, argValidation_emptyString) {
  EXPECT_THROW(HostnameConfigArgs({""}), std::invalid_argument);
}

TEST_F(CmdConfigHostnameTestFixture, argValidation_tooManyArgs) {
  EXPECT_THROW(
      HostnameConfigArgs({"myswitch", "extra"}), std::invalid_argument);
}

TEST_F(CmdConfigHostnameTestFixture, argValidation_invalidFormat) {
  // Invalid: contains underscore (not RFC 952)
  EXPECT_THROW(HostnameConfigArgs({"my_switch"}), std::invalid_argument);

  // Invalid: leading hyphen
  EXPECT_THROW(HostnameConfigArgs({"-invalid"}), std::invalid_argument);

  // Invalid: trailing hyphen
  EXPECT_THROW(HostnameConfigArgs({"invalid-"}), std::invalid_argument);

  // Invalid: label too long (> 63 chars)
  std::string longLabel(65, 'a');
  EXPECT_THROW(HostnameConfigArgs({longLabel}), std::invalid_argument);

  // Invalid: total length > 253
  std::string tooLong(260, 'a');
  EXPECT_THROW(HostnameConfigArgs({tooLong}), std::invalid_argument);
}

// ==============================================================================
// queryClient tests
// ==============================================================================

TEST_F(CmdConfigHostnameTestFixture, setNewHostname) {
  setupTestableConfigSession(cmdPrefix_, "newswitch.example.com");
  CmdConfigHostname cmd;
  HostInfo hostInfo("testhost");
  HostnameConfigArgs arg({"newswitch.example.com"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("newswitch.example.com"));
  EXPECT_THAT(result, HasSubstr("Successfully"));

  auto& hostname = *ConfigSession::getInstance()
                        .config<AgentConfigDomain>()
                        .sw()
                        ->hostname();
  EXPECT_EQ(hostname, "newswitch.example.com");
}

TEST_F(CmdConfigHostnameTestFixture, alreadySet) {
  // Seed config already has this hostname
  setupTestableConfigSession(cmdPrefix_, "rsw001.p001.m002.qzr1.example.com");
  CmdConfigHostname cmd;
  HostInfo hostInfo("testhost");
  HostnameConfigArgs arg({"rsw001.p001.m002.qzr1.example.com"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("already"));
}

// ==============================================================================
// Absent hostname fixture and tests
// ==============================================================================

TEST_F(CmdConfigHostnameAbsentTestFixture, setNewHostnameWhenAbsent) {
  setupTestableConfigSession("config hostname", "newswitch.example.com");
  CmdConfigHostname cmd;
  HostInfo hostInfo("testhost");
  HostnameConfigArgs arg({"newswitch.example.com"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("Successfully"));
  EXPECT_THAT(result, HasSubstr("newswitch.example.com"));

  auto& hostname = *ConfigSession::getInstance()
                        .config<AgentConfigDomain>()
                        .sw()
                        ->hostname();
  EXPECT_EQ(hostname, "newswitch.example.com");
}

} // namespace facebook::fboss
