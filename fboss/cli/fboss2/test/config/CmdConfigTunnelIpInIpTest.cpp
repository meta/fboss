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
#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/decap/CmdConfigTunnelIpInIpDecap.h"
#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/encap/CmdConfigTunnelIpInIpEncap.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed config has one decap tunnel (tunnelType 0 = IP_IN_IP_DECAP) and one
// encap tunnel (tunnelType 2 = IP_IN_IP_ENCAP), plus two interfaces for
// underlay validation.
class CmdConfigTunnelIpInIpTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigTunnelIpInIpTestFixture()
      : CmdConfigTestBase(
            "fboss_tunnel_ipinip_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "interfaces": [
      {
        "intfID": 1,
        "routerID": 0,
        "vlanID": 1,
        "name": "eth1/1/1",
        "mtu": 1500
      },
      {
        "intfID": 2,
        "routerID": 0,
        "vlanID": 2,
        "name": "eth1/2/1",
        "mtu": 1500
      }
    ],
    "ipInIpTunnels": [
      {
        "ipInIpTunnelId": "decap0",
        "underlayIntfID": 1,
        "dstIp": "2001:db8::1",
        "srcIp": "2001:db8::2",
        "tunnelType": 0
      },
      {
        "ipInIpTunnelId": "encap0",
        "underlayIntfID": 2,
        "dstIp": "2001:db8::3",
        "srcIp": "2001:db8::4",
        "tunnelType": 2
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
// Decap arg-type parser tests (TunnelIpInIpDecapConfig)
// ============================================================================

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapBareIdOnly) {
  TunnelIpInIpDecapConfig cfg({"decap0"});
  EXPECT_EQ(cfg.getTunnelId(), "decap0");
  EXPECT_FALSE(cfg.hasAttrs());
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapEmptyThrows) {
  EXPECT_THROW(TunnelIpInIpDecapConfig({}), std::invalid_argument);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapAttrAsFirstTokenThrows) {
  EXPECT_THROW(TunnelIpInIpDecapConfig({"destination"}), std::invalid_argument);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapMaskAsFirstTokenThrows) {
  EXPECT_THROW(
      TunnelIpInIpDecapConfig({"mask", "2001:db8::1"}), std::invalid_argument);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapCaseInsensitiveAttrs) {
  TunnelIpInIpDecapConfig cfg(
      {"Decap0", "TTL-Mode", "UNIFORM", "Termination-Type", "P2P"});
  EXPECT_EQ(cfg.getTunnelId(), "Decap0");
  EXPECT_EQ(cfg.getAttrs().at("ttl-mode"), "uniform");
  EXPECT_EQ(cfg.getAttrs().at("termination-type"), "p2p");
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapUnknownAttrThrows) {
  EXPECT_THROW(
      TunnelIpInIpDecapConfig({"decap0", "bogus-attr", "val"}),
      std::invalid_argument);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapMissingValueThrows) {
  EXPECT_THROW(
      TunnelIpInIpDecapConfig({"decap0", "ttl-mode"}), std::invalid_argument);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapDestinationWithPrefixLen) {
  TunnelIpInIpDecapConfig cfg(
      {"decap0", "destination", "2001:db8::", "mask", "32"});
  ASSERT_EQ(cfg.getAttrs().size(), 2u);
  EXPECT_EQ(cfg.getAttrs().at("destination"), "2001:db8::");
  EXPECT_EQ(cfg.getAttrs().at("destination-mask"), "32");
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapDestinationIpv4Rejected) {
  EXPECT_THROW(
      TunnelIpInIpDecapConfig({"decap0", "destination", "10.0.0.1"}),
      std::invalid_argument);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapPrefixLenOutOfRangeThrows) {
  EXPECT_THROW(
      TunnelIpInIpDecapConfig(
          {"decap0", "destination", "2001:db8::", "mask", "129"}),
      std::invalid_argument);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapTerminationTypeAccepted) {
  TunnelIpInIpDecapConfig cfg({"decap0", "termination-type", "p2mp"});
  EXPECT_EQ(cfg.getAttrs().at("termination-type"), "p2mp");
}

// mp2p/mp2mp exist in the thrift enum but the agent's SaiTunnelManager
// aborts on them at commit, so the CLI must reject them.
TEST_F(CmdConfigTunnelIpInIpTestFixture, decapTerminationTypeMp2mpRejected) {
  EXPECT_THROW(
      TunnelIpInIpDecapConfig({"decap0", "termination-type", "mp2mp"}),
      std::invalid_argument);
  EXPECT_THROW(
      TunnelIpInIpDecapConfig({"decap0", "termination-type", "mp2p"}),
      std::invalid_argument);
}

// std::stoi-style parsing must not accept trailing garbage.
TEST_F(CmdConfigTunnelIpInIpTestFixture, decapUnderlayIntfIdNotIntegerThrows) {
  EXPECT_THROW(
      TunnelIpInIpDecapConfig({"decap0", "underlay-intf-id", "2abc"}),
      std::invalid_argument);
  EXPECT_THROW(
      TunnelIpInIpDecapConfig(
          {"decap0", "destination", "2001:db8::", "mask", "32x"}),
      std::invalid_argument);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapTerminationTypeInvalidThrows) {
  EXPECT_THROW(
      TunnelIpInIpDecapConfig({"decap0", "termination-type", "p2p2p"}),
      std::invalid_argument);
}

// ============================================================================
// Encap arg-type parser tests (TunnelIpInIpEncapConfig)
// ============================================================================

TEST_F(CmdConfigTunnelIpInIpTestFixture, encapBareIdOnly) {
  TunnelIpInIpEncapConfig cfg({"encap0"});
  EXPECT_EQ(cfg.getTunnelId(), "encap0");
  EXPECT_FALSE(cfg.hasAttrs());
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, encapAcceptsCommonAttrs) {
  TunnelIpInIpEncapConfig cfg(
      {"encap0",
       "destination",
       "2001:db8::9",
       "source",
       "2001:db8::a",
       "ttl-mode",
       "pipe",
       "underlay-intf-id",
       "2"});
  EXPECT_EQ(cfg.getAttrs().at("destination"), "2001:db8::9");
  EXPECT_EQ(cfg.getAttrs().at("source"), "2001:db8::a");
  EXPECT_EQ(cfg.getAttrs().at("ttl-mode"), "pipe");
  EXPECT_EQ(cfg.getAttrs().at("underlay-intf-id"), "2");
}

// termination-type is decap-only; the encap parser must reject it.
TEST_F(CmdConfigTunnelIpInIpTestFixture, encapTerminationTypeRejected) {
  try {
    TunnelIpInIpEncapConfig cfg({"encap0", "termination-type", "p2p"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("termination-type"));
  }
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, encapSourceIpv4Rejected) {
  EXPECT_THROW(
      TunnelIpInIpEncapConfig({"encap0", "source", "192.168.1.1"}),
      std::invalid_argument);
}

// ============================================================================
// Decap queryClient tests
// ============================================================================

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapSetDestinationSuccess) {
  CmdConfigTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapConfig cfg({"decap0", "destination", "2001:db8::5"});

  auto result = cmd.queryClient(localhost(), cfg);
  EXPECT_THAT(result, HasSubstr("decap"));
  EXPECT_THAT(result, HasSubstr("2001:db8::5"));

  const auto* t = findTunnel("decap0");
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(*t->dstIp(), "2001:db8::5");
  EXPECT_EQ(*t->tunnelType(), TunnelType::IP_IN_IP_DECAP);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapSetTerminationType) {
  CmdConfigTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapConfig cfg({"decap0", "termination-type", "p2p"});

  auto result = cmd.queryClient(localhost(), cfg);
  EXPECT_THAT(result, HasSubstr("termination-type=p2p"));

  const auto* t = findTunnel("decap0");
  ASSERT_NE(t, nullptr);
  ASSERT_TRUE(t->tunnelTermType().has_value());
  EXPECT_EQ(*t->tunnelTermType(), cfg::TunnelTerminationType::P2P);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapAutoCreateStampsDecapType) {
  CmdConfigTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapConfig cfg(
      {"new-decap", "destination", "2001:db8::6", "underlay-intf-id", "1"});

  auto result = cmd.queryClient(localhost(), cfg);
  EXPECT_THAT(result, HasSubstr("new-decap"));

  const auto* t = findTunnel("new-decap");
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(*t->tunnelType(), TunnelType::IP_IN_IP_DECAP);
}

// Decap requires 'destination' at create: the agent unconditionally parses
// dstIp as an IPv6 address and would reject/abort on the "" default.
TEST_F(
    CmdConfigTunnelIpInIpTestFixture,
    decapCreateWithoutDestinationRejected) {
  CmdConfigTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapConfig cfg({"new-decap-nodst", "underlay-intf-id", "1"});
  try {
    cmd.queryClient(localhost(), cfg);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("destination"));
  }
  EXPECT_EQ(findTunnel("new-decap-nodst"), nullptr);
}

// Both directions require 'underlay-intf-id' at create: the thrift default of
// 0 names no router interface and SaiTunnelManager aborts on it at commit.
TEST_F(CmdConfigTunnelIpInIpTestFixture, decapCreateWithoutUnderlayRejected) {
  CmdConfigTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapConfig cfg(
      {"new-decap-nounder", "destination", "2001:db8::6"});
  try {
    cmd.queryClient(localhost(), cfg);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("underlay-intf-id"));
  }
  EXPECT_EQ(findTunnel("new-decap-nounder"), nullptr);
}

// A p2p termination matches on the outer source; setting it without a source
// (on a new or existing tunnel) would abort the agent at commit.
TEST_F(CmdConfigTunnelIpInIpTestFixture, decapP2pWithoutSourceRejected) {
  CmdConfigTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapConfig cfg(
      {"new-decap-p2p",
       "destination",
       "2001:db8::6",
       "underlay-intf-id",
       "1",
       "termination-type",
       "p2p"});
  try {
    cmd.queryClient(localhost(), cfg);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("source"));
  }
  EXPECT_EQ(findTunnel("new-decap-p2p"), nullptr);
}

// The thrift mask fields hold an IPv6 mask *address* string, not a prefix
// length — ThriftConfigApplier parses them with folly::IPAddressV6 and would
// throw on a bare integer at commit. Verify the CLI stores the expanded form.
TEST_F(CmdConfigTunnelIpInIpTestFixture, decapMaskStoredAsIpv6MaskString) {
  CmdConfigTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapConfig cfg(
      {"decap0", "destination", "2001:db8::", "mask", "64"});

  auto result = cmd.queryClient(localhost(), cfg);
  EXPECT_THAT(result, HasSubstr("prefix-len=64"));

  const auto* t = findTunnel("decap0");
  ASSERT_NE(t, nullptr);
  ASSERT_TRUE(t->dstIpMask().has_value());
  EXPECT_EQ(*t->dstIpMask(), "ffff:ffff:ffff:ffff::");
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapSetUnderlayIntfId) {
  CmdConfigTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapConfig cfg({"decap0", "underlay-intf-id", "2"});
  auto result = cmd.queryClient(localhost(), cfg);
  EXPECT_THAT(result, HasSubstr("underlay-intf-id=2"));

  const auto* t = findTunnel("decap0");
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(*t->underlayIntfID(), 2);
}

TEST_F(
    CmdConfigTunnelIpInIpTestFixture,
    decapSetUnderlayIntfIdNonExistentThrows) {
  CmdConfigTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapConfig cfg({"decap0", "underlay-intf-id", "999"});
  EXPECT_THROW(cmd.queryClient(localhost(), cfg), std::invalid_argument);
}

// ============================================================================
// Encap queryClient tests
// ============================================================================

TEST_F(CmdConfigTunnelIpInIpTestFixture, encapSetDestinationSuccess) {
  CmdConfigTunnelIpInIpEncap cmd;
  TunnelIpInIpEncapConfig cfg({"encap0", "destination", "2001:db8::7"});

  auto result = cmd.queryClient(localhost(), cfg);
  EXPECT_THAT(result, HasSubstr("encap"));
  EXPECT_THAT(result, HasSubstr("2001:db8::7"));

  const auto* t = findTunnel("encap0");
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(*t->dstIp(), "2001:db8::7");
  EXPECT_EQ(*t->tunnelType(), TunnelType::IP_IN_IP_ENCAP);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, encapAutoCreateStampsEncapType) {
  CmdConfigTunnelIpInIpEncap cmd;
  TunnelIpInIpEncapConfig cfg(
      {"new-encap",
       "destination",
       "2001:db8::c",
       "source",
       "2001:db8::b",
       "ttl-mode",
       "uniform",
       "underlay-intf-id",
       "1"});

  auto result = cmd.queryClient(localhost(), cfg);
  EXPECT_THAT(result, HasSubstr("new-encap"));
  EXPECT_THAT(result, HasSubstr("ttl-mode=uniform"));

  const auto* t = findTunnel("new-encap");
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(*t->tunnelType(), TunnelType::IP_IN_IP_ENCAP);
  ASSERT_TRUE(t->srcIp().has_value());
  EXPECT_EQ(*t->srcIp(), "2001:db8::b");
}

// Encap requires both source and destination; creating with only one (or
// neither) must be rejected here rather than crashing the agent at commit.
TEST_F(CmdConfigTunnelIpInIpTestFixture, encapCreateWithoutSourceRejected) {
  CmdConfigTunnelIpInIpEncap cmd;
  TunnelIpInIpEncapConfig cfg(
      {"new-encap-nosrc", "destination", "2001:db8::c"});
  try {
    cmd.queryClient(localhost(), cfg);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("source"));
  }
  // No half-created tunnel should have been left behind.
  EXPECT_EQ(findTunnel("new-encap-nosrc"), nullptr);
}

TEST_F(
    CmdConfigTunnelIpInIpTestFixture,
    encapCreateWithoutDestinationRejected) {
  CmdConfigTunnelIpInIpEncap cmd;
  TunnelIpInIpEncapConfig cfg({"new-encap-nodst", "source", "2001:db8::c"});
  try {
    cmd.queryClient(localhost(), cfg);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("destination"));
  }
  EXPECT_EQ(findTunnel("new-encap-nodst"), nullptr);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, encapCreateWithoutUnderlayRejected) {
  CmdConfigTunnelIpInIpEncap cmd;
  TunnelIpInIpEncapConfig cfg(
      {"new-encap-nounder",
       "destination",
       "2001:db8::c",
       "source",
       "2001:db8::b"});
  try {
    cmd.queryClient(localhost(), cfg);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("underlay-intf-id"));
  }
  EXPECT_EQ(findTunnel("new-encap-nounder"), nullptr);
}

// Updating an existing (complete) encap tunnel one attribute at a time is fine.
TEST_F(CmdConfigTunnelIpInIpTestFixture, encapPartialUpdateOnExistingOk) {
  CmdConfigTunnelIpInIpEncap cmd;
  TunnelIpInIpEncapConfig cfg({"encap0", "ttl-mode", "pipe"});
  auto result = cmd.queryClient(localhost(), cfg);
  EXPECT_THAT(result, HasSubstr("ttl-mode=pipe"));
}

// ============================================================================
// Direction-change guard: reconfiguring an existing tunnel with the wrong
// direction must be rejected rather than silently flipping tunnelType.
// ============================================================================

TEST_F(CmdConfigTunnelIpInIpTestFixture, encapOnExistingDecapRejected) {
  CmdConfigTunnelIpInIpEncap cmd;
  TunnelIpInIpEncapConfig cfg({"decap0", "destination", "2001:db8::8"});
  try {
    cmd.queryClient(localhost(), cfg);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("decap"));
    EXPECT_THAT(e.what(), HasSubstr("encap"));
  }
  // The existing tunnel keeps its decap type.
  const auto* t = findTunnel("decap0");
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(*t->tunnelType(), TunnelType::IP_IN_IP_DECAP);
}

TEST_F(CmdConfigTunnelIpInIpTestFixture, decapOnExistingEncapRejected) {
  CmdConfigTunnelIpInIpDecap cmd;
  TunnelIpInIpDecapConfig cfg({"encap0", "destination", "2001:db8::8"});
  EXPECT_THROW(cmd.queryClient(localhost(), cfg), std::invalid_argument);
  const auto* t = findTunnel("encap0");
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(*t->tunnelType(), TunnelType::IP_IN_IP_ENCAP);
}

} // namespace facebook::fboss
