// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * End-to-end tests for 'fboss2 show interface counters queue'.
 *
 * The command joins getAllPortInfo() (queue names) with getHwPortStats()
 * (per-queue egress counters). The unit test
 * (CmdShowInterfaceCountersQueueTest) covers the join against synthetic data,
 * so what is left to test here is that the command reports what the live agent
 * reports.
 *
 * Tests:
 *
 * 1. CountersMatchHwPortStats
 *    Hardware counters only grow, so read getHwPortStats() before and after
 *    running the command and check that every per-queue counter the command
 *    prints lies between the two reads, and that it reports every queue the
 *    agent counts or has configured, with the configured name.
 *
 * 2. FilterByQueueId
 *    --filter 'queueId == 0' narrows the output to queue 0 of every port.
 *
 * Requirements:
 * - FBOSS agent must be running with a valid configuration.
 */

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace facebook::fboss;

class ShowInterfaceCountersQueueTest : public Fboss2IntegrationTest {
 protected:
  // (interfaceName, queueId)
  using QueueKey = std::pair<std::string, int>;

  std::map<std::string, HwPortStats> getHwPortStats() const {
    HostInfo hostInfo("localhost");
    auto client =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    std::map<std::string, HwPortStats> stats;
    client->sync_getHwPortStats(stats);
    return stats;
  }

  std::map<int32_t, PortInfoThrift> getAllPortInfo() const {
    HostInfo hostInfo("localhost");
    auto client =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    std::map<int32_t, PortInfoThrift> portInfo;
    client->sync_getAllPortInfo(portInfo);
    return portInfo;
  }

  std::map<QueueKey, folly::dynamic> showQueueCounters(
      const std::vector<std::string>& args) const {
    auto json = runCliJson(args);
    std::map<QueueKey, folly::dynamic> rows;
    for (const auto& [host, hostData] : json.items()) {
      if (!hostData.isObject() || !hostData.count("queueCounters")) {
        continue;
      }
      for (const auto& row : hostData["queueCounters"]) {
        QueueKey key{
            row["interfaceName"].asString(),
            static_cast<int>(row["queueId"].asInt())};
        EXPECT_TRUE(rows.emplace(key, row).second)
            << "Duplicate row for " << key.first << " queue " << key.second;
      }
    }
    return rows;
  }
};

TEST_F(ShowInterfaceCountersQueueTest, CountersMatchHwPortStats) {
  auto portName = getRandomInterfacePortName();
  XLOG(INFO) << "Checking per-queue counters of " << portName;

  const auto before = getHwPortStats();
  auto rows =
      showQueueCounters({"show", "interface", portName, "counters", "queue"});
  const auto after = getHwPortStats();

  // The agent may have no stats for the port, e.g. on fake SAI or before its
  // first stats collection. The command must then report every configured
  // queue with all counters n/a, which the checks below cover.
  static const HwPortStats kNoStats;
  const auto statsBeforeIt = before.find(portName);
  const auto statsAfterIt = after.find(portName);
  const auto& statsBefore =
      (statsBeforeIt == before.end()) ? kNoStats : statsBeforeIt->second;
  const auto& statsAfter =
      (statsAfterIt == after.end()) ? kNoStats : statsAfterIt->second;
  if (statsAfterIt == after.end()) {
    XLOG(INFO) << "No hw stats for " << portName
               << ", expecting every counter to be n/a";
  }

  // Expected queues: everything configured plus everything counted.
  std::map<int, std::string> expectedNames;
  for (const auto& [portId, portInfo] : getAllPortInfo()) {
    if (*portInfo.name() != portName) {
      continue;
    }
    for (const auto& queue : *portInfo.portQueues()) {
      expectedNames.emplace(*queue.id(), *queue.name());
    }
  }
  std::set<int> expectedQueues;
  for (const auto& [queueId, name] : expectedNames) {
    expectedQueues.insert(queueId);
  }
  for (const auto& [queueId, value] : *statsAfter.queueOutPackets_()) {
    expectedQueues.insert(queueId);
  }
  ASSERT_FALSE(expectedQueues.empty()) << portName << " has no queues";

  std::set<int> reportedQueues;
  for (const auto& [key, row] : rows) {
    EXPECT_EQ(key.first, portName) << "Row for a port that was not queried";
    reportedQueues.insert(key.second);
  }
  EXPECT_EQ(reportedQueues, expectedQueues);

  using QueueCounterGetter =
      std::function<const std::map<int16_t, int64_t>&(const HwPortStats&)>;
  const std::vector<std::pair<const char*, QueueCounterGetter>> counters = {
      {"outPackets",
       [](const HwPortStats& s) -> const std::map<int16_t, int64_t>& {
         return *s.queueOutPackets_();
       }},
      {"outBytes",
       [](const HwPortStats& s) -> const std::map<int16_t, int64_t>& {
         return *s.queueOutBytes_();
       }},
      {"outDiscardPackets",
       [](const HwPortStats& s) -> const std::map<int16_t, int64_t>& {
         return *s.queueOutDiscardPackets_();
       }},
      {"outDiscardBytes",
       [](const HwPortStats& s) -> const std::map<int16_t, int64_t>& {
         return *s.queueOutDiscardBytes_();
       }},
  };

  for (const auto queueId : expectedQueues) {
    auto it = rows.find({portName, queueId});
    ASSERT_NE(it, rows.end()) << portName << " queue " << queueId;
    const auto& row = it->second;

    auto nameIt = expectedNames.find(queueId);
    EXPECT_EQ(
        row["queueName"].asString(),
        nameIt == expectedNames.end() ? "" : nameIt->second)
        << portName << " queue " << queueId;

    for (const auto& [field, getter] : counters) {
      const auto& mapBefore = getter(statsBefore);
      const auto& mapAfter = getter(statsAfter);
      const auto cliValue = row[field].asInt();
      auto beforeIt = mapBefore.find(static_cast<int16_t>(queueId));
      auto afterIt = mapAfter.find(static_cast<int16_t>(queueId));
      if ((beforeIt == mapBefore.end()) || (afterIt == mapAfter.end()) ||
          (beforeIt->second < 0)) {
        // Not counted by this hardware: the command must not invent a value.
        if (afterIt == mapAfter.end()) {
          EXPECT_LT(cliValue, 0)
              << portName << " queue " << queueId << " " << field;
        }
        continue;
      }
      EXPECT_LE(beforeIt->second, cliValue)
          << portName << " queue " << queueId << " " << field;
      EXPECT_LE(cliValue, afterIt->second)
          << portName << " queue " << queueId << " " << field;
    }
  }
}

TEST_F(ShowInterfaceCountersQueueTest, FilterByQueueId) {
  // --filter is a global option and must precede the subcommand.
  auto rows = showQueueCounters(
      {"--filter", "queueId == 0", "show", "interface", "counters", "queue"});
  ASSERT_FALSE(rows.empty()) << "No port reported queue 0";

  std::set<std::string> filteredPorts;
  for (const auto& [key, row] : rows) {
    EXPECT_EQ(key.second, 0)
        << "Filter let through " << key.first << " queue " << key.second;
    filteredPorts.insert(key.first);
  }

  std::set<std::string> portsWithQueue0;
  for (const auto& [key, row] :
       showQueueCounters({"show", "interface", "counters", "queue"})) {
    if (key.second == 0) {
      portsWithQueue0.insert(key.first);
    }
  }
  EXPECT_EQ(filteredPorts, portsWithQueue0);

  auto badValue = runCli(
      {"--filter",
       "queueId == notANumber",
       "show",
       "interface",
       "counters",
       "queue"});
  EXPECT_NE(badValue.exitCode, 0) << "Invalid filter value was not rejected";
}
