// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/hw/gen-cpp2/hardware_stats_constants.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"

#include <thrift/lib/cpp2/reflection/testing.h>
#include "fboss/cli/fboss2/commands/show/interface/counters/queue/CmdShowInterfaceCountersQueue.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/queue/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {

PortQueueThrift makeQueue(int32_t id, const std::string& name) {
  PortQueueThrift queue;
  queue.id() = id;
  queue.name() = name;
  return queue;
}

PortInfoThrift makePort(
    int32_t portId,
    const std::string& name,
    std::vector<PortQueueThrift> queues) {
  PortInfoThrift portInfo;
  portInfo.portId() = portId;
  portInfo.name() = name;
  portInfo.portQueues() = std::move(queues);
  return portInfo;
}

/*
 * eth1/11/1 has two configured queues (0 named, 1 unnamed) and the hardware
 * also counts queue 2, which is not configured and has no discard-bytes
 * counter. eth1/2/1 has a single queue. fab1/1/1 has neither queues nor
 * hardware stats.
 */
std::map<int32_t, PortInfoThrift> createPortEntries() {
  std::map<int32_t, PortInfoThrift> portEntries;
  portEntries.emplace(
      11, makePort(11, "eth1/11/1", {makeQueue(0, "ncnf"), makeQueue(1, "")}));
  portEntries.emplace(2, makePort(2, "eth1/2/1", {makeQueue(0, "ncnf")}));
  portEntries.emplace(3, makePort(3, "fab1/1/1", {}));
  return portEntries;
}

std::map<std::string, HwPortStats> createPortStats() {
  std::map<std::string, HwPortStats> portStats;

  HwPortStats stats11;
  stats11.queueOutPackets_() = {{0, 10}, {1, 0}, {2, 5}};
  stats11.queueOutBytes_() = {{0, 1000}, {1, 0}, {2, 500}};
  stats11.queueOutDiscardPackets_() = {{0, 0}, {1, 0}, {2, 1}};
  stats11.queueOutDiscardBytes_() = {{0, 0}, {1, 0}};
  portStats.emplace("eth1/11/1", std::move(stats11));

  HwPortStats stats2;
  stats2.queueOutPackets_() = {{0, 7}};
  stats2.queueOutBytes_() = {{0, 700}};
  stats2.queueOutDiscardPackets_() = {{0, 0}};
  stats2.queueOutDiscardBytes_() = {{0, 0}};
  portStats.emplace("eth1/2/1", std::move(stats2));

  return portStats;
}

} // namespace

class CmdShowInterfaceCountersQueueTestFixture : public CmdHandlerTestBase {
 public:
  std::map<int32_t, PortInfoThrift> portEntries;
  std::map<std::string, HwPortStats> portStats;
  std::vector<std::string> queriedEntries;
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    portEntries = createPortEntries();
    portStats = createPortStats();
  }
};

TEST_F(CmdShowInterfaceCountersQueueTestFixture, queryClient) {
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_)).WillOnce([&](auto& entries) {
    entries = portEntries;
  });
  EXPECT_CALL(getMockAgent(), getHwPortStats(_)).WillOnce([&](auto& stats) {
    stats = portStats;
  });

  auto cmd = CmdShowInterfaceCountersQueue();
  auto result = cmd.queryClient(localhost(), queriedEntries);
  auto model = cmd.createModel(portEntries, portStats, queriedEntries);

  EXPECT_THRIFT_EQ(model, result);
}

TEST_F(CmdShowInterfaceCountersQueueTestFixture, createModel) {
  auto model = CmdShowInterfaceCountersQueue().createModel(
      portEntries, portStats, queriedEntries);
  const auto& counters = model.queueCounters().value();

  // fab1/1/1 has nothing to report and produces no rows. Ports are in front
  // panel order, queues in ID order.
  ASSERT_EQ(counters.size(), 4);
  EXPECT_EQ(*counters[0].interfaceName(), "eth1/2/1");
  EXPECT_EQ(*counters[0].queueId(), 0);
  EXPECT_EQ(*counters[0].queueName(), "ncnf");
  EXPECT_EQ(*counters[0].outPackets(), 7);
  EXPECT_EQ(*counters[0].outBytes(), 700);

  EXPECT_EQ(*counters[1].interfaceName(), "eth1/11/1");
  EXPECT_EQ(*counters[1].queueId(), 0);
  EXPECT_EQ(*counters[1].queueName(), "ncnf");
  EXPECT_EQ(*counters[1].outPackets(), 10);
  EXPECT_EQ(*counters[1].outBytes(), 1000);
  EXPECT_EQ(*counters[1].outDiscardPackets(), 0);
  EXPECT_EQ(*counters[1].outDiscardBytes(), 0);

  // Configured but unnamed.
  EXPECT_EQ(*counters[2].queueId(), 1);
  EXPECT_EQ(*counters[2].queueName(), "");

  // Counted by the hardware but not configured, with one counter missing.
  EXPECT_EQ(*counters[3].queueId(), 2);
  EXPECT_EQ(*counters[3].queueName(), "");
  EXPECT_EQ(*counters[3].outPackets(), 5);
  EXPECT_EQ(*counters[3].outBytes(), 500);
  EXPECT_EQ(*counters[3].outDiscardPackets(), 1);
  EXPECT_EQ(
      *counters[3].outDiscardBytes(),
      hardware_stats_constants::STAT_UNINITIALIZED());
}

TEST_F(CmdShowInterfaceCountersQueueTestFixture, createModelQueriedIfs) {
  auto model = CmdShowInterfaceCountersQueue().createModel(
      portEntries, portStats, {"eth1/2/1", "fab1/1/1", "Port-Channel1"});

  ASSERT_EQ(model.queueCounters()->size(), 1);
  EXPECT_EQ(*model.queueCounters()->front().interfaceName(), "eth1/2/1");
}

TEST_F(CmdShowInterfaceCountersQueueTestFixture, configuredQueueWithoutStats) {
  portEntries.at(2).portQueues()->push_back(makeQueue(7, "nc"));

  auto model = CmdShowInterfaceCountersQueue().createModel(
      portEntries, portStats, {"eth1/2/1"});

  ASSERT_EQ(model.queueCounters()->size(), 2);
  const auto& queue7 = model.queueCounters()->back();
  const auto uninitialized = hardware_stats_constants::STAT_UNINITIALIZED();
  EXPECT_EQ(*queue7.queueId(), 7);
  EXPECT_EQ(*queue7.queueName(), "nc");
  EXPECT_EQ(*queue7.outPackets(), uninitialized);
  EXPECT_EQ(*queue7.outBytes(), uninitialized);
  EXPECT_EQ(*queue7.outDiscardPackets(), uninitialized);
  EXPECT_EQ(*queue7.outDiscardBytes(), uninitialized);
}

// Right after the agent starts, getHwPortStats() has no entry for a port yet:
// its configured queues are still listed, with every counter unknown.
TEST_F(CmdShowInterfaceCountersQueueTestFixture, portWithoutStats) {
  portStats.erase("eth1/11/1");

  auto model = CmdShowInterfaceCountersQueue().createModel(
      portEntries, portStats, {"eth1/11/1"});

  const auto uninitialized = hardware_stats_constants::STAT_UNINITIALIZED();
  ASSERT_EQ(model.queueCounters()->size(), 2);
  for (int16_t queueId = 0; queueId < 2; ++queueId) {
    const auto& counters = model.queueCounters()->at(queueId);
    EXPECT_EQ(*counters.interfaceName(), "eth1/11/1");
    EXPECT_EQ(*counters.queueId(), queueId);
    EXPECT_EQ(*counters.outPackets(), uninitialized);
    EXPECT_EQ(*counters.outBytes(), uninitialized);
    EXPECT_EQ(*counters.outDiscardPackets(), uninitialized);
    EXPECT_EQ(*counters.outDiscardBytes(), uninitialized);
  }
  EXPECT_EQ(*model.queueCounters()->at(0).queueName(), "ncnf");
  EXPECT_EQ(*model.queueCounters()->at(1).queueName(), "");
}

TEST_F(CmdShowInterfaceCountersQueueTestFixture, printOutput) {
  auto cmd = CmdShowInterfaceCountersQueue();
  auto model = cmd.createModel(portEntries, portStats, queriedEntries);

  std::stringstream ss;
  cmd.printOutput(model, ss);

  std::string output = ss.str();
  std::string expectOutput =
      " Interface  Queue  Name  Out Pkts  Out Bytes  Discard Pkts  Discard Bytes \n"
      "----------------------------------------------------------------------------------\n"
      " eth1/2/1   0      ncnf  7         700        0             0             \n"
      " eth1/11/1  0      ncnf  10        1000       0             0             \n"
      " eth1/11/1  1            0         0          0             0             \n"
      " eth1/11/1  2            5         500        1             n/a           \n\n";
  EXPECT_EQ(output, expectOutput);
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowInterfaceCountersQueueTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowInterfaceCountersQueueTraits::description().empty());
  EXPECT_FALSE(
      CmdShowInterfaceCountersQueue::sampleModel().queueCounters()->empty());
}

} // namespace facebook::fboss
