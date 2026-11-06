// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <limits>

#include <folly/IPAddressV4.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"

#include <thrift/lib/cpp2/reflection/testing.h>
#include "fboss/cli/fboss2/commands/show/interface/counters/CmdShowInterfaceCounters.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

/*
 * Set up test data
 */
std::map<int32_t, PortInfoThrift> createInterfaceCountersEntries() {
  std::map<int32_t, PortInfoThrift> portMap;

  PortInfoThrift portEntry1;
  portEntry1.portId() = 1;
  portEntry1.name() = "eth1/1/1";

  PortCounters inputCounter;
  PortCounters outputCounter;

  inputCounter.bytes() = 0;
  inputCounter.ucastPkts() = 0;
  inputCounter.multicastPkts() = 0;
  inputCounter.broadcastPkts() = 0;

  outputCounter.bytes() = 0;
  outputCounter.ucastPkts() = 0;
  outputCounter.multicastPkts() = 0;
  outputCounter.broadcastPkts() = 0;

  portEntry1.input() = inputCounter;
  portEntry1.output() = outputCounter;

  PortInfoThrift portEntry2;
  portEntry2.portId() = 2;
  portEntry2.name() = "eth2/1/1";

  PortCounters inputCounter2;
  PortCounters outputCounter2;

  inputCounter2.bytes() = 100;
  inputCounter2.ucastPkts() = 100;
  inputCounter2.multicastPkts() = 100;
  inputCounter2.broadcastPkts() = 100;

  outputCounter2.bytes() = 100;
  outputCounter2.ucastPkts() = 100;
  outputCounter2.multicastPkts() = 100;
  outputCounter2.broadcastPkts() = 100;

  portEntry2.input() = inputCounter2;
  portEntry2.output() = outputCounter2;

  portMap[folly::copy(portEntry1.portId().value())] = portEntry1;
  portMap[folly::copy(portEntry2.portId().value())] = portEntry2;

  return portMap;
}

class CmdShowInterfaceCountersTestFixture : public CmdHandlerTestBase {
 public:
  std::map<int32_t, facebook::fboss::PortInfoThrift> portEntries;
  std::vector<facebook::fboss::AggregatePortThrift> aggregatePortEntries;
  std::vector<std::string> queriedEntries;
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    portEntries = createInterfaceCountersEntries();

    AggregatePortMemberThrift member1;
    member1.memberPortID() = 1;
    AggregatePortMemberThrift member2;
    member2.memberPortID() = 2;
    AggregatePortThrift aggregatePort;
    aggregatePort.name() = "Port-Channel1";
    aggregatePort.memberPorts() = {member1, member2};
    aggregatePortEntries.push_back(std::move(aggregatePort));
  }
};

TEST_F(CmdShowInterfaceCountersTestFixture, queryClient) {
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce([&](auto& countersEntries) { countersEntries = portEntries; });
  EXPECT_CALL(getMockAgent(), getAggregatePortTable(_))
      .WillOnce(
          [&](auto& aggregatePorts) { aggregatePorts = aggregatePortEntries; });

  auto cmd = CmdShowInterfaceCounters();
  auto result = cmd.queryClient(localhost(), queriedEntries);
  auto model =
      cmd.createModel(portEntries, aggregatePortEntries, queriedEntries);

  EXPECT_THRIFT_EQ(model, result);
}

TEST_F(CmdShowInterfaceCountersTestFixture, createModel) {
  auto cmd = CmdShowInterfaceCounters();
  auto model =
      cmd.createModel(portEntries, aggregatePortEntries, queriedEntries);
  auto intCounters = model.int_counters().value();

  ASSERT_EQ(intCounters.size(), 3);
  EXPECT_EQ(*intCounters[0].interfaceName(), "Port-Channel1");
  EXPECT_EQ(*intCounters[0].inputBytes(), 100);
  EXPECT_EQ(*intCounters[0].inputUcastPkts(), 100);
  EXPECT_EQ(*intCounters[0].inputMulticastPkts(), 100);
  EXPECT_EQ(*intCounters[0].inputBroadcastPkts(), 100);
  EXPECT_EQ(*intCounters[0].outputBytes(), 100);
  EXPECT_EQ(*intCounters[0].outputUcastPkts(), 100);
  EXPECT_EQ(*intCounters[0].outputMulticastPkts(), 100);
  EXPECT_EQ(*intCounters[0].outputBroadcastPkts(), 100);
}

TEST_F(CmdShowInterfaceCountersTestFixture, createModelForAggregatePort) {
  auto cmd = CmdShowInterfaceCounters();
  auto model =
      cmd.createModel(portEntries, aggregatePortEntries, {"Port-Channel1"});

  ASSERT_EQ(model.int_counters()->size(), 1);
  EXPECT_EQ(*model.int_counters()->front().interfaceName(), "Port-Channel1");
  EXPECT_EQ(*model.int_counters()->front().inputBytes(), 100);
  EXPECT_EQ(*model.int_counters()->front().outputBytes(), 100);
}

TEST_F(CmdShowInterfaceCountersTestFixture, preservesUninitializedCounter) {
  portEntries.at(2).input()->bytes() =
      hardware_stats_constants::STAT_UNINITIALIZED();

  auto model = CmdShowInterfaceCounters().createModel(
      portEntries, aggregatePortEntries, {"Port-Channel1"});

  ASSERT_EQ(model.int_counters()->size(), 1);
  EXPECT_EQ(
      *model.int_counters()->front().inputBytes(),
      hardware_stats_constants::STAT_UNINITIALIZED());
}

TEST_F(CmdShowInterfaceCountersTestFixture, missingMemberIsUninitialized) {
  portEntries.erase(2);

  auto model = CmdShowInterfaceCounters().createModel(
      portEntries, aggregatePortEntries, {"Port-Channel1"});

  ASSERT_EQ(model.int_counters()->size(), 1);
  const auto& counter = model.int_counters()->front();
  const auto uninitialized = hardware_stats_constants::STAT_UNINITIALIZED();
  EXPECT_EQ(*counter.inputBytes(), uninitialized);
  EXPECT_EQ(*counter.inputUcastPkts(), uninitialized);
  EXPECT_EQ(*counter.inputMulticastPkts(), uninitialized);
  EXPECT_EQ(*counter.inputBroadcastPkts(), uninitialized);
  EXPECT_EQ(*counter.outputBytes(), uninitialized);
  EXPECT_EQ(*counter.outputUcastPkts(), uninitialized);
  EXPECT_EQ(*counter.outputMulticastPkts(), uninitialized);
  EXPECT_EQ(*counter.outputBroadcastPkts(), uninitialized);
}

TEST_F(CmdShowInterfaceCountersTestFixture, saturatesCounterOnOverflow) {
  portEntries.at(1).input()->bytes() = std::numeric_limits<int64_t>::max() - 50;

  auto model = CmdShowInterfaceCounters().createModel(
      portEntries, aggregatePortEntries, {"Port-Channel1"});

  ASSERT_EQ(model.int_counters()->size(), 1);
  EXPECT_EQ(
      *model.int_counters()->front().inputBytes(),
      std::numeric_limits<int64_t>::max());
}

TEST_F(CmdShowInterfaceCountersTestFixture, printOutput) {
  auto cmd = CmdShowInterfaceCounters();
  auto model = cmd.createModel(portEntries, {}, queriedEntries);

  std::stringstream ss;
  cmd.printOutput(model, ss);

  std::string output = ss.str();
  std::string expectOutput =
      " Interface Name  Bytes(in)  Unicast Pkts(in)  Multicast Pkts(in)  Broadcast Pkts(in)  Bytes(out)  Unicast Pkts(out)  Multicast Pkts(out)  Broadcast Pkts(out) \n"
      "------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n"
      " eth1/1/1        0          0                 0                   0                   0           0                  0                    0                   \n"
      " eth2/1/1        100        100               100                 100                 100         100                100                  100                 \n\n";
  EXPECT_EQ(output, expectOutput);
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowInterfaceCountersTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowInterfaceCountersTraits::description().empty());
  EXPECT_FALSE(CmdShowInterfaceCounters::sampleModel().int_counters()->empty());
}
} // namespace facebook::fboss
