/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

/**
 * @file TunManagerRouteProcessorTest.cpp
 * @brief Unit tests for TunManager's routeProcessor functionality
 * @author Ashutosh Grewal (agrewal@meta.com)
 */

/**
 * @def TUNMANAGER_ROUTE_PROCESSOR_FRIEND_TESTS
 * @brief Macro defining friend declarations for TunManager route processor
 * tests
 *
 * This macro contains all the friend class and FRIEND_TEST declarations needed
 * to access private members of TunManager for testing the routeProcessor
 * functionality.
 */
#define TUNMANAGER_ROUTE_PROCESSOR_FRIEND_TESTS                                \
  friend class TunManagerRouteProcessorTest;                                   \
  friend class TunManagerAddressRuleTest;                                      \
  FRIEND_TEST(TunManagerRouteProcessorTest, ProcessIPv4DefaultRoute);          \
  FRIEND_TEST(TunManagerRouteProcessorTest, ProcessIPv6DefaultRoute);          \
  FRIEND_TEST(TunManagerRouteProcessorTest, SkipUnsupportedAddressFamily);     \
  FRIEND_TEST(TunManagerRouteProcessorTest, SkipInvalidTableId);               \
  FRIEND_TEST(TunManagerRouteProcessorTest, SkipNonDefaultRoutes);             \
  FRIEND_TEST(TunManagerRouteProcessorTest, DeleteProbedRoutesIPv4Default);    \
  FRIEND_TEST(TunManagerRouteProcessorTest, DeleteProbedRoutesIPv6Default);    \
  FRIEND_TEST(TunManagerRouteProcessorTest, DeleteProbedRoutesBothV4AndV6);    \
  FRIEND_TEST(TunManagerRouteProcessorTest, DeleteProbedRoutesMultipleTables); \
  FRIEND_TEST(TunManagerRouteProcessorTest, DeleteProbedRoutesEmptyList);      \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest, DeleteProbedRoutesExceptionHandling);      \
  FRIEND_TEST(                                                                 \
      TunManagerAddressRuleTest,                                               \
      DeleteProbedAddressesAndRulesHandleExceptions);                          \
  FRIEND_TEST(                                                                 \
      TunManagerAddressRuleTest,                                               \
      DeleteProbedAddressesAndRulesAddressException);                          \
  FRIEND_TEST(                                                                 \
      TunManagerAddressRuleTest,                                               \
      DeleteProbedAddressesAndRulesSingleInterface);                           \
  FRIEND_TEST(                                                                 \
      TunManagerAddressRuleTest,                                               \
      DeleteProbedAddressesAndRulesMultipleInterfaces);                        \
  FRIEND_TEST(                                                                 \
      TunManagerAddressRuleTest,                                               \
      DeleteProbedAddressesAndRulesEmptyInterfaces);                           \
  FRIEND_TEST(                                                                 \
      TunManagerAddressRuleTest,                                               \
      DeleteProbedAddressesAndRulesNoTableIdMapping);                          \
  FRIEND_TEST(TunManagerAddressRuleTest, DeleteProbedInterfaces);              \
  FRIEND_TEST(TunManagerRouteProcessorTest, BuildIfIdToTableIdMapBasic);       \
  FRIEND_TEST(TunManagerRouteProcessorTest, BuildProbedIfIdToTableIdMapBasic); \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest, BuildProbedIfIdToTableIdMapEmptyRoutes);   \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest,                                            \
      BuildProbedIfIdToTableIdMapEmptyInterfaces);                             \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest, BuildProbedIfIdToTableIdMapMixedScenario); \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest,                                            \
      BuildProbedIfIdToTableIdMapInvalidIfIndex);                              \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest, RequiresProbedDataCleanupIdentical);       \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest, RequiresProbedDataCleanupDifferent);       \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest, RequiresProbedDataCleanupEmptyMaps);       \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest, RequiresProbedDataCleanupSizeDiff);        \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest, RequiresProbedDataCleanupMissingKeys);     \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest,                                            \
      BuildProbedIfIdToTableIdMapWithRulesAugmentation);                       \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest,                                            \
      BuildProbedIfIdToTableIdMapRulesOnlyFallback);                           \
  FRIEND_TEST(                                                                 \
      TunManagerRouteProcessorTest,                                            \
      BuildProbedIfIdToTableIdMapRoutesHavePriority);                          \
  FRIEND_TEST(                                                                 \
      TunManagerAddressRuleTest,                                               \
      DeleteProbedAddressesAndRulesWithRemainingRules);                        \
  FRIEND_TEST(                                                                 \
      TunManagerAddressRuleTest,                                               \
      DeleteProbedAddressesAndRulesRemainingRulesException);

#include <gmock/gmock.h>
#include <gtest/gtest.h>

extern "C" {
#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <netlink/addr.h>
#include <netlink/object.h>
#include <netlink/route/nexthop.h>
#include <netlink/route/route.h>
}

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TunManager.h"
#include "fboss/agent/test/MockTunIntf.h"
#include "fboss/agent/test/MockTunManager.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;
using ::testing::_;

namespace facebook::fboss {

/**
 * @class TunManagerRouteProcessorTest
 * @brief Test fixture for TunManager::routeProcessor functionality
 */
class TunManagerRouteProcessorTest : public ::testing::Test {
 public:
  /**
   * @brief Set up test environment before each test
   *
   * Creates mock platform, switch, and TUN manager instances.
   */
  void SetUp() override {
    platform_ = createMockPlatform();
    sw_ = setupMockSwitchWithoutHW(
        platform_.get(), nullptr, SwitchFlags::ENABLE_TUN);
    tunMgr_ = dynamic_cast<MockTunManager*>(sw_->getTunManager());
    ASSERT_NE(nullptr, tunMgr_);
  }

  /**
   * @brief Clean up test environment after each test
   *
   * Properly destroys mock objects in correct order to avoid memory
   * leaks.
   */
  void TearDown() override {
    // Reset in proper order: sw_ first, then platform_
    sw_.reset();
    platform_.reset();
  }

  static void addProbedRoute(
      TunManager* tunMgr,
      int family,
      int tableId,
      const std::string& destination,
      int ifIndex) {
    TunManager::ProbedRoute route;
    route.family = family;
    route.tableId = tableId;
    route.destination = destination;
    route.ifIndex = ifIndex;
    tunMgr->probedRoutes_.push_back(route);
  }

  /**
   * @brief Build interface index to table ID mapping from probed routes
   *
   * @param tunMgr Pointer to TunManager instance containing probed routes
   * @return std::unordered_map<int, int> Map from interface index to
   * table ID
   */
  static std::unordered_map<int, int> buildIfIndexToTableIdMap(
      TunManager* tunMgr) {
    std::unordered_map<int, int> ifIndexToTableId;
    for (const auto& probedRoute : tunMgr->probedRoutes_) {
      if (probedRoute.ifIndex > 0) {
        ifIndexToTableId[probedRoute.ifIndex] = probedRoute.tableId;
      }
    }
    return ifIndexToTableId;
  }

 protected:
  std::unique_ptr<MockPlatform> platform_; ///< Mock platform instance
  std::unique_ptr<SwSwitch> sw_; ///< Mock switch instance
  MockTunManager* tunMgr_{}; ///< Mock TUN manager for testing

  /**
   * @brief Helper function to test default route processing for both IPv4
   * and IPv6
   *
   * @param family Address family (AF_INET or AF_INET6)
   * @param tableId Routing table ID
   * @param ifIndex Interface index for nexthop
   * @param expectedDestination Expected destination string representation
   */
  void testProcessDefaultRoute(
      int family,
      int tableId,
      int ifIndex,
      const std::string& expectedDestination) {
    // Create a real netlink route object for default route
    auto route = rtnl_route_alloc();
    ASSERT_NE(nullptr, route);

    // Set up default route with specified parameters
    rtnl_route_set_family(route, family);
    rtnl_route_set_table(route, tableId);
    rtnl_route_set_protocol(route, TunManager::RTPROT_FBOSS);

    // Create destination address (default route)
    auto dst = nl_addr_build(family, nullptr, 0);
    ASSERT_NE(nullptr, dst);
    nl_addr_set_prefixlen(dst, 0);
    rtnl_route_set_dst(route, dst);

    // Add nexthop with interface index
    auto nexthop = rtnl_route_nh_alloc();
    ASSERT_NE(nullptr, nexthop);
    rtnl_route_nh_set_ifindex(nexthop, ifIndex);
    rtnl_route_add_nexthop(route, nexthop);

    // Call the actual routeProcessor function
    TunManager::routeProcessor(
        reinterpret_cast<struct nl_object*>(route),
        static_cast<void*>(tunMgr_));

    // Verify the route was stored by routeProcessor
    ASSERT_EQ(1, tunMgr_->probedRoutes_.size());

    const auto& probedRoute = tunMgr_->probedRoutes_[0];
    EXPECT_EQ(family, probedRoute.family);
    EXPECT_EQ(tableId, probedRoute.tableId);
    EXPECT_EQ(expectedDestination, probedRoute.destination);
    EXPECT_EQ(ifIndex, probedRoute.ifIndex);

    // Cleanup
    nl_addr_put(dst);
    rtnl_route_put(route);
  }

  /**
   * @brief Helper function to test non-default route filtering for both IPv4
   * and IPv6
   *
   * @param family Address family (AF_INET or AF_INET6)
   * @param tableId Routing table ID
   * @param addrStr Address string (e.g., "192.168.1.0" or "2001:db8::")
   * @param prefixLen Prefix length
   */
  void testSkipNonDefaultRoute(
      int family,
      int tableId,
      const std::string& addrStr,
      int prefixLen) {
    auto route = rtnl_route_alloc();
    ASSERT_NE(nullptr, route);

    rtnl_route_set_family(route, family);
    rtnl_route_set_table(route, tableId); // Valid table ID

    // Create a specific destination (non-default route)
    nl_addr* dst = nullptr;
    if (family == AF_INET) {
      struct in_addr addr4;
      inet_pton(AF_INET, addrStr.c_str(), &addr4);
      dst = nl_addr_build(AF_INET, &addr4, sizeof(addr4));
    } else if (family == AF_INET6) {
      struct in6_addr addr6;
      inet_pton(AF_INET6, addrStr.c_str(), &addr6);
      dst = nl_addr_build(AF_INET6, &addr6, sizeof(addr6));
    }

    ASSERT_NE(nullptr, dst);
    nl_addr_set_prefixlen(dst, prefixLen);
    rtnl_route_set_dst(route, dst);

    // Call the routeProcessor function
    TunManager::routeProcessor(
        reinterpret_cast<struct nl_object*>(route),
        static_cast<void*>(tunMgr_));

    // Verify no routes were stored
    EXPECT_EQ(0, tunMgr_->probedRoutes_.size());

    // Cleanup
    nl_addr_put(dst);
    rtnl_route_put(route);
  }
};

/**
 * @class TunManagerAddressRuleTest
 * @brief Test fixture for TunManager address rule functionality=
 */
class TunManagerAddressRuleTest : public TunManagerRouteProcessorTest {
 public:
  /**
   * @brief Helper method to add a mock interface with specified addresses
   *
   * @param ifID Interface ID
   * @param ifIndex Interface index
   * @param ifName Interface name
   * @param addresses Vector of IP address and prefix length pairs
   */
  void addMockInterface(
      const InterfaceID& ifID,
      int ifIndex,
      const std::string& ifName,
      const std::vector<std::pair<folly::IPAddress, uint8_t>>& addresses) {
    auto mockIntf = std::make_unique<MockTunIntf>(
        ifID,
        ifName,
        ifIndex,
        1500 // MTU
    );

    // Set the interface addresses using the test helper method
    Interface::Addresses addrs;
    for (const auto& addr : addresses) {
      addrs.emplace(addr.first, addr.second);
    }
    mockIntf->setTestAddresses(addrs);

    tunMgr_->intfs_[ifID] = std::move(mockIntf);
  }

  /**
   * @brief Helper method to create ifIndexToTableId mapping
   *
   * @param mappings Vector of (ifIndex, tableId) pairs
   * @return std::unordered_map<int, int> Map from interface index to
   * table ID
   */
  std::unordered_map<int, int> createIfIndexToTableIdMap(
      const std::vector<std::pair<int, int>>& mappings) {
    std::unordered_map<int, int> ifIndexToTableId;
    for (const auto& mapping : mappings) {
      ifIndexToTableId[mapping.first] = mapping.second;
    }
    return ifIndexToTableId;
  }
};

/**
 * @brief Test processing of IPv4 default route
 *
 * Verifies that routeProcessor correctly processes and stores an IPv4
 * default route (0.0.0.0/0) with valid parameters.
 */
TEST_F(TunManagerRouteProcessorTest, ProcessIPv4DefaultRoute) {
  testProcessDefaultRoute(AF_INET, 100, 42, "0.0.0.0/0");
}

/**
 * @brief Test processing of IPv6 default route
 *
 * Verifies that routeProcessor correctly processes and stores an IPv6
 * default route (::/0) with valid parameters.
 */
TEST_F(TunManagerRouteProcessorTest, ProcessIPv6DefaultRoute) {
  testProcessDefaultRoute(AF_INET6, 150, 24, "::/0");
}

/**
 * @brief Test filtering of unsupported address families
 *
 * Verifies that routeProcessor correctly filters out and ignores routes
 * with unsupported address families. The function should only process
 * AF_INET and AF_INET6 routes, ignoring all others.
 */
TEST_F(TunManagerRouteProcessorTest, SkipUnsupportedAddressFamily) {
  // Create a route with unsupported address family
  auto route = rtnl_route_alloc();
  ASSERT_NE(nullptr, route);

  rtnl_route_set_family(route, AF_UNIX); // Unsupported family
  rtnl_route_set_table(route, 100);

  // Call the routeProcessor function
  TunManager::routeProcessor(
      reinterpret_cast<struct nl_object*>(route), static_cast<void*>(tunMgr_));

  // Verify no routes were stored
  EXPECT_EQ(0, tunMgr_->probedRoutes_.size());

  // Cleanup
  rtnl_route_put(route);
}

/**
 * @brief Test filtering of invalid table IDs
 *
 * Verifies that routeProcessor correctly filters out routes with invalid
 * table IDs. The function should only process routes with table IDs in
 * the range [1-253], as table IDs 0, 254, and 255 are reserved by the
 * kernel.
 *
 * This test uses table ID 254 (outside the valid range) and expects that
 * no routes are stored in probedRoutes_, demonstrating proper table ID
 * validation.
 */
TEST_F(TunManagerRouteProcessorTest, SkipInvalidTableId) {
  // Create a route with invalid table ID
  auto route = rtnl_route_alloc();
  ASSERT_NE(nullptr, route);

  rtnl_route_set_family(route, AF_INET);
  rtnl_route_set_table(route, 254); // Outside valid range [1-253]

  // Call the routeProcessor function
  TunManager::routeProcessor(
      reinterpret_cast<struct nl_object*>(route), static_cast<void*>(tunMgr_));

  // Verify no routes were stored
  EXPECT_EQ(0, tunMgr_->probedRoutes_.size());

  // Cleanup
  rtnl_route_put(route);
}

/**
 * @brief Test filtering of non-default routes
 *
 * Verifies that routeProcessor correctly filters out and ignores routes
 * that are not default routes. The function should only process routes
 * where the destination is "none" (which represents default routes in
 * kernel output), and ignore all other routes like specific network prefixes.
 */
TEST_F(TunManagerRouteProcessorTest, SkipNonDefaultRoutes) {
  // Test IPv4 non-default routes using helper function
  testSkipNonDefaultRoute(AF_INET, 100, "192.168.1.0", 24);
  testSkipNonDefaultRoute(AF_INET, 150, "10.0.0.0", 16);
  testSkipNonDefaultRoute(AF_INET, 200, "127.0.0.1", 8);

  // Test IPv6 non-default routes using helper function
  testSkipNonDefaultRoute(AF_INET6, 100, "2001:db8::", 64);
  testSkipNonDefaultRoute(AF_INET6, 150, "fe80::", 64);
}

/**
 * @brief Test deletion of probed IPv4 default routes
 *
 * Verifies that deleteProbedRoutes correctly identifies and removes IPv4
 * default routes (0.0.0.0/0) by calling addRemoveRouteTable with delete
 * flag.
 */
TEST_F(TunManagerRouteProcessorTest, DeleteProbedRoutesIPv4Default) {
  // Add IPv4 default route to probed routes
  addProbedRoute(tunMgr_, AF_INET, 100, "0.0.0.0/0", 42);

  EXPECT_CALL(
      *tunMgr_,
      addRemoveRouteTable(100, 42, false, ::testing::Eq(std::nullopt)))
      .Times(1);

  // Build ifIndexToTableId map from probed routes
  auto ifIndexToTableId = buildIfIndexToTableIdMap(tunMgr_);

  // Call deleteProbedRoutes
  tunMgr_->deleteProbedRoutes(ifIndexToTableId);

  // Verify probed routes are cleared;
  EXPECT_EQ(0, tunMgr_->probedRoutes_.size());
}

/**
 * @brief Test deletion of probed IPv6 default routes
 *
 * Verifies that deleteProbedRoutes correctly identifies and removes IPv6
 * default routes (::/0) by calling addRemoveRouteTable with delete flag.
 */
TEST_F(TunManagerRouteProcessorTest, DeleteProbedRoutesIPv6Default) {
  // Add IPv6 default route to probed routes
  addProbedRoute(tunMgr_, AF_INET6, 150, "::/0", 24);

  EXPECT_CALL(
      *tunMgr_,
      addRemoveRouteTable(150, 24, false, ::testing::Eq(std::nullopt)))
      .Times(1);

  // Build ifIndexToTableId map from probed routes
  auto ifIndexToTableId = buildIfIndexToTableIdMap(tunMgr_);

  // Call deleteProbedRoutes
  tunMgr_->deleteProbedRoutes(ifIndexToTableId);

  // Verify probed routes are cleared;
  EXPECT_EQ(0, tunMgr_->probedRoutes_.size());
}

/**
 * @brief Test deletion of probed routes with both IPv4 and IPv6 default
 * routes
 *
 * Verifies that deleteProbedRoutes correctly handles multiple routes
 * (both IPv4 and IPv6) but only makes one call to addRemoveRouteTable.
 */
TEST_F(TunManagerRouteProcessorTest, DeleteProbedRoutesBothV4AndV6) {
  // Add both IPv4 and IPv6 default routes to probed routes
  addProbedRoute(tunMgr_, AF_INET, 100, "0.0.0.0/0", 42);
  addProbedRoute(tunMgr_, AF_INET6, 100, "::/0", 42);

  // Expect only one call to addRemoveRouteTable despite having two routes
  EXPECT_CALL(
      *tunMgr_,
      addRemoveRouteTable(100, 42, false, ::testing::Eq(std::nullopt)))
      .Times(1);

  // Build ifIndexToTableId map from probed routes
  auto ifIndexToTableId = buildIfIndexToTableIdMap(tunMgr_);

  // Call deleteProbedRoutes
  tunMgr_->deleteProbedRoutes(ifIndexToTableId);

  // Verify probed routes are cleared
  EXPECT_EQ(0, tunMgr_->probedRoutes_.size());
}

/**
 * @brief Test deletion of probed routes with IPv4 and IPv6 default routes
 * in two tables
 *
 * Verifies that deleteProbedRoutes correctly handles in  different table
 * IDs and makes separate calls to addRemoveRouteTable for each table.
 */
TEST_F(TunManagerRouteProcessorTest, DeleteProbedRoutesMultipleTables) {
  // Add IPv4 default route to table 100 and IPv6 default route to table
  // 200
  addProbedRoute(tunMgr_, AF_INET, 100, "0.0.0.0/0", 42);
  addProbedRoute(tunMgr_, AF_INET6, 100, "::/0", 42);

  addProbedRoute(tunMgr_, AF_INET, 200, "0.0.0.0/0", 24);
  addProbedRoute(tunMgr_, AF_INET6, 200, "::/0", 24);

  // Expect two separate calls to addRemoveRouteTable for different tables
  EXPECT_CALL(
      *tunMgr_,
      addRemoveRouteTable(100, 42, false, ::testing::Eq(std::nullopt)))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveRouteTable(200, 24, false, ::testing::Eq(std::nullopt)))
      .Times(1);

  // Build ifIndexToTableId map from probed routes
  auto ifIndexToTableId = buildIfIndexToTableIdMap(tunMgr_);

  // Call deleteProbedRoutes
  tunMgr_->deleteProbedRoutes(ifIndexToTableId);

  // Verify probed routes are cleared
  EXPECT_EQ(0, tunMgr_->probedRoutes_.size());
}

/**
 * @brief Test deletion of probed routes when no routes exist
 *
 * Verifies that deleteProbedRoutes correctly handles the case when there
 * are no routes in probedRoutes_. Should not make any calls to
 * addRemoveRouteTable.
 */
TEST_F(TunManagerRouteProcessorTest, DeleteProbedRoutesEmptyList) {
  // Verify probedRoutes_ starts empty
  EXPECT_EQ(0, tunMgr_->probedRoutes_.size());

  // Expect no calls to addRemoveRouteTable since there are no routes
  EXPECT_CALL(*tunMgr_, addRemoveRouteTable(_, _, _, _)).Times(0);

  // Build ifIndexToTableId map from probed routes
  auto ifIndexToTableId = buildIfIndexToTableIdMap(tunMgr_);

  // Call deleteProbedRoutes
  tunMgr_->deleteProbedRoutes(ifIndexToTableId);

  // Verify probedRoutes_ remains empty
  EXPECT_EQ(0, tunMgr_->probedRoutes_.size());
}

/**
 * @brief Test exception handling in deleteProbedRoutes
 *
 * Verifies that deleteProbedRoutes propagates exceptions thrown by
 * addRemoveRouteTable. When an exception occurs, the method should throw
 * and stop processing, leaving the probedRoutes_ list unchanged.
 */
TEST_F(TunManagerRouteProcessorTest, DeleteProbedRoutesExceptionHandling) {
  // Add multiple default routes in different tables
  addProbedRoute(tunMgr_, AF_INET, 100, "0.0.0.0/0", 42);
  addProbedRoute(tunMgr_, AF_INET6, 100, "::/0", 42);
  addProbedRoute(tunMgr_, AF_INET, 200, "0.0.0.0/0", 24);
  addProbedRoute(tunMgr_, AF_INET6, 200, "::/0", 24);

  // Set up mock to throw exception for table 100 but succeed for table
  // 200
  EXPECT_CALL(
      *tunMgr_,
      addRemoveRouteTable(100, 42, false, ::testing::Eq(std::nullopt)))
      .Times(1)
      .WillOnce(::testing::Throw(std::runtime_error("Netlink error")));

  EXPECT_CALL(
      *tunMgr_,
      addRemoveRouteTable(200, 24, false, ::testing::Eq(std::nullopt)))
      .Times(1);

  // Build ifIndexToTableId map from probed routes
  auto ifIndexToTableId = buildIfIndexToTableIdMap(tunMgr_);

  // Call deleteProbedRoutes - should throw when addRemoveRouteTable
  // throws
  EXPECT_THROW(
      tunMgr_->deleteProbedRoutes(ifIndexToTableId), std::runtime_error);

  // Verify probed routes are not cleared when exceptions occur
  EXPECT_EQ(tunMgr_->probedRoutes_.size(), 4);
}

/**
 * @brief Test deletion of source IP rules and TUN addresses for a single
 * interface
 *
 * Verifies cleanup of source IP routing rules and TUN interface addresses
 * for a single interface with mixed IPv4/IPv6 addresses.
 */
TEST_F(
    TunManagerAddressRuleTest,
    DeleteProbedAddressesAndRulesSingleInterface) {
  // Add a mock interface with addresses
  std::vector<std::pair<folly::IPAddress, uint8_t>> addresses = {
      {folly::IPAddress("10.1.1.1"), 24},
      {folly::IPAddress("2001:db8::1"), 64}};

  addMockInterface(InterfaceID(2000), 42, "fboss2000", addresses);

  // Create ifIndexToTableId map
  auto ifIndexToTableId = createIfIndexToTableIdMap({{42, 100}});

  // Expect calls to delete source rules and addresses
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          100,
          folly::IPAddress("10.1.1.1"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          100,
          folly::IPAddress("2001:db8::1"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2000", 42, folly::IPAddress("10.1.1.1"), 24, false))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2000", 42, folly::IPAddress("2001:db8::1"), 64, false))
      .Times(1);

  // Call deleteProbedAddressesAndRules
  tunMgr_->deleteProbedAddressesAndRules(ifIndexToTableId);
}

/**
 * @brief Test deletion of source IP rules and TUN addresses across
 * multiple interfaces
 *
 * Verifies cleanup of source IP routing rules (source IP → routing table
 * mapping) and TUN interface addresses for multiple interfaces with
 * different routing tables.
 */
TEST_F(
    TunManagerAddressRuleTest,
    DeleteProbedAddressesAndRulesMultipleInterfaces) {
  // Add multiple mock interfaces with addresses
  std::vector<std::pair<folly::IPAddress, uint8_t>> addresses1 = {
      {folly::IPAddress("10.1.1.1"), 24}};
  std::vector<std::pair<folly::IPAddress, uint8_t>> addresses2 = {
      {folly::IPAddress("10.2.2.2"), 24},
      {folly::IPAddress("2001:db8::2"), 64}};

  addMockInterface(InterfaceID(2000), 42, "fboss2000", addresses1);
  addMockInterface(InterfaceID(2001), 43, "fboss2001", addresses2);

  // Create ifIndexToTableId map
  auto ifIndexToTableId = createIfIndexToTableIdMap({{42, 100}, {43, 101}});

  // Expect calls to delete source rules and addresses for both interfaces
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          100,
          folly::IPAddress("10.1.1.1"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          101,
          folly::IPAddress("10.2.2.2"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          101,
          folly::IPAddress("2001:db8::2"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2000", 42, folly::IPAddress("10.1.1.1"), 24, false))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2001", 43, folly::IPAddress("10.2.2.2"), 24, false))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2001", 43, folly::IPAddress("2001:db8::2"), 64, false))
      .Times(1);

  // Call deleteProbedAddressesAndRules
  tunMgr_->deleteProbedAddressesAndRules(ifIndexToTableId);
}

/**
 * Test that deleteProbedAddressesAndRules propagates exceptions and stops
 * processing when addRemoveSourceRouteRule throws.
 */
TEST_F(
    TunManagerAddressRuleTest,
    DeleteProbedAddressesAndRulesHandleExceptions) {
  // Add a mock interface with addresses
  std::vector<std::pair<folly::IPAddress, uint8_t>> addresses = {
      {folly::IPAddress("10.1.1.1"), 24}, {folly::IPAddress("10.1.1.2"), 24}};

  addMockInterface(InterfaceID(2000), 42, "fboss2000", addresses);

  // Create ifIndexToTableId map
  auto ifIndexToTableId = createIfIndexToTableIdMap({{42, 100}});

  // Make the first addRemoveSourceRouteRule call throw an exception
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          100,
          folly::IPAddress("10.1.1.1"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1)
      .WillOnce(
          ::testing::Throw(std::runtime_error("Mock source rule exception")));

  // Since no exception handling exists, the function should throw and not
  // continue processing remaining addresses
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          100,
          folly::IPAddress("10.1.1.2"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(0);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2000", 42, folly::IPAddress("10.1.1.1"), 24, false))
      .Times(0);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2000", 42, folly::IPAddress("10.1.1.2"), 24, false))
      .Times(0);

  // Call deleteProbedAddressesAndRules - should throw exception since no
  // exception handling
  EXPECT_THROW(
      tunMgr_->deleteProbedAddressesAndRules(ifIndexToTableId),
      std::runtime_error);
}

/**
 * Test that deleteProbedAddressesAndRules handles exceptions gracefully
 * when addRemoveTunAddress throws during address deletion.
 */
TEST_F(
    TunManagerAddressRuleTest,
    DeleteProbedAddressesAndRulesAddressException) {
  // Add a mock interface with addresses
  std::vector<std::pair<folly::IPAddress, uint8_t>> addresses = {
      {folly::IPAddress("10.1.1.1"), 24}};

  addMockInterface(InterfaceID(2000), 42, "fboss2000", addresses);

  // Create ifIndexToTableId map
  auto ifIndexToTableId = createIfIndexToTableIdMap({{42, 100}});

  // Source rule call should succeed
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          100,
          folly::IPAddress("10.1.1.1"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1);

  // Make addRemoveTunAddress throw an exception
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2000", 42, folly::IPAddress("10.1.1.1"), 24, false))
      .Times(1)
      .WillOnce(::testing::Throw(std::runtime_error("Mock address exception")));

  // Call deleteProbedAddressesAndRules - should throw exception since no
  // exception handling for address deletion
  EXPECT_THROW(
      tunMgr_->deleteProbedAddressesAndRules(ifIndexToTableId),
      std::runtime_error);
}

/**
 * @brief Test deletion of source IP rules and TUN addresses with empty
 * interfaces
 *
 * Verifies that deleteProbedAddressesAndRules handles empty interface
 * mapping correctly without making any calls to addRemoveSourceRouteRule
 * or addRemoveTunAddress.
 */
TEST_F(
    TunManagerAddressRuleTest,
    DeleteProbedAddressesAndRulesEmptyInterfaces) {
  // Clear any existing interfaces (handled by test setup/teardown)

  // Create empty ifIndexToTableId map
  std::unordered_map<int, int> ifIndexToTableId;

  // Expect no calls to addRemoveSourceRouteRule or addRemoveTunAddress
  EXPECT_CALL(*tunMgr_, addRemoveSourceRouteRule(_, _, _, _)).Times(0);
  EXPECT_CALL(*tunMgr_, addRemoveTunAddress(_, _, _, _, _)).Times(0);

  // Call deleteProbedAddressesAndRules with empty interfaces
  tunMgr_->deleteProbedAddressesAndRules(ifIndexToTableId);
}

/**
 * @brief Test deletion of source IP rules and TUN addresses with no table ID
 * mapping
 *
 * Verifies that deleteProbedAddressesAndRules handles the case where there's no
 * table ID mapping for an interface index. In this case, tableId becomes 0 and
 * no source route rules should be called, but TUN addresses should still be
 * removed.
 */
TEST_F(
    TunManagerAddressRuleTest,
    DeleteProbedAddressesAndRulesNoTableIdMapping) {
  // Add a mock interface with addresses
  std::vector<std::pair<folly::IPAddress, uint8_t>> addresses = {
      {folly::IPAddress("10.1.1.1"), 24}};

  addMockInterface(InterfaceID(2000), 42, "fboss2000", addresses);

  // Create empty ifIndexToTableId map (no mapping for ifIndex 42)
  std::unordered_map<int, int> ifIndexToTableId;

  // Expect no calls to addRemoveSourceRouteRule since tableId is 0
  EXPECT_CALL(*tunMgr_, addRemoveSourceRouteRule(_, _, _, _)).Times(0);
  // But still expect call to addRemoveTunAddress
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2000", 42, folly::IPAddress("10.1.1.1"), 24, false))
      .Times(1);

  // Call deleteProbedAddressesAndRules
  tunMgr_->deleteProbedAddressesAndRules(ifIndexToTableId);
}

/**
 * @brief Test deleteProbedInterfaces functionality for both empty and
 * populated interfaces map
 *
 * Verifies that deleteProbedInterfaces clears the interfaces map in both
 * scenarios: when interfaces map is empty and when it contains interfaces.
 */
TEST_F(TunManagerAddressRuleTest, DeleteProbedInterfaces) {
  // Define common verification lambda
  auto verifyDeletionBehavior = [&](int expectedInitialSize) {
    // Verify expected initial state
    EXPECT_EQ(expectedInitialSize, tunMgr_->intfs_.size());

    // Call deleteProbedInterfaces
    tunMgr_->deleteProbedInterfaces();

    // Verify interfaces map is cleared
    EXPECT_EQ(0, tunMgr_->intfs_.size());
  };

  // Test 1: Empty interfaces map scenario
  verifyDeletionBehavior(0);

  // Test 2: Populated interfaces map scenario
  addMockInterface(InterfaceID(2000), 42, "fboss2000", {});
  verifyDeletionBehavior(1);
}

/**
 * @brief Test buildIfIdToTableIdMap functionality
 *
 * Verifies that buildIfIdToTableIdMap correctly creates a mapping from
 * interface IDs to table IDs based on a SwitchState.
 */
TEST_F(TunManagerRouteProcessorTest, BuildIfIdToTableIdMapBasic) {
  // Create a simple SwitchState with interfaces
  auto state = std::make_shared<SwitchState>();

  // Create switch settings for NPU type (needed for getTableId to work)
  auto settings = std::make_shared<SwitchSettings>();
  SwitchIdToSwitchInfo info{};
  auto [iter, _] = info.emplace(SwitchID(0), cfg::SwitchInfo{});
  iter->second.switchType() = cfg::SwitchType::NPU;
  iter->second.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
  settings->setSwitchIdToSwitchInfo(info);

  auto multiSwitchSwitchSettings = std::make_shared<MultiSwitchSettings>();
  multiSwitchSwitchSettings->addNode(
      HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(0)})
          .matcherString(),
      settings);

  state->resetSwitchSettings(multiSwitchSwitchSettings);

  // Create some test interfaces
  auto interfaces = std::make_shared<MultiSwitchInterfaceMap>();
  auto interfaceMap = std::make_shared<InterfaceMap>();

  // Add interface with ID 2000 (should map to table ID 1)
  auto intf1 = std::make_shared<Interface>(
      InterfaceID(2000),
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece("intf2000"),
      folly::MacAddress("01:02:03:04:05:06"),
      9000,
      false,
      true);
  interfaceMap->addNode(intf1);

  // Add interface with ID 2001 (should map to table ID 2)
  auto intf2 = std::make_shared<Interface>(
      InterfaceID(2001),
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece("intf2001"),
      folly::MacAddress("01:02:03:04:05:07"),
      9000,
      false,
      true);
  interfaceMap->addNode(intf2);

  HwSwitchMatcher matcher(std::unordered_set<SwitchID>{SwitchID(0)});
  interfaces->addMapNode(interfaceMap, matcher);
  state->resetIntfs(interfaces);

  // Call buildIfIdToTableIdMap
  auto ifIdToTableIdMap = tunMgr_->buildIfIdToTableIdMap(state);

  // Verify the mapping is correct
  EXPECT_EQ(2, ifIdToTableIdMap.size());
  EXPECT_EQ(
      tunMgr_->getTableId(InterfaceID(2000)),
      ifIdToTableIdMap[InterfaceID(2000)]);
  EXPECT_EQ(
      tunMgr_->getTableId(InterfaceID(2001)),
      ifIdToTableIdMap[InterfaceID(2001)]);
}

/**
 * @brief Test buildProbedIfIdToTableIdMap with basic functionality
 *
 * Tests the normal case where interfaces have corresponding probed routes
 * with valid table IDs.
 */
TEST_F(TunManagerRouteProcessorTest, BuildProbedIfIdToTableIdMapBasic) {
  // Add mock interfaces to intfs_ map
  auto mockIntf1 =
      std::make_unique<MockTunIntf>(InterfaceID(2000), "fboss2000", 42, 1500);
  auto mockIntf2 =
      std::make_unique<MockTunIntf>(InterfaceID(2001), "fboss2001", 43, 1500);

  tunMgr_->intfs_[InterfaceID(2000)] = std::move(mockIntf1);
  tunMgr_->intfs_[InterfaceID(2001)] = std::move(mockIntf2);

  // Add probed routes that map to these interfaces
  auto tableId2000 = tunMgr_->getTableId(InterfaceID(2000));
  auto tableId2001 = tunMgr_->getTableId(InterfaceID(2001));

  addProbedRoute(
      tunMgr_,
      AF_INET,
      tableId2000,
      "0.0.0.0/0",
      42); // ifIndex 42 -> InterfaceID(2000)
  addProbedRoute(
      tunMgr_,
      AF_INET6,
      tableId2000,
      "::/0",
      42); // ifIndex 42 -> InterfaceID(2000) (duplicate)
  addProbedRoute(
      tunMgr_,
      AF_INET,
      tableId2001,
      "0.0.0.0/0",
      43); // ifIndex 43 -> InterfaceID(2001)

  // Call buildProbedIfIdToTableIdMap
  auto probedMapping = tunMgr_->buildProbedIfIdToTableIdMap();

  // Verify correct mappings
  EXPECT_EQ(2, probedMapping.size());
  EXPECT_EQ(tableId2000, probedMapping[InterfaceID(2000)]);
  EXPECT_EQ(tableId2001, probedMapping[InterfaceID(2001)]);
}

/**
 * @brief Test buildProbedIfIdToTableIdMap with empty probed routes
 *
 * Tests the case where no probed routes exist, resulting in an empty mapping.
 */
TEST_F(TunManagerRouteProcessorTest, BuildProbedIfIdToTableIdMapEmptyRoutes) {
  // Add mock interfaces to intfs_ map
  auto mockIntf1 =
      std::make_unique<MockTunIntf>(InterfaceID(2000), "fboss2000", 42, 1500);
  tunMgr_->intfs_[InterfaceID(2000)] = std::move(mockIntf1);

  // No probed routes added (probedRoutes_ is empty)

  // Call buildProbedIfIdToTableIdMap
  auto probedMapping = tunMgr_->buildProbedIfIdToTableIdMap();

  // Should return empty mapping
  EXPECT_EQ(0, probedMapping.size());
}

/**
 * @brief Test buildProbedIfIdToTableIdMap with empty interfaces
 *
 * Tests the case where no interfaces exist in intfs_ map.
 */
TEST_F(
    TunManagerRouteProcessorTest,
    BuildProbedIfIdToTableIdMapEmptyInterfaces) {
  // No interfaces in intfs_ map (should be empty by default)

  // Add some probed routes (should not affect result)
  addProbedRoute(tunMgr_, AF_INET, 100, "0.0.0.0/0", 42);

  // Call buildProbedIfIdToTableIdMap
  auto probedMapping = tunMgr_->buildProbedIfIdToTableIdMap();

  // Should return empty mapping
  EXPECT_EQ(0, probedMapping.size());
}

/**
 * @brief Test buildProbedIfIdToTableIdMap with mixed scenario
 *
 * Tests a scenario where some interfaces have probed routes and others
 * don't.
 */
TEST_F(TunManagerRouteProcessorTest, BuildProbedIfIdToTableIdMapMixedScenario) {
  // Add mock interfaces
  auto mockIntf1 =
      std::make_unique<MockTunIntf>(InterfaceID(2000), "fboss2000", 42, 1500);
  auto mockIntf2 =
      std::make_unique<MockTunIntf>(InterfaceID(2001), "fboss2001", 43, 1500);
  auto mockIntf3 =
      std::make_unique<MockTunIntf>(InterfaceID(2002), "fboss2002", 44, 1500);

  tunMgr_->intfs_[InterfaceID(2000)] = std::move(mockIntf1);
  tunMgr_->intfs_[InterfaceID(2001)] = std::move(mockIntf2);
  tunMgr_->intfs_[InterfaceID(2002)] = std::move(mockIntf3);

  // Add probed routes only for some interfaces
  auto tableId2000 = tunMgr_->getTableId(InterfaceID(2000));
  auto tableId2002 = tunMgr_->getTableId(InterfaceID(2002));

  addProbedRoute(
      tunMgr_,
      AF_INET,
      tableId2000,
      "0.0.0.0/0",
      42); // Interface 2000 has route
  // Interface 2001 (ifIndex 43) has no probed route
  addProbedRoute(
      tunMgr_,
      AF_INET,
      tableId2002,
      "0.0.0.0/0",
      44); // Interface 2002 has route

  // Call buildProbedIfIdToTableIdMap
  auto probedMapping = tunMgr_->buildProbedIfIdToTableIdMap();

  // Should only include interfaces that have probed routes
  EXPECT_EQ(2, probedMapping.size());
  EXPECT_EQ(tableId2000, probedMapping[InterfaceID(2000)]);
  EXPECT_EQ(tableId2002, probedMapping[InterfaceID(2002)]);
  EXPECT_EQ(probedMapping.end(), probedMapping.find(InterfaceID(2001)));
}

/**
 * @brief Test buildProbedIfIdToTableIdMap with invalid ifIndex
 *
 * Tests handling of probed routes with zero or negative ifIndex values.
 */
TEST_F(
    TunManagerRouteProcessorTest,
    BuildProbedIfIdToTableIdMapInvalidIfIndex) {
  // Add mock interface
  auto mockIntf1 =
      std::make_unique<MockTunIntf>(InterfaceID(2000), "fboss2000", 42, 1500);
  tunMgr_->intfs_[InterfaceID(2000)] = std::move(mockIntf1);

  // Add probed routes with invalid ifIndex values
  auto tableId2000 = tunMgr_->getTableId(InterfaceID(2000));
  auto tableIdInvalid1 =
      tunMgr_->getTableId(InterfaceID(2001)); // For invalid route 1
  auto tableIdInvalid2 =
      tunMgr_->getTableId(InterfaceID(2002)); // For invalid route 2

  TunManager::ProbedRoute invalidRoute1;
  invalidRoute1.family = AF_INET;
  invalidRoute1.tableId = tableIdInvalid1;
  invalidRoute1.destination = "0.0.0.0/0";
  invalidRoute1.ifIndex = 0; // Invalid ifIndex
  tunMgr_->probedRoutes_.push_back(invalidRoute1);

  TunManager::ProbedRoute invalidRoute2;
  invalidRoute2.family = AF_INET;
  invalidRoute2.tableId = tableIdInvalid2;
  invalidRoute2.destination = "0.0.0.0/0";
  invalidRoute2.ifIndex = -1; // Invalid ifIndex
  tunMgr_->probedRoutes_.push_back(invalidRoute2);

  // Add a valid route for comparison
  addProbedRoute(tunMgr_, AF_INET, tableId2000, "0.0.0.0/0", 42);

  // Call buildProbedIfIdToTableIdMap
  auto probedMapping = tunMgr_->buildProbedIfIdToTableIdMap();

  // Should only include the interface with valid probed route
  EXPECT_EQ(1, probedMapping.size());
  EXPECT_EQ(tableId2000, probedMapping[InterfaceID(2000)]);
}

/**
 * @brief Test requiresProbedDataCleanup with identical mappings
 *
 * Verifies that requiresProbedDataCleanup returns false when state and probed
 * mappings are identical.
 */
TEST_F(TunManagerRouteProcessorTest, RequiresProbedDataCleanupIdentical) {
  // Create identical state and probed mappings
  std::unordered_map<InterfaceID, int> stateMap = {
      {InterfaceID(2000), 100},
      {InterfaceID(2001), 101},
      {InterfaceID(2002), 102}};

  std::unordered_map<InterfaceID, int> probedMap = {
      {InterfaceID(2000), 100},
      {InterfaceID(2001), 101},
      {InterfaceID(2002), 102}};

  // Call requiresProbedDataCleanup
  bool requiresCleanup =
      tunMgr_->requiresProbedDataCleanup(stateMap, probedMap);

  // Should return false since mappings are identical
  EXPECT_FALSE(requiresCleanup);
}

/**
 * @brief Test requiresProbedDataCleanup with different mappings
 *
 * Verifies that requiresProbedDataCleanup returns true when state and probed
 * mappings differ in values.
 */
TEST_F(TunManagerRouteProcessorTest, RequiresProbedDataCleanupDifferent) {
  // Create different state and probed mappings (same keys, different values)
  std::unordered_map<InterfaceID, int> stateMap = {
      {InterfaceID(2000), 100}, {InterfaceID(2001), 101}};

  std::unordered_map<InterfaceID, int> probedMap = {
      {InterfaceID(2000), 200}, // Different table ID
      {InterfaceID(2001), 101}};

  // Call requiresProbedDataCleanup
  bool requiresCleanup =
      tunMgr_->requiresProbedDataCleanup(stateMap, probedMap);

  // Should return true since mappings differ
  EXPECT_TRUE(requiresCleanup);
}

/**
 * @brief Test requiresProbedDataCleanup with empty maps
 *
 * Verifies that requiresProbedDataCleanup handles empty mappings correctly.
 */
TEST_F(TunManagerRouteProcessorTest, RequiresProbedDataCleanupEmptyMaps) {
  // Test 1: Both maps empty - should be identical
  std::unordered_map<InterfaceID, int> emptyState;
  std::unordered_map<InterfaceID, int> emptyProbed;

  bool requiresCleanup =
      tunMgr_->requiresProbedDataCleanup(emptyState, emptyProbed);
  EXPECT_FALSE(requiresCleanup);

  // Test 2: State has entries, probed is empty - should differ
  std::unordered_map<InterfaceID, int> stateMap = {{InterfaceID(2000), 100}};

  requiresCleanup = tunMgr_->requiresProbedDataCleanup(stateMap, emptyProbed);
  EXPECT_TRUE(requiresCleanup);

  // Test 3: State is empty, probed has entries - should differ
  std::unordered_map<InterfaceID, int> probedMap = {{InterfaceID(2000), 100}};

  requiresCleanup = tunMgr_->requiresProbedDataCleanup(emptyState, probedMap);
  EXPECT_TRUE(requiresCleanup);
}
/**
 * @brief Test requiresProbedDataCleanup with different sized mappings
 *
 * Verifies that requiresProbedDataCleanup returns true when mappings have
 * different sizes.
 */
TEST_F(TunManagerRouteProcessorTest, RequiresProbedDataCleanupSizeDiff) {
  // Create state mapping with more entries than probed mapping
  std::unordered_map<InterfaceID, int> stateMap = {
      {InterfaceID(2000), 100},
      {InterfaceID(2001), 101},
      {InterfaceID(2002), 102}};

  std::unordered_map<InterfaceID, int> probedMap = {
      {InterfaceID(2000), 100}, {InterfaceID(2001), 101}};

  // Call requiresProbedDataCleanup
  bool requiresCleanup =
      tunMgr_->requiresProbedDataCleanup(stateMap, probedMap);

  // Should return true since sizes differ
  EXPECT_TRUE(requiresCleanup);

  // Test opposite case: probed has more entries than state
  std::unordered_map<InterfaceID, int> smallerStateMap = {
      {InterfaceID(2000), 100}};

  requiresCleanup =
      tunMgr_->requiresProbedDataCleanup(smallerStateMap, probedMap);
  EXPECT_TRUE(requiresCleanup);
}
/**
 * @brief Test requiresProbedDataCleanup with missing keys
 *
 * Verifies that requiresProbedDataCleanup returns true when mappings have
 * same size but different keys.
 */
TEST_F(TunManagerRouteProcessorTest, RequiresProbedDataCleanupMissingKeys) {
  // Create mappings with same size but different keys
  std::unordered_map<InterfaceID, int> stateMap = {
      {InterfaceID(2000), 100}, {InterfaceID(2001), 101}};

  std::unordered_map<InterfaceID, int> probedMap = {
      {InterfaceID(2000), 100},
      {InterfaceID(2002), 102}}; // Different second key

  // Call requiresProbedDataCleanup
  bool requiresCleanup =
      tunMgr_->requiresProbedDataCleanup(stateMap, probedMap);

  // Should return true since keys differ
  EXPECT_TRUE(requiresCleanup);
}

/**
 * @brief Test buildProbedIfIdToTableIdMap with rules augmentation
 *
 * Verifies that buildProbedIfIdToTableIdMap correctly augments the mapping
 * from probed routes with mappings from probed rules when interfaces have
 * no probed routes but have matching source rules.
 */
TEST_F(
    TunManagerRouteProcessorTest,
    BuildProbedIfIdToTableIdMapWithRulesAugmentation) {
  // Add mock interfaces with addresses
  auto mockIntf1 =
      std::make_unique<MockTunIntf>(InterfaceID(2000), "fboss2000", 42, 1500);
  auto mockIntf2 =
      std::make_unique<MockTunIntf>(InterfaceID(2001), "fboss2001", 43, 1500);

  Interface::Addresses addrs1 = {{folly::IPAddress("192.168.1.100"), 24}};
  Interface::Addresses addrs2 = {{folly::IPAddress("192.168.2.100"), 24}};

  mockIntf1->setTestAddresses(addrs1);
  mockIntf2->setTestAddresses(addrs2);

  tunMgr_->intfs_[InterfaceID(2000)] = std::move(mockIntf1);
  tunMgr_->intfs_[InterfaceID(2001)] = std::move(mockIntf2);

  // Add probed route only for first interface
  auto tableId2000 = tunMgr_->getTableId(InterfaceID(2000));
  addProbedRoute(tunMgr_, AF_INET, tableId2000, "0.0.0.0/0", 42);

  // Add probed rules for both interfaces (second one will augment)
  auto tableId2001 = tunMgr_->getTableId(InterfaceID(2001));
  tunMgr_->probedRules_.clear();
  tunMgr_->probedRules_.emplace_back(AF_INET, tableId2000, "192.168.1.100/32");
  tunMgr_->probedRules_.emplace_back(AF_INET, tableId2001, "192.168.2.100/32");

  // Call buildProbedIfIdToTableIdMap
  auto probedMapping = tunMgr_->buildProbedIfIdToTableIdMap();

  // Should include mapping from both routes and rules
  EXPECT_EQ(2, probedMapping.size());
  EXPECT_EQ(tableId2000, probedMapping[InterfaceID(2000)]); // From route
  EXPECT_EQ(
      tableId2001, probedMapping[InterfaceID(2001)]); // From rule (augmented)
}

/**
 * @brief Test buildProbedIfIdToTableIdMap with rules-only fallback
 *
 * Verifies that buildProbedIfIdToTableIdMap falls back to using only
 * source rules when no probed routes exist (example interface down).
 */
TEST_F(
    TunManagerRouteProcessorTest,
    BuildProbedIfIdToTableIdMapRulesOnlyFallback) {
  // Add mock interface with addresses
  auto mockIntf =
      std::make_unique<MockTunIntf>(InterfaceID(2000), "fboss2000", 42, 1500);

  Interface::Addresses addrs = {{folly::IPAddress("192.168.1.100"), 24}};
  mockIntf->setTestAddresses(addrs);
  tunMgr_->intfs_[InterfaceID(2000)] = std::move(mockIntf);

  // No probed routes added (probedRoutes_ is empty)

  // Add probed rules only
  auto tableId2000 = tunMgr_->getTableId(InterfaceID(2000));
  tunMgr_->probedRules_.clear();
  tunMgr_->probedRules_.emplace_back(AF_INET, tableId2000, "192.168.1.100/32");

  // Call buildProbedIfIdToTableIdMap
  auto probedMapping = tunMgr_->buildProbedIfIdToTableIdMap();

  // Should include mapping from rules only
  EXPECT_EQ(1, probedMapping.size());
  EXPECT_EQ(tableId2000, probedMapping[InterfaceID(2000)]);
}

/**
 * @brief Test buildProbedIfIdToTableIdMap routes have priority over rules
 *
 * Verifies that when both probed routes and probed rules exist for the same
 * interface, the mapping from probed routes takes priority and rules do not
 * overwrite it. This will not happen in production, but adding test to verify
 * the code.
 */
TEST_F(
    TunManagerRouteProcessorTest,
    BuildProbedIfIdToTableIdMapRoutesHavePriority) {
  // Add mock interface with addresses
  auto mockIntf =
      std::make_unique<MockTunIntf>(InterfaceID(2000), "fboss2000", 42, 1500);

  Interface::Addresses addrs = {{folly::IPAddress("192.168.1.100"), 24}};
  mockIntf->setTestAddresses(addrs);
  tunMgr_->intfs_[InterfaceID(2000)] = std::move(mockIntf);

  auto tableId2000 = tunMgr_->getTableId(InterfaceID(2000));
  auto differentTableId = tableId2000 + 10; // Different table ID

  // Add probed route
  addProbedRoute(tunMgr_, AF_INET, tableId2000, "0.0.0.0/0", 42);

  // Add probed rules with different table ID (should not overwrite route
  // mapping)
  tunMgr_->probedRules_.clear();
  tunMgr_->probedRules_.emplace_back(
      AF_INET, differentTableId, "192.168.1.100/32");

  // Call buildProbedIfIdToTableIdMap
  auto probedMapping = tunMgr_->buildProbedIfIdToTableIdMap();

  // Should use mapping from route, not rule
  EXPECT_EQ(1, probedMapping.size());
  EXPECT_EQ(
      tableId2000, probedMapping[InterfaceID(2000)]); // From route, not rule
  EXPECT_NE(differentTableId, probedMapping[InterfaceID(2000)]);
}

/**
 * @brief Test deletion of remaining probed rules that aren't associated
 * with current interfaces
 *
 * This test verifies the specific logic in deleteProbedAddressesAndRules
 * that handles probed rules which exist in probedRules_ but don't correspond
 * to any current interface addresses. These rules need to be deleted
 * separately after processing interface addresses.
 */
TEST_F(
    TunManagerAddressRuleTest,
    DeleteProbedAddressesAndRulesWithRemainingRules) {
  // Add a mock interface with one address
  std::vector<std::pair<folly::IPAddress, uint8_t>> addresses = {
      {folly::IPAddress("10.1.1.1"), 24}};

  addMockInterface(InterfaceID(2000), 42, "fboss2000", addresses);

  // Add probed rules: one that matches interface address, one that doesn't
  tunMgr_->probedRules_.clear();
  tunMgr_->probedRules_.emplace_back(
      AF_INET, 100, "10.1.1.1/32"); // Matches interface address
  tunMgr_->probedRules_.emplace_back(
      AF_INET, 101, "10.2.2.2/32"); // No corresponding interface
  tunMgr_->probedRules_.emplace_back(
      AF_INET6, 102, "2001:db8::1/128"); // No corresponding interface

  // Create ifIndexToTableId map
  auto ifIndexToTableId = createIfIndexToTableIdMap({{42, 100}});

  // Expect calls for interface address cleanup
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          100,
          folly::IPAddress("10.1.1.1"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2000", 42, folly::IPAddress("10.1.1.1"), 24, false))
      .Times(1);

  // Expect calls for remaining probed rules (the ones not matched by interface
  // addresses)
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          101,
          folly::IPAddress("10.2.2.2"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          102,
          folly::IPAddress("2001:db8::1"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1);

  // Call deleteProbedAddressesAndRules
  tunMgr_->deleteProbedAddressesAndRules(ifIndexToTableId);

  // Verify probed rules are cleared
  EXPECT_EQ(0, tunMgr_->probedRules_.size());
}

/**
 * @brief Test exception handling when deleting remaining probed rules
 *
 * This test verifies that exceptions thrown during the deletion of remaining
 * probed rules are now propagated (no try-catch block), stopping processing
 * when an exception occurs.
 */
TEST_F(
    TunManagerAddressRuleTest,
    DeleteProbedAddressesAndRulesRemainingRulesException) {
  // Add a mock interface with one address
  std::vector<std::pair<folly::IPAddress, uint8_t>> addresses = {
      {folly::IPAddress("10.1.1.1"), 24}};

  addMockInterface(InterfaceID(2000), 42, "fboss2000", addresses);

  // Add probed rules: one that matches interface, two that don't
  tunMgr_->probedRules_.clear();
  tunMgr_->probedRules_.emplace_back(
      AF_INET, 100, "10.1.1.1/32"); // Matches interface address
  tunMgr_->probedRules_.emplace_back(
      AF_INET, 101, "10.2.2.2/32"); // Will throw exception
  tunMgr_->probedRules_.emplace_back(
      AF_INET, 102, "10.3.3.3/32"); // Won't be processed due to exception

  // Create ifIndexToTableId map
  auto ifIndexToTableId = createIfIndexToTableIdMap({{42, 100}});

  // Expect calls for interface address cleanup
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          100,
          folly::IPAddress("10.1.1.1"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1);
  EXPECT_CALL(
      *tunMgr_,
      addRemoveTunAddress(
          "fboss2000", 42, folly::IPAddress("10.1.1.1"), 24, false))
      .Times(1);

  // Expect call for first remaining rule - this will throw exception
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          101,
          folly::IPAddress("10.2.2.2"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(1)
      .WillOnce(::testing::Throw(std::runtime_error("Mock netlink error")));

  // No call expected for second remaining rule since exception stops processing
  EXPECT_CALL(
      *tunMgr_,
      addRemoveSourceRouteRule(
          102,
          folly::IPAddress("10.3.3.3"),
          false,
          ::testing::Eq(std::nullopt)))
      .Times(0);

  // Call deleteProbedAddressesAndRules - should throw exception
  EXPECT_THROW(
      tunMgr_->deleteProbedAddressesAndRules(ifIndexToTableId),
      std::runtime_error);

  // Verify probed rules are not cleared due to exception
  EXPECT_EQ(3, tunMgr_->probedRules_.size());
}
} // namespace facebook::fboss
