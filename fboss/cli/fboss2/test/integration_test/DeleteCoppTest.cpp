// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for `fboss2-dev delete copp ...` commands.
 *
 * Covers the delete side of control-plane policing config:
 *
 *   - queue <id> — deletes the whole sw.cpuQueues[] entry.
 *   - reason <reason-name> — deletes the
 *     sw.cpuTrafficPolicy.rxReasonToQueueOrderedList[] entry.
 *
 * One end-to-end test per command. Each provisions the state it deletes with
 * the corresponding `config copp` command, commits (HITLESS — no agent
 * restart), deletes, verifies, and removes anything it created, so the tests
 * run on a DUT whose config carries no cpuQueues or cpuTrafficPolicy at all
 * and never disturb a live CoPP policy. Argument validation, the
 * referenced-queue refusal and the missing-target errors are covered by the
 * unit tests.
 */

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <string>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/test/integration_test/CoppTestUtils.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;

class DeleteCoppTest : public Fboss2IntegrationTest {
 protected:
  // A cfg::PacketRxReason the test can safely program and delete.
  struct Reason {
    const char* name;
    int id;
  };

  // Find the cpuQueues entry with the given id, or return a null dynamic. A
  // config with no cpuQueues at all reads as "not found" rather than an error,
  // so the tests still run on a DUT that has never had a CoPP config.
  folly::dynamic findQueue(int id) const {
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    if (!sw.count("cpuQueues") || !sw["cpuQueues"].isArray()) {
      return {nullptr};
    }
    for (const auto& q : sw["cpuQueues"]) {
      if (q.count("id") && q["id"].asInt() == id) {
        return q;
      }
    }
    return {nullptr};
  }

  std::string scratchStreamType() const {
    return scratchCpuQueueStreamType(getRunningConfig());
  }

  std::optional<int> findUnusedQueueId() const {
    return findUnusedCpuQueueId(getRunningConfig());
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

  // Pick a reason the running config does not map, so the test programs and
  // deletes its own mapping instead of touching a live CoPP policy. The
  // candidates are reasons production policies leave unmapped.
  std::optional<Reason> findUnmappedReason() const {
    static constexpr Reason kCandidates[] = {
        {"eapol", static_cast<int>(cfg::PacketRxReason::EAPOL)},
        {"port_mtu_error",
         static_cast<int>(cfg::PacketRxReason::PORT_MTU_ERROR)},
        {"host_miss", static_cast<int>(cfg::PacketRxReason::HOST_MISS)},
        {"samplepacket", static_cast<int>(cfg::PacketRxReason::SAMPLEPACKET)},
        {"ttl_0", static_cast<int>(cfg::PacketRxReason::TTL_0)},
        {"mpls_ttl_1", static_cast<int>(cfg::PacketRxReason::MPLS_TTL_1)},
    };
    for (const auto& candidate : kCandidates) {
      if (!findReasonQueueId(candidate.id).has_value()) {
        return candidate;
      }
    }
    return std::nullopt;
  }
};

// Whole-queue delete on a queue created by the test itself, so no live
// traffic depends on it and the restore is a no-op.
TEST_F(DeleteCoppTest, DeleteWholeCpuQueue) {
  auto unusedId = findUnusedQueueId();
  ASSERT_TRUE(unusedId.has_value())
      << "No unused cpu queue id in [0, 9] to create and delete — every "
         "platform we ship configures far fewer than 10 CPU queues";
  const auto id = std::to_string(*unusedId);

  XLOG(INFO) << "Creating scratch queue " << id;
  auto result = runCli(
      {"config", "copp", "queue", id, "stream-type", scratchStreamType()});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  commitConfig();
  ASSERT_FALSE(findQueue(*unusedId).isNull());

  XLOG(INFO) << "Deleting queue " << id;
  result = runCli({"delete", "copp", "queue", id});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr("Deleted queue " + id));
  commitConfig();

  EXPECT_TRUE(findQueue(*unusedId).isNull());
}

// Program an rxReason mapping the running config does not carry, delete it,
// then drop the scratch queue it pointed at. `config copp reason` creates
// cpuTrafficPolicy and rxReasonToQueueOrderedList when absent, so this runs
// against a config with no CoPP policy at all.
TEST_F(DeleteCoppTest, DeleteReasonMapping) {
  auto reason = findUnmappedReason();
  ASSERT_TRUE(reason.has_value())
      << "Every candidate reason is already mapped; pick one the running "
         "config leaves free";
  auto unusedId = findUnusedQueueId();
  ASSERT_TRUE(unusedId.has_value())
      << "No unused cpu queue id in [0, 9] to point the mapping at";
  const auto queueId = std::to_string(*unusedId);

  XLOG(INFO) << "Mapping reason " << reason->name << " -> scratch queue "
             << queueId;
  auto result = runCli(
      {"config", "copp", "queue", queueId, "stream-type", scratchStreamType()});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  result = runCli({"config", "copp", "reason", reason->name, "queue", queueId});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  commitConfig();
  ASSERT_EQ(findReasonQueueId(reason->id), *unusedId);

  XLOG(INFO) << "Deleting reason " << reason->name;
  result = runCli({"delete", "copp", "reason", reason->name});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  EXPECT_THAT(result.stdout, HasSubstr("Deleted reason"));
  commitConfig();

  EXPECT_FALSE(findReasonQueueId(reason->id).has_value());

  XLOG(INFO) << "Removing scratch queue " << queueId;
  result = runCli({"delete", "copp", "queue", queueId});
  ASSERT_EQ(result.exitCode, 0)
      << "stdout=" << result.stdout << " stderr=" << result.stderr;
  commitConfig();
  EXPECT_TRUE(findQueue(*unusedId).isNull());
}
