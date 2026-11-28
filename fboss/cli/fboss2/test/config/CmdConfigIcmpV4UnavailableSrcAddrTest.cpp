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
#include "fboss/cli/fboss2/commands/config/switch/icmpv4_unavailable_src_addr/CmdConfigIcmpV4UnavailableSrcAddr.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed mirrors a device with icmpV4UnavailableSrcAddress already configured
class CmdConfigIcmpV4UnavailableSrcAddrTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigIcmpV4UnavailableSrcAddrTestFixture()
      : CmdConfigTestBase(
            "fboss_icmpv4_unavailable_src_addr_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "icmpV4UnavailableSrcAddress": "10.0.0.1"
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config icmpv4-unavailable-src-addr";
};

// Seed mirrors a fresh device where icmpV4UnavailableSrcAddress is absent
class CmdConfigIcmpV4UnavailableSrcAddrAbsentTestFixture
    : public CmdConfigTestBase {
 public:
  CmdConfigIcmpV4UnavailableSrcAddrAbsentTestFixture()
      : CmdConfigTestBase(
            "fboss_icmpv4_unavailable_src_addr_absent_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {}
})") {}

 protected:
  const std::string cmdPrefix_ = "config icmpv4-unavailable-src-addr";
};

// ==============================================================================
// IcmpV4UnavailableSrcAddrArg Validation Tests
// ==============================================================================

TEST_F(CmdConfigIcmpV4UnavailableSrcAddrTestFixture, argValidation) {
  // Valid IPv4 addresses
  EXPECT_EQ(IcmpV4UnavailableSrcAddrArg({"10.0.0.1"}).getAddress(), "10.0.0.1");
  EXPECT_EQ(
      IcmpV4UnavailableSrcAddrArg({"192.168.1.1"}).getAddress(), "192.168.1.1");
  EXPECT_EQ(
      IcmpV4UnavailableSrcAddrArg({"192.0.0.8"}).getAddress(), "192.0.0.8");
  EXPECT_EQ(IcmpV4UnavailableSrcAddrArg({"0.0.0.0"}).getAddress(), "0.0.0.0");
  EXPECT_EQ(
      IcmpV4UnavailableSrcAddrArg({"255.255.255.255"}).getAddress(),
      "255.255.255.255");

  // Invalid: empty
  EXPECT_THROW(IcmpV4UnavailableSrcAddrArg({}), std::invalid_argument);

  // Invalid: too many args
  EXPECT_THROW(
      IcmpV4UnavailableSrcAddrArg({"10.0.0.1", "10.0.0.2"}),
      std::invalid_argument);

  // Invalid: not an IP address
  EXPECT_THROW(IcmpV4UnavailableSrcAddrArg({"abc"}), std::invalid_argument);
  EXPECT_THROW(
      IcmpV4UnavailableSrcAddrArg({"not-an-ip"}), std::invalid_argument);

  // Invalid: octet out of range
  EXPECT_THROW(
      IcmpV4UnavailableSrcAddrArg({"256.1.1.1"}), std::invalid_argument);
  EXPECT_THROW(
      IcmpV4UnavailableSrcAddrArg({"10.0.0.300"}), std::invalid_argument);

  // Invalid: IPv6 address
  EXPECT_THROW(IcmpV4UnavailableSrcAddrArg({"::1"}), std::invalid_argument);
}

// ==============================================================================
// Command Execution Tests
// ==============================================================================

TEST_F(CmdConfigIcmpV4UnavailableSrcAddrTestFixture, setAddress) {
  setupTestableConfigSession(cmdPrefix_, "192.168.1.1");
  CmdConfigIcmpV4UnavailableSrcAddr cmd;
  HostInfo hostInfo("testhost");
  IcmpV4UnavailableSrcAddrArg arg({"192.168.1.1"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("Successfully set"));
  EXPECT_THAT(result, HasSubstr("192.168.1.1"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->icmpV4UnavailableSrcAddress(), "192.168.1.1");
}

TEST_F(CmdConfigIcmpV4UnavailableSrcAddrTestFixture, alreadySet) {
  // Seed has icmpV4UnavailableSrcAddress="10.0.0.1"; setting to same is a no-op
  setupTestableConfigSession(cmdPrefix_, "10.0.0.1");
  CmdConfigIcmpV4UnavailableSrcAddr cmd;
  HostInfo hostInfo("testhost");
  IcmpV4UnavailableSrcAddrArg arg({"10.0.0.1"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("already"));
  EXPECT_THAT(result, Not(HasSubstr("Successfully set")));

  // No-op must leave config value unchanged
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->icmpV4UnavailableSrcAddress(), "10.0.0.1");
}

// ==============================================================================
// Absent-field Tests (field not present in config — real device default state)
// ==============================================================================

TEST_F(
    CmdConfigIcmpV4UnavailableSrcAddrAbsentTestFixture,
    setAddressWhenFieldAbsent) {
  setupTestableConfigSession(cmdPrefix_, "192.0.0.8");
  CmdConfigIcmpV4UnavailableSrcAddr cmd;
  HostInfo hostInfo("testhost");
  IcmpV4UnavailableSrcAddrArg arg({"192.0.0.8"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("Successfully"));
  EXPECT_THAT(result, HasSubstr("192.0.0.8"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->icmpV4UnavailableSrcAddress(), "192.0.0.8");
}

} // namespace facebook::fboss
