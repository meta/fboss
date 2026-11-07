/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <boost/filesystem/operations.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <vector>

#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/cli/fboss2/commands/config/interface/CmdConfigInterface.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigInterfaceTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigInterfaceTestFixture()
      : CmdConfigTestBase(
            "fboss_interface_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000,
        "description": "original description of eth1/1/1"
      },
      {
        "logicalID": 2,
        "name": "eth1/2/1",
        "state": 2,
        "speed": 100000,
        "description": "original description of eth1/2/1"
      }
    ],
    "mirrors": [
      {
        "name": "span0",
        "destination": {"egressPort": {"name": "eth1/2/1"}},
        "dscp": 0,
        "truncate": false
      }
    ],
    "vlanPorts": [
      {
        "vlanID": 1,
        "logicalPort": 1
      },
      {
        "vlanID": 2,
        "logicalPort": 2
      }
    ],
    "interfaces": [
      {
        "intfID": 1,
        "routerID": 0,
        "vlanID": 1,
        "name": "eth1/1/1",
        "mtu": 1500
      },
      {
        "intfID": 2,
        "routerID": 0,
        "vlanID": 2,
        "name": "eth1/2/1",
        "mtu": 1500
      },
      {
        "intfID": 3001,
        "routerID": 0,
        "vlanID": 0,
        "name": "",
        "mtu": 1500
      }
    ]
  }
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupProfileSupportMocks();
  }

  void setupProfileSupportMocks() {
    // Create a mock PlatformMapping with common profiles supported
    cfg::PlatformMapping platformMapping;

    // Add platform supported profiles
    // 10G
    cfg::PlatformPortProfileConfigEntry profile10G;
    profile10G.factor()->profileID() =
        cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER;
    profile10G.profile()->speed() = cfg::PortSpeed::XG;
    platformMapping.platformSupportedProfiles()->push_back(profile10G);

    // 25G
    cfg::PlatformPortProfileConfigEntry profile25G;
    profile25G.factor()->profileID() =
        cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER;
    profile25G.profile()->speed() = cfg::PortSpeed::TWENTYFIVEG;
    platformMapping.platformSupportedProfiles()->push_back(profile25G);

    // 40G
    cfg::PlatformPortProfileConfigEntry profile40G;
    profile40G.factor()->profileID() =
        cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC;
    profile40G.profile()->speed() = cfg::PortSpeed::FORTYG;
    platformMapping.platformSupportedProfiles()->push_back(profile40G);

    // 50G
    cfg::PlatformPortProfileConfigEntry profile50G;
    profile50G.factor()->profileID() =
        cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER;
    profile50G.profile()->speed() = cfg::PortSpeed::FIFTYG;
    platformMapping.platformSupportedProfiles()->push_back(profile50G);

    // 100G
    cfg::PlatformPortProfileConfigEntry profile100G;
    profile100G.factor()->profileID() =
        cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91;
    profile100G.profile()->speed() = cfg::PortSpeed::HUNDREDG;
    platformMapping.platformSupportedProfiles()->push_back(profile100G);

    // 200G
    cfg::PlatformPortProfileConfigEntry profile200G;
    profile200G.factor()->profileID() =
        cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N;
    profile200G.profile()->speed() = cfg::PortSpeed::TWOHUNDREDG;
    platformMapping.platformSupportedProfiles()->push_back(profile200G);

    // 400G
    cfg::PlatformPortProfileConfigEntry profile400G;
    profile400G.factor()->profileID() =
        cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N;
    profile400G.profile()->speed() = cfg::PortSpeed::FOURHUNDREDG;
    platformMapping.platformSupportedProfiles()->push_back(profile400G);

    // Add port entries for eth1/1/1 and eth1/2/1 with all supported profiles
    cfg::PlatformPortEntry port1;
    port1.mapping()->name() = "eth1/1/1";
    port1.mapping()->id() = 1;
    port1.supportedProfiles()
        [cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER] =
        cfg::PlatformPortConfig();
    port1.supportedProfiles()
        [cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER] =
        cfg::PlatformPortConfig();
    port1.supportedProfiles()[cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC] =
        cfg::PlatformPortConfig();
    port1.supportedProfiles()
        [cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER] =
        cfg::PlatformPortConfig();
    port1.supportedProfiles()[cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91] =
        cfg::PlatformPortConfig();
    port1
        .supportedProfiles()[cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N] =
        cfg::PlatformPortConfig();
    port1
        .supportedProfiles()[cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N] =
        cfg::PlatformPortConfig();
    platformMapping.ports()[1] = port1;

    cfg::PlatformPortEntry port2;
    port2.mapping()->name() = "eth1/2/1";
    port2.mapping()->id() = 2;
    port2.supportedProfiles()
        [cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER] =
        cfg::PlatformPortConfig();
    port2.supportedProfiles()
        [cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER] =
        cfg::PlatformPortConfig();
    port2.supportedProfiles()[cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC] =
        cfg::PlatformPortConfig();
    port2.supportedProfiles()
        [cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER] =
        cfg::PlatformPortConfig();
    port2.supportedProfiles()[cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91] =
        cfg::PlatformPortConfig();
    port2
        .supportedProfiles()[cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N] =
        cfg::PlatformPortConfig();
    port2
        .supportedProfiles()[cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N] =
        cfg::PlatformPortConfig();
    platformMapping.ports()[2] = port2;

    // Setup mock to return this platform mapping
    EXPECT_CALL(getMockAgent(), getPlatformMapping(_))
        .WillRepeatedly(DoAll(SetArgReferee<0>(platformMapping), Return()));

    // Setup mock QSFP service to return empty profiles (fallback to hardware)
    std::map<std::string, std::vector<cfg::PortProfileID>> emptyProfiles;
    EXPECT_CALL(getQsfpService(), getAllPortSupportedProfiles(_, _))
        .WillRepeatedly(DoAll(SetArgReferee<0>(emptyProfiles), Return()));
  }

 protected:
  const std::string cmdPrefix_ = "config interface";
};

// ============================================================================
// InterfacesConfig Validation Tests
// ============================================================================

// Test valid config with port + description
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigValidPortAndDescription) {
  setupTestableConfigSession();
  InterfacesConfig config({"eth1/1/1", "description", "My port"});
  EXPECT_EQ(config.getInterfaces().size(), 1);
  EXPECT_TRUE(config.hasAttributes());
  ASSERT_EQ(config.getAttributes().size(), 1);
  EXPECT_EQ(config.getAttributes()[0].first, "description");
  EXPECT_EQ(config.getAttributes()[0].second, "My port");
}

// Test valid config with port + mtu
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigValidPortAndMtu) {
  setupTestableConfigSession();
  InterfacesConfig config({"eth1/1/1", "mtu", "9000"});
  EXPECT_EQ(config.getInterfaces().size(), 1);
  EXPECT_TRUE(config.hasAttributes());
  ASSERT_EQ(config.getAttributes().size(), 1);
  EXPECT_EQ(config.getAttributes()[0].first, "mtu");
  EXPECT_EQ(config.getAttributes()[0].second, "9000");
}

// Test valid config with port + both attributes
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigValidPortAndBothAttrs) {
  setupTestableConfigSession();
  InterfacesConfig config(
      {"eth1/1/1", "description", "My port", "mtu", "9000"});
  EXPECT_EQ(config.getInterfaces().size(), 1);
  EXPECT_TRUE(config.hasAttributes());
  ASSERT_EQ(config.getAttributes().size(), 2);
  EXPECT_EQ(config.getAttributes()[0].first, "description");
  EXPECT_EQ(config.getAttributes()[0].second, "My port");
  EXPECT_EQ(config.getAttributes()[1].first, "mtu");
  EXPECT_EQ(config.getAttributes()[1].second, "9000");
}

// Test valid config with multiple ports + attributes
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigMultiplePorts) {
  setupTestableConfigSession();
  InterfacesConfig config(
      {"eth1/1/1", "eth1/2/1", "description", "Uplink ports"});
  EXPECT_EQ(config.getInterfaces().size(), 2);
  EXPECT_TRUE(config.hasAttributes());
  ASSERT_EQ(config.getAttributes().size(), 1);
  EXPECT_EQ(config.getAttributes()[0].first, "description");
  EXPECT_EQ(config.getAttributes()[0].second, "Uplink ports");
}

// Test port only (no attributes) - for subcommand pass-through
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigPortOnly) {
  setupTestableConfigSession();
  InterfacesConfig config({"eth1/1/1"});
  EXPECT_EQ(config.getInterfaces().size(), 1);
  EXPECT_FALSE(config.hasAttributes());
  EXPECT_TRUE(config.getAttributes().empty());
}

// Test case-insensitive attribute names
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigCaseInsensitiveAttrs) {
  setupTestableConfigSession();
  InterfacesConfig config({"eth1/1/1", "DESCRIPTION", "Test", "MTU", "9000"});
  EXPECT_TRUE(config.hasAttributes());
  ASSERT_EQ(config.getAttributes().size(), 2);
  // Attributes should be normalized to lowercase
  EXPECT_EQ(config.getAttributes()[0].first, "description");
  EXPECT_EQ(config.getAttributes()[1].first, "mtu");
}

TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigMirrorAttributes) {
  setupTestableConfigSession();
  InterfacesConfig config(
      {"eth1/1/1", "MIRROR-INGRESS", "span0", "mirror-egress", "span0"});
  ASSERT_EQ(config.getAttributes().size(), 2);
  EXPECT_EQ(
      config.getAttributes()[0],
      std::make_pair(std::string("mirror-ingress"), std::string("span0")));
  EXPECT_EQ(
      config.getAttributes()[1],
      std::make_pair(std::string("mirror-egress"), std::string("span0")));
}

// Test empty input throws
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigEmptyThrows) {
  setupTestableConfigSession();
  EXPECT_THROW(InterfacesConfig({}), std::invalid_argument);
}

// Test first token is attribute (no port name)
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigNoPortNameThrows) {
  setupTestableConfigSession();
  try {
    InterfacesConfig config({"description", "My port"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("No interface name provided"));
    EXPECT_THAT(e.what(), HasSubstr("description"));
  }
}

// Test missing value for attribute
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigMissingValueThrows) {
  setupTestableConfigSession();
  try {
    InterfacesConfig config({"eth1/1/1", "description"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Missing value for attribute"));
    EXPECT_THAT(e.what(), HasSubstr("description"));
  }
}

// Test value is actually another attribute (forgot value)
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigValueIsAttributeThrows) {
  setupTestableConfigSession();
  try {
    InterfacesConfig config({"eth1/1/1", "description", "mtu"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Missing value for attribute"));
    EXPECT_THAT(e.what(), HasSubstr("description"));
    EXPECT_THAT(e.what(), HasSubstr("mtu"));
  }
}

// Test unknown attribute name - unknown attribute must appear AFTER a known
// attribute to trigger the error. Otherwise, unknown tokens are treated as
// port names and fail during port resolution.
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigUnknownAttributeThrows) {
  setupTestableConfigSession();
  try {
    // "speed" comes after "description", so it's recognized as an attribute
    InterfacesConfig config(
        {"eth1/1/1", "description", "Test", "speed", "100000"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Unknown attribute"));
    EXPECT_THAT(e.what(), HasSubstr("speed"));
  }
}

// Test non-existent port throws
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigNonExistentPortThrows) {
  setupTestableConfigSession();
  EXPECT_THROW(
      InterfacesConfig({"eth1/99/1", "description", "Test"}),
      std::invalid_argument);
}

// ============================================================================
// CmdConfigInterface::queryClient Tests
// ============================================================================

TEST_F(CmdConfigInterfaceTestFixture, queryClientAppliesMirrorsToPorts) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 eth1/2/1 mirror-ingress span0 mirror-egress span0");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1",
       "eth1/2/1",
       "mirror-ingress",
       "span0",
       "mirror-egress",
       "span0"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("mirror-ingress=span0"));
  EXPECT_THAT(result, HasSubstr("mirror-egress=span0"));
  const auto& ports =
      *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    ASSERT_TRUE(port.ingressMirror().has_value());
    EXPECT_EQ(*port.ingressMirror(), "span0");
    ASSERT_TRUE(port.egressMirror().has_value());
    EXPECT_EQ(*port.egressMirror(), "span0");
  }
}

TEST_F(
    CmdConfigInterfaceTestFixture,
    queryClientRejectsUnknownMirrorAtomically) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 description changed mirror-ingress missing");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "description", "changed", "mirror-ingress", "missing"});

  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);

  const auto& ports =
      *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  EXPECT_EQ(*ports[0].description(), "original description of eth1/1/1");
  EXPECT_FALSE(ports[0].ingressMirror().has_value());
}

// Test setting description on a single interface
TEST_F(CmdConfigInterfaceTestFixture, queryClientSetsDescription) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 description \"New description\"");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "description", "New description"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("description"));

  // Verify the description was updated
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();
  auto& ports = *switchConfig.ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.description(), "New description");
    } else {
      // Other ports should be unchanged
      EXPECT_EQ(*port.description(), "original description of " + *port.name());
    }
  }
}

// Test setting MTU on a single interface
TEST_F(CmdConfigInterfaceTestFixture, queryClientSetsMtu) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 mtu 9000");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "mtu", "9000"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("mtu=9000"));

  // Verify the MTU was updated
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();
  auto& intfs = *switchConfig.interfaces();
  for (const auto& intf : intfs) {
    if (*intf.name() == "eth1/1/1") {
      EXPECT_EQ(*intf.mtu(), 9000);
    } else {
      // Other interfaces should be unchanged
      EXPECT_EQ(*intf.mtu(), 1500);
    }
  }
}

// Test renaming an interface addressed by its port name
TEST_F(CmdConfigInterfaceTestFixture, queryClientSetsInterfaceName) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 name uplink_1");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "name", "uplink_1"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("name=\"uplink_1\""));

  auto& intfs =
      *ConfigSession::getInstance().getAgentConfig().sw()->interfaces();
  for (const auto& intf : intfs) {
    if (*intf.intfID() == 1) {
      EXPECT_EQ(*intf.name(), "uplink_1");
    } else if (*intf.intfID() == 2) {
      EXPECT_EQ(*intf.name(), "eth1/2/1");
    }
  }
}

// A nameless interface (name set to the empty string) can still be addressed
// by its interface ID to give it a name.
TEST_F(CmdConfigInterfaceTestFixture, queryClientSetsNameOnNamelessInterface) {
  setupTestableConfigSession(cmdPrefix_, "3001 name svi_mgmt");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"3001", "name", "svi_mgmt"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("name=\"svi_mgmt\""));

  auto& intfs =
      *ConfigSession::getInstance().getAgentConfig().sw()->interfaces();
  bool found = false;
  for (const auto& intf : intfs) {
    if (*intf.intfID() == 3001) {
      found = true;
      EXPECT_EQ(*intf.name(), "svi_mgmt");
    }
  }
  EXPECT_TRUE(found);

  // The interface is now also addressable by its new name.
  utils::InterfaceList relist({"svi_mgmt"});
  ASSERT_EQ(relist.size(), 1);
  ASSERT_NE(relist[0].getInterface(), nullptr);
  EXPECT_EQ(*relist[0].getInterface()->intfID(), 3001);
}

// A purely-numeric name is rejected: it would shadow ID-based lookups.
TEST_F(CmdConfigInterfaceTestFixture, queryClientNumericNameThrows) {
  setupTestableConfigSession(cmdPrefix_, "3001 name 1234");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"3001", "name", "1234"});

  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);
}

// A name already used by another interface or by a port is rejected.
TEST_F(CmdConfigInterfaceTestFixture, queryClientDuplicateNameThrows) {
  setupTestableConfigSession(cmdPrefix_, "3001 name eth1/2/1");
  auto cmd = CmdConfigInterface();

  // eth1/2/1 is both a port name and another interface's name.
  InterfacesConfig config({"3001", "name", "eth1/2/1"});
  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);
}

// Renaming an interface to the name it already has is idempotent.
TEST_F(CmdConfigInterfaceTestFixture, queryClientRenameToSameName) {
  setupTestableConfigSession(cmdPrefix_, "3001 name svi_mgmt");
  auto cmd = CmdConfigInterface();

  InterfacesConfig config({"3001", "name", "svi_mgmt"});
  cmd.queryClient(localhost(), config);
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
}

// Setting the same name on multiple interfaces is rejected.
TEST_F(CmdConfigInterfaceTestFixture, queryClientNameOnMultipleThrows) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 eth1/2/1 name uplink_1");
  auto cmd = CmdConfigInterface();

  InterfacesConfig config({"eth1/1/1", "eth1/2/1", "name", "uplink_1"});
  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);
}

// Regression test: ip-address/ipv6-address must persist the session config
// to disk. A missing `changed = true` in the ip-address branch used to skip
// saveConfig() while still reporting success (so `config session diff`
// showed no changes).
TEST_F(CmdConfigInterfaceTestFixture, queryClientIpAddressesPersistedToDisk) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 ip-address 10.0.0.1/31 ipv6-address cafe::1/127");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "ip-address", "10.0.0.1/31", "ipv6-address", "cafe::1/127"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("ip-address=10.0.0.1/31"));
  EXPECT_THAT(result, HasSubstr("ipv6-address=cafe::1/127"));

  // Verify the addresses were added to the in-memory config
  auto& session = ConfigSession::getInstance();
  auto& intfs = *session.getAgentConfig().sw()->interfaces();
  for (const auto& intf : intfs) {
    if (*intf.name() == "eth1/1/1") {
      EXPECT_THAT(
          *intf.ipAddresses(),
          UnorderedElementsAre("10.0.0.1/31", "cafe::1/127"));
    } else {
      EXPECT_THAT(*intf.ipAddresses(), IsEmpty());
    }
  }

  // Verify the session config was persisted to disk (i.e. saveConfig() ran)
  ASSERT_TRUE(std::filesystem::exists(getSessionConfigPath()));
  auto sessionContent = readFile(getSessionConfigPath());
  EXPECT_THAT(sessionContent, HasSubstr("10.0.0.1/31"));
  EXPECT_THAT(sessionContent, HasSubstr("cafe::1/127"));
}

// Test setting both description and MTU
TEST_F(CmdConfigInterfaceTestFixture, queryClientSetsBothAttributes) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 description \"Updated port\" mtu 9000");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "description", "Updated port", "mtu", "9000"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("description"));
  EXPECT_THAT(result, HasSubstr("mtu=9000"));

  // Verify both were updated
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();

  auto& ports = *switchConfig.ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.description(), "Updated port");
    }
  }

  auto& intfs = *switchConfig.interfaces();
  for (const auto& intf : intfs) {
    if (*intf.name() == "eth1/1/1") {
      EXPECT_EQ(*intf.mtu(), 9000);
    }
  }
}

// Test setting attributes on multiple interfaces
TEST_F(CmdConfigInterfaceTestFixture, queryClientMultipleInterfaces) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 eth1/2/1 description \"Shared description\"");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "eth1/2/1", "description", "Shared description"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));

  // Verify both descriptions were updated
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();
  auto& ports = *switchConfig.ports();
  for (const auto& port : ports) {
    EXPECT_EQ(*port.description(), "Shared description");
  }
}

// Test no attributes throws (pass-through case)
TEST_F(CmdConfigInterfaceTestFixture, queryClientNoAttributesThrows) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1"});

  EXPECT_THROW(cmd.queryClient(localhost(), config), std::runtime_error);
}

// Test invalid MTU value (non-numeric)
TEST_F(CmdConfigInterfaceTestFixture, queryClientInvalidMtuNonNumeric) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "mtu", "abc"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Invalid MTU value"));
    EXPECT_THAT(e.what(), HasSubstr("abc"));
  }
}

// Test MTU out of range (too low)
TEST_F(CmdConfigInterfaceTestFixture, queryClientMtuTooLow) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "mtu", std::to_string(utils::kMtuMin - 1)});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("out of range"));
  }
}

// Test MTU out of range (too high)
TEST_F(CmdConfigInterfaceTestFixture, queryClientMtuTooHigh) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "mtu", std::to_string(utils::kMtuMax + 1)});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("out of range"));
  }
}

// Test MTU boundary values (valid)
TEST_F(CmdConfigInterfaceTestFixture, queryClientMtuBoundaryValid) {
  auto cmd = CmdConfigInterface();

  // Test minimum MTU
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 mtu " + std::to_string(utils::kMtuMin));
  InterfacesConfig configMin(
      {"eth1/1/1", "mtu", std::to_string(utils::kMtuMin)});
  EXPECT_THAT(
      cmd.queryClient(localhost(), configMin),
      HasSubstr("Successfully configured"));

  // Test maximum MTU
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 mtu " + std::to_string(utils::kMtuMax));
  InterfacesConfig configMax(
      {"eth1/1/1", "mtu", std::to_string(utils::kMtuMax)});
  EXPECT_THAT(
      cmd.queryClient(localhost(), configMax),
      HasSubstr("Successfully configured"));
}

// Test printOutput
TEST_F(CmdConfigInterfaceTestFixture, printOutput) {
  auto cmd = CmdConfigInterface();
  std::string successMessage =
      "Successfully configured interface(s) eth1/1/1: description=\"Test\"";

  std::stringstream buffer;
  std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());
  cmd.printOutput(successMessage);
  std::cout.rdbuf(old);

  EXPECT_EQ(buffer.str(), successMessage + "\n");
}

// ============================================================================
// InterfacesConfig Profile Attribute Parsing Tests
// ============================================================================

// Test valid config with port + profile
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigValidPortAndProfile) {
  setupTestableConfigSession();
  InterfacesConfig config({"eth1/1/1", "profile", "PROFILE_100G_4_NRZ_CL91"});
  EXPECT_EQ(config.getInterfaces().size(), 1);
  EXPECT_TRUE(config.hasAttributes());
  ASSERT_EQ(config.getAttributes().size(), 1);
  EXPECT_EQ(config.getAttributes()[0].first, "profile");
  EXPECT_EQ(config.getAttributes()[0].second, "PROFILE_100G_4_NRZ_CL91");
}

// Test case-insensitive 'profile' attribute name
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigProfileCaseInsensitive) {
  setupTestableConfigSession();

  // Uppercase PROFILE attribute name
  InterfacesConfig config1({"eth1/1/1", "PROFILE", "PROFILE_100G_4_NRZ_CL91"});
  ASSERT_EQ(config1.getAttributes().size(), 1);
  EXPECT_EQ(config1.getAttributes()[0].first, "profile");

  // Mixed case Profile attribute name
  InterfacesConfig config2({"eth1/1/1", "Profile", "PROFILE_100G_4_NRZ_CL91"});
  ASSERT_EQ(config2.getAttributes().size(), 1);
  EXPECT_EQ(config2.getAttributes()[0].first, "profile");
}

// Test unknown attribute after profile value triggers updated error message
TEST_F(
    CmdConfigInterfaceTestFixture,
    interfaceConfigUnknownAttrAfterProfileThrows) {
  setupTestableConfigSession();
  try {
    // "bogus" comes after "profile" + value, so it's recognized as an unknown
    // attribute
    InterfacesConfig config(
        {"eth1/1/1", "profile", "PROFILE_100G_4_NRZ_CL91", "bogus", "val"});
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Unknown attribute"));
    EXPECT_THAT(e.what(), HasSubstr("bogus"));
    EXPECT_THAT(e.what(), HasSubstr("profile"));
  }
}

// ============================================================================
// Profile parsing tests
// ============================================================================

// Test valid uppercase profile string
TEST_F(CmdConfigInterfaceTestFixture, parseProfileValidUppercase) {
  auto result = parsePortProfile("PROFILE_100G_4_NRZ_RS528_COPPER");
  EXPECT_EQ(result, cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER);
}

// Profile names must match the Thrift enum exactly.
TEST_F(CmdConfigInterfaceTestFixture, parseProfileRejectsLowercase) {
  EXPECT_THROW(
      parsePortProfile("profile_100g_4_nrz_rs528_copper"),
      std::invalid_argument);
}

// Test unknown profile string throws
TEST_F(CmdConfigInterfaceTestFixture, parseProfileUnknown) {
  try {
    parsePortProfile("PROFILE_BOGUS");
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("not a valid PortProfileID"));
    EXPECT_THAT(e.what(), HasSubstr("PROFILE_BOGUS"));
  }
}

// Test empty string throws
TEST_F(CmdConfigInterfaceTestFixture, parseProfileEmpty) {
  EXPECT_THROW(parsePortProfile(""), std::invalid_argument);
}

// ============================================================================
// CmdConfigInterface::queryClient Profile Tests
// ============================================================================

// Test setting a valid profile on a single interface
TEST_F(CmdConfigInterfaceTestFixture, queryClientSetsProfile) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 profile PROFILE_100G_4_NRZ_CL91");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "profile", "PROFILE_100G_4_NRZ_CL91"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("profile="));
  EXPECT_THAT(result, HasSubstr("speed="));

  // Verify the profile was updated
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();
  auto& ports = *switchConfig.ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.profileID(), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91);
    } // if eth1/1/1
  } // for ports
} // queryClientSetsProfile

// loopback-mode tests
// ============================================================================

TEST_F(CmdConfigInterfaceTestFixture, SetLoopbackModeNone) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 loopback-mode none");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "loopback-mode", "none"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("loopback-mode=none"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.loopbackMode(), cfg::PortLoopbackMode::NONE);
    }
  }
}

// Test unsupported profile throws
TEST_F(CmdConfigInterfaceTestFixture, queryClientProfileUnsupportedThrows) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  // PROFILE_100G_4_NRZ_RS528_COPPER is not in our mock
  // PlatformMapping
  InterfacesConfig config(
      {"eth1/1/1", "profile", "PROFILE_100G_4_NRZ_RS528_COPPER"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Invalid profile"));
    EXPECT_THAT(e.what(), HasSubstr("eth1/1/1"));
    EXPECT_THAT(e.what(), HasSubstr("Supported profiles"));
  }
}

// Test invalid profile string throws before validator is
// constructed
TEST_F(CmdConfigInterfaceTestFixture, queryClientProfileInvalidStringThrows) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "profile", "PROFILE_BOGUS"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("not a valid PortProfileID"));
    EXPECT_THAT(e.what(), HasSubstr("PROFILE_BOGUS"));
  }
}

// Test setting profile on multiple interfaces
TEST_F(CmdConfigInterfaceTestFixture, queryClientSetProfileMultiPort) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 eth1/2/1 profile PROFILE_100G_4_NRZ_CL91");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "eth1/2/1", "profile", "PROFILE_100G_4_NRZ_CL91"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));
  EXPECT_THAT(result, HasSubstr("profile="));

  // Verify both ports were updated
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();
  auto& ports = *switchConfig.ports();
  for (const auto& port : ports) {
    EXPECT_EQ(*port.profileID(), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91);
  }
}

TEST_F(CmdConfigInterfaceTestFixture, queryClientDeduplicatesInterfaceNames) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 eth1/1/1 profile PROFILE_100G_4_NRZ_CL91");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "eth1/1/1", "profile", "PROFILE_100G_4_NRZ_CL91"});

  ASSERT_EQ(config.getInterfaces().size(), 1);
  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  auto& ports = *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.profileID(), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91);
    }
  }
}

// Test setting profile combined with description
TEST_F(CmdConfigInterfaceTestFixture, queryClientSetProfileAndDescription) {
  setupTestableConfigSession(
      cmdPrefix_,
      "eth1/1/1 profile PROFILE_100G_4_NRZ_CL91 description uplink");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1",
       "profile",
       "PROFILE_100G_4_NRZ_CL91",
       "description",
       "uplink"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("profile="));
  EXPECT_THAT(result, HasSubstr("description="));

  // Verify both were updated
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();
  auto& ports = *switchConfig.ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.profileID(), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91);
    } // if eth1/1/1
  } // for ports
} // queryClientSetProfileAndDescription

TEST_F(CmdConfigInterfaceTestFixture, SetLoopbackModeNIF) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 loopback-mode NIF");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "loopback-mode", "NIF"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.loopbackMode(), cfg::PortLoopbackMode::NIF);
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLoopbackModeMAC) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 loopback-mode MAC");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "loopback-mode", "MAC"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.loopbackMode(), cfg::PortLoopbackMode::MAC);
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLoopbackModeCaseInsensitive) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 loopback-mode pHy");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "loopback-mode", "pHy"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.loopbackMode(), cfg::PortLoopbackMode::PHY);
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLoopbackModeInvalid) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "loopback-mode", "loop"});
  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceTestFixture, SetLoopbackModeMultiPort) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 eth1/2/1 loopback-mode PHY");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "eth1/2/1", "loopback-mode", "PHY"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    EXPECT_EQ(*port.loopbackMode(), cfg::PortLoopbackMode::PHY);
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLoopbackModeCombinedWithDescription) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 loopback-mode MAC description testport");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "loopback-mode", "MAC", "description", "testport"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("loopback-mode=MAC"));
  EXPECT_THAT(result, HasSubstr("description="));
  EXPECT_THAT(result, HasSubstr("testport"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.loopbackMode(), cfg::PortLoopbackMode::MAC);
      EXPECT_EQ(*port.description(), "testport");
    }
  }
}

// ============================================================================
// flow-control-rx / flow-control-tx tests
// ============================================================================

TEST_F(CmdConfigInterfaceTestFixture, SetFlowControlRxEnable) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 flow-control-rx enable");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "flow-control-rx", "enable"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("flow-control-rx=enable"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_TRUE(*port.pause()->rx());
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetFlowControlRxDisable) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 flow-control-rx disable");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "flow-control-rx", "disable"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("flow-control-rx=disable"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_FALSE(*port.pause()->rx());
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetFlowControlTxEnable) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 flow-control-tx enable");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "flow-control-tx", "enable"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("flow-control-tx=enable"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_TRUE(*port.pause()->tx());
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetFlowControlTxDisable) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 flow-control-tx disable");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "flow-control-tx", "disable"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("flow-control-tx=disable"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_FALSE(*port.pause()->tx());
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetFlowControlRxCaseInsensitive) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 flow-control-rx ENABLE");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "flow-control-rx", "ENABLE"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_TRUE(*port.pause()->rx());
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetFlowControlRxInvalid) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "flow-control-rx", "yes"});
  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceTestFixture, SetFlowControlTxInvalid) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "flow-control-tx", "no"});
  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceTestFixture, SetFlowControlRxAndTxIndependent) {
  // Set rx=enable first
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 flow-control-rx enable");
  auto cmd1 = CmdConfigInterface();
  InterfacesConfig config1({"eth1/1/1", "flow-control-rx", "enable"});
  cmd1.queryClient(localhost(), config1);

  auto& session1 = ConfigSession::getInstance();
  auto& ports1 = *session1.getAgentConfig().sw()->ports();
  for (const auto& port : ports1) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_TRUE(*port.pause()->rx());
      EXPECT_FALSE(*port.pause()->tx()); // tx should remain false (default)
    }
  }

  // Set tx=enable independently
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 flow-control-tx enable");
  auto cmd2 = CmdConfigInterface();
  InterfacesConfig config2({"eth1/1/1", "flow-control-tx", "enable"});
  cmd2.queryClient(localhost(), config2);

  auto& session2 = ConfigSession::getInstance();
  auto& ports2 = *session2.getAgentConfig().sw()->ports();
  for (const auto& port : ports2) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_TRUE(*port.pause()->tx());
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetFlowControlMultiPort) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 eth1/2/1 flow-control-rx enable");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "eth1/2/1", "flow-control-rx", "enable"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    EXPECT_TRUE(*port.pause()->rx());
  }
}

// ============================================================================
// lldp-expected-value tests
// ============================================================================

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedValue) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 lldp-expected-value ge-0/0/0");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lldp-expected-value", "ge-0/0/0"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("lldp-expected-value="));
  EXPECT_THAT(result, HasSubstr("ge-0/0/0"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      const auto& lldpMap = *port.expectedLLDPValues();
      auto it = lldpMap.find(cfg::LLDPTag::PORT);
      ASSERT_NE(it, lldpMap.end());
      EXPECT_EQ(it->second, "ge-0/0/0");
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedValueOverwrites) {
  // Set value first time
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 lldp-expected-value ge-0/0/0");
  auto cmd1 = CmdConfigInterface();
  InterfacesConfig config1({"eth1/1/1", "lldp-expected-value", "ge-0/0/0"});
  cmd1.queryClient(localhost(), config1);

  // Overwrite with new value
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 lldp-expected-value xe-0/0/1");
  auto cmd2 = CmdConfigInterface();
  InterfacesConfig config2({"eth1/1/1", "lldp-expected-value", "xe-0/0/1"});
  cmd2.queryClient(localhost(), config2);

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      const auto& lldpMap = *port.expectedLLDPValues();
      auto it = lldpMap.find(cfg::LLDPTag::PORT);
      ASSERT_NE(it, lldpMap.end());
      EXPECT_EQ(it->second, "xe-0/0/1");
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedValueEmpty) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  // Construct config manually to bypass InterfacesConfig
  // validation (empty string is a valid value token, just
  // rejected by queryClient)
  InterfacesConfig config({"eth1/1/1", "lldp-expected-value", ""});
  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedValueMultiPort) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 eth1/2/1 lldp-expected-value ge-0/0/0");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "eth1/2/1", "lldp-expected-value", "ge-0/0/0"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    const auto& lldpMap = *port.expectedLLDPValues();
    auto it = lldpMap.find(cfg::LLDPTag::PORT);
    ASSERT_NE(it, lldpMap.end());
    EXPECT_EQ(it->second, "ge-0/0/0");
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedValueOnlySetsPORTTag) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 lldp-expected-value testvalue");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lldp-expected-value", "testvalue"});
  cmd.queryClient(localhost(), config);

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      const auto& lldpMap = *port.expectedLLDPValues();
      // Only PORT tag should be present
      EXPECT_EQ(lldpMap.size(), 1u);
      auto it = lldpMap.find(cfg::LLDPTag::PORT);
      ASSERT_NE(it, lldpMap.end());
      EXPECT_EQ(it->second, "testvalue");
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedChassis) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 lldp-expected-chassis switch1");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lldp-expected-chassis", "switch1"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& ports = *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      const auto& lldpMap = *port.expectedLLDPValues();
      auto it = lldpMap.find(cfg::LLDPTag::CHASSIS);
      ASSERT_NE(it, lldpMap.end());
      EXPECT_EQ(it->second, "switch1");
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedTtl) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 lldp-expected-ttl 120");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lldp-expected-ttl", "120"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& ports = *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      const auto& lldpMap = *port.expectedLLDPValues();
      auto it = lldpMap.find(cfg::LLDPTag::TTL);
      ASSERT_NE(it, lldpMap.end());
      EXPECT_EQ(it->second, "120");
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedPortDesc) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 lldp-expected-port-desc uplink");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lldp-expected-port-desc", "uplink"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& ports = *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      const auto& lldpMap = *port.expectedLLDPValues();
      auto it = lldpMap.find(cfg::LLDPTag::PORT_DESC);
      ASSERT_NE(it, lldpMap.end());
      EXPECT_EQ(it->second, "uplink");
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedSystemName) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 lldp-expected-system-name spine1");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lldp-expected-system-name", "spine1"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& ports = *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      const auto& lldpMap = *port.expectedLLDPValues();
      auto it = lldpMap.find(cfg::LLDPTag::SYSTEM_NAME);
      ASSERT_NE(it, lldpMap.end());
      EXPECT_EQ(it->second, "spine1");
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedSystemDesc) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 lldp-expected-system-desc \"Spine router\"");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "lldp-expected-system-desc", "Spine router"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& ports = *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      const auto& lldpMap = *port.expectedLLDPValues();
      auto it = lldpMap.find(cfg::LLDPTag::SYSTEM_DESC);
      ASSERT_NE(it, lldpMap.end());
      EXPECT_EQ(it->second, "Spine router");
    }
  }
}

TEST_F(
    CmdConfigInterfaceTestFixture,
    SetLldpExpectedMultipleTagsStoreIndependently) {
  // Set several tag types in separate commands; each must be
  // stored under its own key in the expectedLLDPValues map.
  setupTestableConfigSession(
      cmdPrefix_,
      "eth1/1/1 lldp-expected-value ge-0/0/0 lldp-expected-system-name spine1");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1",
       "lldp-expected-value",
       "ge-0/0/0",
       "lldp-expected-system-name",
       "spine1"});
  cmd.queryClient(localhost(), config);

  auto& ports = *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      const auto& lldpMap = *port.expectedLLDPValues();
      EXPECT_EQ(lldpMap.at(cfg::LLDPTag::PORT), "ge-0/0/0");
      EXPECT_EQ(lldpMap.at(cfg::LLDPTag::SYSTEM_NAME), "spine1");
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetLldpExpectedChassisEmptyIsRejected) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lldp-expected-chassis", ""});
  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);
}

// ============================================================================
// type tests
// ============================================================================

TEST_F(CmdConfigInterfaceTestFixture, SetTypeRoutedPort) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 type routed-port");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "type", "routed-port"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("type=routed-port"));

  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  // Port fields
  for (const auto& port : *swConfig.ports()) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.portType(), cfg::PortType::INTERFACE_PORT);
      EXPECT_TRUE(*port.routable());
      EXPECT_EQ(*port.ingressVlan(), 0);
    }
  }

  // eth1/1/1 (logicalID=1) vlanPort entry must be removed;
  // eth1/2/1 must remain
  const auto& vps = *swConfig.vlanPorts();
  EXPECT_EQ(vps.size(), 1u);
  EXPECT_EQ(*vps[0].logicalPort(), 2);
}

TEST_F(CmdConfigInterfaceTestFixture, SetTypeCaseInsensitive) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 type ROUTED-PORT");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "type", "ROUTED-PORT"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.portType(), cfg::PortType::INTERFACE_PORT);
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, SetTypeInvalid) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "type", "fabric-port"});
  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceTestFixture, SetTypeInvalidEmpty) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "type", ""});
  EXPECT_THROW(cmd.queryClient(localhost(), config), std::invalid_argument);
}

TEST_F(CmdConfigInterfaceTestFixture, SetTypeMultiPort) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 eth1/2/1 type routed-port");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "eth1/2/1", "type", "routed-port"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));

  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  for (const auto& port : *swConfig.ports()) {
    EXPECT_EQ(*port.portType(), cfg::PortType::INTERFACE_PORT);
    EXPECT_TRUE(*port.routable());
    EXPECT_EQ(*port.ingressVlan(), 0);
  }

  // All vlanPort entries must be removed
  EXPECT_TRUE(swConfig.vlanPorts()->empty());
}

TEST_F(CmdConfigInterfaceTestFixture, SetTypeWithDescription) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 type routed-port description uplink");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "type", "routed-port", "description", "uplink"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("type=routed-port"));
  EXPECT_THAT(result, HasSubstr("description="));
  EXPECT_THAT(result, HasSubstr("uplink"));

  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  for (const auto& port : *swConfig.ports()) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.portType(), cfg::PortType::INTERFACE_PORT);
      EXPECT_TRUE(*port.routable());
      EXPECT_EQ(*port.ingressVlan(), 0);
      EXPECT_EQ(*port.description(), "uplink");
    }
  }
}

// Test no-attributes error message mentions profile
TEST_F(
    CmdConfigInterfaceTestFixture,
    queryClientNoAttributesErrorMentionsProfile) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(e.what(), HasSubstr("profile"));
  }
}

TEST_F(
    CmdConfigInterfaceTestFixture,
    SetTypeRoutedPortLeavesOtherVlanPortsIntact) {
  // Only eth1/1/1 (logicalID=1) is made routed; eth1/2/1
  // (logicalID=2) must retain its vlanPort entry.
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 type routed-port");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "type", "routed-port"});
  cmd.queryClient(localhost(), config);

  const auto& vps =
      *ConfigSession::getInstance().getAgentConfig().sw()->vlanPorts();
  ASSERT_EQ(vps.size(), 1u);
  EXPECT_EQ(*vps[0].logicalPort(), 2);
  EXPECT_EQ(*vps[0].vlanID(), 2);
}

// ============================================================================
// shutdown / no-shutdown Tests (NOS-6133)
// ============================================================================

TEST_F(CmdConfigInterfaceTestFixture, ShutdownSetsStateDisabled) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 shutdown");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "shutdown"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("state=disabled"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.state(), cfg::PortState::DISABLED);
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, NoShutdownSetsStateEnabled) {
  // Start with a disabled port and bring it up
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 no-shutdown");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "no-shutdown"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("state=enabled"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.state(), cfg::PortState::ENABLED);
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, ShutdownMultiPort) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 eth1/2/1 shutdown");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "eth1/2/1", "shutdown"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    EXPECT_EQ(*port.state(), cfg::PortState::DISABLED);
  }
}

TEST_F(CmdConfigInterfaceTestFixture, NoShutdownMultiPort) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 eth1/2/1 no-shutdown");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "eth1/2/1", "no-shutdown"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    EXPECT_EQ(*port.state(), cfg::PortState::ENABLED);
  }
}

TEST_F(CmdConfigInterfaceTestFixture, ShutdownThenNoShutdown) {
  // First disable the port
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 shutdown");
  {
    auto cmd = CmdConfigInterface();
    InterfacesConfig config({"eth1/1/1", "shutdown"});
    cmd.queryClient(localhost(), config);
  }
  auto& session1 = ConfigSession::getInstance();
  for (const auto& port : *session1.getAgentConfig().sw()->ports()) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.state(), cfg::PortState::DISABLED);
    }
  }

  // Now re-enable it
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 no-shutdown");
  {
    auto cmd2 = CmdConfigInterface();
    InterfacesConfig config2({"eth1/1/1", "no-shutdown"});
    cmd2.queryClient(localhost(), config2);
  }
  auto& session2 = ConfigSession::getInstance();
  for (const auto& port : *session2.getAgentConfig().sw()->ports()) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.state(), cfg::PortState::ENABLED);
    }
  }
}

TEST_F(CmdConfigInterfaceTestFixture, ShutdownWithDescription) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 shutdown description downlink");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "shutdown", "description", "downlink"});
  auto result = cmd.queryClient(localhost(), config);
  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("state=disabled"));
  EXPECT_THAT(result, HasSubstr("description=\"downlink\""));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      EXPECT_EQ(*port.state(), cfg::PortState::DISABLED);
      EXPECT_EQ(*port.description(), "downlink");
    }
  }
}

// Valueless attribute parser tests
TEST_F(CmdConfigInterfaceTestFixture, ShutdownParserRecognizedAsValueless) {
  setupTestableConfigSession();
  // "shutdown" should parse as a valueless attribute with
  // empty value
  InterfacesConfig config({"eth1/1/1", "shutdown"});
  EXPECT_TRUE(config.hasAttributes());
  ASSERT_EQ(config.getAttributes().size(), 1);
  EXPECT_EQ(config.getAttributes()[0].first, "shutdown");
  EXPECT_EQ(config.getAttributes()[0].second, "");
}

TEST_F(CmdConfigInterfaceTestFixture, NoShutdownParserRecognizedAsValueless) {
  setupTestableConfigSession();
  InterfacesConfig config({"eth1/1/1", "no-shutdown"});
  EXPECT_TRUE(config.hasAttributes());
  ASSERT_EQ(config.getAttributes().size(), 1);
  EXPECT_EQ(config.getAttributes()[0].first, "no-shutdown");
  EXPECT_EQ(config.getAttributes()[0].second, "");
}

// ============================================================================
// applyProfileImpl integration test
// ============================================================================

// Fixture with a well-formed session config (eth1/1/1 controlling, eth1/1/2 in
// its own vlan) so the subsumed-port removal exercises the full vlan/interface
// pruning path.
class ApplyProfileSubsumeTestFixture : public CmdConfigTestBase {
 public:
  ApplyProfileSubsumeTestFixture()
      : CmdConfigTestBase(
            "fboss_subsume_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "defaultVlan": 1,
    "ports": [
      {"logicalID": 1, "name": "eth1/1/1", "state": 2, "ingressVlan": 1},
      {"logicalID": 2, "name": "eth1/1/2", "state": 2, "ingressVlan": 2}
    ],
    "vlans": [
      {"id": 1, "name": "vlan1"},
      {"id": 2, "name": "vlan2"}
    ],
    "vlanPorts": [
      {"vlanID": 1, "logicalPort": 1},
      {"vlanID": 2, "logicalPort": 2}
    ],
    "interfaces": [
      {"intfID": 1, "routerID": 0, "vlanID": 1, "name": "eth1/1/1", "mtu": 1500},
      {"intfID": 2, "routerID": 0, "vlanID": 2, "name": "eth1/1/2", "mtu": 1500}
    ]
  }
})") {}

  // Platform mapping where eth1/1/1 at 400G subsumes eth1/1/2.
  PlatformMapping makeSubsumePlatformMapping() {
    cfg::PlatformMapping pm;

    cfg::PlatformPortProfileConfigEntry profile400G;
    profile400G.factor()->profileID() =
        cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N;
    profile400G.profile()->speed() = cfg::PortSpeed::FOURHUNDREDG;
    pm.platformSupportedProfiles()->push_back(profile400G);

    cfg::PlatformPortEntry port1;
    port1.mapping()->id() = 1;
    port1.mapping()->name() = "eth1/1/1";
    port1.mapping()->controllingPort() = 1;
    cfg::PlatformPortConfig wide;
    wide.subsumedPorts() = {2};
    port1
        .supportedProfiles()[cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N] =
        wide;
    pm.ports()[1] = port1;

    cfg::PlatformPortEntry port2;
    port2.mapping()->id() = 2;
    port2.mapping()->name() = "eth1/1/2";
    port2.mapping()->controllingPort() = 1;
    port2.supportedProfiles()[cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91] =
        cfg::PlatformPortConfig();
    pm.ports()[2] = port2;

    return PlatformMapping(pm);
  }
};

TEST_F(
    ApplyProfileSubsumeTestFixture,
    appliesProfileRemovesSubsumedAndReports) {
  setupTestableConfigSession(
      "config interface", "eth1/1/1 profile PROFILE_400G_8_PAM4_RS544X2N");
  auto platformMapping = makeSubsumePlatformMapping();
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  utils::InterfaceList interfaces({"eth1/1/1"});

  auto result = applyProfileImpl(
      swConfig,
      platformMapping,
      {},
      interfaces,
      "PROFILE_400G_8_PAM4_RS544X2N");

  EXPECT_THAT(result, HasSubstr("profile=PROFILE_400G_8_PAM4_RS544X2N"));
  EXPECT_THAT(result, HasSubstr("auto-removed subsumed port(s): eth1/1/2"));

  // eth1/1/1 took the new profile; eth1/1/2 and its dependents are gone.
  std::set<int> portIds;
  for (const auto& p : *swConfig.ports()) {
    portIds.insert(*p.logicalID());
    if (*p.logicalID() == 1) {
      EXPECT_EQ(
          *p.profileID(), cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N);
    }
  }
  EXPECT_EQ(portIds, (std::set<int>{1}));

  for (const auto& vp : *swConfig.vlanPorts()) {
    EXPECT_NE(*vp.logicalPort(), 2);
  }
  for (const auto& v : *swConfig.vlans()) {
    EXPECT_NE(*v.id(), 2);
  }
  for (const auto& i : *swConfig.interfaces()) {
    EXPECT_NE(*i.intfID(), 2);
  }
}

// A subsuming profile change escalates the action level to
// DISRUPTIVE_SERVICE_RESTART so the CLI commits it as an agent coldboot instead
// of a hitless reloadConfig (T281221621).
TEST_F(ApplyProfileSubsumeTestFixture, subsumeEscalatesToColdboot) {
  setupTestableConfigSession(
      "config interface", "eth1/1/1 profile PROFILE_400G_8_PAM4_RS544X2N");
  auto platformMapping = makeSubsumePlatformMapping();
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  utils::InterfaceList interfaces({"eth1/1/1"});

  cli::ConfigActionLevel actionLevel = cli::ConfigActionLevel::HITLESS;
  applyProfileImpl(
      swConfig,
      platformMapping,
      {},
      interfaces,
      "PROFILE_400G_8_PAM4_RS544X2N",
      &actionLevel);

  EXPECT_EQ(actionLevel, cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);
}

// ============================================================================
// applyProfileImpl pending-port (creation) tests
// ============================================================================

namespace {
// Builds a PlatformMapping with two ports. eth1/1/1 (id 1) is a self-
// controlling INTERFACE_PORT that supports 100G and 400G (400G subsumes port
// 2). eth1/1/2 (id 2) is an INTERFACE_PORT controlled by port 1 that supports
// 100G. If `port2IsFabric` is set, port 2 is a FABRIC_PORT instead so it cannot
// be created as an interface port.
cfg::PlatformMapping makePendingPlatformMapping(bool port2IsFabric = false) {
  cfg::PlatformMapping pm;

  cfg::PlatformPortProfileConfigEntry profile100G;
  profile100G.factor()->profileID() =
      cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91;
  profile100G.profile()->speed() = cfg::PortSpeed::HUNDREDG;
  pm.platformSupportedProfiles()->push_back(profile100G);

  cfg::PlatformPortProfileConfigEntry profile400G;
  profile400G.factor()->profileID() =
      cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N;
  profile400G.profile()->speed() = cfg::PortSpeed::FOURHUNDREDG;
  pm.platformSupportedProfiles()->push_back(profile400G);

  cfg::PlatformPortEntry port1;
  port1.mapping()->id() = 1;
  port1.mapping()->name() = "eth1/1/1";
  port1.mapping()->controllingPort() = 1;
  port1.mapping()->portType() = cfg::PortType::INTERFACE_PORT;
  port1.supportedProfiles()[cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91] =
      cfg::PlatformPortConfig();
  cfg::PlatformPortConfig wide;
  wide.subsumedPorts() = {2};
  port1.supportedProfiles()[cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N] =
      wide;
  pm.ports()[1] = port1;

  cfg::PlatformPortEntry port2;
  port2.mapping()->id() = 2;
  port2.mapping()->name() = "eth1/1/2";
  port2.mapping()->controllingPort() = 1;
  port2.mapping()->portType() = port2IsFabric ? cfg::PortType::FABRIC_PORT
                                              : cfg::PortType::INTERFACE_PORT;
  port2.supportedProfiles()[cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91] =
      cfg::PlatformPortConfig();
  pm.ports()[2] = port2;

  return pm;
}

PlatformMapping makePendingMapping(bool port2IsFabric = false) {
  return PlatformMapping(makePendingPlatformMapping(port2IsFabric));
}
} // namespace

// Fixture with an empty running config so InterfaceList(name,
// allowMissing=true) yields pending Intfs (getPort()==nullptr) that
// applyProfileImpl is meant to create.
class CreatePendingInterfacePortsTestFixture : public CmdConfigTestBase {
 public:
  CreatePendingInterfacePortsTestFixture()
      : CmdConfigTestBase(
            "fboss_pending_ports_test_%%%%-%%%%-%%%%-%%%%",
            R"({"sw": {}})") {}
};

// A pending INTERFACE_PORT with a supported profile is created: the config
// gains a Port + Vlan + VlanPort + Interface for it, and the result mentions
// the created port.
TEST_F(CreatePendingInterfacePortsTestFixture, createsSupportedInterfacePort) {
  setupTestableConfigSession();
  auto platformMapping = makePendingMapping();
  cfg::SwitchConfig config;
  utils::InterfaceList interfaces(
      std::vector<std::string>{"eth1/1/1"}, /*allowMissing*/ true);

  auto result = applyProfileImpl(
      config, platformMapping, {}, interfaces, "PROFILE_100G_4_NRZ_CL91");

  EXPECT_THAT(result, HasSubstr("created port(s): eth1/1/1"));

  ASSERT_EQ(config.ports()->size(), 1);
  const auto& port = config.ports()->at(0);
  EXPECT_EQ(*port.logicalID(), 1);
  EXPECT_EQ(*port.profileID(), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91);
  EXPECT_EQ(config.vlans()->size(), 1);
  EXPECT_EQ(config.vlanPorts()->size(), 1);
  EXPECT_EQ(config.interfaces()->size(), 1);
}

TEST_F(
    CreatePendingInterfacePortsTestFixture,
    rejectsProfileUnsupportedByQsfp) {
  setupTestableConfigSession();
  auto platformMapping = makePendingMapping();
  cfg::SwitchConfig config;
  utils::InterfaceList interfaces(
      std::vector<std::string>{"eth1/1/1"}, /*allowMissing*/ true);
  const utility::PortProfileSupportMap qsfpSupportedProfiles{
      {"eth1/1/1", {cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N}}};

  EXPECT_THROW(
      applyProfileImpl(
          config,
          platformMapping,
          qsfpSupportedProfiles,
          interfaces,
          "PROFILE_100G_4_NRZ_CL91"),
      std::invalid_argument);
  EXPECT_TRUE(config.ports()->empty());
}

// Creating a port removes nothing, so the action level stays HITLESS (no
// coldboot needed; the commit remains a hitless reloadConfig). See T281221621.
TEST_F(CreatePendingInterfacePortsTestFixture, createStaysHitless) {
  setupTestableConfigSession();
  auto platformMapping = makePendingMapping();
  cfg::SwitchConfig config;
  utils::InterfaceList interfaces(
      std::vector<std::string>{"eth1/1/1"}, /*allowMissing*/ true);

  cli::ConfigActionLevel actionLevel = cli::ConfigActionLevel::HITLESS;
  applyProfileImpl(
      config,
      platformMapping,
      {},
      interfaces,
      "PROFILE_100G_4_NRZ_CL91",
      &actionLevel);

  EXPECT_EQ(actionLevel, cli::ConfigActionLevel::HITLESS);
}

// A pending non-INTERFACE_PORT (e.g. FABRIC_PORT) cannot be created.
TEST_F(CreatePendingInterfacePortsTestFixture, throwsOnNonInterfacePort) {
  setupTestableConfigSession();
  auto platformMapping = makePendingMapping(/*port2IsFabric*/ true);
  cfg::SwitchConfig config;
  utils::InterfaceList interfaces(
      std::vector<std::string>{"eth1/1/2"}, /*allowMissing*/ true);

  try {
    applyProfileImpl(
        config, platformMapping, {}, interfaces, "PROFILE_100G_4_NRZ_CL91");
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Only interface ports can be created"));
    EXPECT_THAT(e.what(), HasSubstr("eth1/1/2"));
  }
  EXPECT_TRUE(config.ports()->empty());
}

// A name that is not a platform port at all is rejected.
TEST_F(CreatePendingInterfacePortsTestFixture, throwsOnBogusName) {
  setupTestableConfigSession();
  auto platformMapping = makePendingMapping();
  cfg::SwitchConfig config;
  utils::InterfaceList interfaces(
      std::vector<std::string>{"eth9/9/9"}, /*allowMissing*/ true);

  EXPECT_THROW(
      applyProfileImpl(
          config, platformMapping, {}, interfaces, "PROFILE_100G_4_NRZ_CL91"),
      std::invalid_argument);
}

// Existing controlling port narrowed in the SAME command frees a subsumed
// absent port: port1 (present at 400G, subsumes port2) is narrowed to 100G and
// port2 (pending) is created. Both end up present at 100G.
TEST_F(
    CreatePendingInterfacePortsTestFixture,
    existingNarrowedFreesAbsentSubport) {
  setupTestableConfigSession();
  auto platformMapping = makePendingMapping();
  // Controlling port 1 is present at 400G (which subsumes port 2).
  cfg::SwitchConfig config;
  cfg::Port controlling;
  controlling.logicalID() = 1;
  controlling.name() = "eth1/1/1";
  controlling.profileID() = cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N;
  config.ports() = {controlling};

  // Both ports are in the request; port 1 (present) resolves, port 2 is
  // pending. Narrowing both to 100G frees port 2.
  utils::InterfaceList interfaces(
      std::vector<std::string>{"eth1/1/1", "eth1/1/2"}, /*allowMissing*/ true);
  interfaces[0].setPort(&config.ports()->at(0));

  auto result = applyProfileImpl(
      config, platformMapping, {}, interfaces, "PROFILE_100G_4_NRZ_CL91");

  EXPECT_THAT(result, HasSubstr("created port(s): eth1/1/2"));
  ASSERT_EQ(config.ports()->size(), 2);
  for (const auto& p : *config.ports()) {
    EXPECT_EQ(*p.profileID(), cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91);
  }
}

// A single absent port subsumed by a present controlling port that is NOT in
// the request is rejected (the controlling port stays at its subsuming
// profile), leaving the config unchanged.
TEST_F(
    CreatePendingInterfacePortsTestFixture,
    throwsWhenSubsumedAndLeavesConfig) {
  setupTestableConfigSession();
  auto platformMapping = makePendingMapping();
  // Controlling port 1 is already in the config at 400G, which subsumes port 2.
  cfg::SwitchConfig config;
  cfg::Port controlling;
  controlling.logicalID() = 1;
  controlling.name() = "eth1/1/1";
  controlling.profileID() = cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N;
  config.ports() = {controlling};

  // Only port 2 is requested; port 1 keeps its subsuming 400G profile.
  utils::InterfaceList interfaces(
      std::vector<std::string>{"eth1/1/2"}, /*allowMissing*/ true);

  const cfg::SwitchConfig before = config;
  EXPECT_THROW(
      applyProfileImpl(
          config, platformMapping, {}, interfaces, "PROFILE_100G_4_NRZ_CL91"),
      std::invalid_argument);
  EXPECT_EQ(config, before);
}

// ============================================================================
// lookup-class tests
// ============================================================================

// Test valid config with port + lookup-class is parsed as an attribute pair
TEST_F(CmdConfigInterfaceTestFixture, interfaceConfigValidPortAndLookupClass) {
  setupTestableConfigSession();
  InterfacesConfig config({"eth1/1/1", "lookup-class", "10"});
  EXPECT_EQ(config.getInterfaces().size(), 1);
  EXPECT_TRUE(config.hasAttributes());
  ASSERT_EQ(config.getAttributes().size(), 1);
  EXPECT_EQ(config.getAttributes()[0].first, "lookup-class");
  EXPECT_EQ(config.getAttributes()[0].second, "10");
}

// Test setting a valid lookup-class replaces lookupClasses with [id]
TEST_F(CmdConfigInterfaceTestFixture, queryClientSetsLookupClass) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 lookup-class 10");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lookup-class", "10"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("lookup-class=10"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      ASSERT_EQ(port.lookupClasses()->size(), 1);
      EXPECT_EQ(
          port.lookupClasses()->at(0),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
    } else {
      // Other ports should be untouched.
      EXPECT_TRUE(port.lookupClasses()->empty());
    }
  }
}

// Test comma-separated list sets the full lookupClasses pool
TEST_F(CmdConfigInterfaceTestFixture, queryClientSetsLookupClassList) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 lookup-class 10,11,12,13,14");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lookup-class", "10,11,12,13,14"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("lookup-class=10,11,12,13,14"));

  auto& ports = *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      ASSERT_EQ(port.lookupClasses()->size(), 5);
      EXPECT_EQ(
          port.lookupClasses()->at(0),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
      EXPECT_EQ(
          port.lookupClasses()->at(1),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);
      EXPECT_EQ(
          port.lookupClasses()->at(2),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2);
      EXPECT_EQ(
          port.lookupClasses()->at(3),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3);
      EXPECT_EQ(
          port.lookupClasses()->at(4),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4);
    } else {
      EXPECT_TRUE(port.lookupClasses()->empty());
    }
  }
}

TEST_F(
    CmdConfigInterfaceTestFixture,
    interfaceConfigValidPortAndLookupClassList) {
  setupTestableConfigSession();
  InterfacesConfig config({"eth1/1/1", "lookup-class", "10,11,12"});
  ASSERT_EQ(config.getAttributes().size(), 1);
  EXPECT_EQ(config.getAttributes()[0].second, "10,11,12");
}

// Test that setting a new lookup-class replaces any pre-existing list
TEST_F(CmdConfigInterfaceTestFixture, queryClientLookupClassReplacesList) {
  auto cmd = CmdConfigInterface();

  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 lookup-class 10,11");
  cmd.queryClient(
      localhost(), InterfacesConfig({"eth1/1/1", "lookup-class", "10,11"}));

  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 lookup-class 12,13,14");
  cmd.queryClient(
      localhost(), InterfacesConfig({"eth1/1/1", "lookup-class", "12,13,14"}));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      ASSERT_EQ(port.lookupClasses()->size(), 3);
      EXPECT_EQ(
          port.lookupClasses()->at(0),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2);
      EXPECT_EQ(
          port.lookupClasses()->at(1),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3);
      EXPECT_EQ(
          port.lookupClasses()->at(2),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4);
    }
  }
}

// Test setting lookup-class on multiple interfaces
TEST_F(CmdConfigInterfaceTestFixture, queryClientLookupClassMultiPort) {
  setupTestableConfigSession(cmdPrefix_, "eth1/1/1 eth1/2/1 lookup-class 12");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "eth1/2/1", "lookup-class", "12"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));
  EXPECT_THAT(result, HasSubstr("eth1/1/1"));
  EXPECT_THAT(result, HasSubstr("eth1/2/1"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    ASSERT_EQ(port.lookupClasses()->size(), 1);
    EXPECT_EQ(
        port.lookupClasses()->at(0),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2);
  }
}

// Test a class name (case-insensitive) is accepted in place of a numeric id
TEST_F(CmdConfigInterfaceTestFixture, queryClientLookupClassByName) {
  setupTestableConfigSession(
      cmdPrefix_, "eth1/1/1 lookup-class class_queue_per_host_queue_0");
  auto cmd = CmdConfigInterface();
  InterfacesConfig config(
      {"eth1/1/1", "lookup-class", "class_queue_per_host_queue_0"});

  auto result = cmd.queryClient(localhost(), config);

  EXPECT_THAT(result, HasSubstr("Successfully configured"));

  auto& session = ConfigSession::getInstance();
  auto& ports = *session.getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    if (*port.name() == "eth1/1/1") {
      ASSERT_EQ(port.lookupClasses()->size(), 1);
      EXPECT_EQ(
          port.lookupClasses()->at(0),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
    }
  }
}

// Test an agent-reserved class id (valid enum member, not queue-per-host)
// is rejected
TEST_F(CmdConfigInterfaceTestFixture, queryClientLookupClassRejectsReserved) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  // 9 is CLASS_DROP, reserved for the agent's blocked-neighbor feature.
  InterfacesConfig config({"eth1/1/1", "lookup-class", "9"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("reserved for agent use"));
    EXPECT_THAT(e.what(), HasSubstr("CLASS_DROP"));
  }
}

// Test non-integer lookup-class value throws
TEST_F(CmdConfigInterfaceTestFixture, queryClientLookupClassNonNumeric) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lookup-class", "abc"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Invalid lookup-class value"));
    EXPECT_THAT(e.what(), HasSubstr("abc"));
  }
}

// Test an integer that is not a valid AclLookupClass enum value throws
TEST_F(CmdConfigInterfaceTestFixture, queryClientLookupClassInvalidEnum) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  // 999 is not a member of AclLookupClass.
  InterfacesConfig config({"eth1/1/1", "lookup-class", "999"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Invalid lookup-class value"));
    EXPECT_THAT(e.what(), HasSubstr("Valid values"));
  }
}

TEST_F(CmdConfigInterfaceTestFixture, queryClientLookupClassRejectsDuplicate) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lookup-class", "10,11,10"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("duplicate id"));
  }

  // The list is fully parsed before any port is mutated, so a rejected list
  // must leave lookupClasses empty — the valid prefix "10" is not applied.
  auto& ports = *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    EXPECT_TRUE(port.lookupClasses()->empty());
  }
}

TEST_F(CmdConfigInterfaceTestFixture, queryClientLookupClassRejectsEmptySlot) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lookup-class", "10,,11"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("empty id"));
  }
}

TEST_F(CmdConfigInterfaceTestFixture, queryClientLookupClassRejectsBadInList) {
  setupTestableConfigSession();
  auto cmd = CmdConfigInterface();
  InterfacesConfig config({"eth1/1/1", "lookup-class", "10,999"});

  try {
    cmd.queryClient(localhost(), config);
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), HasSubstr("Invalid lookup-class value"));
    EXPECT_THAT(e.what(), HasSubstr("Valid values"));
  }

  // The list is fully parsed before any port is mutated, so a rejected list
  // must leave lookupClasses empty — the valid prefix "10" is not applied.
  auto& ports = *ConfigSession::getInstance().getAgentConfig().sw()->ports();
  for (const auto& port : ports) {
    EXPECT_TRUE(port.lookupClasses()->empty());
  }
}

// ============================================================================
// queue-config attribute
// ============================================================================

// Needs its own seed: portQueueConfigs entries to bind to, and a port that
// starts out already bound so the clear path is reachable.
class QueueConfigAttrTestFixture : public CmdConfigTestBase {
 public:
  QueueConfigAttrTestFixture()
      : CmdConfigTestBase(
            "fboss_ifqc_attr_test_%%%%-%%%%-%%%%-%%%%",
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
        *ConfigSession::getInstance().getAgentConfig().sw()->ports();
    for (const auto& port : ports) {
      if (*port.name() == name) {
        return &port;
      }
    }
    return nullptr;
  }

  std::string configure(const std::vector<std::string>& args) {
    auto cmd = CmdConfigInterface();
    return cmd.queryClient(localhost(), InterfacesConfig(args));
  }
};

TEST_F(QueueConfigAttrTestFixture, bindsNamedQueueConfig) {
  setupTestableConfigSession();
  auto result = configure({"eth1/2/1", "queue-config", "rsw_queues"});

  EXPECT_THAT(result, ::testing::HasSubstr("queue-config=rsw_queues"));
  const auto* port = findPort("eth1/2/1");
  ASSERT_NE(port, nullptr);
  ASSERT_TRUE(port->portQueueConfigName().has_value());
  EXPECT_EQ(*port->portQueueConfigName(), "rsw_queues");
}

// `default` is not a portQueueConfigs entry; per Port::portQueueConfigName's
// contract an unset field already resolves to defaultPortQueues, so selecting
// it clears the override rather than writing an unresolvable name.
TEST_F(QueueConfigAttrTestFixture, defaultClearsExistingBinding) {
  setupTestableConfigSession();
  ASSERT_TRUE(findPort("eth1/1/1")->portQueueConfigName().has_value());

  configure({"eth1/1/1", "queue-config", "default"});

  EXPECT_FALSE(findPort("eth1/1/1")->portQueueConfigName().has_value());
}

TEST_F(QueueConfigAttrTestFixture, defaultOnUnboundPortIsNoOp) {
  setupTestableConfigSession();
  EXPECT_NO_THROW(configure({"eth1/2/1", "queue-config", "default"}));
  EXPECT_FALSE(findPort("eth1/2/1")->portQueueConfigName().has_value());
}

// A named config must exist before it can be bound; `default` bypasses this
// check because it never has a portQueueConfigs entry to find.
TEST_F(QueueConfigAttrTestFixture, unknownNameThrows) {
  setupTestableConfigSession();
  try {
    configure({"eth1/1/1", "queue-config", "no_such_config"});
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_THAT(e.what(), ::testing::HasSubstr("no_such_config"));
    EXPECT_THAT(e.what(), ::testing::HasSubstr("does not exist"));
  }
  // The failed bind must leave the previously-bound port alone.
  EXPECT_EQ(*findPort("eth1/1/1")->portQueueConfigName(), "rsw_queues");
}

TEST_F(QueueConfigAttrTestFixture, invalidNameThrows) {
  setupTestableConfigSession();
  EXPECT_THROW(
      configure({"eth1/1/1", "queue-config", "9bad"}), std::invalid_argument);
}

// The payoff of folding this into the attribute model: queue-config composes
// with other attributes in a single command, which the old standalone
// subcommand could not do.
TEST_F(QueueConfigAttrTestFixture, composesWithOtherAttributes) {
  setupTestableConfigSession();
  auto result = configure(
      {"eth1/2/1", "description", "uplink", "queue-config", "rsw_queues"});

  EXPECT_THAT(result, ::testing::HasSubstr("queue-config=rsw_queues"));
  EXPECT_THAT(result, ::testing::HasSubstr("description"));

  const auto* port = findPort("eth1/2/1");
  ASSERT_NE(port, nullptr);
  ASSERT_TRUE(port->portQueueConfigName().has_value());
  EXPECT_EQ(*port->portQueueConfigName(), "rsw_queues");
  EXPECT_EQ(*port->description(), "uplink");
}

} // namespace facebook::fboss
