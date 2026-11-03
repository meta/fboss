/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

/**
 * End-to-end test for:
 *   fboss2-dev config switch admin-distance <client-id> <distance>
 *
 * Reads the current clientIdToAdminDistance map, picks an existing allowed
 * client, changes its distance, verifies the new value round-trips through the
 * agent's running config (and that the other entries survive), then restores
 * the original. The change requires a coldboot because existing routes are not
 * re-stamped when the map changes — only newly programmed routes pick up the
 * new distance.
 *
 * A second test verifies that forbidden client IDs (STATIC_ROUTE,
 * INTERFACE_ROUTE, LINKLOCAL_ROUTE, REMOTE_INTERFACE_ROUTE) are rejected by
 * the CLI before any config is modified.
 */

#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

class ConfigAdminDistanceTest : public Fboss2IntegrationTest {
 protected:
  std::map<int, int> getAdminDistances() const {
    std::map<int, int> result;
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    if (!sw.count("clientIdToAdminDistance")) {
      return result;
    }
    for (const auto& [clientId, distance] :
         sw["clientIdToAdminDistance"].items()) {
      result[folly::to<int>(clientId.asString())] = distance.asInt();
    }
    return result;
  }

  auto runAdminDistance(int clientId, int distance) {
    return runCli(
        {"config",
         "switch",
         "admin-distance",
         std::to_string(clientId),
         std::to_string(distance)});
  }

  void setAdminDistance(int clientId, int distance) {
    auto result = runAdminDistance(clientId, distance);
    ASSERT_EQ(result.exitCode, 0)
        << "admin-distance CLI failed: " << result.stderr;
    commitConfig();
    waitForAgentReady();
  }
};

TEST_F(ConfigAdminDistanceTest, SetAndRestoreAdminDistance) {
  XLOG(INFO) << "[Step 1] Reading current admin distances...";
  auto original = getAdminDistances();
  ASSERT_FALSE(original.empty())
      << "Running config has no clientIdToAdminDistance entries to test with";

  // Skip hardcoded client IDs (1-4) when picking the target.
  static const std::set<int> kForbidden = {1, 2, 3, 4};
  auto it = original.begin();
  while (it != original.end() && kForbidden.count(it->first)) {
    ++it;
  }
  ASSERT_NE(it, original.end())
      << "No allowed client-id found in clientIdToAdminDistance";

  int clientId = it->first;
  int originalDistance = it->second;
  XLOG(INFO) << "  client-id " << clientId << " -> " << originalDistance;

  // Pick a distinct, valid distance in [0, 255].
  int newDistance = (originalDistance == 42) ? 43 : 42;

  XLOG(INFO) << "[Step 2] Setting client-id " << clientId << " to "
             << newDistance << "...";
  setAdminDistance(clientId, newDistance);
  auto updated = getAdminDistances();
  EXPECT_EQ(updated[clientId], newDistance);

  // The other entries must survive untouched.
  for (const auto& [id, distance] : original) {
    if (id != clientId) {
      EXPECT_EQ(updated[id], distance)
          << "client-id " << id << " changed unexpectedly";
    }
  }

  XLOG(INFO) << "[Step 3] Restoring client-id " << clientId << " to "
             << originalDistance << "...";
  setAdminDistance(clientId, originalDistance);
  EXPECT_EQ(getAdminDistances()[clientId], originalDistance);

  XLOG(INFO) << "TEST PASSED";
}

TEST_F(ConfigAdminDistanceTest, SetThenDeleteAdminDistance) {
  auto original = getAdminDistances();

  // Pick a client-id with no entry in the running config, so set + delete
  // returns the config exactly to its original state.
  int clientId = 900;
  while (original.count(clientId)) {
    ++clientId;
  }

  XLOG(INFO) << "[Step 1] Adding admin distance entry for unused client-id "
             << clientId << "...";
  setAdminDistance(clientId, 42);
  EXPECT_EQ(getAdminDistances()[clientId], 42);

  XLOG(INFO) << "[Step 2] delete switch admin-distance " << clientId;
  auto result =
      runCli({"delete", "switch", "admin-distance", std::to_string(clientId)});
  ASSERT_EQ(result.exitCode, 0)
      << "delete admin-distance CLI failed: " << result.stderr;
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 3] Verifying the entry is gone and others survive...";
  auto afterDelete = getAdminDistances();
  EXPECT_FALSE(afterDelete.count(clientId));
  EXPECT_EQ(afterDelete, original);

  XLOG(INFO) << "TEST PASSED";
}

TEST_F(ConfigAdminDistanceTest, ForbiddenClientIdsRejected) {
  // client-ids 1-4 have hardcoded admin distances in the agent; the CLI must
  // reject them before touching config so no coldboot is triggered.
  const std::vector<std::pair<int, std::string>> forbidden = {
      {1, "STATIC_ROUTE"},
      {2, "INTERFACE_ROUTE"},
      {3, "LINKLOCAL_ROUTE"},
      {4, "REMOTE_INTERFACE_ROUTE"},
  };
  for (const auto& [clientId, name] : forbidden) {
    XLOG(INFO) << "  Verifying client-id " << clientId << " (" << name
               << ") is rejected...";
    auto result = runAdminDistance(clientId, 50);
    EXPECT_NE(result.exitCode, 0)
        << "Expected failure for forbidden client-id " << clientId << " ("
        << name << ") but CLI succeeded";

    // delete is symmetric with config: the same ids are rejected.
    auto deleteResult = runCli(
        {"delete", "switch", "admin-distance", std::to_string(clientId)});
    EXPECT_NE(deleteResult.exitCode, 0)
        << "Expected delete failure for forbidden client-id " << clientId
        << " (" << name << ") but CLI succeeded";
  }
  XLOG(INFO) << "TEST PASSED";
}
