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
#include "fboss/cli/fboss2/commands/delete/tunnel/ip_in_ip/decap/CmdDeleteTunnelIpInIpDecap.h"
#include "fboss/cli/fboss2/commands/delete/tunnel/ip_in_ip/encap/CmdDeleteTunnelIpInIpEncap.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed: one fully-populated decap tunnel, one encap tunnel, a bare decap
// tunnel with no optional attrs set, and a p2p-terminated decap tunnel.
// Enum values in the JSON seed (JSON cannot carry comments):
//   tunnelType:     0 = IP_IN_IP_DECAP, 2 = IP_IN_IP_ENCAP
//   tunnelTermType: 1 = P2P, 2 = P2MP
//   ttl/dscp/ecnMode: 0 = UNIFORM, 1 = PIPE
class CmdDeleteTunnelIpInIpTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteTunnelIpInIpTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_tunnel_ipinip_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ipInIpTunnels": [
      {
        "ipInIpTunnelId": "decap0",
        "underlayIntfID": 1,
        "dstIp": "2001:db8::1",
        "dstIpMask": "64",
        "srcIp": "2001:db8::2",
        "srcIpMask": "48",
        "ttlMode": 1,
        "dscpMode": 0,
        "ecnMode": 1,
        "tunnelTermType": 2,
        "tunnelType": 0
      },
      {
        "ipInIpTunnelId": "encap0",
        "underlayIntfID": 2,
        "dstIp": "2001:db8::3",
        "srcIp": "2001:db8::4",
        "ttlMode": 1,
        "tunnelType": 2
      },
      {
        "ipInIpTunnelId": "decap1",
        "underlayIntfID": 2,
        "dstIp": "2001:db8::5",
        "tunnelType": 0
      },
      {
        "ipInIpTunnelId": "decapP2p",
        "underlayIntfID": 1,
        "dstIp": "2001:db8::6",
        "srcIp": "2001:db8::7",
        "tunnelTermType": 1,
        "tunnelType": 0
      }
    ]
  }
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

 protected:
  const cfg::IpInIpTunnel* findTunnel(const std::string& id) {
    auto& swConfig =
        *ConfigSession::getInstance().config<AgentConfigDomain>().sw();
    if (!swConfig.ipInIpTunnels().has_value()) {
      return nullptr;
    }
    for (const auto& t : *swConfig.ipInIpTunnels()) {
      if (*t.ipInIpTunnelId() == id) {
        return &t;
      }
    }
    return nullptr;
  }
};

// ============================================================================
// Decap delete-args parser tests
// ============================================================================

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapArgsEmptyThrows) {
  EXPECT_THROW(TunnelIpInIpDecapDeleteArgs({}), std::invalid_argument);
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapArgsAttrAsFirstTokenThrows) {
  EXPECT_THROW(
      TunnelIpInIpDecapDeleteArgs({"ttl-mode"}), std::invalid_argument);
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapArgsBareIdMeansDeleteWhole) {
  TunnelIpInIpDecapDeleteArgs args({"decap0"});
  EXPECT_EQ(args.getTunnelId(), "decap0");
  EXPECT_TRUE(args.deleteEntireTunnel());
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapArgsRequiredFieldRejected) {
  try {
    TunnelIpInIpDecapDeleteArgs args({"decap0", "destination"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Cannot reset required field"));
  }
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapArgsTerminationTypeAccepted) {
  TunnelIpInIpDecapDeleteArgs args({"decap0", "termination-type"});
  ASSERT_EQ(args.getAttributes().size(), 1u);
  EXPECT_EQ(args.getAttributes()[0], "termination-type");
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapArgsMultipleAttrs) {
  TunnelIpInIpDecapDeleteArgs args(
      {"decap0", "ttl-mode", "dscp-mode", "ecn-mode"});
  ASSERT_EQ(args.getAttributes().size(), 3u);
}

// ============================================================================
// Encap delete-args parser tests
// ============================================================================

TEST_F(CmdDeleteTunnelIpInIpTestFixture, encapArgsBareIdMeansDeleteWhole) {
  TunnelIpInIpEncapDeleteArgs args({"encap0"});
  EXPECT_EQ(args.getTunnelId(), "encap0");
  EXPECT_TRUE(args.deleteEntireTunnel());
}

// termination-type is decap-only; encap delete must reject resetting it.
TEST_F(CmdDeleteTunnelIpInIpTestFixture, encapArgsTerminationTypeRejected) {
  try {
    TunnelIpInIpEncapDeleteArgs args({"encap0", "termination-type"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("termination-type"));
  }
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, encapArgsCommonAttrsAccepted) {
  TunnelIpInIpEncapDeleteArgs args({"encap0", "ttl-mode", "ecn-mode"});
  ASSERT_EQ(args.getAttributes().size(), 2u);
}

// 'source' is required for encap — the agent reads it unconditionally when
// programming the encap — so resetting it must be rejected like the other
// required fields.
TEST_F(CmdDeleteTunnelIpInIpTestFixture, encapArgsSourceRejected) {
  try {
    TunnelIpInIpEncapDeleteArgs args({"encap0", "source"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Cannot reset required field"));
  }
  // The tunnel is untouched.
  const auto* t = findTunnel("encap0");
  ASSERT_NE(t, nullptr);
  ASSERT_TRUE(t->srcIp().has_value());
  EXPECT_EQ(*t->srcIp(), "2001:db8::4");
}

// ============================================================================
// Decap queryClient — reset + delete
// ============================================================================

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapResetTerminationType) {
  CmdDeleteTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapDeleteArgs args({"decap0", "termination-type"});
  auto result = cmd.queryClient(localhost(), args);
  EXPECT_THAT(result, HasSubstr("Successfully reset"));

  const auto* t = findTunnel("decap0");
  ASSERT_NE(t, nullptr);
  EXPECT_FALSE(t->tunnelTermType().has_value());
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapResetSource) {
  CmdDeleteTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapDeleteArgs args({"decap0", "source"});
  cmd.queryClient(localhost(), args);

  const auto* t = findTunnel("decap0");
  ASSERT_NE(t, nullptr);
  EXPECT_FALSE(t->srcIp().has_value());
  EXPECT_FALSE(t->srcIpMask().has_value());
  EXPECT_EQ(*t->dstIp(), "2001:db8::1");
}

// Resetting 'source' on a p2p-terminated decap tunnel would leave the agent
// unable to build the tunnel term — it must be rejected unless
// termination-type is reset in the same command.
TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapResetSourceOnP2pRejected) {
  CmdDeleteTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapDeleteArgs args({"decapP2p", "source"});
  try {
    cmd.queryClient(localhost(), args);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("source"));
  }
  const auto* t = findTunnel("decapP2p");
  ASSERT_NE(t, nullptr);
  ASSERT_TRUE(t->srcIp().has_value());
  EXPECT_EQ(*t->srcIp(), "2001:db8::7");
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapResetSourceAndTermTypeOnP2pOk) {
  CmdDeleteTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapDeleteArgs args({"decapP2p", "source", "termination-type"});
  auto result = cmd.queryClient(localhost(), args);
  EXPECT_THAT(result, HasSubstr("Successfully reset"));

  const auto* t = findTunnel("decapP2p");
  ASSERT_NE(t, nullptr);
  EXPECT_FALSE(t->srcIp().has_value());
  EXPECT_FALSE(t->tunnelTermType().has_value());
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapDeleteEntireTunnel) {
  CmdDeleteTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapDeleteArgs args({"decap0"});
  auto result = cmd.queryClient(localhost(), args);
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));

  EXPECT_EQ(findTunnel("decap0"), nullptr);
  EXPECT_NE(findTunnel("encap0"), nullptr);
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapDeleteUnknownIsIdempotent) {
  CmdDeleteTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapDeleteArgs args({"does-not-exist"});
  auto result = cmd.queryClient(localhost(), args);
  EXPECT_THAT(result, HasSubstr("not found"));
}

// ============================================================================
// Encap queryClient — reset + delete
// ============================================================================

TEST_F(CmdDeleteTunnelIpInIpTestFixture, encapResetTtlMode) {
  CmdDeleteTunnelIpInIpEncap cmd;
  TunnelIpInIpEncapDeleteArgs args({"encap0", "ttl-mode"});
  auto result = cmd.queryClient(localhost(), args);
  EXPECT_THAT(result, HasSubstr("Successfully reset"));

  const auto* t = findTunnel("encap0");
  ASSERT_NE(t, nullptr);
  EXPECT_FALSE(t->ttlMode().has_value());
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, encapDeleteEntireTunnel) {
  CmdDeleteTunnelIpInIpEncap cmd;
  TunnelIpInIpEncapDeleteArgs args({"encap0"});
  auto result = cmd.queryClient(localhost(), args);
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_EQ(findTunnel("encap0"), nullptr);
}

// ============================================================================
// Direction guard — deleting with the wrong direction is rejected.
// ============================================================================

TEST_F(CmdDeleteTunnelIpInIpTestFixture, encapDeleteOnDecapTunnelRejected) {
  CmdDeleteTunnelIpInIpEncap cmd;
  TunnelIpInIpEncapDeleteArgs args({"decap0"});
  try {
    cmd.queryClient(localhost(), args);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("decap"));
  }
  // Tunnel must survive the rejected delete.
  EXPECT_NE(findTunnel("decap0"), nullptr);
}

TEST_F(CmdDeleteTunnelIpInIpTestFixture, decapDeleteOnEncapTunnelRejected) {
  CmdDeleteTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapDeleteArgs args({"encap0"});
  EXPECT_THROW(cmd.queryClient(localhost(), args), std::invalid_argument);
  EXPECT_NE(findTunnel("encap0"), nullptr);
}

// ============================================================================
// printOutput
// ============================================================================

TEST_F(CmdDeleteTunnelIpInIpTestFixture, printOutput) {
  CmdDeleteTunnelIpInIpDecap cmd;
  testing::internal::CaptureStdout();
  cmd.printOutput("test output");
  std::string output = testing::internal::GetCapturedStdout();
  EXPECT_THAT(output, HasSubstr("test output"));
}

} // namespace facebook::fboss
