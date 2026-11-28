// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <algorithm>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/delete/interface/CmdDeleteInterface.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdDeleteConfigInterfaceTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteConfigInterfaceTestFixture()
      : CmdConfigTestBase(
            "delete_interface_test_%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "interfaces": [
      {
        "intfID": 1,
        "name": "eth1/1/1",
        "vlanID": 100,
        "routerID": 0,
        "ipAddresses": []
      }
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete interface";
};

// ============================================================================
// InterfaceDeleteConfig Validation Tests
// ============================================================================

// Test valid delete config with single interface
TEST_F(CmdDeleteConfigInterfaceTestFixture, validSingleInterface) {
  setupTestableConfigSession();
  InterfaceDeleteConfig config({"eth1/1/1", "ip-address", "10.0.0.1/24"});
  EXPECT_EQ(config.getInterfaces()[0].name(), "eth1/1/1");
  EXPECT_EQ(config.getAttributes()[0].first, "ip-address");
  EXPECT_EQ(config.getAttributes()[0].second, "10.0.0.1/24");
}

// Test IPv6 address
TEST_F(CmdDeleteConfigInterfaceTestFixture, validIpv6Address) {
  setupTestableConfigSession();
  InterfaceDeleteConfig config({"eth1/1/1", "ipv6-address", "2001:db8::1/64"});
  EXPECT_EQ(config.getInterfaces()[0].name(), "eth1/1/1");
  EXPECT_EQ(config.getAttributes()[0].first, "ipv6-address");
  EXPECT_EQ(config.getAttributes()[0].second, "2001:db8::1/64");
}

// Test case-insensitive attribute names
TEST_F(CmdDeleteConfigInterfaceTestFixture, caseInsensitiveAttribute) {
  setupTestableConfigSession();
  InterfaceDeleteConfig config({"eth1/1/1", "IP-ADDRESS", "10.0.0.1/24"});
  // Attribute should be normalized to lowercase
  EXPECT_EQ(config.getAttributes()[0].first, "ip-address");
}

// Test unknown interface throws (eth1/2/1 is not in the test config)
TEST_F(CmdDeleteConfigInterfaceTestFixture, unknownInterfaceThrows) {
  setupTestableConfigSession();
  EXPECT_THROW(
      InterfaceDeleteConfig(
          {"eth1/1/1", "eth1/2/1", "ip-address", "10.0.0.1/24"}),
      std::invalid_argument);
}

// Test too few arguments throws
TEST_F(CmdDeleteConfigInterfaceTestFixture, tooFewArgumentsThrows) {
  setupTestableConfigSession();
  EXPECT_THROW(
      InterfaceDeleteConfig({"eth1/1/1", "ip-address"}), std::invalid_argument);
}

// Test unknown attribute throws
TEST_F(CmdDeleteConfigInterfaceTestFixture, unknownAttributeThrows) {
  setupTestableConfigSession();
  EXPECT_THROW(
      InterfaceDeleteConfig({"eth1/1/1", "description", "test"}),
      std::invalid_argument);
}

// Test invalid IP address format throws
TEST_F(CmdDeleteConfigInterfaceTestFixture, invalidIpAddressThrows) {
  setupTestableConfigSession();
  EXPECT_THROW(
      InterfaceDeleteConfig({"eth1/1/1", "ip-address", "invalid"}),
      std::exception);
}

// Test IP version mismatch throws during construction
TEST_F(CmdDeleteConfigInterfaceTestFixture, ipVersionMismatchThrows) {
  setupTestableConfigSession();
  EXPECT_THROW(
      (InterfaceDeleteConfig({"eth1/1/1", "ip-address", "2001:db8::1/64"})),
      std::invalid_argument);
}

// ============================================================================
// CmdDeleteConfigInterface::queryClient Tests
// ============================================================================

// Test deleting existing IPv4 address
TEST_F(CmdDeleteConfigInterfaceTestFixture, deleteExistingIpv4Address) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 ip-address 10.0.0.1/24");

  // First add the IP address
  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& intfs = *config.sw()->interfaces();
  for (auto& intf : intfs) {
    if (*intf.name() == "eth1/1/1") {
      intf.ipAddresses()->emplace_back("10.0.0.1/24");
      break;
    }
  }
  session.save<AgentConfigDomain>();

  // Now delete it
  auto cmd = CmdDeleteInterface();
  InterfaceDeleteConfig deleteConfig({"eth1/1/1", "ip-address", "10.0.0.1/24"});
  auto result = cmd.queryClient(localhost(), deleteConfig);

  EXPECT_THAT(result, HasSubstr("Successfully removed"));
  EXPECT_THAT(result, HasSubstr("10.0.0.1/24"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));

  // Verify it was removed
  auto& updatedIntfs = *config.sw()->interfaces();
  for (const auto& intf : updatedIntfs) {
    if (*intf.name() == "eth1/1/1") {
      auto& ips = *intf.ipAddresses();
      EXPECT_EQ(std::find(ips.begin(), ips.end(), "10.0.0.1/24"), ips.end());
    }
  }
}

// Test deleting non-existent IP address (idempotent)
TEST_F(CmdDeleteConfigInterfaceTestFixture, deleteNonExistentIpAddress) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 ip-address 10.0.0.1/24");

  auto cmd = CmdDeleteInterface();
  InterfaceDeleteConfig deleteConfig({"eth1/1/1", "ip-address", "10.0.0.1/24"});
  auto result = cmd.queryClient(localhost(), deleteConfig);

  EXPECT_THAT(result, HasSubstr("not configured"));
}

// ============================================================================
// Whole-port delete (bare `delete interface <port>`, no attributes)
// ============================================================================

// Fixture with a full port -> vlanPort -> vlan -> interface chain so we can
// verify that deleting a port also prunes its dependents.
class CmdDeleteWholeInterfaceTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteWholeInterfaceTestFixture()
      : CmdConfigTestBase(
            "delete_whole_interface_test_%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 100
      },
      {
        "logicalID": 2,
        "name": "eth1/2/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 200
      }
    ],
    "vlanPorts": [
      {"vlanID": 100, "logicalPort": 1, "spanningTreeState": 2, "emitTags": false},
      {"vlanID": 200, "logicalPort": 2, "spanningTreeState": 2, "emitTags": false}
    ],
    "defaultVlan": 4000,
    "vlans": [
      {"id": 100, "name": "vlan100", "routable": true, "intfID": 100},
      {"id": 200, "name": "vlan200", "routable": true, "intfID": 200},
      {"id": 4000, "name": "vlan4000", "routable": false, "intfID": 0}
    ],
    "interfaces": [
      {"intfID": 100, "vlanID": 100, "routerID": 0, "type": 1, "mtu": 9412},
      {"intfID": 200, "vlanID": 200, "routerID": 0, "type": 1, "mtu": 9412}
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete interface";
};

// Bare `delete interface eth1/1/1` removes the port and its vlanPort, the now
// empty vlan, and the vlan's interface, while leaving the other port's chain.
TEST_F(CmdDeleteWholeInterfaceTestFixture, deletesPortAndDependents) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1");
  auto cmd = CmdDeleteInterface();
  InterfaceDeleteConfig deleteConfig({"eth1/1/1"});

  auto result = cmd.queryClient(localhost(), deleteConfig);

  EXPECT_THAT(result, HasSubstr("Deleted interface(s)"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));

  auto& swConfig =
      *ConfigSession::getInstance().config<AgentConfigDomain>().sw();

  const auto& ports = *swConfig.ports();
  EXPECT_EQ(
      std::none_of(
          ports.begin(),
          ports.end(),
          [](const auto& p) { return *p.logicalID() == 1; }),
      true);

  const auto& vlanPorts = *swConfig.vlanPorts();
  EXPECT_EQ(
      std::none_of(
          vlanPorts.begin(),
          vlanPorts.end(),
          [](const auto& vp) { return *vp.logicalPort() == 1; }),
      true);

  const auto& vlans = *swConfig.vlans();
  EXPECT_EQ(
      std::none_of(
          vlans.begin(),
          vlans.end(),
          [](const auto& v) { return *v.id() == 100; }),
      true);

  const auto& interfaces = *swConfig.interfaces();
  EXPECT_EQ(
      std::none_of(
          interfaces.begin(),
          interfaces.end(),
          [](const auto& i) { return *i.intfID() == 100; }),
      true);

  // The other port's chain is untouched.
  EXPECT_EQ(
      std::any_of(
          ports.begin(),
          ports.end(),
          [](const auto& p) { return *p.logicalID() == 2; }),
      true);
  EXPECT_EQ(
      std::any_of(
          vlans.begin(),
          vlans.end(),
          [](const auto& v) { return *v.id() == 200; }),
      true);
  EXPECT_EQ(
      std::any_of(
          interfaces.begin(),
          interfaces.end(),
          [](const auto& i) { return *i.intfID() == 200; }),
      true);
}

// ============================================================================
// queue-config reset
// ============================================================================

// `delete interface <intf> queue-config` is the same reset as
// `config interface <intf> queue-config default`: it clears
// Port::portQueueConfigName, which makes the port fall back to
// SwitchConfig::defaultPortQueues.
class DeleteQueueConfigAttrTestFixture : public CmdConfigTestBase {
 public:
  DeleteQueueConfigAttrTestFixture()
      : CmdConfigTestBase(
            "delete_ifqc_attr_test_%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 1,
        "portQueueConfigName": "rsw_queues"
      },
      {
        "logicalID": 2,
        "name": "eth1/2/1",
        "state": 2,
        "speed": 100000,
        "ingressVlan": 1
      }
    ],
    "portQueueConfigs": {
      "rsw_queues": [
        {"id": 0, "streamType": 1, "weight": 1, "scheduling": 5}
      ]
    }
  }
})") {}

 protected:
  static const cfg::Port* findPort(const std::string& name) {
    const auto& ports =
        *ConfigSession::getInstance().config<AgentConfigDomain>().sw()->ports();
    for (const auto& port : ports) {
      if (*port.name() == name) {
        return &port;
      }
    }
    return nullptr;
  }

  std::string runDelete(const std::vector<std::string>& args) {
    auto cmd = CmdDeleteInterface();
    return cmd.queryClient(localhost(), InterfaceDeleteConfig(args));
  }
};

// queue-config takes no value on the delete side -- it is a reset, not a
// removal of a particular binding.
TEST_F(DeleteQueueConfigAttrTestFixture, parsesAsValuelessAttribute) {
  setupTestableConfigSession();
  InterfaceDeleteConfig config({"eth1/1/1", "queue-config"});
  ASSERT_EQ(config.getAttributes().size(), 1);
  EXPECT_EQ(config.getAttributes()[0].first, "queue-config");
  EXPECT_EQ(config.getAttributes()[0].second, "");
}

TEST_F(DeleteQueueConfigAttrTestFixture, clearsExistingBinding) {
  setupTestableConfigSession();
  ASSERT_TRUE(findPort("eth1/1/1")->portQueueConfigName().has_value());

  auto result = runDelete({"eth1/1/1", "queue-config"});
  EXPECT_THAT(result, ::testing::HasSubstr("queue-config"));

  EXPECT_FALSE(findPort("eth1/1/1")->portQueueConfigName().has_value());
  // Clearing a binding must not remove the config it pointed at.
  EXPECT_TRUE(
      ConfigSession::getInstance()
          .config<AgentConfigDomain>()
          .sw()
          ->portQueueConfigs()
          ->count("rsw_queues"));
}

TEST_F(DeleteQueueConfigAttrTestFixture, unboundPortIsNoOp) {
  setupTestableConfigSession();
  EXPECT_NO_THROW(runDelete({"eth1/2/1", "queue-config"}));
  EXPECT_FALSE(findPort("eth1/2/1")->portQueueConfigName().has_value());
}

} // namespace facebook::fboss
