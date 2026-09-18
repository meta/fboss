// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/port_channel/CmdConfigPortChannel.h"
#include "fboss/cli/fboss2/commands/config/port_channel/PortChannelUtils.h"
#include "fboss/cli/fboss2/commands/config/port_channel/member/CmdConfigPortChannelMember.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigPortChannelTestFixture : public CmdConfigTestBase {
 public:
  // Seed mirrors the aggregatePorts shape of production configs (see
  // fboss/oss/link_test_configs/meru800bia_c0.materialized_JSON): members
  // carry priority 32768, rate FAST (1), activity ACTIVE (1),
  // holdTimerMultiplier 3. eth1/1/1..eth1/3/1 share VLAN 2001 (members of
  // one port-channel must), eth1/4/1 has no VLAN (routed) and eth1/5/1 is
  // alone in VLAN 2005 for the mismatch cases.
  CmdConfigPortChannelTestFixture()
      : CmdConfigTestBase(
            "fboss_port_channel_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 2001
      },
      {
        "logicalID": 2,
        "name": "eth1/2/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 2001
      },
      {
        "logicalID": 3,
        "name": "eth1/3/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 2001
      },
      {
        "logicalID": 4,
        "name": "eth1/4/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 0
      },
      {
        "logicalID": 5,
        "name": "eth1/5/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 2005
      }
    ],
    "vlanPorts": [
      {"vlanID": 2001, "logicalPort": 1, "spanningTreeState": 2, "emitTags": false},
      {"vlanID": 2001, "logicalPort": 2, "spanningTreeState": 2, "emitTags": false},
      {"vlanID": 2001, "logicalPort": 3, "spanningTreeState": 2, "emitTags": false},
      {"vlanID": 2005, "logicalPort": 5, "spanningTreeState": 2, "emitTags": false}
    ],
    "vlans": [
      {"id": 2001, "name": "vlan2001", "routable": true, "intfID": 2001},
      {"id": 2005, "name": "vlan2005", "routable": true, "intfID": 2005}
    ],
    "interfaces": [
      {"intfID": 2001, "vlanID": 2001, "routerID": 0, "type": 1, "mtu": 9412},
      {"intfID": 2005, "vlanID": 2005, "routerID": 0, "type": 1, "mtu": 9412}
    ],
    "aggregatePorts": [
      {
        "key": 100,
        "name": "port-channel100",
        "description": "existing lag",
        "memberPorts": [
          {
            "memberPortID": 1,
            "priority": 32768,
            "rate": 1,
            "activity": 1,
            "holdTimerMultiplier": 3
          }
        ],
        "minimumCapacity": {"linkCount": 1}
      }
    ]
  }
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession(
        "config port-channel port-channel100", "description lag");
  }

  static cfg::AggregatePort* portChannel(const std::string& name) {
    auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
    return findPortChannel(swConfig, name);
  }
};

// ---- PortChannelConfigArgs validation ----

TEST_F(CmdConfigPortChannelTestFixture, argsValidBare) {
  PortChannelConfigArgs args({"port-channel100"});
  EXPECT_EQ(args.getName(), "port-channel100");
  EXPECT_FALSE(args.hasAttributes());
}

TEST_F(CmdConfigPortChannelTestFixture, argsValidAttrs) {
  PortChannelConfigArgs args(
      {"port-channel100", "description", "uplink", "minimum-links", "2"});
  EXPECT_EQ(args.getName(), "port-channel100");
  ASSERT_EQ(args.getAttributes().size(), 2);
  EXPECT_EQ(args.getAttributes()[0].first, "description");
  EXPECT_EQ(args.getAttributes()[0].second, "uplink");
  EXPECT_EQ(args.getAttributes()[1].first, "minimum-links");
  EXPECT_EQ(args.getAttributes()[1].second, "2");
}

TEST_F(CmdConfigPortChannelTestFixture, argsEmptyInvalid) {
  EXPECT_THROW(PortChannelConfigArgs({}), std::invalid_argument);
}

TEST_F(CmdConfigPortChannelTestFixture, argsMultipleNamesInvalid) {
  EXPECT_THROW(
      PortChannelConfigArgs({"port-channel100", "port-channel101"}),
      std::invalid_argument);
}

TEST_F(CmdConfigPortChannelTestFixture, argsAnyNameAccepted) {
  // The name shape is only enforced on creation: an existing aggregate port
  // may carry any name (e.g. one written by another tool).
  EXPECT_EQ(PortChannelConfigArgs({"agg"}).getName(), "agg");
  EXPECT_EQ(
      PortChannelConfigArgs({"port-channel0"}).getName(), "port-channel0");
}

TEST_F(CmdConfigPortChannelTestFixture, argsUnknownAttrInvalid) {
  EXPECT_THROW(
      PortChannelConfigArgs({"port-channel100", "speed", "100"}),
      std::invalid_argument);
}

TEST_F(CmdConfigPortChannelTestFixture, argsMissingValueInvalid) {
  EXPECT_THROW(
      PortChannelConfigArgs({"port-channel100", "description"}),
      std::invalid_argument);
}

// ---- PortChannelMemberArgs validation ----

TEST_F(CmdConfigPortChannelTestFixture, memberArgsAdd) {
  PortChannelMemberArgs args({"add", "eth1/2/1", "eth1/3/1"});
  EXPECT_EQ(args.getOp(), PortChannelMemberArgs::Op::ADD);
  EXPECT_THAT(args.getMemberNames(), ElementsAre("eth1/2/1", "eth1/3/1"));
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsRemove) {
  PortChannelMemberArgs args({"remove", "eth1/2/1"});
  EXPECT_EQ(args.getOp(), PortChannelMemberArgs::Op::REMOVE);
  EXPECT_THAT(args.getMemberNames(), ElementsAre("eth1/2/1"));
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsAddWithoutNamesInvalid) {
  EXPECT_THROW(PortChannelMemberArgs({"add"}), std::invalid_argument);
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsPriority) {
  PortChannelMemberArgs args({"eth1/1/1", "priority", "100"});
  EXPECT_EQ(args.getOp(), PortChannelMemberArgs::Op::SET_ATTR);
  EXPECT_EQ(args.getMemberName(), "eth1/1/1");
  EXPECT_EQ(args.getAttr(), "priority");
  EXPECT_EQ(args.getValue(), "100");
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsPriorityOutOfRangeInvalid) {
  EXPECT_THROW(
      PortChannelMemberArgs({"eth1/1/1", "priority", "65536"}),
      std::invalid_argument);
  EXPECT_THROW(
      PortChannelMemberArgs({"eth1/1/1", "priority", "-1"}),
      std::invalid_argument);
  EXPECT_THROW(
      PortChannelMemberArgs({"eth1/1/1", "priority", "abc"}),
      std::invalid_argument);
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsLacpRate) {
  PortChannelMemberArgs args({"eth1/1/1", "lacp", "rate", "slow"});
  EXPECT_EQ(args.getOp(), PortChannelMemberArgs::Op::SET_ATTR);
  EXPECT_EQ(args.getAttr(), "lacp rate");
  EXPECT_EQ(args.getValue(), "slow");
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsCaseInsensitive) {
  // Attribute and value keywords match case-insensitively.
  PortChannelMemberArgs args({"eth1/1/1", "LACP", "Rate", "SLOW"});
  EXPECT_EQ(args.getOp(), PortChannelMemberArgs::Op::SET_ATTR);
  EXPECT_EQ(args.getAttr(), "lacp rate");
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsLacpRateInvalid) {
  EXPECT_THROW(
      PortChannelMemberArgs({"eth1/1/1", "lacp", "rate", "medium"}),
      std::invalid_argument);
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsLacpMode) {
  PortChannelMemberArgs args({"eth1/1/1", "lacp", "mode", "passive"});
  EXPECT_EQ(args.getOp(), PortChannelMemberArgs::Op::SET_ATTR);
  EXPECT_EQ(args.getAttr(), "lacp mode");
  EXPECT_EQ(args.getValue(), "passive");
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsLacpHoldTimer) {
  PortChannelMemberArgs args({"eth1/1/1", "lacp", "hold-timer", "5"});
  EXPECT_EQ(args.getOp(), PortChannelMemberArgs::Op::SET_ATTR);
  EXPECT_EQ(args.getAttr(), "lacp hold-timer");
  EXPECT_EQ(args.getValue(), "5");
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsLacpHoldTimerZeroInvalid) {
  EXPECT_THROW(
      PortChannelMemberArgs({"eth1/1/1", "lacp", "hold-timer", "0"}),
      std::invalid_argument);
}

TEST_F(CmdConfigPortChannelTestFixture, memberArgsUnknownAttrInvalid) {
  EXPECT_THROW(
      PortChannelMemberArgs({"eth1/1/1", "speed", "100"}),
      std::invalid_argument);
  EXPECT_THROW(
      PortChannelMemberArgs({"eth1/1/1", "lacp", "timeout", "3"}),
      std::invalid_argument);
}

// ---- CmdConfigPortChannel::queryClient ----

TEST_F(CmdConfigPortChannelTestFixture, queryClientBareExisting) {
  auto cmd = CmdConfigPortChannel();
  auto result =
      cmd.queryClient(localhost(), PortChannelConfigArgs({"port-channel100"}));
  EXPECT_THAT(result, HasSubstr("already exists"));
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientBareMissingThrows) {
  auto cmd = CmdConfigPortChannel();
  try {
    cmd.queryClient(localhost(), PortChannelConfigArgs({"port-channel200"}));
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(std::string(e.what()), HasSubstr("member add"));
  }
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientSetsDescription) {
  auto cmd = CmdConfigPortChannel();
  auto result = cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel100", "description", "uplink"}));
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  EXPECT_EQ(*pc->description(), "uplink");
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientSetsMinimumLinks) {
  auto cmd = CmdConfigPortChannel();
  auto result = cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel100", "minimum-links", "1"}));
  // Fixture already has linkCount 1, so this is a no-op.
  EXPECT_THAT(result, HasSubstr("No changes"));

  result = cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel100", "minimum-links", "2"}));
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  ASSERT_EQ(
      pc->minimumCapacity()->getType(), cfg::MinimumCapacity::Type::linkCount);
  EXPECT_EQ(pc->minimumCapacity()->get_linkCount(), 2);
}

TEST_F(CmdConfigPortChannelTestFixture, argsInvalidAttrValueThrowsBeforeApply) {
  // Values are validated at parse time, so a bad trailing value rejects the
  // whole command before any earlier attribute is applied.
  EXPECT_THROW(
      PortChannelConfigArgs(
          {"port-channel100", "description", "uplink", "minimum-links", "999"}),
      std::invalid_argument);
  EXPECT_THROW(
      PortChannelConfigArgs({"port-channel100", "minimum-links", "abc"}),
      std::invalid_argument);
  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  EXPECT_EQ(*pc->description(), "existing lag");
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientMinimumLinksOutOfRange) {
  auto cmd = CmdConfigPortChannel();
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          PortChannelConfigArgs({"port-channel100", "minimum-links", "0"})),
      std::invalid_argument);
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          PortChannelConfigArgs({"port-channel100", "minimum-links", "128"})),
      std::invalid_argument);
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientPersistsToDisk) {
  auto cmd = CmdConfigPortChannel();
  cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel100", "description", "persisted"}));
  ASSERT_TRUE(std::filesystem::exists(getSessionConfigPath()));
  EXPECT_THAT(readFile(getSessionConfigPath()), HasSubstr("persisted"));
}

// ---- CmdConfigPortChannelMember::queryClient ----

TEST_F(CmdConfigPortChannelTestFixture, queryClientCreateNameWithoutIdThrows) {
  auto cmd = CmdConfigPortChannelMember();
  for (const auto& name :
       {"port-channel", "port-channel0", "port-channel32768"}) {
    EXPECT_THROW(
        cmd.queryClient(
            localhost(),
            PortChannelConfigArgs({name}),
            PortChannelMemberArgs({"add", "eth1/2/1"})),
        std::invalid_argument)
        << name;
    EXPECT_EQ(portChannel(name), nullptr) << name;
  }
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientExistingNameWithoutIdOk) {
  // A pre-existing aggregate port whose name has no numeric suffix can still
  // be configured.
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  swConfig.aggregatePorts()->front().name() = "agg";
  auto cmd = CmdConfigPortChannel();
  auto result = cmd.queryClient(
      localhost(), PortChannelConfigArgs({"agg", "description", "renamed"}));
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_EQ(*portChannel("agg")->description(), "renamed");
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientMemberAddCreates) {
  auto cmd = CmdConfigPortChannelMember();
  auto result = cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel101"}),
      PortChannelMemberArgs({"add", "eth1/2/1", "eth1/3/1"}));
  EXPECT_THAT(result, HasSubstr("Successfully added"));
  EXPECT_THAT(result, HasSubstr("created"));

  auto* pc = portChannel("port-channel101");
  ASSERT_NE(pc, nullptr);
  EXPECT_EQ(*pc->key(), 101);
  ASSERT_EQ(pc->memberPorts()->size(), 2);
  EXPECT_EQ(*(*pc->memberPorts())[0].memberPortID(), 2);
  EXPECT_EQ(
      *(*pc->memberPorts())[0].priority(), kPortChannelDefaultMemberPriority);
  EXPECT_EQ(*(*pc->memberPorts())[0].rate(), cfg::LacpPortRate::FAST);
  EXPECT_EQ(*(*pc->memberPorts())[0].activity(), cfg::LacpPortActivity::ACTIVE);
  EXPECT_EQ(*(*pc->memberPorts())[0].holdTimerMultiplier(), 3);
  EXPECT_EQ(*(*pc->memberPorts())[1].memberPortID(), 3);
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientMemberAddToExisting) {
  auto cmd = CmdConfigPortChannelMember();
  auto result = cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel100"}),
      PortChannelMemberArgs({"add", "eth1/2/1"}));
  EXPECT_THAT(result, HasSubstr("Successfully added"));
  EXPECT_THAT(result, Not(HasSubstr("created")));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  EXPECT_EQ(pc->memberPorts()->size(), 2);
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientMemberAddDuplicateIsNoop) {
  auto cmd = CmdConfigPortChannelMember();
  auto result = cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel100"}),
      PortChannelMemberArgs({"add", "eth1/1/1"}));
  EXPECT_THAT(result, HasSubstr("already member"));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  EXPECT_EQ(pc->memberPorts()->size(), 1);
}

TEST_F(
    CmdConfigPortChannelTestFixture,
    queryClientMemberAddInOtherPortChannelThrows) {
  auto cmd = CmdConfigPortChannelMember();
  // eth1/1/1 is already a member of port-channel100.
  try {
    cmd.queryClient(
        localhost(),
        PortChannelConfigArgs({"port-channel101"}),
        PortChannelMemberArgs({"add", "eth1/1/1"}));
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(std::string(e.what()), HasSubstr("port-channel100"));
  }
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientMemberAddUnknownPortThrows) {
  auto cmd = CmdConfigPortChannelMember();
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          PortChannelConfigArgs({"port-channel101"}),
          PortChannelMemberArgs({"add", "eth9/9/9"})),
      std::invalid_argument);
  // The failed creation must not leave a memberless port-channel behind.
  EXPECT_EQ(portChannel("port-channel101"), nullptr);
}

TEST_F(
    CmdConfigPortChannelTestFixture,
    queryClientMemberAddVlanMismatchWithExistingMemberThrows) {
  auto cmd = CmdConfigPortChannelMember();
  // port-channel100's member eth1/1/1 is in VLAN 2001; eth1/5/1 is in 2005.
  try {
    cmd.queryClient(
        localhost(),
        PortChannelConfigArgs({"port-channel100"}),
        PortChannelMemberArgs({"add", "eth1/5/1"}));
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(
        std::string(e.what()),
        HasSubstr(
            "'eth1/5/1' has ingress VLAN 2005 but the members of "
            "port-channel 'port-channel100' have ingress VLAN 2001"));
  }
  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  EXPECT_EQ(pc->memberPorts()->size(), 1);
}

TEST_F(
    CmdConfigPortChannelTestFixture,
    queryClientMemberAddVlanMismatchAmongNewMembersThrows) {
  auto cmd = CmdConfigPortChannelMember();
  // A fresh port-channel takes its VLAN from the first listed member.
  try {
    cmd.queryClient(
        localhost(),
        PortChannelConfigArgs({"port-channel101"}),
        PortChannelMemberArgs({"add", "eth1/2/1", "eth1/5/1"}));
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(
        std::string(e.what()),
        HasSubstr(
            "'eth1/5/1' has ingress VLAN 2005 but the members of "
            "port-channel 'port-channel101' have ingress VLAN 2001"));
  }
  // The failed creation must not leave a port-channel behind.
  EXPECT_EQ(portChannel("port-channel101"), nullptr);
}

TEST_F(
    CmdConfigPortChannelTestFixture,
    queryClientMemberAddPortWithoutVlanCreatesRoutedPortChannel) {
  auto cmd = CmdConfigPortChannelMember();
  // eth1/4/1 has ingressVlan 0: a routed LAG (its router interface binds to
  // the aggregate port) has members in no VLAN.
  auto result = cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel101"}),
      PortChannelMemberArgs({"add", "eth1/4/1"}));
  EXPECT_THAT(result, HasSubstr("Successfully added"));
  ASSERT_NE(portChannel("port-channel101"), nullptr);
}

TEST_F(
    CmdConfigPortChannelTestFixture,
    queryClientMemberAddPortWithoutVlanToVlanPortChannelThrows) {
  auto cmd = CmdConfigPortChannelMember();
  try {
    cmd.queryClient(
        localhost(),
        PortChannelConfigArgs({"port-channel100"}),
        PortChannelMemberArgs({"add", "eth1/4/1"}));
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(
        std::string(e.what()),
        HasSubstr(
            "'eth1/4/1' has ingress VLAN 0 but the members of "
            "port-channel 'port-channel100' have ingress VLAN 2001"));
  }
  EXPECT_EQ(portChannel("port-channel100")->memberPorts()->size(), 1);
}

TEST_F(
    CmdConfigPortChannelTestFixture,
    queryClientMemberAddNonPhysicalInterfaceThrows) {
  auto cmd = CmdConfigPortChannelMember();
  // "2001" resolves as an interface ID (the SVI for vlan2001), which has no
  // underlying physical port.
  try {
    cmd.queryClient(
        localhost(),
        PortChannelConfigArgs({"port-channel101"}),
        PortChannelMemberArgs({"add", "2001"}));
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(std::string(e.what()), HasSubstr("not a physical port"));
  }
  EXPECT_EQ(portChannel("port-channel101"), nullptr);
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientMemberKeyCollisionThrows) {
  auto cmd = CmdConfigPortChannelMember();
  // "po100" derives key 100, already taken by port-channel100.
  try {
    cmd.queryClient(
        localhost(),
        PortChannelConfigArgs({"po100"}),
        PortChannelMemberArgs({"add", "eth1/2/1"}));
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(std::string(e.what()), HasSubstr("already used"));
  }
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientMemberRemove) {
  auto cmd = CmdConfigPortChannelMember();
  cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel100"}),
      PortChannelMemberArgs({"add", "eth1/2/1"}));

  auto result = cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel100"}),
      PortChannelMemberArgs({"remove", "eth1/2/1"}));
  EXPECT_THAT(result, HasSubstr("Successfully removed"));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  ASSERT_EQ(pc->memberPorts()->size(), 1);
  EXPECT_EQ(*(*pc->memberPorts())[0].memberPortID(), 1);
}

TEST_F(
    CmdConfigPortChannelTestFixture,
    queryClientMemberRemoveDuplicateNamesOnce) {
  auto cmd = CmdConfigPortChannelMember();
  cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel100"}),
      PortChannelMemberArgs({"add", "eth1/2/1", "eth1/3/1"}));

  // Repeating a name must not inflate the removal count: three tokens name
  // only two distinct members of the three-member port-channel, so this is a
  // valid removal, not an attempt to empty it.
  auto result = cmd.queryClient(
      localhost(),
      PortChannelConfigArgs({"port-channel100"}),
      PortChannelMemberArgs({"remove", "eth1/2/1", "eth1/2/1", "eth1/3/1"}));
  EXPECT_THAT(result, HasSubstr("Successfully removed"));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  ASSERT_EQ(pc->memberPorts()->size(), 1);
  EXPECT_EQ(*(*pc->memberPorts())[0].memberPortID(), 1);
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientMemberRemoveLastThrows) {
  auto cmd = CmdConfigPortChannelMember();
  try {
    cmd.queryClient(
        localhost(),
        PortChannelConfigArgs({"port-channel100"}),
        PortChannelMemberArgs({"remove", "eth1/1/1"}));
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(std::string(e.what()), HasSubstr("delete port-channel"));
  }
  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  EXPECT_EQ(pc->memberPorts()->size(), 1);
}

TEST_F(CmdConfigPortChannelTestFixture, queryClientMemberSetAttrs) {
  auto cmd = CmdConfigPortChannelMember();
  PortChannelConfigArgs parent({"port-channel100"});

  cmd.queryClient(
      localhost(),
      parent,
      PortChannelMemberArgs({"eth1/1/1", "priority", "1000"}));
  cmd.queryClient(
      localhost(),
      parent,
      PortChannelMemberArgs({"eth1/1/1", "lacp", "rate", "slow"}));
  cmd.queryClient(
      localhost(),
      parent,
      PortChannelMemberArgs({"eth1/1/1", "lacp", "mode", "passive"}));
  cmd.queryClient(
      localhost(),
      parent,
      PortChannelMemberArgs({"eth1/1/1", "lacp", "hold-timer", "5"}));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  const auto& member = (*pc->memberPorts())[0];
  EXPECT_EQ(*member.priority(), 1000);
  EXPECT_EQ(*member.rate(), cfg::LacpPortRate::SLOW);
  EXPECT_EQ(*member.activity(), cfg::LacpPortActivity::PASSIVE);
  EXPECT_EQ(*member.holdTimerMultiplier(), 5);
}

TEST_F(
    CmdConfigPortChannelTestFixture,
    queryClientMemberSetAttrNonMemberThrows) {
  auto cmd = CmdConfigPortChannelMember();
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          PortChannelConfigArgs({"port-channel100"}),
          PortChannelMemberArgs({"eth1/2/1", "priority", "1000"})),
      std::invalid_argument);
}

TEST_F(
    CmdConfigPortChannelTestFixture,
    queryClientMemberRejectsParentAttributes) {
  auto cmd = CmdConfigPortChannelMember();
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          PortChannelConfigArgs({"port-channel100", "description", "x"}),
          PortChannelMemberArgs({"add", "eth1/2/1"})),
      std::invalid_argument);
}

} // namespace facebook::fboss
