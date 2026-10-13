// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for QoS policy map CLI commands:
 *   fboss2-dev config qos policy <name> map ...
 *
 * This test creates a sample QoS policy configuring:
 * - DOT1P/PCP maps (both ingress and egress)
 * - trafficClassToQueueId mappings
 *
 * The test:
 * 1. Creates a test QoS policy
 * 2. Configures pcpMaps similar to sample config
 * 3. Configures tc-to-queue mappings
 * 4. Commits the config
 * 5. Verifies the config was applied by querying the agent's running config
 * 6. Cleans up by removing the test policy
 *
 * Requirements:
 * - FBOSS agent must be running with valid configuration
 * - The test must be run as root (or with appropriate permissions)
 */

#include <fmt/format.h>
#include <folly/json/dynamic.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

namespace {
// Generate a unique policy name using a timestamp to avoid conflicts with
// stale data from previous test runs
std::string generateTestPolicyName() {
  auto now = std::chrono::system_clock::now();
  auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now.time_since_epoch())
                     .count();
  return fmt::format("cli_e2e_test_qos_policy_{}", epochMs);
}
} // namespace

class ConfigQosPolicyMapTest : public Fboss2IntegrationTest {
 protected:
  // Policy name is generated once per test instance
  std::string testPolicyName_ = generateTestPolicyName();

  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    XLOG(INFO) << "Using test policy name: " << testPolicyName_;
    // Clean up any existing test policy from a previous failed run
    cleanupTestPolicy();
  }

  void TearDown() override {
    // Clean up test policy
    cleanupTestPolicy();
    Fboss2IntegrationTest::TearDown();
  }

  void cleanupTestPolicy() {
    // NOTE: We don't have a 'delete' command for QoS policies yet.
    // For now, just discard any pending session. The test policy may
    // remain in the config after a successful run, but that's OK for
    // this test - it uses a unique name so won't conflict with previous runs.
    discardSession();
  }

  /**
   * Configure a list map (DSCP/EXP/DOT1P) with 4-token syntax:
   * <type1> <value1> <type2> <value2>
   */
  void configureMap(
      const std::string& mapType1,
      int value1,
      const std::string& mapType2,
      int value2) {
    auto result = runCli(
        {"config",
         "qos",
         "policy",
         testPolicyName_,
         "map",
         mapType1,
         std::to_string(value1),
         mapType2,
         std::to_string(value2)});
    // Log the command output for debugging (use DBG1 to reduce noise)
    XLOG(DBG1) << "Command: " << mapType1 << " " << value1 << " " << mapType2
               << " " << value2;
    XLOG(DBG1) << "stdout: " << result.stdout;
    if (!result.stderr.empty()) {
      XLOG(DBG1) << "stderr: " << result.stderr;
    }
    ASSERT_EQ(result.exitCode, 0)
        << "Failed to set map " << mapType1 << " " << value1 << " " << mapType2
        << " " << value2 << ": " << result.stderr;
  }

  /**
   * Configure a simple map (tc-to-queue, pfc-pri-to-queue, etc) with 3-token
   * syntax: <map-type> <key> <value>
   */
  void configureSimpleMap(const std::string& mapType, int key, int value) {
    auto result = runCli(
        {"config",
         "qos",
         "policy",
         testPolicyName_,
         "map",
         mapType,
         std::to_string(key),
         std::to_string(value)});
    // Log the command output for debugging (use DBG1 to reduce noise)
    XLOG(DBG1) << "Command: " << mapType << " " << key << " " << value;
    XLOG(DBG1) << "stdout: " << result.stdout;
    if (!result.stderr.empty()) {
      XLOG(DBG1) << "stderr: " << result.stderr;
    }
    ASSERT_EQ(result.exitCode, 0)
        << "Failed to set simple map " << mapType << " " << key << " " << value
        << ": " << result.stderr;
  }

  /**
   * Find a QoS policy by name in the running config.
   * Returns nullptr if not found.
   */
  const folly::dynamic* findQosPolicy(
      const folly::dynamic& config,
      const std::string& policyName) const {
    if (!config.isObject() || !config.count("sw")) {
      return nullptr;
    }
    const auto& sw = config["sw"];
    if (!sw.isObject() || !sw.count("qosPolicies")) {
      return nullptr;
    }
    const auto& policies = sw["qosPolicies"];
    if (!policies.isArray()) {
      return nullptr;
    }
    for (const auto& policy : policies) {
      if (policy.isObject() && policy.count("name") &&
          policy["name"].asString() == policyName) {
        return &policy;
      }
    }
    return nullptr;
  }

  /**
   * Verify that a pcpMap entry has the expected values.
   */
  void verifyPcpMapEntry(
      const folly::dynamic& pcpMaps,
      int16_t trafficClass,
      const std::vector<int8_t>& expectedIngress,
      std::optional<int8_t> expectedEgress) const {
    // Find the entry with the given traffic class
    const folly::dynamic* entry = nullptr;
    for (const auto& e : pcpMaps) {
      if (e.isObject() && e.count("internalTrafficClass") &&
          e["internalTrafficClass"].asInt() == trafficClass) {
        entry = &e;
        break;
      }
    }
    ASSERT_NE(entry, nullptr)
        << "pcpMap entry for TC " << trafficClass << " not found";

    // Verify ingress list
    ASSERT_TRUE(entry->count("fromPcpToTrafficClass"))
        << "fromPcpToTrafficClass missing for TC " << trafficClass;
    const auto& ingressList = (*entry)["fromPcpToTrafficClass"];
    ASSERT_TRUE(ingressList.isArray());
    ASSERT_EQ(ingressList.size(), expectedIngress.size())
        << "Wrong ingress list size for TC " << trafficClass;
    for (const auto& expectedVal : expectedIngress) {
      // The ingress list may not be in order, so check if value exists
      bool found = false;
      for (const auto& v : ingressList) {
        if (v.asInt() == expectedVal) {
          found = true;
          break;
        }
      }
      EXPECT_TRUE(found) << "Expected ingress value "
                         << static_cast<int>(expectedVal)
                         << " not found for TC " << trafficClass;
    }

    // Verify egress value
    if (expectedEgress.has_value()) {
      ASSERT_TRUE(entry->count("fromTrafficClassToPcp"))
          << "fromTrafficClassToPcp missing for TC " << trafficClass;
      EXPECT_EQ((*entry)["fromTrafficClassToPcp"].asInt(), *expectedEgress)
          << "Wrong egress value for TC " << trafficClass;
    }
  }
};

TEST_F(ConfigQosPolicyMapTest, CreateSamplePolicy) {
  XLOG(INFO) << "========================================";
  XLOG(INFO) << "ConfigQosPolicyMapTest::CreateSamplePolicy";
  XLOG(INFO) << "========================================";

  // Step 1: Configure pcpMaps with a sample configuration:
  // - TC 0: pcp 0,2 -> TC 0, TC 0 -> pcp 0
  // - TC 1: pcp 3 -> TC 1, TC 1 -> pcp 3
  // - TC 2: pcp 1 -> TC 2, TC 2 -> pcp 1
  // - TC 4: pcp 4,5 -> TC 4, TC 4 -> pcp 4
  // - TC 6: pcp 6 -> TC 6, TC 6 -> pcp 6
  // - TC 7: pcp 7 -> TC 7, TC 7 -> pcp 7

  XLOG(INFO) << "[Step 1] Configuring DOT1P/PCP maps...";

  // TC 0: ingress pcp 0,2 -> TC 0
  configureMap("dot1p", 0, "traffic-class", 0);
  configureMap("dot1p", 2, "traffic-class", 0);
  // TC 0: egress TC 0 -> pcp 0
  configureMap("traffic-class", 0, "dot1p", 0);

  // TC 1: ingress pcp 3 -> TC 1
  configureMap("dot1p", 3, "traffic-class", 1);
  // TC 1: egress TC 1 -> pcp 3
  configureMap("traffic-class", 1, "dot1p", 3);

  // TC 2: ingress pcp 1 -> TC 2
  configureMap("dot1p", 1, "traffic-class", 2);
  // TC 2: egress TC 2 -> pcp 1
  configureMap("traffic-class", 2, "dot1p", 1);

  // TC 4: ingress pcp 4,5 -> TC 4
  configureMap("dot1p", 4, "traffic-class", 4);
  configureMap("dot1p", 5, "traffic-class", 4);
  // TC 4: egress TC 4 -> pcp 4
  configureMap("traffic-class", 4, "dot1p", 4);

  // TC 6: ingress pcp 6 -> TC 6
  configureMap("dot1p", 6, "traffic-class", 6);
  // TC 6: egress TC 6 -> pcp 6
  configureMap("traffic-class", 6, "dot1p", 6);

  // TC 7: ingress pcp 7 -> TC 7
  configureMap("dot1p", 7, "traffic-class", 7);
  // TC 7: egress TC 7 -> pcp 7
  configureMap("traffic-class", 7, "dot1p", 7);

  XLOG(INFO) << "  DOT1P maps configured";

  // Step 2: Configure trafficClassToQueueId mappings using tc-to-queue syntax
  XLOG(INFO) << "[Step 2] Configuring traffic-class-to-queue mappings...";
  configureSimpleMap("tc-to-queue", 0, 0);
  configureSimpleMap("tc-to-queue", 1, 1);
  configureSimpleMap("tc-to-queue", 2, 2);
  configureSimpleMap("tc-to-queue", 4, 4);
  configureSimpleMap("tc-to-queue", 6, 6);
  configureSimpleMap("tc-to-queue", 7, 7);
  XLOG(INFO) << "  TC-to-queue mappings configured";

  // Step 3: Commit the config
  XLOG(INFO) << "[Step 3] Committing config...";
  commitConfig();
  XLOG(INFO) << "  Config committed";

  // Step 4: Verify config by querying the agent's running config
  XLOG(INFO) << "[Step 4] Verifying config was applied...";

  auto config = getRunningConfig();
  XLOG(INFO) << "  Got running config from agent";

  // Find the test policy
  const auto* policy = findQosPolicy(config, testPolicyName_);
  ASSERT_NE(policy, nullptr)
      << "Test policy '" << testPolicyName_ << "' not found in running config";
  XLOG(INFO) << "  Found test policy in running config";

  // Verify qosMap exists
  ASSERT_TRUE(policy->count("qosMap")) << "qosMap not found in test policy";
  const auto& qosMap = (*policy)["qosMap"];

  // Verify pcpMaps
  ASSERT_TRUE(qosMap.count("pcpMaps")) << "pcpMaps not found in qosMap";
  const auto& pcpMaps = qosMap["pcpMaps"];
  ASSERT_TRUE(pcpMaps.isArray()) << "pcpMaps is not an array";
  ASSERT_EQ(pcpMaps.size(), 6) << "Expected 6 pcpMap entries";
  XLOG(INFO) << "  Verified pcpMaps has 6 entries";

  // Verify each pcpMap entry
  // TC 0: ingress pcp 0,2 -> TC 0, egress TC 0 -> pcp 0
  verifyPcpMapEntry(pcpMaps, 0, {0, 2}, 0);
  // TC 1: ingress pcp 3 -> TC 1, egress TC 1 -> pcp 3
  verifyPcpMapEntry(pcpMaps, 1, {3}, 3);
  // TC 2: ingress pcp 1 -> TC 2, egress TC 2 -> pcp 1
  verifyPcpMapEntry(pcpMaps, 2, {1}, 1);
  // TC 4: ingress pcp 4,5 -> TC 4, egress TC 4 -> pcp 4
  verifyPcpMapEntry(pcpMaps, 4, {4, 5}, 4);
  // TC 6: ingress pcp 6 -> TC 6, egress TC 6 -> pcp 6
  verifyPcpMapEntry(pcpMaps, 6, {6}, 6);
  // TC 7: ingress pcp 7 -> TC 7, egress TC 7 -> pcp 7
  verifyPcpMapEntry(pcpMaps, 7, {7}, 7);
  XLOG(INFO) << "  All pcpMap entries verified";

  // Verify trafficClassToQueueId
  ASSERT_TRUE(qosMap.count("trafficClassToQueueId"))
      << "trafficClassToQueueId not found in qosMap";
  const auto& tcToQueue = qosMap["trafficClassToQueueId"];
  ASSERT_TRUE(tcToQueue.isObject()) << "trafficClassToQueueId is not an object";

  // Check expected TC-to-queue mappings
  // The JSON keys are strings (e.g. "0", "1", etc.)
  std::map<std::string, int> expectedMappings = {
      {"0", 0}, {"1", 1}, {"2", 2}, {"4", 4}, {"6", 6}, {"7", 7}};
  for (const auto& [tc, queue] : expectedMappings) {
    ASSERT_TRUE(tcToQueue.count(tc))
        << "TC " << tc << " not found in trafficClassToQueueId";
    EXPECT_EQ(tcToQueue[tc].asInt(), queue)
        << "TC " << tc << " has wrong queue mapping";
  }
  XLOG(INFO) << "  trafficClassToQueueId verified";

  XLOG(INFO) << "TEST PASSED";
}

TEST_F(ConfigQosPolicyMapTest, IdentityMappingsAcrossAllMapTypes) {
  XLOG(INFO) << "========================================";
  XLOG(INFO) << "ConfigQosPolicyMapTest::IdentityMappingsAcrossAllMapTypes";
  XLOG(INFO) << "========================================";

  // Configure identity mapping 0->0, 1->1, ..., 7->7 for all four simple-map
  // types. Mirrors the coverage of the Python test_config_qos.py test.
  constexpr int kMaxIdx = 8;
  const std::vector<std::string> kMapTypes = {
      "tc-to-queue", "pfc-pri-to-queue", "tc-to-pg", "pfc-pri-to-pg"};

  XLOG(INFO) << "[Step 1] Configuring identity maps...";
  for (const auto& mapType : kMapTypes) {
    for (int i = 0; i < kMaxIdx; ++i) {
      configureSimpleMap(mapType, i, i);
    }
  }

  XLOG(INFO) << "[Step 2] Committing...";
  commitConfig();

  XLOG(INFO) << "[Step 3] Verifying running config...";
  auto config = getRunningConfig();
  const auto* policy = findQosPolicy(config, testPolicyName_);
  ASSERT_NE(policy, nullptr)
      << "Test policy '" << testPolicyName_ << "' not found in running config";
  ASSERT_TRUE(policy->count("qosMap"));
  const auto& qosMap = (*policy)["qosMap"];

  auto expectIdentityObject = [&](const std::string& field) {
    ASSERT_TRUE(qosMap.count(field)) << field << " missing in qosMap";
    const auto& m = qosMap[field];
    ASSERT_TRUE(m.isObject()) << field << " is not an object";
    for (int i = 0; i < kMaxIdx; ++i) {
      auto key = std::to_string(i);
      ASSERT_TRUE(m.count(key)) << field << " missing key " << key;
      EXPECT_EQ(m[key].asInt(), i) << field << "[" << key << "]";
    }
  };

  // All four maps serialize as {traffic-class: value} objects in running
  // config.
  expectIdentityObject("trafficClassToQueueId");
  expectIdentityObject("pfcPriorityToQueueId");
  expectIdentityObject("trafficClassToPgId");
  expectIdentityObject("pfcPriorityToPgId");

  XLOG(INFO) << "TEST PASSED";
}

TEST_F(ConfigQosPolicyMapTest, DscpMoveBetweenTrafficClasses) {
  XLOG(INFO) << "========================================";
  XLOG(INFO) << "ConfigQosPolicyMapTest::DscpMoveBetweenTrafficClasses";
  XLOG(INFO) << "========================================";

  // A DSCP classifies to exactly one traffic class. Re-mapping a
  // codepoint that another class already carries must move it, not leave a
  // copy behind in the old class. Mirrors the field repro: 9 lives in TC 0,
  // is moved to TC 2, then on to TC 4.
  auto ownersOf = [this](int dscp) {
    std::vector<int> owners;
    auto config = getRunningConfig();
    const auto* policy = findQosPolicy(config, testPolicyName_);
    if (policy == nullptr || !policy->count("qosMap") ||
        !(*policy)["qosMap"].count("dscpMaps")) {
      return owners;
    }
    for (const auto& e : (*policy)["qosMap"]["dscpMaps"]) {
      if (!e.count("fromDscpToTrafficClass")) {
        continue;
      }
      for (const auto& v : e["fromDscpToTrafficClass"]) {
        if (v.asInt() == dscp) {
          owners.push_back(e["internalTrafficClass"].asInt());
        }
      }
    }
    return owners;
  };

  XLOG(INFO) << "[Step 1] Seeding DSCP 8, 9 under TC 0 and committing";
  configureMap("dscp", 8, "traffic-class", 0);
  configureMap("dscp", 9, "traffic-class", 0);
  commitConfig();
  ASSERT_EQ(ownersOf(9), std::vector<int>{0});

  XLOG(INFO) << "[Step 2] Moving DSCP 9 to TC 2";
  configureMap("dscp", 9, "traffic-class", 2);
  commitConfig();
  EXPECT_EQ(ownersOf(9), std::vector<int>{2})
      << "DSCP 9 must be owned by TC 2 only";
  EXPECT_EQ(ownersOf(8), std::vector<int>{0}) << "DSCP 8 must stay in TC 0";

  XLOG(INFO) << "[Step 3] Moving DSCP 9 again, to TC 4";
  configureMap("dscp", 9, "traffic-class", 4);
  commitConfig();
  EXPECT_EQ(ownersOf(9), std::vector<int>{4})
      << "DSCP 9 must be owned by TC 4 only";

  auto config = getRunningConfig();
  const auto* policy = findQosPolicy(config, testPolicyName_);
  ASSERT_NE(policy, nullptr);
  for (const auto& e : (*policy)["qosMap"]["dscpMaps"]) {
    EXPECT_NE(e["internalTrafficClass"].asInt(), 2)
        << "TC 2 held only the moved codepoint and must have been dropped";
  }

  XLOG(INFO) << "TEST PASSED";
}
