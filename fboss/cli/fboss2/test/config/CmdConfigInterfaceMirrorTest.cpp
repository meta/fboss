// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string>

#include "fboss/cli/fboss2/commands/config/interface/mirror/CmdConfigInterfaceMirror.h"
#include "fboss/cli/fboss2/commands/config/interface/sflow/CmdConfigInterfaceSflow.h"
#include "fboss/cli/fboss2/commands/delete/interface/mirror/CmdDeleteInterfaceMirror.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigInterfaceMirrorTestFixture : public CmdConfigTestBase {
 public:
  // eth1/1/1 already samples (sampleDest MIRROR) through sflowcol;
  // eth1/3/1 has sampleDest MIRROR but no ingress mirror yet; eth1/2/1 has
  // no sampling, so any mirror can be bound to it.
  // span0's egress port is eth1/4/1 (by name) and span1's is eth1/5/1 (by
  // logicalID 5); the agent rejects binding a port to a mirror whose egress
  // port it is.
  CmdConfigInterfaceMirrorTestFixture()
      : CmdConfigTestBase(
            "fboss_interface_mirror_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 1,
        "ingressMirror": "sflowcol",
        "sampleDest": 1,
        "sFlowIngressRate": 90000,
        "sFlowEgressRate": 0
      },
      {
        "logicalID": 2,
        "name": "eth1/2/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 1,
        "sFlowIngressRate": 0,
        "sFlowEgressRate": 0
      },
      {
        "logicalID": 3,
        "name": "eth1/3/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 1,
        "sampleDest": 1,
        "sFlowIngressRate": 90000,
        "sFlowEgressRate": 0
      },
      {
        "logicalID": 4,
        "name": "eth1/4/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 1,
        "sFlowIngressRate": 0,
        "sFlowEgressRate": 0
      },
      {
        "logicalID": 5,
        "name": "eth1/5/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 1,
        "sFlowIngressRate": 0,
        "sFlowEgressRate": 0
      }
    ],
    "mirrors": [
      {
        "name": "sflowcol",
        "destination": {
          "tunnel": {
            "sflowTunnel": {"ip": "2001:db8::2", "udpSrcPort": 6343, "udpDstPort": 6343}
          }
        }
      },
      {
        "name": "span0",
        "destination": {"egressPort": {"name": "eth1/4/1"}}
      },
      {
        "name": "span1",
        "destination": {"egressPort": {"logicalID": 5}}
      }
    ],
    "vlans": [
      {"id": 1, "name": "vlan1", "routable": true, "intfID": 1}
    ],
    "interfaces": [
      {"intfID": 1, "vlanID": 1, "routerID": 0, "type": 1, "mtu": 9412, "name": "vlan1"}
    ]
  }
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession(
        "config interface mirror eth1/2/1", "ingress span0");
  }

  static const cfg::Port& portOf(const std::string& portName) {
    auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
    for (const auto& port : *swConfig.ports()) {
      if (*port.name() == portName) {
        return port;
      }
    }
    throw std::runtime_error("port not found: " + portName);
  }

  static std::optional<std::string> ingressOf(const std::string& portName) {
    return portOf(portName).ingressMirror().to_optional();
  }

  static std::optional<std::string> egressOf(const std::string& portName) {
    return portOf(portName).egressMirror().to_optional();
  }
};

// MirrorAttrArgs / MirrorDeleteAttrArgs parsing

TEST_F(CmdConfigInterfaceMirrorTestFixture, configArgsEmptyThrows) {
  EXPECT_THROW(MirrorAttrArgs({}), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, configArgsOddCountThrows) {
  EXPECT_THROW(MirrorAttrArgs({"ingress"}), std::invalid_argument);
  EXPECT_THROW(
      MirrorAttrArgs({"ingress", "span0", "egress"}), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, configArgsUnknownDirectionThrows) {
  EXPECT_THROW(MirrorAttrArgs({"both", "span0"}), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, configArgsEmptyNameThrows) {
  EXPECT_THROW(MirrorAttrArgs({"ingress", ""}), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, configArgsBothDirectionsValid) {
  auto args = MirrorAttrArgs({"INGRESS", "span0", "egress", "span0"});
  ASSERT_EQ(args.getAttributes().size(), 2u);
  EXPECT_EQ(args.getAttributes()[0].first, "ingress");
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, deleteArgsEmptyThrows) {
  EXPECT_THROW(MirrorDeleteAttrArgs({}), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, deleteArgsUnknownThrows) {
  EXPECT_THROW(MirrorDeleteAttrArgs({"span0"}), std::invalid_argument);
}

// config queryClient

TEST_F(CmdConfigInterfaceMirrorTestFixture, setIngress) {
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/2/1"});
  auto result = cmd.queryClient(
      localhost(), interfaces, MirrorAttrArgs({"ingress", "span0"}));

  EXPECT_THAT(result, HasSubstr("eth1/2/1"));
  EXPECT_THAT(result, HasSubstr("ingress=span0"));
  EXPECT_EQ(ingressOf("eth1/2/1"), "span0");
  EXPECT_EQ(egressOf("eth1/2/1"), std::nullopt);
  // Other ports are untouched.
  EXPECT_EQ(ingressOf("eth1/1/1"), "sflowcol");
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, setBothDirections) {
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/2/1"});
  cmd.queryClient(
      localhost(),
      interfaces,
      MirrorAttrArgs({"ingress", "span0", "egress", "sflowcol"}));

  EXPECT_EQ(ingressOf("eth1/2/1"), "span0");
  EXPECT_EQ(egressOf("eth1/2/1"), "sflowcol");
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, lastRepeatedDirectionWins) {
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/2/1"});
  cmd.queryClient(
      localhost(),
      interfaces,
      MirrorAttrArgs({"egress", "span0", "egress", "sflowcol"}));

  EXPECT_EQ(egressOf("eth1/2/1"), "sflowcol");
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, multipleInterfaces) {
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/1/1", "eth1/2/1"});
  auto result = cmd.queryClient(
      localhost(), interfaces, MirrorAttrArgs({"egress", "span0"}));

  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));
  EXPECT_EQ(egressOf("eth1/1/1"), "span0");
  EXPECT_EQ(egressOf("eth1/2/1"), "span0");
}

TEST_F(
    CmdConfigInterfaceMirrorTestFixture,
    unknownMirrorThrowsAndLeavesConfig) {
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/2/1"});
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          interfaces,
          MirrorAttrArgs({"ingress", "span0", "egress", "nosuch"})),
      std::invalid_argument);

  // Validation runs before any port is touched.
  EXPECT_EQ(ingressOf("eth1/2/1"), std::nullopt);
  EXPECT_EQ(egressOf("eth1/2/1"), std::nullopt);
}

// A mirror's egress port is where its mirrored copies go. The agent rejects a
// config where that port is also bound to the same mirror, so the CLI must
// refuse the binding before staging it, for ingress and for egress, whether the
// mirror names its egress port by name (span0 -> eth1/4/1) or by logicalID
// (span1 -> logicalID 5 = eth1/5/1).

TEST_F(CmdConfigInterfaceMirrorTestFixture, destinationPortIngressThrows) {
  // eth1/4/1 is span0's egress port, so it cannot take span0 as ingress. The
  // error names both the port and the mirror.
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/4/1"});
  try {
    cmd.queryClient(
        localhost(), interfaces, MirrorAttrArgs({"ingress", "span0"}));
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("eth1/4/1"));
    EXPECT_THAT(e.what(), HasSubstr("egress port of mirror 'span0'"));
  }
  EXPECT_EQ(ingressOf("eth1/4/1"), std::nullopt);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, destinationPortEgressThrows) {
  // Same rule for an egress binding: eth1/4/1 cannot take span0 as egress.
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/4/1"});
  EXPECT_THROW(
      cmd.queryClient(
          localhost(), interfaces, MirrorAttrArgs({"egress", "span0"})),
      std::invalid_argument);
  EXPECT_EQ(egressOf("eth1/4/1"), std::nullopt);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, destinationByLogicalIdThrows) {
  // span1 names its egress port by logicalID 5, not by name. Port eth1/5/1 has
  // logicalID 5, so it is span1's egress port and cannot take span1.
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/5/1"});
  EXPECT_THROW(
      cmd.queryClient(
          localhost(), interfaces, MirrorAttrArgs({"ingress", "span1"})),
      std::invalid_argument);
  EXPECT_EQ(ingressOf("eth1/5/1"), std::nullopt);
}

TEST_F(
    CmdConfigInterfaceMirrorTestFixture,
    destinationInMultiInterfaceCallThrows) {
  // eth1/2/1 could take span0 on its own, but eth1/4/1 is span0's egress port.
  // One bad port fails the whole call, so neither port is updated.
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/2/1", "eth1/4/1"});
  EXPECT_THROW(
      cmd.queryClient(
          localhost(), interfaces, MirrorAttrArgs({"ingress", "span0"})),
      std::invalid_argument);
  EXPECT_EQ(ingressOf("eth1/2/1"), std::nullopt);
  EXPECT_EQ(ingressOf("eth1/4/1"), std::nullopt);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, destinationPortOtherMirrorAllowed) {
  // The check is per mirror: eth1/4/1 is span0's egress port only, so it can
  // still be bound to span1 and sflowcol.
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/4/1"});
  cmd.queryClient(
      localhost(),
      interfaces,
      MirrorAttrArgs({"ingress", "span1", "egress", "sflowcol"}));

  EXPECT_EQ(ingressOf("eth1/4/1"), "span1");
  EXPECT_EQ(egressOf("eth1/4/1"), "sflowcol");
}

// The agent samples every sampleDest-mirror port through a single mirror.

TEST_F(CmdConfigInterfaceMirrorTestFixture, secondSamplingMirrorThrows) {
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/3/1"});
  try {
    cmd.queryClient(
        localhost(), interfaces, MirrorAttrArgs({"ingress", "span0"}));
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("same ingress mirror"));
    EXPECT_THAT(e.what(), HasSubstr("sflowcol"));
    EXPECT_THAT(e.what(), HasSubstr("span0"));
  }
  EXPECT_EQ(ingressOf("eth1/3/1"), std::nullopt);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, sharedSamplingMirrorAllowed) {
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/3/1"});
  cmd.queryClient(
      localhost(), interfaces, MirrorAttrArgs({"ingress", "sflowcol"}));

  EXPECT_EQ(ingressOf("eth1/3/1"), "sflowcol");
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, rebindAllSamplingPortsTogether) {
  // Moving every sampling port to another mirror in one call leaves a single
  // sampling mirror, so it is allowed even though sflowcol is in use today.
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/1/1", "eth1/3/1"});
  cmd.queryClient(
      localhost(), interfaces, MirrorAttrArgs({"ingress", "span0"}));

  EXPECT_EQ(ingressOf("eth1/1/1"), "span0");
  EXPECT_EQ(ingressOf("eth1/3/1"), "span0");
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, egressIgnoresSamplingRule) {
  // Only the ingress mirror carries sampled packets.
  auto cmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/3/1"});
  cmd.queryClient(localhost(), interfaces, MirrorAttrArgs({"egress", "span0"}));

  EXPECT_EQ(egressOf("eth1/3/1"), "span0");
}

// `config interface <port> sflow sample-dest mirror` enforces the same
// single-sampling-mirror rule. Without it, a user could bind a port to a second
// mirror first (allowed, since the port is not sampling yet) and turn on mirror
// sampling second, and the agent would reject the config only at commit.

TEST_F(CmdConfigInterfaceMirrorTestFixture, sflowMirrorDestSecondMirrorThrows) {
  // eth1/1/1 already samples through sflowcol. Binding eth1/2/1 to span0 is
  // fine while it does not sample, but turning on mirror sampling on it would
  // leave two sampling mirrors, so the sflow command refuses and leaves
  // sampleDest unset.
  utils::InterfaceList interfaces({"eth1/2/1"});
  CmdConfigInterfaceMirror().queryClient(
      localhost(), interfaces, MirrorAttrArgs({"ingress", "span0"}));

  auto cmd = CmdConfigInterfaceSflow();
  try {
    cmd.queryClient(
        localhost(), interfaces, SflowAttrArgs({"sample-dest", "mirror"}));
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("same ingress mirror"));
    EXPECT_THAT(e.what(), HasSubstr("sflowcol"));
    EXPECT_THAT(e.what(), HasSubstr("span0"));
  }
  EXPECT_EQ(portOf("eth1/2/1").sampleDest().to_optional(), std::nullopt);
}

TEST_F(
    CmdConfigInterfaceMirrorTestFixture,
    sflowMirrorDestSharedMirrorAllowed) {
  // eth1/2/1 is bound to sflowcol, the mirror eth1/1/1 already samples
  // through, so turning on mirror sampling still leaves a single mirror.
  utils::InterfaceList interfaces({"eth1/2/1"});
  CmdConfigInterfaceMirror().queryClient(
      localhost(), interfaces, MirrorAttrArgs({"ingress", "sflowcol"}));

  CmdConfigInterfaceSflow().queryClient(
      localhost(), interfaces, SflowAttrArgs({"sample-dest", "mirror"}));

  EXPECT_EQ(
      portOf("eth1/2/1").sampleDest().to_optional(),
      cfg::SampleDestination::MIRROR);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, sflowMirrorDestUnboundPortAllowed) {
  // eth1/2/1 has no ingress mirror. The agent only counts the ingress mirrors
  // of sampling ports, so this port adds no mirror and the call is allowed.
  utils::InterfaceList interfaces({"eth1/2/1"});
  CmdConfigInterfaceSflow().queryClient(
      localhost(), interfaces, SflowAttrArgs({"sample-dest", "mirror"}));

  EXPECT_EQ(
      portOf("eth1/2/1").sampleDest().to_optional(),
      cfg::SampleDestination::MIRROR);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, sflowCpuDestIgnoresSamplingRule) {
  // Same setup as sflowMirrorDestSecondMirrorThrows, but sample-dest cpu sends
  // samples to the CPU, not through a mirror, so the rule does not apply.
  utils::InterfaceList interfaces({"eth1/2/1"});
  CmdConfigInterfaceMirror().queryClient(
      localhost(), interfaces, MirrorAttrArgs({"ingress", "span0"}));

  CmdConfigInterfaceSflow().queryClient(
      localhost(), interfaces, SflowAttrArgs({"sample-dest", "cpu"}));

  EXPECT_EQ(
      portOf("eth1/2/1").sampleDest().to_optional(),
      cfg::SampleDestination::CPU);
}

// delete queryClient

TEST_F(CmdConfigInterfaceMirrorTestFixture, deleteIngress) {
  auto cmd = CmdDeleteInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/1/1"});
  auto result = cmd.queryClient(
      localhost(), interfaces, MirrorDeleteAttrArgs({"ingress"}));

  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_EQ(ingressOf("eth1/1/1"), std::nullopt);
  // Sampling settings are left alone.
  EXPECT_EQ(
      portOf("eth1/1/1").sampleDest().to_optional(),
      cfg::SampleDestination::MIRROR);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, deleteBothDirections) {
  auto setCmd = CmdConfigInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/2/1"});
  setCmd.queryClient(
      localhost(),
      interfaces,
      MirrorAttrArgs({"ingress", "span0", "egress", "span0"}));

  auto cmd = CmdDeleteInterfaceMirror();
  cmd.queryClient(
      localhost(), interfaces, MirrorDeleteAttrArgs({"ingress", "egress"}));

  EXPECT_EQ(ingressOf("eth1/2/1"), std::nullopt);
  EXPECT_EQ(egressOf("eth1/2/1"), std::nullopt);
}

TEST_F(CmdConfigInterfaceMirrorTestFixture, deleteUnsetIsNoop) {
  auto cmd = CmdDeleteInterfaceMirror();
  utils::InterfaceList interfaces({"eth1/2/1"});
  EXPECT_NO_THROW(cmd.queryClient(
      localhost(), interfaces, MirrorDeleteAttrArgs({"egress"})));
  EXPECT_EQ(egressOf("eth1/2/1"), std::nullopt);
}

} // namespace facebook::fboss
