// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * End-to-end test for:
 *   fboss2-dev config interface <name> mirror ingress <m> egress <m>
 *   fboss2-dev delete interface <name> mirror ingress egress
 *
 * Creates two SPAN mirrors with `config mirror`, binds them to a port's
 * ingress and egress, checks the binding in the agent's running config,
 * then unbinds, restores the port's original bindings, and deletes the test
 * mirrors. Along the way it checks that a bind to an undefined mirror is
 * refused, and that `delete mirror` refuses while the port still names the
 * mirror.
 *
 * The test does not enable sampling (`sflow sample-dest mirror`), so the
 * mirrors are plain SPAN sessions.
 *
 * Requirements:
 *   - FBOSS agent is running with a valid configuration
 *   - At least three interface ports
 *   - Test is run as root (or with sudo) on a DUT
 */

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

namespace {

constexpr auto kIngressMirrorBase = "cli_e2e_test_span_in";
constexpr auto kEgressMirrorBase = "cli_e2e_test_span_out";
constexpr auto kUndefinedMirrorBase = "cli_e2e_test_no_such_mirror";

const folly::dynamic* findPort(
    const folly::dynamic& config,
    const std::string& portName) {
  for (const auto& p : config["sw"]["ports"]) {
    if (p.count("name") && p["name"].asString() == portName) {
      return &p;
    }
  }
  return nullptr;
}

// The port's ingressMirror/egressMirror ("ingressMirror" or "egressMirror"),
// or "" if unset.
std::string boundMirror(
    const folly::dynamic& config,
    const std::string& portName,
    const std::string& field) {
  const auto* p = findPort(config, portName);
  return (p != nullptr && p->count(field)) ? (*p)[field].asString() : "";
}

bool hasMirror(const folly::dynamic& config, const std::string& name) {
  if (!config["sw"].count("mirrors")) {
    return false;
  }
  for (const auto& m : config["sw"]["mirrors"]) {
    if (m.count("name") && m["name"].asString() == name) {
      return true;
    }
  }
  return false;
}

std::string pickUnusedMirrorName(
    const folly::dynamic& config,
    const std::string& base,
    const std::vector<std::string>& reserved = {}) {
  for (int suffix = 0;; ++suffix) {
    const std::string candidate =
        suffix == 0 ? base : base + "_" + std::to_string(suffix);
    bool isReserved = false;
    for (const auto& name : reserved) {
      if (candidate == name) {
        isReserved = true;
        break;
      }
    }
    if (!isReserved && !hasMirror(config, candidate)) {
      return candidate;
    }
  }
}

} // namespace

class ConfigInterfaceMirrorTest : public Fboss2IntegrationTest {
 protected:
  void TearDown() override {
    try {
      discardSession();
      const auto config = getRunningConfig();
      bool changed = stageOriginalBindings(config);
      for (const auto& mirror : {ingressMirror_, egressMirror_}) {
        if (!mirror.empty() && hasMirror(config, mirror)) {
          runOk({"delete", "mirror", mirror});
          changed = true;
        }
      }
      if (changed) {
        commitConfig();
        waitForAgentReady();
      }
    } catch (const std::exception&) {
      // Best-effort; do not mask the test result.
    }
    Fboss2IntegrationTest::TearDown();
  }

  void runOk(const std::vector<std::string>& args) {
    const auto r = runCli(args);
    if (r.exitCode != 0) {
      throw std::runtime_error(
          "CLI command failed: stdout=" + r.stdout + " stderr=" + r.stderr);
    }
  }

  void commitAndWait() {
    commitConfig();
    waitForAgentReady();
  }

  // Two eth ports other than `exclude`, to serve as the mirrors' egress ports.
  std::vector<std::string> pickEgressPorts(const std::string& exclude) const {
    std::vector<std::string> ports;
    for (const auto& [name, intf] : getAllInterfaces()) {
      if (name != exclude && name.starts_with("eth")) {
        ports.push_back(name);
        if (ports.size() == 2) {
          break;
        }
      }
    }
    return ports;
  }

  bool stageOriginalBindings(const folly::dynamic& config) {
    if (!originalBindingsCaptured_) {
      return false;
    }

    const auto currentIngress = boundMirror(config, port_, "ingressMirror");
    const auto currentEgress = boundMirror(config, port_, "egressMirror");
    std::vector<std::string> deleteArgs{"delete", "interface", port_, "mirror"};
    if (originalIngressMirror_.empty() && !currentIngress.empty()) {
      deleteArgs.push_back("ingress");
    }
    if (originalEgressMirror_.empty() && !currentEgress.empty()) {
      deleteArgs.push_back("egress");
    }

    bool changed = false;
    if (deleteArgs.size() > 4) {
      runOk(deleteArgs);
      changed = true;
    }

    std::vector<std::string> configArgs{"config", "interface", port_, "mirror"};
    if (!originalIngressMirror_.empty() &&
        currentIngress != originalIngressMirror_) {
      configArgs.insert(configArgs.end(), {"ingress", originalIngressMirror_});
    }
    if (!originalEgressMirror_.empty() &&
        currentEgress != originalEgressMirror_) {
      configArgs.insert(configArgs.end(), {"egress", originalEgressMirror_});
    }
    if (configArgs.size() > 4) {
      runOk(configArgs);
      changed = true;
    }
    return changed;
  }

  std::string port_;
  std::string ingressMirror_;
  std::string egressMirror_;
  std::string undefinedMirror_;
  std::string originalIngressMirror_;
  std::string originalEgressMirror_;
  bool originalBindingsCaptured_{false};
};

TEST_F(ConfigInterfaceMirrorTest, BindThenUnbindIngressAndEgress) {
  port_ = getRandomInterfacePortName();
  const auto initial = getRunningConfig();
  originalIngressMirror_ = boundMirror(initial, port_, "ingressMirror");
  originalEgressMirror_ = boundMirror(initial, port_, "egressMirror");
  originalBindingsCaptured_ = true;
  ingressMirror_ = pickUnusedMirrorName(
      initial,
      kIngressMirrorBase,
      {originalIngressMirror_, originalEgressMirror_});
  egressMirror_ = pickUnusedMirrorName(
      initial,
      kEgressMirrorBase,
      {originalIngressMirror_, originalEgressMirror_, ingressMirror_});
  undefinedMirror_ = pickUnusedMirrorName(
      initial,
      kUndefinedMirrorBase,
      {originalIngressMirror_,
       originalEgressMirror_,
       ingressMirror_,
       egressMirror_});

  const auto egressPorts = pickEgressPorts(port_);
  ASSERT_EQ(egressPorts.size(), 2) << "need two more eth ports";
  XLOG(INFO) << "Source port " << port_ << ", mirror egress ports "
             << egressPorts[0] << ", " << egressPorts[1];

  // 1. Create the two SPAN mirrors.
  XLOG(INFO) << "[Step 1] Create mirrors";
  ASSERT_NO_THROW(runOk(
      {"config", "mirror", ingressMirror_, "egress-port", egressPorts[0]}));
  ASSERT_NO_THROW(runOk(
      {"config", "mirror", egressMirror_, "egress-port", egressPorts[1]}));
  commitAndWait();
  auto config = waitForRunningConfig([&](const folly::dynamic& c) {
    return hasMirror(c, ingressMirror_) && hasMirror(c, egressMirror_);
  });
  ASSERT_TRUE(hasMirror(config, ingressMirror_));
  ASSERT_TRUE(hasMirror(config, egressMirror_));

  // 2. A bind to an undefined mirror is refused and changes nothing.
  XLOG(INFO) << "[Step 2] Expect refusal: undefined mirror";
  auto r = runCli(
      {"config", "interface", port_, "mirror", "ingress", undefinedMirror_});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_NE((r.stdout + r.stderr).find("is not configured"), std::string::npos)
      << r.stdout << r.stderr;

  // 3. Bind ingress and egress in one command.
  XLOG(INFO) << "[Step 3] Bind " << port_;
  ASSERT_NO_THROW(runOk(
      {"config",
       "interface",
       port_,
       "mirror",
       "ingress",
       ingressMirror_,
       "egress",
       egressMirror_}));
  commitAndWait();
  config = waitForRunningConfig([&](const folly::dynamic& c) {
    return boundMirror(c, port_, "ingressMirror") == ingressMirror_ &&
        boundMirror(c, port_, "egressMirror") == egressMirror_;
  });
  EXPECT_EQ(boundMirror(config, port_, "ingressMirror"), ingressMirror_);
  EXPECT_EQ(boundMirror(config, port_, "egressMirror"), egressMirror_);

  // 4. A mirror the port still names can't be deleted.
  XLOG(INFO) << "[Step 4] Expect refusal: mirror still bound";
  r = runCli({"delete", "mirror", ingressMirror_});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_NE(
      (r.stdout + r.stderr).find("still referenced by"), std::string::npos)
      << r.stdout << r.stderr;
  discardSession();

  // 5. Unbind both directions.
  XLOG(INFO) << "[Step 5] Unbind " << port_;
  ASSERT_NO_THROW(
      runOk({"delete", "interface", port_, "mirror", "ingress", "egress"}));
  commitAndWait();
  config = waitForRunningConfig([&](const folly::dynamic& c) {
    return boundMirror(c, port_, "ingressMirror").empty() &&
        boundMirror(c, port_, "egressMirror").empty();
  });
  EXPECT_TRUE(boundMirror(config, port_, "ingressMirror").empty());
  EXPECT_TRUE(boundMirror(config, port_, "egressMirror").empty());

  // 6. Restore the original bindings and delete the test mirrors.
  XLOG(INFO) << "[Step 6] Restore original bindings and delete mirrors";
  ASSERT_NO_THROW(stageOriginalBindings(config));
  ASSERT_NO_THROW(runOk({"delete", "mirror", ingressMirror_}));
  ASSERT_NO_THROW(runOk({"delete", "mirror", egressMirror_}));
  commitAndWait();
  config = waitForRunningConfig([&](const folly::dynamic& c) {
    return boundMirror(c, port_, "ingressMirror") == originalIngressMirror_ &&
        boundMirror(c, port_, "egressMirror") == originalEgressMirror_ &&
        !hasMirror(c, ingressMirror_) && !hasMirror(c, egressMirror_);
  });
  EXPECT_EQ(
      boundMirror(config, port_, "ingressMirror"), originalIngressMirror_);
  EXPECT_EQ(boundMirror(config, port_, "egressMirror"), originalEgressMirror_);
  EXPECT_FALSE(hasMirror(config, ingressMirror_));
  EXPECT_FALSE(hasMirror(config, egressMirror_));
}
