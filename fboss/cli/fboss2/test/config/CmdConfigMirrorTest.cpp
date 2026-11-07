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

#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/commands/config/mirror/CmdConfigMirror.h"
#include "fboss/cli/fboss2/commands/delete/mirror/CmdDeleteMirror.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {

cfg::TrafficPolicyConfig makeMirrorTrafficPolicy(
    const std::string& mirrorName) {
  cfg::MatchAction action;
  action.egressMirror() = mirrorName;
  cfg::MatchToAction matchToAction;
  matchToAction.matcher() = "mirror_acl";
  matchToAction.action() = std::move(action);
  cfg::TrafficPolicyConfig policy;
  policy.matchToAction()->push_back(std::move(matchToAction));
  return policy;
}

} // namespace

class CmdConfigMirrorTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigMirrorTestFixture()
      : CmdConfigTestBase(
            "fboss_mirror_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 4,
        "state": 2,
        "speed": 100000
      },
      {
        "logicalID": 5,
        "name": "eth1/1/5",
        "state": 2,
        "speed": 100000
      }
    ],
    "mirrors": [
      {
        "name": "existing_span",
        "destination": {"egressPort": {"name": "eth1/1/5"}},
        "dscp": 0,
        "truncate": false
      },
      {
        "name": "existing_sflow",
        "destination": {
          "tunnel": {
            "sflowTunnel": {
              "ip": "2001:db8::2",
              "udpSrcPort": 12355,
              "udpDstPort": 6343
            },
            "srcIp": "2001:db8::1"
          }
        },
        "dscp": 0,
        "truncate": false
      }
    ]
  }
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

 protected:
  const cfg::Mirror* findMirror(const std::string& name) {
    const auto& mirrors =
        *ConfigSession::getInstance().getAgentConfig().sw()->mirrors();
    auto it = std::find_if(
        mirrors.begin(), mirrors.end(), [&](const cfg::Mirror& mirror) {
          return *mirror.name() == name;
        });
    return it == mirrors.end() ? nullptr : &*it;
  }
};

TEST_F(CmdConfigMirrorTestFixture, validatesArguments) {
  EXPECT_NO_THROW(MirrorConfigArg(
      {"mirror0", "egress-port", "eth1/1/5", "truncate", "TRUE"}));
  EXPECT_NO_THROW(MirrorConfigArg(
      {"mirror0",
       "destination-ip",
       "2000:0:4:1::2",
       "source-ip",
       "2000:0:4:1::1",
       "udp-src-port",
       "12355",
       "udp-dst-port",
       "6343",
       "dscp",
       "63"}));

  EXPECT_THROW(MirrorConfigArg({}), std::invalid_argument);
  EXPECT_THROW(MirrorConfigArg({"mirror0"}), std::invalid_argument);
  EXPECT_THROW(
      MirrorConfigArg({"mirror0", "destination-ip"}), std::invalid_argument);
  EXPECT_THROW(
      MirrorConfigArg({"mirror0", "unknown", "value"}), std::invalid_argument);
  EXPECT_THROW(
      MirrorConfigArg({"mirror0", "dscp", "64"}), std::invalid_argument);
  EXPECT_THROW(
      MirrorConfigArg({"mirror0", "udp-src-port", "0"}), std::invalid_argument);
  EXPECT_THROW(
      MirrorConfigArg({"mirror0", "truncate", "yes"}), std::invalid_argument);
  EXPECT_THROW(
      MirrorConfigArg({"mirror0", "source-ip", "not-an-ip"}),
      std::invalid_argument);
}

TEST_F(CmdConfigMirrorTestFixture, createsSpanMirrorFromTicketFields) {
  CmdConfigMirror cmd;
  MirrorConfigArg args(
      {"capture_srv6_local_n1",
       "egress-port",
       "eth1/1/5",
       "dscp",
       "0",
       "truncate",
       "false"});

  EXPECT_THAT(
      cmd.queryClient(HostInfo("testhost"), args), HasSubstr("created"));
  const auto* mirror = findMirror("capture_srv6_local_n1");
  ASSERT_NE(mirror, nullptr);
  ASSERT_TRUE(mirror->destination()->egressPort().has_value());
  EXPECT_EQ(*mirror->destination()->egressPort()->name(), "eth1/1/5");
  EXPECT_FALSE(mirror->destination()->tunnel().has_value());
  EXPECT_EQ(*mirror->dscp(), 0);
  EXPECT_FALSE(*mirror->truncate());
}

TEST_F(CmdConfigMirrorTestFixture, createsSpanMirrorByLogicalId) {
  CmdConfigMirror cmd;
  MirrorConfigArg args(
      {"span_by_logical_id",
       "egress-port",
       "4",
       "dscp",
       "0",
       "truncate",
       "false"});

  EXPECT_THAT(
      cmd.queryClient(HostInfo("testhost"), args), HasSubstr("created"));
  const auto* mirror = findMirror("span_by_logical_id");
  ASSERT_NE(mirror, nullptr);
  ASSERT_TRUE(mirror->destination()->egressPort().has_value());
  const auto& egressPort = *mirror->destination()->egressPort();
  EXPECT_EQ(egressPort.getType(), cfg::MirrorEgressPort::Type::logicalID);
  EXPECT_EQ(egressPort.get_logicalID(), 4);
}

TEST_F(CmdConfigMirrorTestFixture, createsSflowMirror) {
  CmdConfigMirror cmd;
  MirrorConfigArg args(
      {"sflow_mirror",
       "destination-ip",
       "2000:0:4:1::2",
       "udp-src-port",
       "12355",
       "udp-dst-port",
       "6343",
       "source-ip",
       "2000:0:4:1::1",
       "dscp",
       "63",
       "truncate",
       "true"});

  EXPECT_THAT(
      cmd.queryClient(HostInfo("testhost"), args), HasSubstr("created"));
  const auto* mirror = findMirror("sflow_mirror");
  ASSERT_NE(mirror, nullptr);
  const auto& tunnel = *mirror->destination()->tunnel();
  const auto& sflow = *tunnel.sflowTunnel();
  EXPECT_EQ(*sflow.ip(), "2000:0:4:1::2");
  EXPECT_EQ(*sflow.udpSrcPort(), 12355);
  EXPECT_EQ(*sflow.udpDstPort(), 6343);
  EXPECT_EQ(*tunnel.srcIp(), "2000:0:4:1::1");
  EXPECT_EQ(*mirror->dscp(), 63);
  EXPECT_TRUE(*mirror->truncate());
}

TEST_F(CmdConfigMirrorTestFixture, updatesExistingMirror) {
  CmdConfigMirror cmd;
  MirrorConfigArg args({"existing_sflow", "dscp", "24", "truncate", "true"});

  EXPECT_THAT(
      cmd.queryClient(HostInfo("testhost"), args), HasSubstr("updated"));
  const auto* mirror = findMirror("existing_sflow");
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(*mirror->dscp(), 24);
  EXPECT_TRUE(*mirror->truncate());
  EXPECT_EQ(
      *mirror->destination()->tunnel()->sflowTunnel()->ip(), "2001:db8::2");
}

TEST_F(CmdConfigMirrorTestFixture, rejectsIncompleteAndInvalidDestinations) {
  CmdConfigMirror cmd;
  EXPECT_THROW(
      cmd.queryClient(
          HostInfo("testhost"),
          MirrorConfigArg({"incomplete", "destination-ip", "2001:db8::2"})),
      std::invalid_argument);
  EXPECT_THROW(
      cmd.queryClient(
          HostInfo("testhost"),
          MirrorConfigArg({"bad_port", "egress-port", "eth9/9/9"})),
      std::invalid_argument);
  EXPECT_THROW(
      cmd.queryClient(
          HostInfo("testhost"),
          MirrorConfigArg({"bad_port_id", "egress-port", "9999"})),
      std::invalid_argument);
  EXPECT_THROW(
      cmd.queryClient(
          HostInfo("testhost"),
          MirrorConfigArg({"existing_span", "destination-ip", "2001:db8::2"})),
      std::invalid_argument);
}

TEST_F(CmdConfigMirrorTestFixture, rejectsEgressPortReferencingSameMirror) {
  auto& port = ConfigSession::getInstance().getAgentConfig().sw()->ports()[1];
  port.ingressMirror() = "existing_sflow";
  const auto original = *findMirror("existing_sflow");

  CmdConfigMirror cmd;
  for (const auto* portValue : {"eth1/1/5", "5"}) {
    EXPECT_THROW(
        cmd.queryClient(
            HostInfo("testhost"),
            MirrorConfigArg({"existing_sflow", "egress-port", portValue})),
        std::invalid_argument);
    ASSERT_NE(findMirror("existing_sflow"), nullptr);
    EXPECT_EQ(*findMirror("existing_sflow"), original);
  }
}

TEST_F(CmdConfigMirrorTestFixture, rejectsMixedAddressFamilies) {
  CmdConfigMirror cmd;
  EXPECT_THROW(
      cmd.queryClient(
          HostInfo("testhost"),
          MirrorConfigArg(
              {"mixed_create",
               "destination-ip",
               "192.0.2.2",
               "source-ip",
               "2001:db8::1",
               "udp-src-port",
               "12355",
               "udp-dst-port",
               "6343"})),
      std::invalid_argument);
  EXPECT_THROW(
      cmd.queryClient(
          HostInfo("testhost"),
          MirrorConfigArg({"existing_sflow", "destination-ip", "192.0.2.2"})),
      std::invalid_argument);
}

TEST_F(CmdConfigMirrorTestFixture, deletesUnreferencedMirror) {
  CmdDeleteMirror cmd;
  EXPECT_THAT(
      cmd.queryClient(HostInfo("testhost"), MirrorNameArg({"existing_span"})),
      HasSubstr("deleted"));
  EXPECT_EQ(findMirror("existing_span"), nullptr);
}

TEST_F(CmdConfigMirrorTestFixture, refusesToDeleteReferencedMirror) {
  auto& port = ConfigSession::getInstance().getAgentConfig().sw()->ports()[0];
  port.ingressMirror() = "existing_span";

  CmdDeleteMirror cmd;
  try {
    cmd.queryClient(HostInfo("testhost"), MirrorNameArg({"existing_span"}));
    FAIL() << "Expected deletion of a referenced mirror to fail";
  } catch (const FbossError& error) {
    EXPECT_THAT(error.what(), HasSubstr("port with logical ID 4"));
  }
  EXPECT_NE(findMirror("existing_span"), nullptr);
}

TEST_F(
    CmdConfigMirrorTestFixture,
    refusesToDeleteMirrorReferencedByDataPlanePolicy) {
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  swConfig.dataPlaneTrafficPolicy() = makeMirrorTrafficPolicy("existing_span");

  CmdDeleteMirror cmd;
  EXPECT_THROW(
      cmd.queryClient(HostInfo("testhost"), MirrorNameArg({"existing_span"})),
      FbossError);
  EXPECT_NE(findMirror("existing_span"), nullptr);
}

TEST_F(CmdConfigMirrorTestFixture, refusesToDeleteMirrorReferencedByCpuPolicy) {
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  cfg::CPUTrafficPolicyConfig cpuPolicy;
  cpuPolicy.trafficPolicy() = makeMirrorTrafficPolicy("existing_span");
  swConfig.cpuTrafficPolicy() = std::move(cpuPolicy);

  CmdDeleteMirror cmd;
  EXPECT_THROW(
      cmd.queryClient(HostInfo("testhost"), MirrorNameArg({"existing_span"})),
      FbossError);
  EXPECT_NE(findMirror("existing_span"), nullptr);
}

TEST_F(
    CmdConfigMirrorTestFixture,
    refusesToDeleteMirrorReferencedByDeprecatedGlobalEgressPolicy) {
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  swConfig.globalEgressTrafficPolicy_DEPRECATED() =
      makeMirrorTrafficPolicy("existing_span");

  CmdDeleteMirror cmd;
  try {
    cmd.queryClient(HostInfo("testhost"), MirrorNameArg({"existing_span"}));
    FAIL() << "Expected deletion of a referenced mirror to fail";
  } catch (const FbossError& error) {
    EXPECT_THAT(
        error.what(), HasSubstr("globalEgressTrafficPolicy_DEPRECATED"));
  }
  EXPECT_NE(findMirror("existing_span"), nullptr);
}

} // namespace facebook::fboss
