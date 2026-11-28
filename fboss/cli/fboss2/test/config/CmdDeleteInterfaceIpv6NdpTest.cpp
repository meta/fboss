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
#include <vector>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/delete/interface/ipv6/ndp/CmdDeleteInterfaceIpv6Ndp.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class CmdDeleteInterfaceIpv6NdpTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteInterfaceIpv6NdpTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_ipv6_ndp_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [],
    "interfaces": [
      {
        "intfID": 1,
        "routerID": 0,
        "vlanID": 1,
        "name": "eth1/1/1",
        "mtu": 1500,
        "ndp": {
          "routerAdvertisementSeconds": 30,
          "curHopLimit": 64,
          "routerLifetime": 900,
          "prefixValidLifetimeSeconds": 86400,
          "prefixPreferredLifetimeSeconds": 43200,
          "routerAdvertisementManagedBit": true,
          "routerAdvertisementOtherBit": true,
          "routerAddress": "2001:db8::1"
        }
      },
      {
        "intfID": 2,
        "routerID": 0,
        "vlanID": 2,
        "name": "eth1/2/1",
        "mtu": 1500
      }
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete interface";
};

// ---------------------------------------------------------------------------
// NdpDeleteAttrs parsing tests
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, ndpDeleteAttrsEmpty) {
  setupTestableConfigSession();
  EXPECT_THROW(NdpDeleteAttrs({}), std::invalid_argument);
}

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, ndpDeleteAttrsUnknown) {
  setupTestableConfigSession();
  try {
    NdpDeleteAttrs({"bogus"}); // NOLINT(bugprone-unused-raii)
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Unknown ndp attribute"));
    EXPECT_THAT(e.what(), HasSubstr("bogus"));
  }
}

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, ndpDeleteAttrsCaseInsensitive) {
  setupTestableConfigSession();
  NdpDeleteAttrs attrs({"RA-INTERVAL"});
  ASSERT_EQ(attrs.getAttributes().size(), 1);
  EXPECT_EQ(attrs.getAttributes()[0], "ra-interval");
}

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, ndpDeleteAttrsMultiple) {
  setupTestableConfigSession();
  NdpDeleteAttrs attrs({"ra-interval", "hop-limit"});
  ASSERT_EQ(attrs.getAttributes().size(), 2);
  EXPECT_EQ(attrs.getAttributes()[0], "ra-interval");
  EXPECT_EQ(attrs.getAttributes()[1], "hop-limit");
}

// ---------------------------------------------------------------------------
// queryClient: individual attribute reset tests
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, queryClientResetsRaInterval) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 ipv6 ndp ra-interval");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  utils::InterfaceList interfaces({"eth1/1/1"});
  NdpDeleteAttrs attrs({"ra-interval"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully reset"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));

  auto& ifaces = *ConfigSession::getInstance()
                      .config<AgentConfigDomain>()
                      .sw()
                      ->interfaces();
  EXPECT_EQ(*ifaces[0].ndp()->routerAdvertisementSeconds(), 0);
}

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, queryClientResetsHopLimit) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 ipv6 ndp hop-limit");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  utils::InterfaceList interfaces({"eth1/1/1"});
  NdpDeleteAttrs attrs({"hop-limit"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully reset"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));

  auto& ifaces = *ConfigSession::getInstance()
                      .config<AgentConfigDomain>()
                      .sw()
                      ->interfaces();
  EXPECT_EQ(*ifaces[0].ndp()->curHopLimit(), 255);
}

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, queryClientResetsRaLifetime) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 ipv6 ndp ra-lifetime");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  utils::InterfaceList interfaces({"eth1/1/1"});
  NdpDeleteAttrs attrs({"ra-lifetime"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully reset"));

  auto& ifaces = *ConfigSession::getInstance()
                      .config<AgentConfigDomain>()
                      .sw()
                      ->interfaces();
  EXPECT_EQ(*ifaces[0].ndp()->routerLifetime(), 1800);
}

TEST_F(
    CmdDeleteInterfaceIpv6NdpTestFixture,
    queryClientResetsPrefixValidLifetime) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 ipv6 ndp prefix-valid-lifetime");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  utils::InterfaceList interfaces({"eth1/1/1"});
  NdpDeleteAttrs attrs({"prefix-valid-lifetime"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully reset"));

  auto& ifaces = *ConfigSession::getInstance()
                      .config<AgentConfigDomain>()
                      .sw()
                      ->interfaces();
  EXPECT_EQ(*ifaces[0].ndp()->prefixValidLifetimeSeconds(), 2592000);
}

TEST_F(
    CmdDeleteInterfaceIpv6NdpTestFixture,
    queryClientResetsPrefixPreferredLifetime) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 ipv6 ndp prefix-preferred-lifetime");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  utils::InterfaceList interfaces({"eth1/1/1"});
  NdpDeleteAttrs attrs({"prefix-preferred-lifetime"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully reset"));

  auto& ifaces = *ConfigSession::getInstance()
                      .config<AgentConfigDomain>()
                      .sw()
                      ->interfaces();
  EXPECT_EQ(*ifaces[0].ndp()->prefixPreferredLifetimeSeconds(), 604800);
}

TEST_F(
    CmdDeleteInterfaceIpv6NdpTestFixture,
    queryClientResetsManagedConfigFlag) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 ipv6 ndp managed-config-flag");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  utils::InterfaceList interfaces({"eth1/1/1"});
  NdpDeleteAttrs attrs({"managed-config-flag"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully reset"));

  auto& ifaces = *ConfigSession::getInstance()
                      .config<AgentConfigDomain>()
                      .sw()
                      ->interfaces();
  EXPECT_FALSE(*ifaces[0].ndp()->routerAdvertisementManagedBit());
}

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, queryClientResetsOtherConfigFlag) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 ipv6 ndp other-config-flag");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  utils::InterfaceList interfaces({"eth1/1/1"});
  NdpDeleteAttrs attrs({"other-config-flag"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully reset"));

  auto& ifaces = *ConfigSession::getInstance()
                      .config<AgentConfigDomain>()
                      .sw()
                      ->interfaces();
  EXPECT_FALSE(*ifaces[0].ndp()->routerAdvertisementOtherBit());
}

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, queryClientResetsRaAddress) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 ipv6 ndp ra-address");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  utils::InterfaceList interfaces({"eth1/1/1"});
  NdpDeleteAttrs attrs({"ra-address"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully reset"));

  auto& ifaces = *ConfigSession::getInstance()
                      .config<AgentConfigDomain>()
                      .sw()
                      ->interfaces();
  EXPECT_FALSE(ifaces[0].ndp()->routerAddress().has_value());
}

// ---------------------------------------------------------------------------
// queryClient: idempotent — no NDP config on interface
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, queryClientIdempotentNoNdp) {
  // eth1/2/1 has no ndp block in the initial config
  setupTestableConfigSession(cmdPrefix_, "eth1/2/1 ipv6 ndp ra-interval");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  utils::InterfaceList interfaces({"eth1/2/1"});
  NdpDeleteAttrs attrs({"ra-interval"});

  // Should not throw, and should report that there was nothing to reset
  auto result = cmd.queryClient(localhost(), interfaces, attrs);
  EXPECT_THAT(result, HasSubstr("nothing to reset"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));

  // The no-op must not spuriously create an NDP block on the interface
  auto& ifaces = *ConfigSession::getInstance()
                      .config<AgentConfigDomain>()
                      .sw()
                      ->interfaces();
  EXPECT_FALSE(ifaces[1].ndp().has_value());
}

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, ndpDeleteAttrsDuplicateRejected) {
  setupTestableConfigSession();
  try {
    NdpDeleteAttrs( // NOLINT(bugprone-unused-raii)
        {"ra-interval", "ra-interval"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Duplicate ndp attribute"));
    EXPECT_THAT(e.what(), HasSubstr("ra-interval"));
  }
}

// ---------------------------------------------------------------------------
// queryClient: multiple attributes at once
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, queryClientMultipleAttrs) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 ipv6 ndp ra-interval hop-limit");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  utils::InterfaceList interfaces({"eth1/1/1"});
  NdpDeleteAttrs attrs({"ra-interval", "hop-limit"});

  auto result = cmd.queryClient(localhost(), interfaces, attrs);

  EXPECT_THAT(result, HasSubstr("Successfully reset"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));

  auto& ifaces = *ConfigSession::getInstance()
                      .config<AgentConfigDomain>()
                      .sw()
                      ->interfaces();
  EXPECT_EQ(*ifaces[0].ndp()->routerAdvertisementSeconds(), 0);
  EXPECT_EQ(*ifaces[0].ndp()->curHopLimit(), 255);
}

// ---------------------------------------------------------------------------
// printOutput test
// ---------------------------------------------------------------------------

TEST_F(CmdDeleteInterfaceIpv6NdpTestFixture, printOutput) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 ipv6 ndp ra-interval");
  auto cmd = CmdDeleteInterfaceIpv6Ndp();
  testing::internal::CaptureStdout();
  cmd.printOutput("test output");
  std::string output = testing::internal::GetCapturedStdout();
  EXPECT_THAT(output, HasSubstr("test output"));
}

} // namespace facebook::fboss
