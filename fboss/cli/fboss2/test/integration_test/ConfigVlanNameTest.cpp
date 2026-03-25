// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for the 'fboss2-dev config vlan <id> name <value>' command.
 *
 * Tests:
 *
 * 1. SetNameOnExistingVlan
 *    - Finds the first eth interface (and its VLAN) from the running config
 *    - Records the current VLAN name from the session JSON
 *    - Sets a new name, commits, and verifies the agent accepts it
 *    - Restores the original name
 *
 * 2. AutoCreateVlanAndSetName
 *    - Ensures VLAN 3100 is absent (cleanup from prior runs)
 *    - Runs 'config vlan 3100 name TestAutoCreateVlan'
 *    - Verifies "Created VLAN 3100" is printed
 *    - Verifies the name attribute is set to "TestAutoCreateVlan"
 *    - Commits
 *    - Cleans up by removing VLAN 3100 from the config
 *
 * Requirements:
 * - FBOSS agent must be running with a valid configuration
 * - At least one eth interface with VLAN > 1 must be present
 * - The test must be run as root (or with appropriate permissions)
 */

#include <fmt/format.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

namespace fs = std::filesystem;

using namespace facebook::fboss;

class ConfigVlanNameTest : public Fboss2IntegrationTest {
 protected:
  // VLAN ID used for the auto-create test; cleaned up before/after each run.
  // Must be in range 3000-3152 so that getTableIdForNpu() maps it to a
  // Linux routing table ID in [1, 253] (3000+N → 101+N; max safe N = 152).
  static constexpr int kNewVlanId = 3100;

  /**
   * Initialize the session with the running config (via a benign idempotent
   * command) and return the name of the given VLAN from the session JSON.
   */
  std::string getVlanName(int vlanId) {
    // Initialize session from running config
    runCli(
        {"config",
         "vlan",
         std::to_string(vlanId),
         "static-mac",
         "delete",
         "00:00:00:00:00:00"});

    const char* home = std::getenv("HOME");
    EXPECT_NE(home, nullptr);
    fs::path sessionConfig = fs::path(home) / ".fboss2" / "agent.conf";
    EXPECT_TRUE(fs::exists(sessionConfig));

    std::string content;
    {
      std::ifstream ifs(sessionConfig);
      EXPECT_TRUE(ifs.is_open());
      content.assign(
          std::istreambuf_iterator<char>(ifs),
          std::istreambuf_iterator<char>{});
    }

    auto cfg = folly::parseJson(content);
    for (const auto& vlan : cfg["sw"]["vlans"]) {
      if (vlan.count("id") && vlan["id"].asInt() == vlanId) {
        return vlan["name"].asString();
      }
    }
    throw std::runtime_error(
        fmt::format("VLAN {} not found in running config", vlanId));
  }

  void setVlanName(int vlanId, const std::string& name) {
    auto result =
        runCli({"config", "vlan", std::to_string(vlanId), "name", name});
    ASSERT_EQ(result.exitCode, 0)
        << "Failed to set VLAN name: " << result.stderr;
    commitConfig();
  }

  /**
   * Remove kNewVlanId from the running config if present, for idempotency.
   * Uses the same JSON-edit strategy as
   * ConfigVlanCreateTest::cleanupVlanFromConfig.
   */
  void cleanupNewVlan() {
    XLOG(INFO) << "[Cleanup] Removing VLAN " << kNewVlanId
               << " from config (if present)...";

    // Initialize session from running config via a benign idempotent command
    runCli(
        {"config",
         "vlan",
         std::to_string(kNewVlanId),
         "static-mac",
         "delete",
         "00:00:00:00:00:00"});

    const char* home = std::getenv("HOME");
    ASSERT_NE(home, nullptr);
    fs::path sessionConfig = fs::path(home) / ".fboss2" / "agent.conf";
    ASSERT_TRUE(fs::exists(sessionConfig));

    std::string content;
    {
      std::ifstream ifs(sessionConfig);
      ASSERT_TRUE(ifs.is_open());
      content.assign(
          std::istreambuf_iterator<char>(ifs),
          std::istreambuf_iterator<char>{});
    }

    auto cfg = folly::parseJson(content);
    auto& sw = cfg["sw"];

    bool vlanFound = false;
    if (sw.count("vlans")) {
      for (const auto& v : sw["vlans"]) {
        if (v.count("id") && v["id"].asInt() == kNewVlanId) {
          vlanFound = true;
          break;
        }
      }
    }

    if (!vlanFound) {
      XLOG(INFO) << "[Cleanup]   VLAN " << kNewVlanId
                 << " not present — no cleanup needed.";
      runCli({"config", "session", "clear"});
      return;
    }

    auto filterById =
        [](const folly::dynamic& arr, const std::string& field, int id) {
          folly::dynamic result = folly::dynamic::array;
          for (const auto& item : arr) {
            if (!item.count(field) || item[field].asInt() != id) {
              result.push_back(item);
            }
          }
          return result;
        };

    if (sw.count("vlans")) {
      sw["vlans"] = filterById(sw["vlans"], "id", kNewVlanId);
    }
    if (sw.count("interfaces")) {
      sw["interfaces"] = filterById(sw["interfaces"], "vlanID", kNewVlanId);
    }
    if (sw.count("staticMacAddrs")) {
      sw["staticMacAddrs"] =
          filterById(sw["staticMacAddrs"], "vlanID", kNewVlanId);
    }

    {
      std::ofstream ofs(sessionConfig);
      ASSERT_TRUE(ofs.is_open());
      ofs << folly::toJson(cfg);
    }

    commitConfig();
    XLOG(INFO) << "[Cleanup]   VLAN " << kNewVlanId
               << " removed and committed.";
  }
};

TEST_F(ConfigVlanNameTest, SetNameOnExistingVlan) {
  // Step 1: Find an eth interface and its VLAN
  XLOG(INFO) << "[Step 1] Finding an eth interface...";
  Interface iface = findFirstEthInterface();
  ASSERT_TRUE(iface.vlan.has_value());
  int vlanId = *iface.vlan;
  XLOG(INFO) << "  Using interface " << iface.name << " (VLAN " << vlanId
             << ")";

  // Step 2: Record current VLAN name
  XLOG(INFO) << "[Step 2] Reading current VLAN " << vlanId << " name...";
  std::string originalName = getVlanName(vlanId);
  XLOG(INFO) << "  Current name: " << originalName;
  runCli({"config", "session", "clear"});

  // Step 3: Set a new name
  const std::string testName = "TestVlanName";
  XLOG(INFO) << "[Step 3] Setting VLAN " << vlanId << " name to \"" << testName
             << "\"...";
  auto result =
      runCli({"config", "vlan", std::to_string(vlanId), "name", testName});
  ASSERT_EQ(result.exitCode, 0) << "config vlan name failed: " << result.stderr;
  EXPECT_THAT(
      result.stdout,
      ::testing::HasSubstr(
          fmt::format("Successfully configured VLAN {}", vlanId)));
  // The output is JSON-encoded, so quotes inside the value are escaped as \"
  EXPECT_THAT(
      result.stdout,
      ::testing::HasSubstr(fmt::format("name=\\\"{}\\\"", testName)));
  XLOG(INFO) << "  Output: " << result.stdout;

  // Step 4: Commit
  XLOG(INFO) << "[Step 4] Committing...";
  commitConfig();
  XLOG(INFO) << "  Committed.";

  // Step 5: Restore original name
  XLOG(INFO) << "[Step 5] Restoring original name \"" << originalName
             << "\"...";
  setVlanName(vlanId, originalName);
  XLOG(INFO) << "  Restored. TEST PASSED";
}

TEST_F(ConfigVlanNameTest, AutoCreateVlanAndSetName) {
  // Step 0: Ensure VLAN kNewVlanId is absent (idempotency across runs)
  XLOG(INFO) << "[Step 0] Ensuring VLAN " << kNewVlanId
             << " is absent from running config...";
  cleanupNewVlan();

  // Step 1: Set a name on a non-existent VLAN — should auto-create it
  const std::string testName = "TestAutoCreateVlan";
  XLOG(INFO) << "[Step 1] Setting name on non-existent VLAN " << kNewVlanId
             << "...";
  auto result =
      runCli({"config", "vlan", std::to_string(kNewVlanId), "name", testName});
  ASSERT_EQ(result.exitCode, 0) << "Command failed: " << result.stderr;
  EXPECT_THAT(result.stdout, ::testing::HasSubstr("Created VLAN"))
      << "Expected VLAN auto-creation message";
  EXPECT_THAT(
      result.stdout,
      ::testing::HasSubstr(
          fmt::format("Successfully configured VLAN {}", kNewVlanId)));
  // The output is JSON-encoded, so quotes inside the value are escaped as \"
  EXPECT_THAT(
      result.stdout,
      ::testing::HasSubstr(fmt::format("name=\\\"{}\\\"", testName)));
  XLOG(INFO) << "  Output: " << result.stdout;

  // Step 2: Commit
  XLOG(INFO) << "[Step 2] Committing session...";
  commitConfig();
  XLOG(INFO) << "  Config committed.";

  // Step 3: Clean up — remove VLAN kNewVlanId from the running config
  XLOG(INFO) << "[Step 3] Cleaning up VLAN " << kNewVlanId << "...";
  cleanupNewVlan();
  XLOG(INFO) << "  Cleanup complete. TEST PASSED";
}
