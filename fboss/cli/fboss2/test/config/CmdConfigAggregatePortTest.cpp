// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/aggregate_port/AggregatePortConfigUtils.h"
#include "fboss/cli/fboss2/commands/config/aggregate_port/CmdConfigAggregatePort.h"
#include "fboss/cli/fboss2/commands/delete/aggregate_port/CmdDeleteAggregatePort.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigAggregatePortTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigAggregatePortTestFixture()
      : CmdConfigTestBase(
            "fboss_aggregate_port_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {"logicalID": 1, "name": "eth1/1/1", "state": 2, "speed": 100000},
      {"logicalID": 2, "name": "eth1/2/1", "state": 2, "speed": 100000},
      {"logicalID": 3, "name": "eth1/3/1", "state": 2, "speed": 100000}
    ],
    "aggregatePorts": [
      {
        "key": 4000,
        "name": "Port-Channel4000",
        "description": "existing LAG",
        "memberPorts": [
          {
            "memberPortID": 2,
            "priority": 32768,
            "rate": 1,
            "activity": 1,
            "holdTimerMultiplier": 3
          }
        ],
        "minimumCapacity": {"linkPercentage": 0.5},
        "minimumCapacityToUp": {"linkPercentage": 0.75},
        "aggregatePortType": 0,
        "extendedKey": 0
      },
      {
        "key": 5000,
        "name": "Port-Channel5000",
        "description": "second LAG",
        "memberPorts": [
          {
            "memberPortID": 3,
            "priority": 32768,
            "rate": 1,
            "activity": 1,
            "holdTimerMultiplier": 3
          }
        ],
        "minimumCapacity": {"linkPercentage": 1.0},
        "aggregatePortType": 0,
        "extendedKey": 0
      }
    ],
    "interfaces": [
      {
        "intfID": 4000,
        "routerID": 0,
        "vlanID": 0,
        "name": "Port-Channel4000",
        "type": 3,
        "aggregatePortID": 4000
      }
    ]
  }
})") {}

  const cfg::AggregatePort& getAggregatePort(const std::string& name) {
    const auto& ports =
        *ConfigSession::getInstance().getAgentConfig().sw()->aggregatePorts();
    auto* port = aggregate_port_utils::findAggregatePort(ports, name);
    if (!port) {
      throw std::runtime_error("Aggregate port not found in test");
    }
    return *port;
  }
};

TEST_F(CmdConfigAggregatePortTestFixture, validatesArguments) {
  EXPECT_THROW(AggregatePortConfigArg({}), std::invalid_argument);
  EXPECT_THROW(
      AggregatePortConfigArg({"Port-Channel1", "key"}), std::invalid_argument);
  EXPECT_THROW(
      AggregatePortConfigArg({"Port-Channel1", "unknown", "1"}),
      std::invalid_argument);
  EXPECT_THROW(
      AggregatePortConfigArg({"Port-Channel1", "extended-key", "1"}),
      std::invalid_argument);
  EXPECT_THROW(
      AggregatePortConfigArg(
          {"Port-Channel1", "minimum-capacity-link-percentage", "0.5"}),
      std::invalid_argument);
  EXPECT_THROW(
      AggregatePortConfigArg(
          {"Port-Channel1", "minimum-capacity-to-up-link-percentage", "0.75"}),
      std::invalid_argument);
  EXPECT_THROW(AggregatePortMemberConfigArg({}), std::invalid_argument);
  EXPECT_THROW(
      AggregatePortMemberConfigArg({"eth1/1/1", "priority"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAggregatePortTestFixture, createsJiraAggregatePort) {
  setupTestableConfigSession();
  CmdConfigAggregatePort cmd;
  AggregatePortConfigArg args({
      "Port-Channel3001",
      "key",
      "3001",
      "description",
      "LAG to n3 eth1/41/1 (eth1/1/1)",
      "type",
      "lag",
      "minimum-capacity-link-count",
      "1",
      "minimum-capacity-to-up-link-count",
      "2",
  });

  EXPECT_THAT(cmd.queryClient(localhost(), args), HasSubstr("created"));

  const auto& port = getAggregatePort("Port-Channel3001");
  EXPECT_EQ(*port.key(), 3001);
  EXPECT_EQ(*port.extendedKey(), 0);
  EXPECT_EQ(*port.description(), "LAG to n3 eth1/41/1 (eth1/1/1)");
  EXPECT_EQ(*port.aggregatePortType(), cfg::AggregatePortType::LAG_PORT);
  EXPECT_EQ(port.minimumCapacity()->get_linkCount(), 1);
  ASSERT_TRUE(port.minimumCapacityToUp().has_value());
  EXPECT_EQ(port.minimumCapacityToUp()->get_linkCount(), 2);
  EXPECT_TRUE(port.memberPorts()->empty());
}

TEST_F(CmdConfigAggregatePortTestFixture, updatesWithoutClearingOtherFields) {
  setupTestableConfigSession();
  CmdConfigAggregatePort cmd;
  AggregatePortConfigArg args(
      {"Port-Channel4000", "description", "updated description"});

  EXPECT_THAT(cmd.queryClient(localhost(), args), HasSubstr("updated"));

  const auto& port = getAggregatePort("Port-Channel4000");
  EXPECT_EQ(*port.description(), "updated description");
  EXPECT_EQ(*port.key(), 4000);
  EXPECT_EQ(port.memberPorts()->size(), 1);
  EXPECT_DOUBLE_EQ(port.minimumCapacity()->get_linkPercentage(), 0.5);
  EXPECT_TRUE(port.minimumCapacityToUp().has_value());
}

TEST_F(CmdConfigAggregatePortTestFixture, configuresMultipleMembers) {
  setupTestableConfigSession();
  CmdConfigAggregatePortMember cmd;
  AggregatePortConfigArg aggregatePort({"Port-Channel4000"});
  AggregatePortMemberConfigArg members({
      "eth1/1/1",
      "2",
      "activity",
      "passive",
      "rate",
      "slow",
      "priority",
      "1234",
      "hold-timer-multiplier",
      "5",
  });

  EXPECT_THAT(
      cmd.queryClient(localhost(), aggregatePort, members),
      HasSubstr("2 member(s)"));

  const auto& configured = *getAggregatePort("Port-Channel4000").memberPorts();
  ASSERT_EQ(configured.size(), 2);
  for (const auto& member : configured) {
    EXPECT_EQ(*member.activity(), cfg::LacpPortActivity::PASSIVE);
    EXPECT_EQ(*member.rate(), cfg::LacpPortRate::SLOW);
    EXPECT_EQ(*member.priority(), 1234);
    EXPECT_EQ(*member.holdTimerMultiplier(), 5);
  }
}

TEST_F(CmdConfigAggregatePortTestFixture, newMemberUsesDefaults) {
  setupTestableConfigSession();
  CmdConfigAggregatePortMember cmd;
  AggregatePortConfigArg aggregatePort({"Port-Channel4000"});
  AggregatePortMemberConfigArg member({"eth1/1/1"});

  cmd.queryClient(localhost(), aggregatePort, member);

  const auto& members = *getAggregatePort("Port-Channel4000").memberPorts();
  auto it = std::find_if(members.begin(), members.end(), [](const auto& entry) {
    return *entry.memberPortID() == 1;
  });
  ASSERT_NE(it, members.end());
  EXPECT_EQ(*it->activity(), cfg::LacpPortActivity::ACTIVE);
  EXPECT_EQ(*it->rate(), cfg::LacpPortRate::FAST);
  EXPECT_EQ(*it->priority(), 32768);
  EXPECT_EQ(*it->holdTimerMultiplier(), 3);
}

TEST_F(CmdConfigAggregatePortTestFixture, rejectsCrossLagMemberAtomically) {
  setupTestableConfigSession();
  CmdConfigAggregatePortMember cmd;
  AggregatePortConfigArg aggregatePort({"Port-Channel4000"});
  AggregatePortMemberConfigArg members(
      {"eth1/1/1", "eth1/3/1", "priority", "1234"});
  const auto before =
      *ConfigSession::getInstance().getAgentConfig().sw()->aggregatePorts();

  EXPECT_THROW(
      cmd.queryClient(localhost(), aggregatePort, members),
      std::invalid_argument);

  EXPECT_EQ(
      *ConfigSession::getInstance().getAgentConfig().sw()->aggregatePorts(),
      before);
}

TEST_F(CmdConfigAggregatePortTestFixture, rejectsInvalidHysteresis) {
  setupTestableConfigSession();
  CmdConfigAggregatePort cmd;
  AggregatePortConfigArg args({
      "Port-Channel3001",
      "key",
      "3001",
      "minimum-capacity-link-count",
      "2",
      "minimum-capacity-to-up-link-count",
      "1",
  });

  EXPECT_THROW(cmd.queryClient(localhost(), args), std::invalid_argument);
  EXPECT_EQ(
      aggregate_port_utils::findAggregatePort(
          *ConfigSession::getInstance().getAgentConfig().sw()->aggregatePorts(),
          "Port-Channel3001"),
      nullptr);
}

TEST_F(CmdConfigAggregatePortTestFixture, removesMinimumCapacityToUp) {
  setupTestableConfigSession();
  CmdDeleteAggregatePortMinimumCapacityToUp cmd;
  AggregatePortNameArg aggregatePort({"Port-Channel4000"});

  EXPECT_THAT(
      cmd.queryClient(localhost(), aggregatePort),
      HasSubstr("removed minimum-capacity-to-up"));

  const auto& port = getAggregatePort("Port-Channel4000");
  EXPECT_FALSE(port.minimumCapacityToUp().has_value());
  EXPECT_DOUBLE_EQ(port.minimumCapacity()->get_linkPercentage(), 0.5);
  EXPECT_EQ(port.memberPorts()->size(), 1);
}

TEST_F(CmdConfigAggregatePortTestFixture, removesMembersIdempotently) {
  setupTestableConfigSession();
  CmdDeleteAggregatePortMember cmd;
  AggregatePortNameArg aggregatePort({"Port-Channel5000"});
  AggregatePortMemberDeleteArg members({"eth1/1/1", "eth1/3/1"});

  EXPECT_THAT(
      cmd.queryClient(localhost(), aggregatePort, members),
      HasSubstr("removed 1 member(s)"));
  EXPECT_TRUE(getAggregatePort("Port-Channel5000").memberPorts()->empty());
  EXPECT_THAT(
      cmd.queryClient(localhost(), aggregatePort, members),
      HasSubstr("No requested members"));
}

TEST_F(CmdConfigAggregatePortTestFixture, deletesAggregatePort) {
  setupTestableConfigSession();
  CmdDeleteAggregatePort cmd;
  AggregatePortNameArg aggregatePort({"Port-Channel5000"});

  EXPECT_THAT(
      cmd.queryClient(localhost(), aggregatePort), HasSubstr("deleted"));
  EXPECT_EQ(
      aggregate_port_utils::findAggregatePort(
          *ConfigSession::getInstance().getAgentConfig().sw()->aggregatePorts(),
          "Port-Channel5000"),
      nullptr);
}

TEST_F(
    CmdConfigAggregatePortTestFixture,
    rejectsRemovingLastMemberUsedByRouterInterface) {
  setupTestableConfigSession();
  CmdDeleteAggregatePortMember cmd;
  AggregatePortNameArg aggregatePort({"Port-Channel4000"});
  AggregatePortMemberDeleteArg members({"eth1/2/1"});

  EXPECT_THROW(
      cmd.queryClient(localhost(), aggregatePort, members),
      std::invalid_argument);
  EXPECT_EQ(getAggregatePort("Port-Channel4000").memberPorts()->size(), 1);
}

TEST_F(
    CmdConfigAggregatePortTestFixture,
    rejectsDeletingAggregatePortUsedByRouterInterface) {
  setupTestableConfigSession();
  CmdDeleteAggregatePort cmd;
  AggregatePortNameArg aggregatePort({"Port-Channel4000"});

  EXPECT_THROW(
      cmd.queryClient(localhost(), aggregatePort), std::invalid_argument);
  EXPECT_NE(
      aggregate_port_utils::findAggregatePort(
          *ConfigSession::getInstance().getAgentConfig().sw()->aggregatePorts(),
          "Port-Channel4000"),
      nullptr);
}

} // namespace facebook::fboss
