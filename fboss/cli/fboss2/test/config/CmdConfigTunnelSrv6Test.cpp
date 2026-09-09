/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/commands/config/tunnel/srv6/decap/CmdConfigTunnelSrv6Decap.h"
#include "fboss/cli/fboss2/commands/config/tunnel/srv6/encap/CmdConfigTunnelSrv6Encap.h"
#include "fboss/cli/fboss2/commands/delete/tunnel/srv6/decap/CmdDeleteTunnelSrv6Decap.h"
#include "fboss/cli/fboss2/commands/delete/tunnel/srv6/encap/CmdDeleteTunnelSrv6Encap.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigTunnelSrv6TestFixture : public CmdConfigTestBase {
 public:
  CmdConfigTunnelSrv6TestFixture()
      : CmdConfigTestBase(
            "fboss_tunnel_srv6_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "interfaces": [
      {
        "intfID": 11,
        "routerID": 0,
        "vlanID": 11,
        "name": "eth1/1/1",
        "mtu": 1500
      },
      {
        "intfID": 12,
        "routerID": 0,
        "vlanID": 12,
        "name": "eth1/2/1",
        "mtu": 1500
      }
    ],
    "srv6Tunnels": [
      {
        "srv6TunnelId": "existing-encap",
        "underlayIntfID": 11,
        "srcIp": "2001:db8::1",
        "ttlMode": 1,
        "dscpMode": 0,
        "ecnMode": 1,
        "tunnelTermType": 2,
        "tunnelType": 1
      },
      {
        "srv6TunnelId": "existing-decap",
        "underlayIntfID": 12,
        "ttlMode": 0,
        "tunnelType": 3
      }
    ]
  }
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

 protected:
  const cfg::Srv6Tunnel* findTunnel(const std::string& id) {
    auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
    if (!swConfig.srv6Tunnels().has_value()) {
      return nullptr;
    }
    for (const auto& tunnel : *swConfig.srv6Tunnels()) {
      if (*tunnel.srv6TunnelId() == id) {
        return &tunnel;
      }
    }
    return nullptr;
  }

  HostInfo hostInfo_{"testhost"};
};

TEST_F(CmdConfigTunnelSrv6TestFixture, parsesTicketExample) {
  TunnelSrv6EncapConfig args(
      {"srv6_tunnel",
       "underlay-intf-id",
       "11",
       "source",
       "fdad:feff:200::1:0",
       "termination-type",
       "p2mp",
       "ttl-mode",
       "uniform",
       "dscp-mode",
       "uniform",
       "ecn-mode",
       "uniform"});

  EXPECT_EQ(args.getTunnelId(), "srv6_tunnel");
  EXPECT_EQ(args.getAttrs().at("underlay-intf-id"), "11");
  EXPECT_EQ(args.getAttrs().at("source"), "fdad:feff:200::1:0");
  EXPECT_EQ(args.getAttrs().at("termination-type"), "p2mp");
}

TEST_F(CmdConfigTunnelSrv6TestFixture, configuresTicketExample) {
  CmdConfigTunnelSrv6Encap command;
  TunnelSrv6EncapConfig args(
      {"srv6_tunnel",
       "underlay-intf-id",
       "11",
       "source",
       "fdad:feff:200::1:0",
       "termination-type",
       "p2mp",
       "ttl-mode",
       "uniform",
       "dscp-mode",
       "uniform",
       "ecn-mode",
       "uniform"});

  command.queryClient(hostInfo_, args);
  auto* tunnel = findTunnel("srv6_tunnel");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(*tunnel->srv6TunnelId(), "srv6_tunnel");
  EXPECT_EQ(*tunnel->underlayIntfID(), 11);
  EXPECT_EQ(*tunnel->srcIp(), "fdad:feff:200::1:0");
  EXPECT_EQ(*tunnel->tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(*tunnel->tunnelTermType(), cfg::TunnelTerminationType::P2MP);
  EXPECT_EQ(*tunnel->ttlMode(), cfg::TunnelMode::UNIFORM);
  EXPECT_EQ(*tunnel->dscpMode(), cfg::TunnelMode::UNIFORM);
  EXPECT_EQ(*tunnel->ecnMode(), cfg::TunnelMode::UNIFORM);
}

TEST_F(CmdConfigTunnelSrv6TestFixture, updatesExistingTunnel) {
  CmdConfigTunnelSrv6Encap command;
  TunnelSrv6EncapConfig args(
      {"existing-encap", "ttl-mode", "uniform", "source", "2001:db8::2"});
  command.queryClient(hostInfo_, args);

  auto* tunnel = findTunnel("existing-encap");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(*tunnel->srcIp(), "2001:db8::2");
  EXPECT_EQ(*tunnel->ttlMode(), cfg::TunnelMode::UNIFORM);
}

TEST_F(CmdConfigTunnelSrv6TestFixture, validatesArguments) {
  EXPECT_THROW(TunnelSrv6EncapConfig({}), std::invalid_argument);
  EXPECT_THROW(
      TunnelSrv6EncapConfig(
          {"new", "underlay-intf-id", "11", "source", "192.0.2.1"}),
      std::invalid_argument);
  EXPECT_THROW(
      TunnelSrv6EncapConfig({"new", "ttl-mode", "user"}),
      std::invalid_argument);
  EXPECT_THROW(
      TunnelSrv6EncapConfig({"new", "ttl-mode", "pipe", "ttl-mode", "uniform"}),
      std::invalid_argument);
  EXPECT_THROW(
      TunnelSrv6DecapConfig({"new", "source", "2001:db8::1"}),
      std::invalid_argument);
  EXPECT_THROW(
      srv6_tunnel_utils::directionName(TunnelType::IP_IN_IP_ENCAP),
      std::invalid_argument);
}

TEST_F(CmdConfigTunnelSrv6TestFixture, rejectsIncompleteOrWrongDirection) {
  CmdConfigTunnelSrv6Encap encapCommand;
  EXPECT_THROW(
      encapCommand.queryClient(
          hostInfo_, TunnelSrv6EncapConfig({"new", "source", "2001:db8::1"})),
      std::invalid_argument);
  EXPECT_THROW(
      encapCommand.queryClient(
          hostInfo_,
          TunnelSrv6EncapConfig(
              {"new", "underlay-intf-id", "99", "source", "2001:db8::1"})),
      std::invalid_argument);
  EXPECT_THROW(
      encapCommand.queryClient(
          hostInfo_, TunnelSrv6EncapConfig({"existing-decap"})),
      std::invalid_argument);
}

TEST_F(CmdConfigTunnelSrv6TestFixture, configuresDecap) {
  CmdConfigTunnelSrv6Decap command;
  TunnelSrv6DecapConfig args(
      {"existing-decap", "ttl-mode", "pipe", "ecn-mode", "pipe"});
  command.queryClient(hostInfo_, args);

  auto* tunnel = findTunnel("existing-decap");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(*tunnel->tunnelType(), TunnelType::SRV6_DECAP);
  EXPECT_FALSE(tunnel->srcIp().has_value());
  EXPECT_EQ(*tunnel->ttlMode(), cfg::TunnelMode::PIPE);
  EXPECT_EQ(*tunnel->ecnMode(), cfg::TunnelMode::PIPE);
}

TEST_F(CmdConfigTunnelSrv6TestFixture, createsDecapWithoutUnderlayInterface) {
  CmdDeleteTunnelSrv6Decap deleteCommand;
  deleteCommand.queryClient(
      hostInfo_,
      srv6_tunnel_delete_utils::TunnelSrv6DeleteArgs({"existing-decap"}));

  CmdConfigTunnelSrv6Decap configCommand;
  configCommand.queryClient(
      hostInfo_, TunnelSrv6DecapConfig({"new-decap", "ttl-mode", "pipe"}));

  auto* tunnel = findTunnel("new-decap");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_EQ(*tunnel->tunnelType(), TunnelType::SRV6_DECAP);
  EXPECT_EQ(*tunnel->underlayIntfID(), 0);
  EXPECT_FALSE(tunnel->srcIp().has_value());
  EXPECT_EQ(*tunnel->ttlMode(), cfg::TunnelMode::PIPE);
}

TEST_F(CmdConfigTunnelSrv6TestFixture, rejectsSecondDecap) {
  CmdConfigTunnelSrv6Decap command;
  EXPECT_THROW(
      command.queryClient(
          hostInfo_,
          TunnelSrv6DecapConfig({"second-decap", "ttl-mode", "pipe"})),
      std::invalid_argument);
}

TEST_F(CmdConfigTunnelSrv6TestFixture, resetsOptionalAttributes) {
  CmdDeleteTunnelSrv6Encap command;
  srv6_tunnel_delete_utils::TunnelSrv6DeleteArgs args(
      {"existing-encap", "ttl-mode", "termination-type"});
  command.queryClient(hostInfo_, args);

  auto* tunnel = findTunnel("existing-encap");
  ASSERT_NE(tunnel, nullptr);
  EXPECT_FALSE(tunnel->ttlMode().has_value());
  EXPECT_FALSE(tunnel->tunnelTermType().has_value());
  EXPECT_TRUE(tunnel->dscpMode().has_value());
}

TEST_F(CmdConfigTunnelSrv6TestFixture, deletesEntireTunnel) {
  CmdDeleteTunnelSrv6Decap command;
  srv6_tunnel_delete_utils::TunnelSrv6DeleteArgs args({"existing-decap"});
  command.queryClient(hostInfo_, args);
  EXPECT_EQ(findTunnel("existing-decap"), nullptr);
  EXPECT_NE(findTunnel("existing-encap"), nullptr);
}

TEST_F(CmdConfigTunnelSrv6TestFixture, deleteIsIdempotentAndChecksDirection) {
  CmdDeleteTunnelSrv6Encap command;
  EXPECT_THAT(
      command.queryClient(
          hostInfo_,
          srv6_tunnel_delete_utils::TunnelSrv6DeleteArgs({"missing"})),
      HasSubstr("does not exist"));
  EXPECT_THROW(
      command.queryClient(
          hostInfo_,
          srv6_tunnel_delete_utils::TunnelSrv6DeleteArgs({"existing-decap"})),
      std::invalid_argument);
  EXPECT_THROW(
      srv6_tunnel_delete_utils::TunnelSrv6DeleteArgs(
          {"existing-encap", "source"}),
      std::invalid_argument);
  EXPECT_THROW(
      srv6_tunnel_delete_utils::TunnelSrv6DeleteArgs({"ttl-mode"}),
      std::invalid_argument);
  EXPECT_THROW(
      srv6_tunnel_delete_utils::TunnelSrv6DeleteArgs({"source"}),
      std::invalid_argument);
}

} // namespace facebook::fboss
