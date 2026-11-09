/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/utils/PortMap.h"
#include <folly/FileUtil.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace facebook::fboss::utils {

namespace fs = std::filesystem;

namespace {

/**
 * Get the full path to a config file given a relative path.
 * This abstracts the path resolution between OSS and FB internal environments.
 *
 * In Buck builds, resources declared in the BUCK file are accessible via
 * relative paths from the repo root (the working directory for tests).
 *
 * @param relativePath Path relative to the fboss repo root,
 *        e.g. "fboss/oss/link_test_configs/minipack3n.materialized_JSON"
 * @return Full path to the config file
 */
std::string getConfigPath(const std::string& relativePath) {
  // First, try the relative path directly (works in Buck builds where
  // resources are available and the working directory is the repo root)
  if (fs::exists(relativePath)) {
    return relativePath;
  }

#if IS_OSS
  // Fall back to the hardcoded OSS path for CMake builds
  std::string ossPath = "/var/FBOSS/fboss/" + relativePath;
  if (fs::exists(ossPath)) {
    return ossPath;
  }
#endif

  // Return the relative path and let the caller handle the error
  return relativePath;
}

/**
 * Load a config file using a relative path.
 * The path is relative to the fboss repo root.
 */
cfg::AgentConfig loadConfig(const std::string& relativePath) {
  std::string configJson;
  std::string fullPath = getConfigPath(relativePath);
  if (!folly::readFile(fullPath.c_str(), configJson)) {
    throw std::runtime_error("Failed to read config file: " + fullPath);
  }

  cfg::AgentConfig agentConfig;
  apache::thrift::SimpleJSONSerializer::deserialize<cfg::AgentConfig>(
      configJson, agentConfig);
  return agentConfig;
}

} // namespace

class PortMapTest : public ::testing::Test {};

// Test with minipack3n config which uses portID attribute
TEST_F(PortMapTest, Minipack3nPortIdMapping) {
  auto config =
      loadConfig("fboss/oss/link_test_configs/minipack3n.materialized_JSON");
  PortMap portMap(config);

  // Test port name to interface ID mapping using portID attribute
  // Interface 2001 has portID 241, which is port eth1/1/1
  auto intfId = portMap.getInterfaceIdForPort("eth1/1/1");
  ASSERT_TRUE(intfId.has_value());
  EXPECT_EQ(*intfId, InterfaceID(2001));

  // Interface 2002 has portID 243, which is port eth1/1/5
  intfId = portMap.getInterfaceIdForPort("eth1/1/5");
  ASSERT_TRUE(intfId.has_value());
  EXPECT_EQ(*intfId, InterfaceID(2002));

  // Interface 2003 has portID 245, which is port eth1/2/1
  intfId = portMap.getInterfaceIdForPort("eth1/2/1");
  ASSERT_TRUE(intfId.has_value());
  EXPECT_EQ(*intfId, InterfaceID(2003));

  // Test reverse mapping
  auto portName = portMap.getPortNameForInterface(InterfaceID(2001));
  ASSERT_TRUE(portName.has_value());
  EXPECT_EQ(*portName, "eth1/1/1");

  portName = portMap.getPortNameForInterface(InterfaceID(2002));
  ASSERT_TRUE(portName.has_value());
  EXPECT_EQ(*portName, "eth1/1/5");
}

// Test with montblanc config which uses VLAN-based mapping
TEST_F(PortMapTest, MontblancVlanMapping) {
  auto config =
      loadConfig("fboss/oss/link_test_configs/montblanc.materialized_JSON");
  PortMap portMap(config);

  // Test VLAN-based mapping
  // Port eth1/1/1 has logicalID 9, vlanPorts maps 9 -> vlanID 2001,
  // interface 2001 has vlanID 2001
  auto intfId = portMap.getInterfaceIdForPort("eth1/1/1");
  ASSERT_TRUE(intfId.has_value());
  EXPECT_EQ(*intfId, InterfaceID(2001));

  // Port eth1/2/1 has logicalID 1, vlanPorts maps 1 -> vlanID 2003,
  // interface 2003 has vlanID 2003
  intfId = portMap.getInterfaceIdForPort("eth1/2/1");
  ASSERT_TRUE(intfId.has_value());
  EXPECT_EQ(*intfId, InterfaceID(2003));

  // Port eth1/2/5 has logicalID 5, vlanPorts maps 5 -> vlanID 2004,
  // interface 2004 has vlanID 2004
  intfId = portMap.getInterfaceIdForPort("eth1/2/5");
  ASSERT_TRUE(intfId.has_value());
  EXPECT_EQ(*intfId, InterfaceID(2004));

  // Test reverse mapping
  auto portName = portMap.getPortNameForInterface(InterfaceID(2001));
  ASSERT_TRUE(portName.has_value());
  EXPECT_EQ(*portName, "eth1/1/1");

  portName = portMap.getPortNameForInterface(InterfaceID(2003));
  ASSERT_TRUE(portName.has_value());
  EXPECT_EQ(*portName, "eth1/2/1");
}

// Test port existence checks
TEST_F(PortMapTest, PortExistenceChecks) {
  auto config =
      loadConfig("fboss/oss/link_test_configs/minipack3n.materialized_JSON");
  PortMap portMap(config);

  // Test hasPort
  EXPECT_TRUE(portMap.hasPort("eth1/1/1"));
  EXPECT_TRUE(portMap.hasPort("eth1/1/5"));
  EXPECT_FALSE(portMap.hasPort("eth99/99/99"));
  EXPECT_FALSE(portMap.hasPort("nonexistent"));

  // Test hasInterface
  EXPECT_TRUE(portMap.hasInterface(InterfaceID(2001)));
  EXPECT_TRUE(portMap.hasInterface(InterfaceID(2002)));
  EXPECT_FALSE(portMap.hasInterface(InterfaceID(9999)));
}

// Test port logical ID lookup
TEST_F(PortMapTest, PortLogicalIdLookup) {
  auto config =
      loadConfig("fboss/oss/link_test_configs/minipack3n.materialized_JSON");
  PortMap portMap(config);

  // Test getPortLogicalId
  auto logicalId = portMap.getPortLogicalId("eth1/1/1");
  ASSERT_TRUE(logicalId.has_value());
  EXPECT_EQ(*logicalId, PortID(241));

  logicalId = portMap.getPortLogicalId("eth1/1/5");
  ASSERT_TRUE(logicalId.has_value());
  EXPECT_EQ(*logicalId, PortID(243));

  logicalId = portMap.getPortLogicalId("eth1/2/1");
  ASSERT_TRUE(logicalId.has_value());
  EXPECT_EQ(*logicalId, PortID(245));

  // Test non-existent port
  logicalId = portMap.getPortLogicalId("nonexistent");
  EXPECT_FALSE(logicalId.has_value());
}

// Test getAllPortNames
TEST_F(PortMapTest, GetAllPortNames) {
  auto config =
      loadConfig("fboss/oss/link_test_configs/minipack3n.materialized_JSON");
  PortMap portMap(config);

  auto portNames = portMap.getAllPortNames();
  EXPECT_GT(portNames.size(), 0);

  // Check that some expected ports are in the list
  bool foundEth1_1_1 = false;
  bool foundEth1_1_5 = false;
  for (const auto& name : portNames) {
    if (name == "eth1/1/1") {
      foundEth1_1_1 = true;
    }
    if (name == "eth1/1/5") {
      foundEth1_1_5 = true;
    }
  }
  EXPECT_TRUE(foundEth1_1_1);
  EXPECT_TRUE(foundEth1_1_5);
}

// Test getAllInterfaceIds
TEST_F(PortMapTest, GetAllInterfaceIds) {
  auto config =
      loadConfig("fboss/oss/link_test_configs/minipack3n.materialized_JSON");
  PortMap portMap(config);

  auto interfaceIds = portMap.getAllInterfaceIds();
  EXPECT_GT(interfaceIds.size(), 0);

  // Check that some expected interfaces are in the list
  bool found2001 = false;
  bool found2002 = false;
  for (const auto& id : interfaceIds) {
    if (id == InterfaceID(2001)) {
      found2001 = true;
    }
    if (id == InterfaceID(2002)) {
      found2002 = true;
    }
  }
  EXPECT_TRUE(found2001);
  EXPECT_TRUE(found2002);
}

// Test non-existent port/interface lookups
TEST_F(PortMapTest, NonExistentLookups) {
  auto config =
      loadConfig("fboss/oss/link_test_configs/minipack3n.materialized_JSON");
  PortMap portMap(config);

  // Test non-existent port name
  auto intfId = portMap.getInterfaceIdForPort("eth99/99/99");
  EXPECT_FALSE(intfId.has_value());

  // Test non-existent interface ID
  auto portName = portMap.getPortNameForInterface(InterfaceID(9999));
  EXPECT_FALSE(portName.has_value());
}

// Test with multiple mapping strategies in montblanc config
TEST_F(PortMapTest, MontblancMultiplePorts) {
  auto config =
      loadConfig("fboss/oss/link_test_configs/montblanc.materialized_JSON");
  PortMap portMap(config);

  // Test multiple ports to ensure consistency
  std::vector<std::pair<std::string, InterfaceID>> expectedMappings = {
      {"eth1/1/1", InterfaceID(2001)},
      {"eth1/2/1", InterfaceID(2003)},
      {"eth1/2/5", InterfaceID(2004)},
  };

  for (const auto& [portName, expectedIntfId] : expectedMappings) {
    auto intfId = portMap.getInterfaceIdForPort(portName);
    ASSERT_TRUE(intfId.has_value())
        << "Port " << portName << " should map to an interface";
    EXPECT_EQ(*intfId, expectedIntfId)
        << "Port " << portName << " should map to interface " << expectedIntfId;

    // Test reverse mapping
    auto reversedPortName = portMap.getPortNameForInterface(expectedIntfId);
    ASSERT_TRUE(reversedPortName.has_value())
        << "Interface " << expectedIntfId << " should map to a port";
    EXPECT_EQ(*reversedPortName, portName)
        << "Interface " << expectedIntfId << " should map to port " << portName;
  }
}

// Test that interfaces without port mappings are handled correctly
TEST_F(PortMapTest, InterfacesWithoutPortMapping) {
  auto config =
      loadConfig("fboss/oss/link_test_configs/minipack3n.materialized_JSON");
  PortMap portMap(config);

  // Interface 10 is a virtual interface (vlanID 1) and may not have a port
  // mapping
  auto portName = portMap.getPortNameForInterface(InterfaceID(10));
  // This may or may not have a mapping depending on the config
  // Just verify it doesn't crash
  if (portName.has_value()) {
    EXPECT_FALSE(portName->empty());
  }
}

// Test that duplicate port mappings are rejected
TEST_F(PortMapTest, DuplicatePortMappingKeepsFirst) {
  // Load a valid config
  auto config =
      loadConfig("fboss/oss/link_test_configs/minipack3n.materialized_JSON");

  // Modify the config to create a duplicate port mapping
  // Find two interfaces and make them both point to the same port
  auto& interfaces = *config.sw()->interfaces();
  if (interfaces.size() >= 2) {
    // Get the portID from the first interface
    auto firstInterfacePortId = interfaces[0].portID();
    auto firstInterfaceId = InterfaceID(*interfaces[0].intfID());
    if (firstInterfacePortId.has_value()) {
      // Make the second interface point to the same port
      interfaces[1].portID() = *firstInterfacePortId;

      // Building the PortMap should succeed (no exception thrown).
      // A single port may be associated with multiple VLANs (and thus
      // multiple interfaces). The first mapping is kept and subsequent
      // ones are skipped.
      PortMap portMap(config);

      // The first interface mapping should be kept
      auto portName = portMap.getPortNameForInterface(firstInterfaceId);
      EXPECT_TRUE(portName.has_value());
    }
  }
}

// Parameterized test to load all config files from link_test_configs directory
class PortMapAllConfigsTest : public ::testing::TestWithParam<std::string> {};

// Test that we can successfully build a PortMap for all config files
TEST_P(PortMapAllConfigsTest, CanLoadConfig) {
  const std::string& relativePath = GetParam();

  // Load the config
  auto config = loadConfig(relativePath);

  // Build the PortMap - this should not throw
  PortMap portMap(config);

  // Verify that the PortMap has some ports
  auto portNames = portMap.getAllPortNames();
  EXPECT_GT(portNames.size(), 0)
      << "Config " << relativePath << " should have at least one port";

  // Verify that we can get Port objects for all port names
  for (const auto& portName : portNames) {
    auto* port = portMap.getPort(portName);
    EXPECT_NE(port, nullptr) << "Port " << portName << " in config "
                             << relativePath << " should be retrievable";
  }

  // Verify that we can get Interface objects for all interface IDs
  // Note: Some configs may not have interfaces defined, so we only check
  // if there are any
  auto interfaceIds = portMap.getAllInterfaceIds();
  for (const auto& interfaceId : interfaceIds) {
    auto* interface = portMap.getInterface(interfaceId);
    EXPECT_NE(interface, nullptr)
        << "Interface " << interfaceId << " in config " << relativePath
        << " should be retrievable";
  }
}

// Helper function to get all config files from the link_test_configs directory
std::vector<std::string> getAllConfigFiles() {
  std::vector<std::string> configFiles;
  std::string configDir = getConfigPath("fboss/oss/link_test_configs/");

  try {
    for (const auto& entry : fs::directory_iterator(configDir)) {
      if (entry.is_regular_file()) {
        std::string filename = entry.path().filename().string();
        // Only include .materialized_JSON files
        if (filename.find(".materialized_JSON") != std::string::npos) {
          // Store relative path for portability
          configFiles.push_back("fboss/oss/link_test_configs/" + filename);
        }
      }
    }
  } catch (const std::exception& e) {
    // If directory doesn't exist or can't be read, return empty vector
    std::cerr << "Warning: Could not read config directory: " << e.what()
              << std::endl;
  }

  // Sort for consistent test ordering
  std::sort(configFiles.begin(), configFiles.end());
  return configFiles;
}

INSTANTIATE_TEST_SUITE_P(
    AllConfigs,
    PortMapAllConfigsTest,
    ::testing::ValuesIn(getAllConfigFiles()),
    [](const ::testing::TestParamInfo<std::string>& info) {
      // Extract just the filename (without path and extension) for test name
      std::string filename = fs::path(info.param).filename().string();
      // Remove .materialized_JSON extension
      size_t pos = filename.find(".materialized_JSON");
      if (pos != std::string::npos) {
        filename = filename.substr(0, pos);
      }
      return filename;
    });

} // namespace facebook::fboss::utils
