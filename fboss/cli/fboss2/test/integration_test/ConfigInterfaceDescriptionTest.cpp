// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for 'fboss2-dev config interface <name> description <string>'
 *
 * This test:
 * 1. Picks an interface from the running system
 * 2. Gets the current description
 * 3. Sets a new description
 * 4. Verifies the description was set correctly via 'fboss2-dev show interface'
 * 5. Restores the original description
 *
 * Requirements:
 * - FBOSS agent must be running with a valid configuration
 * - The test must be run as root (or with appropriate permissions)
 */

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <string>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

class ConfigInterfaceDescriptionTest : public Fboss2IntegrationTest {
 protected:
  void setInterfaceDescription(
      const std::string& interfaceName,
      const std::string& description) {
    auto result = runCli(
        {"config", "interface", interfaceName, "description", description});
    ASSERT_EQ(result.exitCode, 0)
        << "Failed to set description: " << result.stderr;
    commitConfig();
  }
};

TEST_F(ConfigInterfaceDescriptionTest, SetAndVerifyDescription) {
  // Step 1: Find an interface to test with
  XLOG(INFO) << "[Step 1] Finding an interface to test...";
  Interface interface = getInterfaceInfo(getRandomInterfacePortName());
  XLOG(INFO) << "  Using interface: " << interface.name << " (VLAN: "
             << (interface.vlan.has_value() ? std::to_string(*interface.vlan)
                                            : "none")
             << ")";

  // Step 2: Get the current description
  XLOG(INFO) << "[Step 2] Getting current description...";
  std::string originalDescription = interface.description;
  XLOG(INFO) << "  Current description: '" << originalDescription << "'";

  // Step 3: Set a new description
  std::string testDescription = "CLI_E2E_TEST_DESCRIPTION";
  if (originalDescription == testDescription) {
    testDescription = "CLI_E2E_TEST_DESCRIPTION_ALT";
  }
  XLOG(INFO) << "[Step 3] Setting description to '" << testDescription
             << "'...";
  setInterfaceDescription(interface.name, testDescription);
  XLOG(INFO) << "  Description set to '" << testDescription << "'";

  // Step 4: Poll 'show interface' until the description propagates
  XLOG(INFO) << "[Step 4] Waiting for description to propagate...";
  Interface updatedInterface = waitForInterfaceInfo(
      interface.name, [&testDescription](const auto& info) {
        return info.description == testDescription;
      });
  EXPECT_EQ(updatedInterface.description, testDescription)
      << "Expected description '" << testDescription << "', got '"
      << updatedInterface.description << "'";
  XLOG(INFO) << "  Verified: Description is '" << updatedInterface.description
             << "'";

  // Step 5: Restore original description
  XLOG(INFO) << "[Step 5] Restoring original description ('"
             << originalDescription << "')...";
  setInterfaceDescription(interface.name, originalDescription);
  XLOG(INFO) << "  Restored description to '" << originalDescription << "'";

  // Verify restoration
  Interface restoredInterface = waitForInterfaceInfo(
      interface.name, [&originalDescription](const auto& info) {
        return info.description == originalDescription;
      });
  if (restoredInterface.description != originalDescription) {
    XLOG(WARN) << "  WARNING: Restoration may have failed. Current: '"
               << restoredInterface.description << "'";
  }

  XLOG(INFO) << "TEST PASSED";
}

// Setting an interface description to the value it already has must produce a
// no-op commit: the staged agent.conf is byte-identical to what is running, so
// `config session commit` reports "Nothing to commit" and does NOT reload the
// agent (skip-when-unchanged). Regression test for the agent-side unification.
TEST_F(ConfigInterfaceDescriptionTest, SetSameDescriptionIsNoOpCommit) {
  XLOG(INFO) << "[Step 1] Finding an interface to test...";
  Interface interface = getInterfaceInfo(getRandomInterfacePortName());
  const std::string originalDescription = interface.description;
  XLOG(INFO) << "  Using interface: " << interface.name << " (description: '"
             << originalDescription << "')";

  // Set a deterministic description and commit so cli/agent.conf is in the
  // canonical serialized form.
  std::string testDescription = "CLI_E2E_NOOP_DESCRIPTION";
  XLOG(INFO) << "[Step 2] Setting description to '" << testDescription
             << "' and committing...";
  setInterfaceDescription(interface.name, testDescription);
  waitForInterfaceInfo(interface.name, [&](const auto& info) {
    return info.description == testDescription;
  });

  // Stage the SAME description again, then commit directly so we can inspect
  // the output. The staged config equals what is running -> the commit is a
  // no-op.
  XLOG(INFO) << "[Step 3] Re-setting the SAME description and committing...";
  auto setResult = runCli(
      {"config", "interface", interface.name, "description", testDescription});
  ASSERT_EQ(setResult.exitCode, 0) << setResult.stderr;
  auto commitResult = runCli({"config", "session", "commit"});
  ASSERT_EQ(commitResult.exitCode, 0) << commitResult.stderr;
  XLOG(INFO) << "  Commit output: " << commitResult.stdout;
  EXPECT_NE(commitResult.stdout.find("Nothing to commit"), std::string::npos)
      << "Re-committing an unchanged description must be a no-op, got: "
      << commitResult.stdout;
  EXPECT_EQ(commitResult.stdout.find("reloaded"), std::string::npos)
      << "A no-op commit must not reload the agent, got: "
      << commitResult.stdout;

  // Restore the original description.
  XLOG(INFO) << "[Step 4] Restoring original description ('"
             << originalDescription << "')...";
  setInterfaceDescription(interface.name, originalDescription);
  waitForInterfaceInfo(interface.name, [&](const auto& info) {
    return info.description == originalDescription;
  });

  XLOG(INFO) << "TEST PASSED";
}
