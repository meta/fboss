// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fboss/cli/fboss2/commands/config/port_channel/PortChannelUtils.h"
#include "fboss/cli/fboss2/commands/delete/port_channel/CmdDeletePortChannel.h"
#include "fboss/cli/fboss2/commands/delete/port_channel/member/CmdDeletePortChannelMember.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdDeletePortChannelTestFixture : public CmdConfigTestBase {
 public:
  // Seed mirrors the aggregatePorts shape of production configs (see
  // fboss/oss/link_test_configs/meru800bia_c0.materialized_JSON); member
  // attributes deliberately differ from their defaults so the reset paths
  // are observable.
  CmdDeletePortChannelTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_port_channel_test_%%%%-%%%%-%%%%-%%%%",
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
        "ingressVlan": 2002
      }
    ],
    "vlanPorts": [
      {"vlanID": 2001, "logicalPort": 1, "spanningTreeState": 2, "emitTags": false},
      {"vlanID": 2002, "logicalPort": 2, "spanningTreeState": 2, "emitTags": false}
    ],
    "vlans": [
      {"id": 2001, "name": "vlan2001", "routable": true, "intfID": 2001},
      {"id": 2002, "name": "vlan2002", "routable": true, "intfID": 2002}
    ],
    "interfaces": [
      {"intfID": 2001, "vlanID": 2001, "routerID": 0, "type": 1, "mtu": 9412},
      {"intfID": 2002, "vlanID": 2002, "routerID": 0, "type": 1, "mtu": 9412}
    ],
    "aggregatePorts": [
      {
        "key": 100,
        "name": "port-channel100",
        "description": "uplink lag",
        "memberPorts": [
          {
            "memberPortID": 1,
            "priority": 1000,
            "rate": 0,
            "activity": 0,
            "holdTimerMultiplier": 5
          },
          {
            "memberPortID": 2,
            "priority": 32768,
            "rate": 1,
            "activity": 1,
            "holdTimerMultiplier": 3
          }
        ],
        "minimumCapacity": {"linkCount": 2}
      }
    ]
  }
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession("delete port-channel port-channel100", "");
  }

  static cfg::AggregatePort* portChannel(const std::string& name) {
    auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
    return findPortChannel(swConfig, name);
  }
};

// ---- PortChannelDeleteArgs validation ----

TEST_F(CmdDeletePortChannelTestFixture, argsValidBare) {
  PortChannelDeleteArgs args({"port-channel100"});
  EXPECT_EQ(args.getName(), "port-channel100");
  EXPECT_FALSE(args.hasAttributes());
}

TEST_F(CmdDeletePortChannelTestFixture, argsValidAttrs) {
  PortChannelDeleteArgs args(
      {"port-channel100", "description", "minimum-links"});
  ASSERT_EQ(args.getAttributes().size(), 2);
  EXPECT_EQ(args.getAttributes()[0].first, "description");
  EXPECT_EQ(args.getAttributes()[1].first, "minimum-links");
}

TEST_F(CmdDeletePortChannelTestFixture, argsUnknownAttrInvalid) {
  EXPECT_THROW(
      PortChannelDeleteArgs({"port-channel100", "members"}),
      std::invalid_argument);
}

// ---- PortChannelMemberDeleteArgs validation ----

TEST_F(CmdDeletePortChannelTestFixture, memberArgsRemoveList) {
  PortChannelMemberDeleteArgs args({"eth1/1/1", "eth1/2/1"});
  EXPECT_EQ(args.getOp(), PortChannelMemberDeleteArgs::Op::REMOVE);
  EXPECT_THAT(args.getMemberNames(), ElementsAre("eth1/1/1", "eth1/2/1"));
}

TEST_F(CmdDeletePortChannelTestFixture, memberArgsResets) {
  auto expectReset = [](const std::vector<std::string>& tokens,
                        const std::string& attr) {
    PortChannelMemberDeleteArgs args(tokens);
    EXPECT_EQ(args.getOp(), PortChannelMemberDeleteArgs::Op::RESET_ATTR);
    EXPECT_EQ(args.getAttr(), attr);
  };
  expectReset({"eth1/1/1", "priority"}, "priority");
  expectReset({"eth1/1/1", "lacp", "rate"}, "lacp rate");
  expectReset({"eth1/1/1", "lacp", "mode"}, "lacp mode");
  expectReset({"eth1/1/1", "lacp", "hold-timer"}, "lacp hold-timer");
}

TEST_F(CmdDeletePortChannelTestFixture, memberArgsInvalid) {
  EXPECT_THROW(PortChannelMemberDeleteArgs({}), std::invalid_argument);
  EXPECT_THROW(
      PortChannelMemberDeleteArgs({"priority"}), std::invalid_argument);
  EXPECT_THROW(
      PortChannelMemberDeleteArgs({"eth1/1/1", "priority", "100"}),
      std::invalid_argument);
  EXPECT_THROW(
      PortChannelMemberDeleteArgs({"eth1/1/1", "lacp"}), std::invalid_argument);
  EXPECT_THROW(
      PortChannelMemberDeleteArgs({"eth1/1/1", "lacp", "timeout"}),
      std::invalid_argument);
  EXPECT_THROW(
      PortChannelMemberDeleteArgs({"eth1/1/1", "eth1/2/1", "priority"}),
      std::invalid_argument);
}

// ---- CmdDeletePortChannel::queryClient ----

TEST_F(CmdDeletePortChannelTestFixture, queryClientDeletesPortChannel) {
  auto cmd = CmdDeletePortChannel();
  auto result =
      cmd.queryClient(localhost(), PortChannelDeleteArgs({"port-channel100"}));
  EXPECT_THAT(result, HasSubstr("Deleted port-channel"));

  EXPECT_EQ(portChannel("port-channel100"), nullptr);
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  EXPECT_TRUE(swConfig.aggregatePorts()->empty());
}

TEST_F(CmdDeletePortChannelTestFixture, queryClientMissingThrows) {
  auto cmd = CmdDeletePortChannel();
  EXPECT_THROW(
      cmd.queryClient(localhost(), PortChannelDeleteArgs({"port-channel200"})),
      std::invalid_argument);
}

TEST_F(CmdDeletePortChannelTestFixture, queryClientResetsDescription) {
  auto cmd = CmdDeletePortChannel();
  auto result = cmd.queryClient(
      localhost(), PortChannelDeleteArgs({"port-channel100", "description"}));
  EXPECT_THAT(result, HasSubstr("Reset"));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  EXPECT_EQ(*pc->description(), "");
}

TEST_F(CmdDeletePortChannelTestFixture, queryClientResetsMinimumLinks) {
  auto cmd = CmdDeletePortChannel();
  auto result = cmd.queryClient(
      localhost(), PortChannelDeleteArgs({"port-channel100", "minimum-links"}));
  EXPECT_THAT(result, HasSubstr("Reset"));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  ASSERT_EQ(
      pc->minimumCapacity()->getType(),
      cfg::MinimumCapacity::Type::linkPercentage);
  EXPECT_EQ(pc->minimumCapacity()->get_linkPercentage(), 1.0);
}

// ---- CmdDeletePortChannelMember::queryClient ----

TEST_F(CmdDeletePortChannelTestFixture, queryClientRemovesMember) {
  auto cmd = CmdDeletePortChannelMember();
  auto result = cmd.queryClient(
      localhost(),
      PortChannelDeleteArgs({"port-channel100"}),
      PortChannelMemberDeleteArgs({"eth1/2/1"}));
  EXPECT_THAT(result, HasSubstr("Removed member"));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  ASSERT_EQ(pc->memberPorts()->size(), 1);
  EXPECT_EQ(*(*pc->memberPorts())[0].memberPortID(), 1);
}

TEST_F(CmdDeletePortChannelTestFixture, queryClientRemoveAllMembersThrows) {
  auto cmd = CmdDeletePortChannelMember();
  try {
    cmd.queryClient(
        localhost(),
        PortChannelDeleteArgs({"port-channel100"}),
        PortChannelMemberDeleteArgs({"eth1/1/1", "eth1/2/1"}));
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(std::string(e.what()), HasSubstr("delete port-channel"));
  }
  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  EXPECT_EQ(pc->memberPorts()->size(), 2);
}

TEST_F(CmdDeletePortChannelTestFixture, queryClientRemoveNonMemberThrows) {
  auto cmd = CmdDeletePortChannelMember();
  // First remove eth1/2/1, then removing it again must fail cleanly.
  cmd.queryClient(
      localhost(),
      PortChannelDeleteArgs({"port-channel100"}),
      PortChannelMemberDeleteArgs({"eth1/2/1"}));
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          PortChannelDeleteArgs({"port-channel100"}),
          PortChannelMemberDeleteArgs({"eth1/2/1"})),
      std::invalid_argument);
}

TEST_F(CmdDeletePortChannelTestFixture, queryClientResetsMemberAttrs) {
  auto cmd = CmdDeletePortChannelMember();
  PortChannelDeleteArgs parent({"port-channel100"});

  // eth1/1/1 starts with priority 1000, rate SLOW (0), activity PASSIVE (0),
  // holdTimerMultiplier 5.
  cmd.queryClient(
      localhost(),
      parent,
      PortChannelMemberDeleteArgs({"eth1/1/1", "priority"}));
  cmd.queryClient(
      localhost(),
      parent,
      PortChannelMemberDeleteArgs({"eth1/1/1", "lacp", "rate"}));
  cmd.queryClient(
      localhost(),
      parent,
      PortChannelMemberDeleteArgs({"eth1/1/1", "lacp", "mode"}));
  cmd.queryClient(
      localhost(),
      parent,
      PortChannelMemberDeleteArgs({"eth1/1/1", "lacp", "hold-timer"}));

  auto* pc = portChannel("port-channel100");
  ASSERT_NE(pc, nullptr);
  const auto& member = (*pc->memberPorts())[0];
  EXPECT_EQ(*member.priority(), kPortChannelDefaultMemberPriority);
  EXPECT_EQ(*member.rate(), cfg::LacpPortRate::FAST);
  EXPECT_EQ(*member.activity(), cfg::LacpPortActivity::ACTIVE);
  EXPECT_EQ(*member.holdTimerMultiplier(), 3);
}

TEST_F(
    CmdDeletePortChannelTestFixture,
    queryClientMemberRejectsParentAttributes) {
  auto cmd = CmdDeletePortChannelMember();
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          PortChannelDeleteArgs({"port-channel100", "description"}),
          PortChannelMemberDeleteArgs({"eth1/2/1"})),
      std::invalid_argument);
}

TEST_F(CmdDeletePortChannelTestFixture, queryClientResetNonMemberThrows) {
  auto cmd = CmdDeletePortChannelMember();
  // Delete eth1/2/1 first so the reset targets a non-member.
  cmd.queryClient(
      localhost(),
      PortChannelDeleteArgs({"port-channel100"}),
      PortChannelMemberDeleteArgs({"eth1/2/1"}));
  EXPECT_THROW(
      cmd.queryClient(
          localhost(),
          PortChannelDeleteArgs({"port-channel100"}),
          PortChannelMemberDeleteArgs({"eth1/2/1", "priority"})),
      std::invalid_argument);
}

TEST_F(
    CmdDeletePortChannelTestFixture,
    queryClientDeleteReferencedByInterfaceThrows) {
  // A port router interface bound to the port-channel would dangle; the
  // agent rejects such a config on apply.
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  cfg::Interface intf;
  intf.intfID() = 3000;
  intf.type() = cfg::InterfaceType::PORT;
  intf.aggregatePortID() = 100;
  swConfig.interfaces()->push_back(std::move(intf));

  auto cmd = CmdDeletePortChannel();
  try {
    cmd.queryClient(localhost(), PortChannelDeleteArgs({"port-channel100"}));
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(
        std::string(e.what()),
        HasSubstr("still referenced by interface(s) 3000"));
  }
  EXPECT_NE(portChannel("port-channel100"), nullptr);

  // Attribute resets on the referenced port-channel are still allowed.
  auto result = cmd.queryClient(
      localhost(), PortChannelDeleteArgs({"port-channel100", "description"}));
  EXPECT_THAT(result, HasSubstr("Reset description"));
}

} // namespace facebook::fboss
