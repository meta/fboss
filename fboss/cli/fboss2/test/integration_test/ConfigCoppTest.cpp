// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for `fboss2-dev config copp ...` commands.
 *
 * Covers the two families that mutate control-plane policing state:
 *
 *   - queue <id> [name <string> | rate-limit <kbps|pps> <max> | <attr>
 *     <value> ...] — hitless edits to entries in sw.cpuQueues[]; generic
 *     attributes (scheduling, weight, reserved-bytes, ...) flow through
 *     utils::applyPortQueueConfig.
 *   - reason <reason-name> queue <id> [order <n>] — hitless upsert into
 *     sw.cpuTrafficPolicy.rxReasonToQueueOrderedList[], positioned at <n>
 *     when given.
 *
 * For each attribute the test:
 *   1. Reads the current state from the agent's running config
 *   2. Applies the CLI change
 *   3. Commits the session (HITLESS — no agent restart)
 *   4. Verifies the running config reflects the new value
 *   5. Restores the original value (to leave the DUT unchanged)
 *
 * Requirements:
 *   - FBOSS agent is running with a valid configuration that already contains
 *     cpuQueues and cpuTrafficPolicy entries (true on every platform that
 *     supports the CPU port, which is every fboss DUT we ship).
 */

#include <fmt/format.h>
#include <folly/ScopeGuard.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;

class ConfigCoppTest : public Fboss2IntegrationTest {
 protected:
  folly::dynamic getRunningConfig() const {
    HostInfo hostInfo("localhost");
    auto client =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    std::string configStr;
    client->sync_getRunningConfig(configStr);
    return folly::parseJson(configStr);
  }

  // Find the cpuQueues entry with the given id, or return a null dynamic.
  folly::dynamic findQueue(int id) const {
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    if (!sw.count("cpuQueues") || !sw["cpuQueues"].isArray()) {
      throw std::runtime_error("Running config missing 'sw.cpuQueues' array");
    }
    for (const auto& q : sw["cpuQueues"]) {
      if (q.count("id") && q["id"].asInt() == id) {
        return q;
      }
    }
    return {nullptr};
  }

  // Return the first cpu queue id that exists in the running config.
  int getFirstExistingQueueId() const {
    auto config = getRunningConfig();
    const auto& queues = config["sw"]["cpuQueues"];
    if (!queues.isArray() || queues.empty()) {
      throw std::runtime_error("Running config has no cpuQueues entries");
    }
    return queues[0]["id"].asInt();
  }

  // The running config serializes thrift enums as integers, while the CLI
  // takes their names -- so read one back as the enum and let enumNameSafe
  // produce the token to feed the command.
  static cfg::QueueScheduling schedulingOf(const folly::dynamic& queue) {
    return static_cast<cfg::QueueScheduling>(queue["scheduling"].asInt());
  }

  // Look up the rxReason mapping for a numeric reason id in the running
  // config. Returns std::nullopt if not present.
  std::optional<int> findReasonQueueId(int rxReason) const {
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    if (!sw.count("cpuTrafficPolicy")) {
      return std::nullopt;
    }
    const auto& policy = sw["cpuTrafficPolicy"];
    if (!policy.count("rxReasonToQueueOrderedList")) {
      return std::nullopt;
    }
    for (const auto& entry : policy["rxReasonToQueueOrderedList"]) {
      if (entry["rxReason"].asInt() == rxReason) {
        return static_cast<int>(entry["queueId"].asInt());
      }
    }
    return std::nullopt;
  }

  // The SAI schedulers currently programmed, one 'show hw-object SCHEDULER'
  // line each. The JSON output maps each host to that hw agent's text dump.
  std::set<std::string> saiSchedulers() {
    std::set<std::string> lines;
    auto dump = runCliJson({"show", "hw-object", "SCHEDULER"});
    for (const auto& [host, text] : dump.items()) {
      std::istringstream out(text.asString());
      std::string line;
      while (std::getline(out, line)) {
        if (line.find("SchedulerSaiId(") != std::string::npos) {
          lines.insert(line);
        }
      }
    }
    return lines;
  }

  // Shared body of the rate-limit minimum tests. On a CPU queue that already
  // has a rate, sets 'rate-limit <unit> <min> <max>' with a non-zero minimum
  // (the CLI defaults it to 0), then checks the minimum in the running config
  // and in the queue's SAI scheduler, and restores the queue's original rate.
  //
  // SaiSchedulerManager programs the minimum as the scheduler's
  // MinBandwidthRate: as-is with a packet meter (MeterType 0) for pps, and
  // converted from kbps to bytes per second (x1000/8) with a byte meter
  // (MeterType 1) for kbps.
  void checkCpuQueueRateMinimum(const std::string& unit) {
    const bool pps = unit == "pps";
    const std::string field = pps ? "pktsPerSec" : "kbitsPerSec";
    const std::string otherField = pps ? "kbitsPerSec" : "pktsPerSec";

    // A queue that already carries a rate, so it can be put back exactly.
    int queueId = -1;
    folly::dynamic originalRate = nullptr;
    auto config = getRunningConfig();
    for (const auto& q : config["sw"]["cpuQueues"]) {
      if (q.count("portQueueRate")) {
        queueId = static_cast<int>(q["id"].asInt());
        originalRate = q["portQueueRate"];
        break;
      }
    }
    if (queueId < 0) {
      GTEST_SKIP() << "No CPU queue has a rate-limit to restore afterwards";
    }
    const std::string originalUnit =
        originalRate.count("pktsPerSec") ? "pps" : "kbps";
    const auto& original =
        originalRate[originalUnit == "pps" ? "pktsPerSec" : "kbitsPerSec"];
    // Distinctive values, so the scheduler they produce is not one that
    // already exists.
    const int minimum = pps ? 123 : 456;
    const int maximum = pps ? 1123 : 1456;
    const int saiMin = pps ? minimum : minimum * 1000 / 8;
    const int saiMax = pps ? maximum : maximum * 1000 / 8;
    const std::string saiAttrs = fmt::format(
        "MeterType: {}, MinBandwidthRate: {}, MaxBandwidthRate: {})",
        pps ? 0 : 1,
        saiMin,
        saiMax);

    auto setRate = [&](const std::string& u, int64_t mn, int64_t mx) {
      return runCli(
          {"config",
           "copp",
           "queue",
           std::to_string(queueId),
           "rate-limit",
           u,
           std::to_string(mn),
           std::to_string(mx)});
    };
    // Puts the queue's original rate back. Armed as a guard so a failed
    // check still leaves the DUT as found; runs explicitly in Step 3.
    auto restore = [&]() {
      auto r = setRate(
          originalUnit,
          original["minimum"].asInt(),
          original["maximum"].asInt());
      if (r.exitCode != 0) {
        ADD_FAILURE() << "restore failed: " << r.stderr;
        return;
      }
      commitConfig();
    };
    auto restoreGuard = folly::makeGuard(restore);

    const auto schedulersBefore = saiSchedulers();
    for (const auto& line : schedulersBefore) {
      ASSERT_EQ(line.find(saiAttrs), std::string::npos)
          << "a scheduler with the test's rates already exists: " << line;
    }

    XLOG(INFO) << "[Step 1] queue " << queueId << " rate-limit " << unit << " "
               << minimum << " " << maximum;
    auto result = setRate(unit, minimum, maximum);
    ASSERT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    commitConfig();

    XLOG(INFO) << "[Step 2] Verifying the minimum in config and SAI...";
    auto queue = findQueue(queueId);
    ASSERT_FALSE(queue.isNull());
    ASSERT_TRUE(queue.count("portQueueRate"));
    const auto& rate = queue["portQueueRate"];
    ASSERT_TRUE(rate.count(field)) << field << " missing from portQueueRate";
    EXPECT_EQ(rate[field]["minimum"].asInt(), minimum);
    EXPECT_EQ(rate[field]["maximum"].asInt(), maximum);
    EXPECT_FALSE(rate.count(otherField))
        << otherField << " left set next to " << field;

    std::vector<std::string> matching;
    for (const auto& line : saiSchedulers()) {
      if (line.find(saiAttrs) != std::string::npos) {
        matching.push_back(line);
      }
    }
    ASSERT_EQ(matching.size(), 1)
        << "expected one SAI scheduler with '" << saiAttrs << "'";
    XLOG(INFO) << "SAI scheduler: " << matching[0];

    XLOG(INFO) << "[Step 3] Restoring rate-limit " << originalUnit << " "
               << original["minimum"].asInt() << " "
               << original["maximum"].asInt();
    restoreGuard.dismiss();
    restore();
    EXPECT_EQ(findQueue(queueId)["portQueueRate"], originalRate)
        << "queue rate not restored";
    for (const auto& line : saiSchedulers()) {
      EXPECT_EQ(line.find(saiAttrs), std::string::npos)
          << "test scheduler left after restore: " << line;
    }
  }
};

// queue <id>: when the id already exists in the running config, the
// command must be a no-op. We exercise the code path and verify the queue is
// still present (and unchanged) after commit.
TEST_F(ConfigCoppTest, CpuQueueEnsureExistsIsNoOp) {
  int id = getFirstExistingQueueId();
  XLOG(INFO) << "queue " << id << " (ensure-exists no-op)";

  auto before = findQueue(id);
  ASSERT_FALSE(before.isNull());
  std::string originalName = before.count("name") && before["name"].isString()
      ? before["name"].asString()
      : "";

  auto result = runCli({"config", "copp", "queue", std::to_string(id)});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr(std::to_string(id)));
  commitConfig();

  auto after = findQueue(id);
  ASSERT_FALSE(after.isNull());
  std::string afterName = after.count("name") && after["name"].isString()
      ? after["name"].asString()
      : "";
  EXPECT_EQ(afterName, originalName);
}

TEST_F(ConfigCoppTest, CpuQueueSetName) {
  int id = getFirstExistingQueueId();
  auto before = findQueue(id);
  ASSERT_FALSE(before.isNull());
  std::string originalName = before.count("name") && before["name"].isString()
      ? before["name"].asString()
      : "";
  const std::string newName = "cpuQueue-test-nos6185";
  ASSERT_NE(originalName, newName);

  XLOG(INFO) << "queue " << id << " name " << newName;
  auto result =
      runCli({"config", "copp", "queue", std::to_string(id), "name", newName});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr(newName));
  commitConfig();

  auto after = findQueue(id);
  ASSERT_FALSE(after.isNull());
  ASSERT_TRUE(after["name"].isString());
  EXPECT_EQ(after["name"].asString(), newName);

  XLOG(INFO) << "Restoring name to '" << originalName << "'";
  if (!originalName.empty()) {
    result = runCli(
        {"config", "copp", "queue", std::to_string(id), "name", originalName});
    ASSERT_EQ(result.exitCode, 0) << result.stderr;
    commitConfig();
    EXPECT_EQ(findQueue(id)["name"].asString(), originalName);
  }
}

// Only the two-token <unit> <max> form is exercised here: the CLI has no
// unset, so a test writing a minimum could not restore it.
TEST_F(ConfigCoppTest, CpuQueueSetRateLimitPps) {
  // Low-priority queues (id 0 and 1) are typically pps-rate-limited in
  // production configs — find one that has an existing pps cap to minimize
  // disruption. Fall back to id 0 if none is found.
  int targetId = 0;
  std::optional<int> originalPpsMax;
  auto config = getRunningConfig();
  for (const auto& q : config["sw"]["cpuQueues"]) {
    if (q.count("portQueueRate") && q["portQueueRate"].count("pktsPerSec") &&
        q["portQueueRate"]["pktsPerSec"].count("maximum")) {
      targetId = q["id"].asInt();
      originalPpsMax = q["portQueueRate"]["pktsPerSec"]["maximum"].asInt();
      break;
    }
  }
  XLOG(INFO) << "Using queue id=" << targetId
             << ", originalPpsMax=" << originalPpsMax.value_or(-1);

  const int newPps = 1234;
  XLOG(INFO) << "queue " << targetId << " rate-limit pps " << newPps;
  auto result = runCli(
      {"config",
       "copp",
       "queue",
       std::to_string(targetId),
       "rate-limit",
       "pps",
       std::to_string(newPps)});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  commitConfig();

  auto after = findQueue(targetId);
  ASSERT_FALSE(after.isNull());
  ASSERT_TRUE(after.count("portQueueRate"));
  ASSERT_TRUE(after["portQueueRate"].count("pktsPerSec"));
  EXPECT_EQ(after["portQueueRate"]["pktsPerSec"]["maximum"].asInt(), newPps);

  if (originalPpsMax.has_value()) {
    XLOG(INFO) << "Restoring pps max to " << *originalPpsMax;
    result = runCli(
        {"config",
         "copp",
         "queue",
         std::to_string(targetId),
         "rate-limit",
         "pps",
         std::to_string(*originalPpsMax)});
    ASSERT_EQ(result.exitCode, 0) << result.stderr;
    commitConfig();
    EXPECT_EQ(
        findQueue(targetId)["portQueueRate"]["pktsPerSec"]["maximum"].asInt(),
        *originalPpsMax);
  }
}

// rate-limit <unit> <min> <max>: the minimum (Range.minimum) reaches the
// queue's SAI scheduler as MinBandwidthRate. pps and kbps take different
// paths in the agent (packet meter as-is vs byte meter in bytes per second),
// so each unit has its own test.
TEST_F(ConfigCoppTest, CpuQueueRateLimitMinimumPps) {
  checkCpuQueueRateMinimum("pps");
}

TEST_F(ConfigCoppTest, CpuQueueRateLimitMinimumKbps) {
  checkCpuQueueRateMinimum("kbps");
}

// Reason -> queue mapping: update an existing entry. We pick ARP because
// every production CoPP policy maps ARP, and we can read its current queue
// id to compute a distinct new value and then restore.
TEST_F(ConfigCoppTest, ReasonToQueueUpdate) {
  const int kArpReason = static_cast<int>(cfg::PacketRxReason::ARP);

  auto originalQueueId = findReasonQueueId(kArpReason);
  ASSERT_TRUE(originalQueueId.has_value())
      << "Running config has no arp reason mapping — test needs a populated "
         "rxReasonToQueueOrderedList";
  // Pick a target that differs from the original. If the original is 0, move
  // to 1, otherwise move to 0 — both are valid CPU queue ids on every
  // platform we ship.
  const int newQueueId = (*originalQueueId == 0) ? 1 : 0;

  XLOG(INFO) << "reason arp queue " << newQueueId << " (was "
             << *originalQueueId << ")";
  auto result = runCli(
      {"config", "copp", "reason", "arp", "queue", std::to_string(newQueueId)});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr("ARP"));
  commitConfig();

  auto after = findReasonQueueId(kArpReason);
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(*after, newQueueId);

  XLOG(INFO) << "Restoring arp -> queue " << *originalQueueId;
  result = runCli(
      {"config",
       "copp",
       "reason",
       "arp",
       "queue",
       std::to_string(*originalQueueId)});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  commitConfig();
  EXPECT_EQ(findReasonQueueId(kArpReason), originalQueueId);
}

// Shipped configs may run every CPU queue as strict-priority with no weight
// field at all, so the test cannot assume a starting discipline: it forces WRR
// and sets a weight, then forces SP, then restores whatever was there. A
// weight left behind on a strict-priority queue is ignored by the agent (see
// the PortQueue.weight comment in switch_config.thrift), and the CLI has no
// way to unset it.
TEST_F(ConfigCoppTest, CpuQueueSetSchedulingAndWeight) {
  constexpr auto kWrr = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  constexpr auto kSp = cfg::QueueScheduling::STRICT_PRIORITY;

  int id = getFirstExistingQueueId();
  auto before = findQueue(id);
  ASSERT_FALSE(before.isNull());
  ASSERT_TRUE(before.count("scheduling"));
  const auto originalScheduling = schedulingOf(before);
  const std::optional<int> originalWeight = before.count("weight")
      ? std::make_optional<int>(before["weight"].asInt())
      : std::nullopt;

  const int newWeight = originalWeight.value_or(0) == 7 ? 8 : 7;
  XLOG(INFO) << "queue " << id << " scheduling "
             << apache::thrift::util::enumNameSafe(kWrr) << " weight "
             << newWeight << " (was scheduling "
             << apache::thrift::util::enumNameSafe(originalScheduling)
             << ", weight "
             << (originalWeight.has_value() ? std::to_string(*originalWeight)
                                            : "unset")
             << ")";
  // Both edits ride in one invocation -- the command applies attribute pairs
  // left to right.
  auto result = runCli(
      {"config",
       "copp",
       "queue",
       std::to_string(id),
       "scheduling",
       apache::thrift::util::enumNameSafe(kWrr),
       "weight",
       std::to_string(newWeight)});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  commitConfig();

  auto after = findQueue(id);
  ASSERT_FALSE(after.isNull());
  EXPECT_EQ(schedulingOf(after), kWrr);
  ASSERT_TRUE(after.count("weight"));
  EXPECT_EQ(after["weight"].asInt(), newWeight);

  XLOG(INFO) << "queue " << id << " scheduling "
             << apache::thrift::util::enumNameSafe(kSp);
  result = runCli(
      {"config",
       "copp",
       "queue",
       std::to_string(id),
       "scheduling",
       apache::thrift::util::enumNameSafe(kSp)});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  commitConfig();

  after = findQueue(id);
  ASSERT_FALSE(after.isNull());
  EXPECT_EQ(schedulingOf(after), kSp);

  XLOG(INFO) << "Restoring scheduling "
             << apache::thrift::util::enumNameSafe(originalScheduling)
             << " and weight";
  if (originalWeight.has_value()) {
    result = runCli(
        {"config",
         "copp",
         "queue",
         std::to_string(id),
         "weight",
         std::to_string(*originalWeight)});
    ASSERT_EQ(result.exitCode, 0) << result.stderr;
  }
  result = runCli(
      {"config",
       "copp",
       "queue",
       std::to_string(id),
       "scheduling",
       apache::thrift::util::enumNameSafe(originalScheduling)});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  commitConfig();
  EXPECT_EQ(schedulingOf(findQueue(id)), originalScheduling);
}

// reason ... order <n>: move an existing reason to the front of
// rxReasonToQueueOrderedList and restore its original position. The list is
// position-sensitive, so the test asserts on the index, not just membership.
TEST_F(ConfigCoppTest, ReasonOrderMove) {
  const int kArpReason = static_cast<int>(cfg::PacketRxReason::ARP);

  auto findReasonIndex = [this](int rxReason) -> std::optional<size_t> {
    auto config = getRunningConfig();
    const auto& policy = config["sw"]["cpuTrafficPolicy"];
    if (!policy.count("rxReasonToQueueOrderedList")) {
      return std::nullopt;
    }
    const auto& list = policy["rxReasonToQueueOrderedList"];
    for (size_t i = 0; i < list.size(); ++i) {
      if (list[i]["rxReason"].asInt() == rxReason) {
        return i;
      }
    }
    return std::nullopt;
  };

  auto originalIndex = findReasonIndex(kArpReason);
  ASSERT_TRUE(originalIndex.has_value())
      << "Running config has no arp reason mapping — test needs a populated "
         "rxReasonToQueueOrderedList";
  auto queueId = findReasonQueueId(kArpReason);
  ASSERT_TRUE(queueId.has_value());

  XLOG(INFO) << "reason arp queue " << *queueId << " order 0 (was index "
             << *originalIndex << ")";
  auto result = runCli(
      {"config",
       "copp",
       "reason",
       "arp",
       "queue",
       std::to_string(*queueId),
       "order",
       "0"});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  commitConfig();
  EXPECT_EQ(findReasonIndex(kArpReason), std::make_optional<size_t>(0));

  XLOG(INFO) << "Restoring arp to index " << *originalIndex;
  result = runCli(
      {"config",
       "copp",
       "reason",
       "arp",
       "queue",
       std::to_string(*queueId),
       "order",
       std::to_string(*originalIndex)});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  commitConfig();
  EXPECT_EQ(findReasonIndex(kArpReason), originalIndex);
}
