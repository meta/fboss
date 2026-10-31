// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * End-to-end test for:
 *   fboss2-dev config interface <name> mirror ingress <m> egress <m>
 *   fboss2-dev delete interface <name> mirror ingress egress
 *
 * Creates two SPAN mirrors with `config mirror`, binds them to a port's
 * ingress and egress, checks the binding in the agent's running config,
 * then unbinds and deletes the mirrors. Along the way it checks that a bind
 * to an undefined mirror is refused, and that `delete mirror` refuses while
 * the port still names the mirror.
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
#include <string>
#include <vector>
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

namespace {

constexpr auto kIngressMirror = "cli_e2e_test_span_in";
constexpr auto kEgressMirror = "cli_e2e_test_span_out";

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

} // namespace

class ConfigInterfaceMirrorTest : public Fboss2IntegrationTest {
 protected:
  void TearDown() override {
    // Best-effort: leave a shared DUT without the test's bindings and mirrors
    // even if the test failed midway.
    try {
      discardSession();
      auto config = getRunningConfig();
      bool touched = false;
      if (!port_.empty() &&
          (!boundMirror(config, port_, "ingressMirror").empty() ||
           !boundMirror(config, port_, "egressMirror").empty())) {
        runCli({"delete", "interface", port_, "mirror", "ingress", "egress"});
        touched = true;
      }
      for (const auto* m : {kIngressMirror, kEgressMirror}) {
        if (hasMirror(config, m)) {
          runCli({"delete", "mirror", m});
          touched = true;
        }
      }
      if (touched) {
        commitConfig();
        waitForAgentReady();
      }
    } catch (const std::exception&) {
      // Best-effort; do not mask the test result.
    }
    Fboss2IntegrationTest::TearDown();
  }

  void runOk(const std::vector<std::string>& args) {
    auto r = runCli(args);
    ASSERT_EQ(r.exitCode, 0) << "stdout=" << r.stdout << " stderr=" << r.stderr;
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

  std::string port_;
};

TEST_F(ConfigInterfaceMirrorTest, BindThenUnbindIngressAndEgress) {
  port_ = getRandomInterfacePortName();
  const auto egressPorts = pickEgressPorts(port_);
  ASSERT_EQ(egressPorts.size(), 2) << "need two more eth ports";
  XLOG(INFO) << "Source port " << port_ << ", mirror egress ports "
             << egressPorts[0] << ", " << egressPorts[1];

  auto initial = getRunningConfig();
  ASSERT_TRUE(
      boundMirror(initial, port_, "ingressMirror").empty() &&
      boundMirror(initial, port_, "egressMirror").empty())
      << port_ << " already has a mirror binding";

  // 1. Create the two SPAN mirrors.
  XLOG(INFO) << "[Step 1] Create mirrors";
  runOk({"config", "mirror", kIngressMirror, "egress-port", egressPorts[0]});
  runOk({"config", "mirror", kEgressMirror, "egress-port", egressPorts[1]});
  commitAndWait();
  auto config = waitForRunningConfig([](const folly::dynamic& c) {
    return hasMirror(c, kIngressMirror) && hasMirror(c, kEgressMirror);
  });
  ASSERT_TRUE(hasMirror(config, kIngressMirror));
  ASSERT_TRUE(hasMirror(config, kEgressMirror));

  // 2. A bind to an undefined mirror is refused and changes nothing.
  XLOG(INFO) << "[Step 2] Expect refusal: undefined mirror";
  auto r = runCli(
      {"config",
       "interface",
       port_,
       "mirror",
       "ingress",
       "cli_e2e_test_no_such_mirror"});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_NE((r.stdout + r.stderr).find("is not configured"), std::string::npos)
      << r.stdout << r.stderr;

  // 3. Bind ingress and egress in one command.
  XLOG(INFO) << "[Step 3] Bind " << port_;
  runOk(
      {"config",
       "interface",
       port_,
       "mirror",
       "ingress",
       kIngressMirror,
       "egress",
       kEgressMirror});
  commitAndWait();
  config = waitForRunningConfig([&](const folly::dynamic& c) {
    return boundMirror(c, port_, "ingressMirror") == kIngressMirror &&
        boundMirror(c, port_, "egressMirror") == kEgressMirror;
  });
  EXPECT_EQ(boundMirror(config, port_, "ingressMirror"), kIngressMirror);
  EXPECT_EQ(boundMirror(config, port_, "egressMirror"), kEgressMirror);

  // 4. A mirror the port still names can't be deleted.
  XLOG(INFO) << "[Step 4] Expect refusal: mirror still bound";
  r = runCli({"delete", "mirror", kIngressMirror});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_NE(
      (r.stdout + r.stderr).find("still referenced by"), std::string::npos)
      << r.stdout << r.stderr;
  discardSession();

  // 5. Unbind both directions.
  XLOG(INFO) << "[Step 5] Unbind " << port_;
  runOk({"delete", "interface", port_, "mirror", "ingress", "egress"});
  commitAndWait();
  config = waitForRunningConfig([&](const folly::dynamic& c) {
    return boundMirror(c, port_, "ingressMirror").empty() &&
        boundMirror(c, port_, "egressMirror").empty();
  });
  EXPECT_TRUE(boundMirror(config, port_, "ingressMirror").empty());
  EXPECT_TRUE(boundMirror(config, port_, "egressMirror").empty());

  // 6. Delete the mirrors.
  XLOG(INFO) << "[Step 6] Delete mirrors";
  runOk({"delete", "mirror", kIngressMirror});
  runOk({"delete", "mirror", kEgressMirror});
  commitAndWait();
  config = waitForRunningConfig([](const folly::dynamic& c) {
    return !hasMirror(c, kIngressMirror) && !hasMirror(c, kEgressMirror);
  });
  EXPECT_FALSE(hasMirror(config, kIngressMirror));
  EXPECT_FALSE(hasMirror(config, kEgressMirror));
}
