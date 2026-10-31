// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for:
 *   fboss2-dev config interface <name> sflow sample-dest <cpu|mirror>
 *   fboss2-dev delete interface <name> sflow sample-dest
 *
 * Picks a port without a sampleDest, sets it (to cpu, then in a second test
 * to mirror), verifies the value round-trips through the agent's running
 * config, deletes it, and verifies the port returns exactly to its original
 * (unset) state.
 *
 * The mirror case also guards the CLI parse: `mirror` is a subcommand of
 * `config interface`, and `sample-dest mirror` must still be read as a value.
 *
 * Requirements:
 *   - FBOSS agent is running with a valid configuration
 *   - Test is run as root (or with sudo) on a DUT
 */

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <string>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

class ConfigSflowSampleDestTest : public Fboss2IntegrationTest {
 protected:
  void TearDown() override {
    // If the test failed after committing the sample-dest but before the
    // delete commit, best-effort clear it so a shared DUT is left clean.
    if (!committedPort_.empty()) {
      try {
        if (sampleDestOf(getRunningConfig(), committedPort_) != -1) {
          runCli(
              {"delete", "interface", committedPort_, "sflow", "sample-dest"});
          commitConfig();
        }
      } catch (const std::exception&) {
        // Best-effort; do not mask the test result.
      }
    }
    Fboss2IntegrationTest::TearDown();
  }

  std::string committedPort_;

  // sampleDest of the named port in the given running config, or -1 if unset.
  static int sampleDestOf(
      const folly::dynamic& config,
      const std::string& portName) {
    for (const auto& p : config["sw"]["ports"]) {
      if (p.count("name") && p["name"].asString() == portName) {
        return p.count("sampleDest") ? p["sampleDest"].asInt() : -1;
      }
    }
    return -1;
  }

  // Name of the first port with no sampleDest configured.
  static std::string pickPortWithoutSampleDest(const folly::dynamic& config) {
    for (const auto& p : config["sw"]["ports"]) {
      if (p.count("name") && !p.count("sampleDest")) {
        return p["name"].asString();
      }
    }
    return "";
  }

  void runCliOkOrDiscard(const std::vector<std::string>& args) {
    auto result = runCli(args);
    if (result.exitCode != 0) {
      discardSession();
    }
    ASSERT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
  }

  // Set sample-dest to `dest` (thrift value `expected`), check the running
  // config, delete it, and check the port is back to unset.
  void setThenDelete(const std::string& dest, int expected) {
    auto initial = getRunningConfig();
    ASSERT_TRUE(initial.isObject() && initial.count("sw"));

    const std::string portName = pickPortWithoutSampleDest(initial);
    ASSERT_FALSE(portName.empty()) << "every port already has a sampleDest";

    XLOG(INFO) << "[Step 1] config interface " << portName
               << " sflow sample-dest " << dest;
    runCliOkOrDiscard(
        {"config", "interface", portName, "sflow", "sample-dest", dest});
    commitConfig();
    committedPort_ = portName;
    waitForAgentReady();
    {
      auto config = waitForRunningConfig([&](const folly::dynamic& c) {
        return sampleDestOf(c, portName) == expected;
      });
      ASSERT_EQ(sampleDestOf(config, portName), expected)
          << "sampleDest " << dest << " (" << expected
          << ") not in running config for " << portName;
    }

    XLOG(INFO) << "[Step 2] delete interface " << portName
               << " sflow sample-dest";
    runCliOkOrDiscard(
        {"delete", "interface", portName, "sflow", "sample-dest"});
    commitConfig();
    waitForAgentReady();

    XLOG(INFO) << "[Step 3] Verify " << portName << " has no sampleDest again";
    auto config = waitForRunningConfig([&](const folly::dynamic& c) {
      return sampleDestOf(c, portName) == -1;
    });
    EXPECT_EQ(sampleDestOf(config, portName), -1)
        << "sampleDest still present on " << portName << " after delete";
  }
};

TEST_F(ConfigSflowSampleDestTest, SetThenDeleteSampleDest) {
  setThenDelete("cpu", 0);
}

TEST_F(ConfigSflowSampleDestTest, SetThenDeleteSampleDestMirror) {
  setThenDelete("mirror", 1);
}
