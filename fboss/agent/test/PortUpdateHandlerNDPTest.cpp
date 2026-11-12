/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;

namespace {

constexpr auto DESIRED_PEER_ADDRESS_IPV6 = "2401:db00:2110:3005::1/64";
constexpr auto DESIRED_PEER_ADDRESS_IPV6_2 = "2001:db8:100::2/64";
constexpr auto VLAN_ID = 100;
constexpr auto INTERFACE_ID = 100;
constexpr auto PORT_ID = 1;
constexpr auto PORT_ID_5 = 5;

cfg::SwitchConfig createVlanInterfaceConfig() {
  cfg::SwitchConfig config;
  config.switchSettings()->switchIdToSwitchInfo() = {
      std::make_pair(0, createSwitchInfo(cfg::SwitchType::NPU))};

  // Create a simpler VLAN setup with only one port
  config.vlans()->resize(1);
  *config.vlans()[0].name() = "TestVlan";
  *config.vlans()[0].id() = VLAN_ID;
  *config.vlans()[0].routable() = true;
  config.vlans()[0].intfID() = INTERFACE_ID;

  config.vlanPorts()->resize(1);
  config.ports()->resize(1);

  // Setup only one port for VLAN interface testing
  preparedMockPortConfig(config.ports()[0], PORT_ID);
  *config.ports()[0].routable() = true;
  *config.ports()[0].ingressVlan() = VLAN_ID;

  *config.vlanPorts()[0].vlanID() = VLAN_ID;
  *config.vlanPorts()[0].logicalPort() = PORT_ID;
  *config.vlanPorts()[0].spanningTreeState() =
      cfg::SpanningTreeState::FORWARDING;
  *config.vlanPorts()[0].emitTags() = 0;

  // Add VLAN interface configuration
  config.interfaces()->resize(1);
  *config.interfaces()[0].intfID() = INTERFACE_ID;
  *config.interfaces()[0].vlanID() = VLAN_ID;
  config.interfaces()[0].routerID() = 0;
  config.interfaces()[0].type() = cfg::InterfaceType::VLAN;
  config.interfaces()[0].name() = "VlanInterface100";
  config.interfaces()[0].mtu() = 1500;
  config.interfaces()[0].ipAddresses()->resize(1);
  config.interfaces()[0].ipAddresses()[0] = "2001:db8:100::1/64";

  // Configure Router Advertisement and desiredPeerAddressIPv6
  config.interfaces()[0].ndp() = cfg::NdpConfig();
  *config.interfaces()[0].ndp()->routerAdvertisementSeconds() = 30;
  config.interfaces()[0].desiredPeerAddressIPv6() = DESIRED_PEER_ADDRESS_IPV6_2;

  return config;
}

cfg::SwitchConfig createSystemPortInterfaceConfig() {
  auto config = testConfigA(cfg::SwitchType::VOQ);
  // Configure Router Advertisement and desiredPeerAddressIPv6
  XLOG(DBG2) << " config.interfaces()[0]: "
             << static_cast<int>(config.interfaces()[0].intfID().value());
  config.interfaces()[0].ndp() = cfg::NdpConfig();
  *config.interfaces()[0].ndp()->routerAdvertisementSeconds() = 30;
  config.interfaces()[0].desiredPeerAddressIPv6() = DESIRED_PEER_ADDRESS_IPV6;
  return config;
}

} // namespace
class PortUpdateHandlerNDPTest : public ::testing::Test {
 public:
  void SetUp() override {
    // Enable neighbor cache so that class id is set
    FLAGS_disable_neighbor_updates = false;
    FLAGS_ndp_static_neighbor = true;
  }

  std::unique_ptr<HwTestHandle> setupTestHandle(cfg::SwitchConfig config) {
    auto handle = createTestHandle(&config);
    sw_ = handle->getSw();
    sw_->initialConfigApplied(std::chrono::steady_clock::now());
    return handle;
  }

 protected:
  // Helper function to verify neighbor solicitation for a given interface and
  // target IP
  void verifyNeighborSolicitation(
      SwSwitch* sw,
      InterfaceID intfId,
      const std::string& expectedDesiredPeer,
      cfg::InterfaceType expectedType) {
    auto state = sw->getState();
    auto interfaces = state->getInterfaces();
    auto intf = interfaces->getNode(intfId);
    EXPECT_NE(intf, nullptr);

    // Verify interface type
    EXPECT_EQ(intf->getType(), expectedType);

    // For SYSTEM_PORT interfaces, verify system port ID
    if (expectedType == cfg::InterfaceType::SYSTEM_PORT) {
      EXPECT_TRUE(intf->getSystemPortID().has_value());
    }

    // Check if desiredPeerAddressIPv6 is configured
    auto desiredPeer = intf->getDesiredPeerAddressIPv6();
    EXPECT_TRUE(desiredPeer.has_value());
    EXPECT_EQ(desiredPeer.value(), expectedDesiredPeer);

    // Log the conditions that should trigger sendNdpSolicitationHelper
    auto desiredPeerAddressString = intf->getDesiredPeerAddressIPv6();
    // Use folly's built-in network parsing for CIDR notation
    auto cidrNetwork =
        folly::IPAddress::createNetwork(*desiredPeerAddressString, -1, false);
    auto targetIP = cidrNetwork.first.asV6();

    XLOG(INFO)
        << "verifyNeighborSolicitation: All conditions met - Router Advertisement enabled,"
        << "desiredPeerAddressIPv6 configured ("
        << intf->getDesiredPeerAddressIPv6().value()
        << "), interface associated with port. "
        << "sendNdpSolicitationHelper should be called.";

    // Verify that neighbor solicitation was sent by checking NDP cache
    XLOG(INFO)
        << "verifyNeighborSolicitation: Checking NDP cache for evidence on neighbor solicitation ";

    // Get all NDP entries from neighbor updater
    bool foundEntry = false;
    XLOG(DBG2) << "verify neighbor cache entry for " << targetIP.str();
    WITH_RETRIES({
      auto neighborCache =
          sw->getNeighborUpdater()->getNdpCacheDataForIntf().get();
      XLOG(DBG2) << "Neighbor cache size: " << neighborCache.size();
      auto cacheEntry = std::find_if(
          neighborCache.begin(),
          neighborCache.end(),
          [&targetIP](const auto& entry) {
            return entry.ip() == facebook::network::toBinaryAddress(targetIP);
          });
      XLOG(DBG2) << "NDP : " << targetIP.str();
      // verify neighbor cache entry and neighbor table entry
      EXPECT_EVENTUALLY_TRUE(cacheEntry != neighborCache.end());
      EXPECT_EVENTUALLY_TRUE(neighborCache.size() == 1);
      XLOG(DBG2) << "Neighbor entry matched";
      foundEntry = true;
      XLOG(DBG2) << "verifyNeighborSolicitation:foundEntry: " << foundEntry
                 << " neighborCache.size():" << neighborCache.size();
    });
    XLOG(DBG2) << "verifyNeighborSolicitation:foundEntry: " << foundEntry;
    if (foundEntry) {
      XLOG(INFO)
          << "verifyNeighborSolicitation: CONFIRMED - Neighbor solicitation was sent for "
          << targetIP.str() << " as evidenced by NeighborUpdatercache";
      auto maxNeighborProbes =
          sw->getNeighborUpdater()->getMaxNeighborProbes(intf->getID()).get();
      WITH_RETRIES({
        auto probesLeft = sw->getNeighborUpdater()
                              ->getProbesLeft(intf->getID(), targetIP)
                              .get();
        XLOG(DBG2) << "Total neighbor SolicitationHelper sent: "
                   << maxNeighborProbes - probesLeft;
        EXPECT_EVENTUALLY_GT(maxNeighborProbes - probesLeft, 4);
      });
    }
  }

  SwSwitch* sw_{};
};

// Test that NDP solicitation is NOT called when desiredPeerAddressIPv6 is
// missing
TEST_F(PortUpdateHandlerNDPTest, VlanInterfaceNoDesiredPeer) {
  auto config = createVlanInterfaceConfig();
  // Remove desiredPeerAddressIPv6
  config.interfaces()[0].desiredPeerAddressIPv6().reset();

  auto handle = setupTestHandle(config);

  // Initially set port to DOWN
  sw_->linkStateChanged(PortID(PORT_ID), false, cfg::PortType::INTERFACE_PORT);

  // Bring port UP - should NOT trigger NDP solicitation due to missing
  // desiredPeerAddressIPv6
  sw_->linkStateChanged(PortID(PORT_ID), true, cfg::PortType::INTERFACE_PORT);

  // Verify interface exists but lacks desiredPeerAddressIPv6
  auto state = sw_->getState();
  auto interfaces = state->getInterfaces();
  auto intf = interfaces->getNode(InterfaceID(100));
  EXPECT_NE(intf, nullptr);

  // Verify desiredPeerAddressIPv6 is not set
  auto desiredPeer = intf->getDesiredPeerAddressIPv6();
  EXPECT_FALSE(desiredPeer.has_value());
}

// Test VLAN interface handling
TEST_F(PortUpdateHandlerNDPTest, VlanInterfacePortUp) {
  auto config = createVlanInterfaceConfig();
  // The port enable during initial config apply also triggers handlePortUp,
  // so configure desiredPeerAddressIPv6 only once the port is DOWN.
  config.interfaces()[0].desiredPeerAddressIPv6().reset();
  auto handle = setupTestHandle(config);

  // Initially set port to DOWN
  sw_->linkStateChanged(PortID(PORT_ID), false, cfg::PortType::INTERFACE_PORT);
  config.interfaces()[0].desiredPeerAddressIPv6() = DESIRED_PEER_ADDRESS_IPV6_2;
  sw_->applyConfig("Add desiredPeerAddressIPv6", config);
  waitForStateUpdates(sw_);
  waitForNeighborCacheThread(sw_);
  {
    auto neighborCache =
        sw_->getNeighborUpdater()->getNdpCacheDataForIntf().get();
    EXPECT_EQ(neighborCache.size(), 0);
  }
  // Bring port UP
  sw_->linkStateChanged(PortID(PORT_ID), true, cfg::PortType::INTERFACE_PORT);

  // Verify behavior based on current implementation
  verifyNeighborSolicitation(
      sw_,
      InterfaceID(INTERFACE_ID),
      DESIRED_PEER_ADDRESS_IPV6_2,
      cfg::InterfaceType::VLAN);
}

// Test port UP but interface operationally DOWN
TEST_F(PortUpdateHandlerNDPTest, VlanUpInterfaceDown) {
  auto config = createVlanInterfaceConfig();
  auto handle = setupTestHandle(config);

  // Initially set port to DOWN and also disable it administratively
  sw_->linkStateChanged(PortID(PORT_ID), false, cfg::PortType::INTERFACE_PORT);

  // Update state to disable port administratively
  auto updateFn = [](const std::shared_ptr<SwitchState>& state) {
    auto newState = state->clone();
    auto port =
        newState->getPorts()->getNode(PortID(PORT_ID))->modify(&newState);
    port->setAdminState(cfg::PortState::DISABLED);
    return newState;
  };
  sw_->updateState("Disable port admin", updateFn);

  // Bring port UP operationally, but it's still disabled administratively
  sw_->linkStateChanged(PortID(PORT_ID), true, cfg::PortType::INTERFACE_PORT);
}

// Unit tests for system port interface handling
// Test SYSTEM_PORT interface handling
TEST_F(PortUpdateHandlerNDPTest, SystemPortInterfacePortUp) {
  auto config = createSystemPortInterfaceConfig();
  auto handle = setupTestHandle(config);

  // Initially set port to DOWN
  sw_->linkStateChanged(
      PortID(PORT_ID_5), false, cfg::PortType::INTERFACE_PORT);

  // Bring port UP - should trigger NDP solicitation for SYSTEM_PORT interface
  sw_->linkStateChanged(PortID(PORT_ID_5), true, cfg::PortType::INTERFACE_PORT);

  verifyNeighborSolicitation(
      sw_,
      InterfaceID(5),
      DESIRED_PEER_ADDRESS_IPV6,
      cfg::InterfaceType::SYSTEM_PORT);
}

TEST_F(PortUpdateHandlerNDPTest, SystemPortInterfaceUpdate) {
  auto config = testConfigA(cfg::SwitchType::VOQ);
  auto handle = setupTestHandle(config);

  { // Verify behavior based on current implementation
    auto state = sw_->getState();
    auto interfaces = state->getInterfaces();
    auto intf = interfaces->getNode(InterfaceID(5));
    EXPECT_NE(intf, nullptr);

    // For SYSTEM_PORT interfaces, verify configuration
    EXPECT_EQ(intf->getType(), cfg::InterfaceType::SYSTEM_PORT);
    EXPECT_TRUE(intf->getSystemPortID().has_value());

    // Check if desiredPeerAddressIPv6 is not configured
    auto desiredPeer = intf->getDesiredPeerAddressIPv6();
    EXPECT_FALSE(desiredPeer.has_value());
  }
  // set port to DOWN
  sw_->linkStateChanged(
      PortID(PORT_ID_5), false, cfg::PortType::INTERFACE_PORT);
  // update desiredPeerAddressIPv6
  config.interfaces()[0].desiredPeerAddressIPv6() = DESIRED_PEER_ADDRESS_IPV6;

  sw_->applyConfig("applyConfig", config);
  // Bring port UP - should trigger NDP solicitation for SYSTEM_PORT interface
  sw_->linkStateChanged(PortID(PORT_ID_5), true, cfg::PortType::INTERFACE_PORT);

  // verify
  verifyNeighborSolicitation(
      sw_,
      InterfaceID(5),
      DESIRED_PEER_ADDRESS_IPV6,
      cfg::InterfaceType::SYSTEM_PORT);
}
