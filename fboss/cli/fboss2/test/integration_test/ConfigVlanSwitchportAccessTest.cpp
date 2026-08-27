// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end test for:
 *   fboss2-dev config interface <name> switchport access vlan <id>
 *
 * NewVlanAcrossPorts attaches a brand-new VLAN to one port (auto-create
 * path), then the now-existing VLAN to a second port, verifying ingressVlan
 * and the untagged sw.vlanPorts[] membership after each warmboot commit.
 * No manual unwind: the test harness records the config baseline before the
 * suite starts and rolls back to it afterwards (run_test.py suite hooks), so
 * the DUT is left as found even when the test fails mid-way.
 */

#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <optional>
#include <set>
#include <string>
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

class ConfigVlanSwitchportAccessTest : public Fboss2IntegrationTest {
 protected:
  void SetUp() override {
    Fboss2IntegrationTest::SetUp();
    discardSession();
    // Committed (session-view) config: the agent prunes memberless VLANs from
    // its running view but sessions still see them, so pick VLAN IDs here.
    committedSw_ = folly::parseJson(
        apache::thrift::SimpleJSONSerializer::serialize<std::string>(
            ConfigSession::getInstance().getAgentConfig()))["sw"];
  }

  std::optional<int> getIngressVlan(const std::string& portName) const {
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    if (!sw.count("ports")) {
      return std::nullopt;
    }
    for (const auto& port : sw["ports"]) {
      if (port.count("name") && port["name"].asString() == portName) {
        if (port.count("ingressVlan") && !port["ingressVlan"].isNull()) {
          return static_cast<int>(port["ingressVlan"].asInt());
        }
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

  // The port must have exactly one untagged sw.vlanPorts[] membership, in
  // the given VLAN.
  void checkUntaggedVlanPort(const std::string& portName, int vlanId) const {
    auto config = getRunningConfig();
    const auto& sw = config["sw"];
    ASSERT_TRUE(sw.count("ports"));
    int logicalPort = -1;
    for (const auto& port : sw["ports"]) {
      if (port.count("name") && port["name"].asString() == portName) {
        ASSERT_TRUE(port.count("logicalID"));
        logicalPort = static_cast<int>(port["logicalID"].asInt());
        break;
      }
    }
    ASSERT_NE(logicalPort, -1) << "Port " << portName << " not found";
    ASSERT_TRUE(sw.count("vlanPorts")) << "Running config has no sw.vlanPorts";
    int untaggedCount = 0;
    int untaggedVlan = -1;
    for (const auto& vp : sw["vlanPorts"]) {
      if (static_cast<int>(vp["logicalPort"].asInt()) != logicalPort) {
        continue;
      }
      bool tagged = vp.count("emitTags") && vp["emitTags"].asBool();
      if (!tagged) {
        untaggedCount++;
        untaggedVlan = static_cast<int>(vp["vlanID"].asInt());
      }
    }
    EXPECT_EQ(untaggedCount, 1)
        << "Expected exactly one untagged vlanPorts entry for " << portName
        << " (logicalPort " << logicalPort << "), found " << untaggedCount;
    EXPECT_EQ(untaggedVlan, vlanId);
  }

  // First VLAN ID unused by both sw.vlans and sw.interfaces.
  static int findUnusedVlanId(const folly::dynamic& sw) {
    std::set<int> used;
    if (sw.count("vlans")) {
      for (const auto& v : sw["vlans"]) {
        if (v.count("id") && !v["id"].isNull()) {
          used.insert(static_cast<int>(v["id"].asInt()));
        }
      }
    }
    if (sw.count("interfaces")) {
      for (const auto& i : sw["interfaces"]) {
        if (i.count("vlanID") && !i["vlanID"].isNull()) {
          used.insert(static_cast<int>(i["vlanID"].asInt()));
        }
      }
    }
    for (int id = 2; id < 4094; ++id) {
      if (!used.count(id)) {
        return id;
      }
    }
    return -1;
  }

  static bool vlanExists(const folly::dynamic& config, int vlanId) {
    const auto& sw = config["sw"];
    if (!sw.count("vlans")) {
      return false;
    }
    for (const auto& v : sw["vlans"]) {
      if (v.count("id") && v["id"].asInt() == vlanId) {
        return true;
      }
    }
    return false;
  }

  std::string setAccessVlan(const std::string& portName, int vlanId) {
    auto result = runCli(
        {"config",
         "interface",
         portName,
         "switchport",
         "access",
         "vlan",
         std::to_string(vlanId)});
    EXPECT_EQ(result.exitCode, 0)
        << "Failed to set access VLAN " << vlanId << " on " << portName << ": "
        << result.stderr;
    return result.stdout;
  }

  folly::dynamic committedSw_;
};

TEST_F(ConfigVlanSwitchportAccessTest, NewVlanAcrossPorts) {
  XLOG(INFO) << "[Step 1] Picking two ports and an unused VLAN...";
  const std::string port1 = getRandomInterfacePortName();
  std::string port2 = port1;
  for (int attempt = 0; attempt < 20 && port2 == port1; ++attempt) {
    port2 = getRandomInterfacePortName();
  }
  ASSERT_NE(port1, port2) << "Need two distinct INTERFACE_PORTs";

  auto origVlan2 = getIngressVlan(port2);
  ASSERT_TRUE(origVlan2.has_value())
      << "Port " << port2 << " has no ingressVlan in running config";

  // Pick against the committed config, not the running config (see SetUp).
  int testVlan = findUnusedVlanId(committedSw_);
  ASSERT_GT(testVlan, 0) << "No unused VLAN ID available";
  XLOG(INFO) << "  Ports: " << port1 << ", " << port2
             << "; new VLAN: " << testVlan;

  XLOG(INFO) << "[Step 2] Attaching new VLAN " << testVlan << " to " << port1
             << " (auto-create path)...";
  auto output = setAccessVlan(port1, testVlan);
  EXPECT_TRUE(
      output.find("(VLAN " + std::to_string(testVlan) + " created)") !=
      std::string::npos)
      << "Expected auto-create notice in output: " << output;
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 2] Verifying...";
  EXPECT_TRUE(vlanExists(getRunningConfig(), testVlan))
      << "VLAN " << testVlan << " not in running config after commit";
  auto readBack1 = getIngressVlan(port1);
  ASSERT_TRUE(readBack1.has_value());
  EXPECT_EQ(*readBack1, testVlan);
  checkUntaggedVlanPort(port1, testVlan);
  auto readBack2 = getIngressVlan(port2);
  ASSERT_TRUE(readBack2.has_value());
  EXPECT_EQ(*readBack2, *origVlan2);

  XLOG(INFO) << "[Step 3] Attaching now-existing VLAN " << testVlan << " to "
             << port2 << "...";
  output = setAccessVlan(port2, testVlan);
  EXPECT_TRUE(output.find("created") == std::string::npos)
      << "VLAN already exists; no auto-create notice expected: " << output;
  commitConfig();
  waitForAgentReady();

  XLOG(INFO) << "[Step 3] Verifying...";
  readBack1 = getIngressVlan(port1);
  ASSERT_TRUE(readBack1.has_value());
  EXPECT_EQ(*readBack1, testVlan);
  readBack2 = getIngressVlan(port2);
  ASSERT_TRUE(readBack2.has_value());
  EXPECT_EQ(*readBack2, testVlan);
  checkUntaggedVlanPort(port1, testVlan);
  checkUntaggedVlanPort(port2, testVlan);

  XLOG(INFO) << "TEST PASSED (the harness rolls back to the pre-suite config)";
}
