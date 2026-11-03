/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/state/LabelForwardingAction.h"
#include "fboss/agent/state/RouteNextHop.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>
#include <unordered_map>
#include <unordered_set>

using namespace facebook::fboss;

namespace {
ResolvedNextHop makeResolvedSrv6NextHop(
    const folly::IPAddress& addr,
    InterfaceID intfID,
    NextHopWeight weight,
    std::vector<folly::IPAddressV6> segList = {},
    std::optional<TunnelType> tunnelType = std::nullopt,
    std::optional<std::string> tunnelId = std::nullopt) {
  return ResolvedNextHop(
      addr,
      intfID,
      weight,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::move(segList),
      tunnelType,
      std::move(tunnelId));
}

UnresolvedNextHop makeSrv6UnresolvedNextHop(
    const folly::IPAddress& addr,
    NextHopWeight weight,
    std::vector<folly::IPAddressV6> segList = {},
    std::optional<TunnelType> tunnelType = std::nullopt,
    std::optional<std::string> tunnelId = std::nullopt) {
  return UnresolvedNextHop(
      addr,
      weight,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::move(segList),
      tunnelType,
      std::move(tunnelId));
}
} // namespace

TEST(RouteNextHopTest, ResolvedNextHopEquality) {
  auto addr1 = folly::IPAddress("2401:db00::1");
  auto addr2 = folly::IPAddress("2401:db00::2");
  auto intfID1 = InterfaceID(1);
  auto intfID2 = InterfaceID(2);

  // Test basic equality
  ResolvedNextHop nh1(addr1, intfID1, 10);
  ResolvedNextHop nh2(addr1, intfID1, 10);
  EXPECT_EQ(nh1, nh2);

  // Test inequality for different addresses
  ResolvedNextHop nh3(addr2, intfID1, 10);
  EXPECT_NE(nh1, nh3);

  // Test inequality for different interface IDs
  ResolvedNextHop nh4(addr1, intfID2, 10);
  EXPECT_NE(nh1, nh4);

  // Test inequality for different weights
  ResolvedNextHop nh5(addr1, intfID1, 20);
  EXPECT_NE(nh1, nh5);
}

TEST(RouteNextHopTest, ResolvedNextHopEqualityWithOptionalFields) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  // Test with labelForwardingAction
  LabelForwardingAction action1(MplsActionCode::PUSH, MplsLabelStack{1001});
  LabelForwardingAction action2(MplsActionCode::PUSH, MplsLabelStack{1002});

  ResolvedNextHop nh1(addr, intfID, 10, action1);
  ResolvedNextHop nh2(addr, intfID, 10, action1);
  ResolvedNextHop nh3(addr, intfID, 10, action2);

  EXPECT_EQ(nh1, nh2);
  EXPECT_NE(nh1, nh3);

  // Test with disableTTLDecrement
  ResolvedNextHop nh4(addr, intfID, 10, std::nullopt, true);
  ResolvedNextHop nh5(addr, intfID, 10, std::nullopt, true);
  ResolvedNextHop nh6(addr, intfID, 10, std::nullopt, false);

  EXPECT_EQ(nh4, nh5);
  EXPECT_NE(nh4, nh6);

  // Test with topologyInfo
  NetworkTopologyInformation topo1;
  topo1.rack_id() = 1;
  topo1.plane_id() = 2;

  NetworkTopologyInformation topo2;
  topo2.rack_id() = 1;
  topo2.plane_id() = 2;

  NetworkTopologyInformation topo3;
  topo3.rack_id() = 2;
  topo3.plane_id() = 2;

  ResolvedNextHop nh7(addr, intfID, 10, std::nullopt, std::nullopt, topo1);
  ResolvedNextHop nh8(addr, intfID, 10, std::nullopt, std::nullopt, topo2);
  ResolvedNextHop nh9(addr, intfID, 10, std::nullopt, std::nullopt, topo3);

  EXPECT_EQ(nh7, nh8);
  EXPECT_NE(nh7, nh9);

  // Test with adjustedWeight
  ResolvedNextHop nh10(
      addr, intfID, 10, std::nullopt, std::nullopt, std::nullopt, 5);
  ResolvedNextHop nh11(
      addr, intfID, 10, std::nullopt, std::nullopt, std::nullopt, 5);
  ResolvedNextHop nh12(
      addr, intfID, 10, std::nullopt, std::nullopt, std::nullopt, 6);

  EXPECT_EQ(nh10, nh11);
  EXPECT_NE(nh10, nh12);

  // Test with srv6SegmentList
  std::vector<folly::IPAddressV6> segList1{
      folly::IPAddressV6("2001:db8::1"), folly::IPAddressV6("2001:db8::2")};
  std::vector<folly::IPAddressV6> segList2{
      folly::IPAddressV6("2001:db8::3"), folly::IPAddressV6("2001:db8::4")};

  auto nh13 = makeResolvedSrv6NextHop(
      addr, intfID, 10, segList1, TunnelType::SRV6_ENCAP, "tunnel_1");
  auto nh14 = makeResolvedSrv6NextHop(
      addr, intfID, 10, segList1, TunnelType::SRV6_ENCAP, "tunnel_1");
  auto nh15 = makeResolvedSrv6NextHop(
      addr, intfID, 10, segList2, TunnelType::SRV6_ENCAP, "tunnel_1");

  EXPECT_EQ(nh13, nh14);
  EXPECT_NE(nh13, nh15);

  // Test with tunnelType
  auto nh16 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, TunnelType::SRV6_ENCAP);
  auto nh17 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, TunnelType::SRV6_ENCAP);
  auto nh18 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, TunnelType::IP_IN_IP_DECAP);

  EXPECT_EQ(nh16, nh17);
  EXPECT_NE(nh16, nh18);

  // Test with tunnelId
  auto nh19 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, std::nullopt, "tunnel_1");
  auto nh20 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, std::nullopt, "tunnel_1");
  auto nh21 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, std::nullopt, "tunnel_2");

  EXPECT_EQ(nh19, nh20);
  EXPECT_NE(nh19, nh21);
}

TEST(RouteNextHopTest, UnresolvedNextHopEquality) {
  auto addr1 = folly::IPAddress("2401:db00::1");
  auto addr2 = folly::IPAddress("2401:db00::2");

  // Test basic equality
  UnresolvedNextHop nh1(addr1, 10);
  UnresolvedNextHop nh2(addr1, 10);
  EXPECT_EQ(nh1, nh2);

  // Test inequality for different addresses
  UnresolvedNextHop nh3(addr2, 10);
  EXPECT_NE(nh1, nh3);

  // Test inequality for different weights
  UnresolvedNextHop nh4(addr1, 20);
  EXPECT_NE(nh1, nh4);
}

TEST(RouteNextHopTest, UnresolvedNextHopEqualityWithOptionalFields) {
  auto addr = folly::IPAddress("2401:db00::1");

  // Test with labelForwardingAction
  LabelForwardingAction action1(MplsActionCode::PUSH, MplsLabelStack{1001});
  LabelForwardingAction action2(MplsActionCode::PUSH, MplsLabelStack{1002});

  UnresolvedNextHop nh1(addr, 10, action1);
  UnresolvedNextHop nh2(addr, 10, action1);
  UnresolvedNextHop nh3(addr, 10, action2);

  EXPECT_EQ(nh1, nh2);
  EXPECT_NE(nh1, nh3);

  // Test with disableTTLDecrement
  UnresolvedNextHop nh4(addr, 10, std::nullopt, true);
  UnresolvedNextHop nh5(addr, 10, std::nullopt, true);
  UnresolvedNextHop nh6(addr, 10, std::nullopt, false);

  EXPECT_EQ(nh4, nh5);
  EXPECT_NE(nh4, nh6);

  // Test with topologyInfo
  NetworkTopologyInformation topo1;
  topo1.rack_id() = 1;
  topo1.plane_id() = 2;

  NetworkTopologyInformation topo2;
  topo2.rack_id() = 1;
  topo2.plane_id() = 2;

  NetworkTopologyInformation topo3;
  topo3.rack_id() = 2;
  topo3.plane_id() = 2;

  UnresolvedNextHop nh7(addr, 10, std::nullopt, std::nullopt, topo1);
  UnresolvedNextHop nh8(addr, 10, std::nullopt, std::nullopt, topo2);
  UnresolvedNextHop nh9(addr, 10, std::nullopt, std::nullopt, topo3);

  EXPECT_EQ(nh7, nh8);
  EXPECT_NE(nh7, nh9);

  // Test with adjustedWeight
  UnresolvedNextHop nh10(addr, 10, std::nullopt, std::nullopt, std::nullopt, 5);
  UnresolvedNextHop nh11(addr, 10, std::nullopt, std::nullopt, std::nullopt, 5);
  UnresolvedNextHop nh12(addr, 10, std::nullopt, std::nullopt, std::nullopt, 6);

  EXPECT_EQ(nh10, nh11);
  EXPECT_NE(nh10, nh12);

  // Test with srv6SegmentList
  std::vector<folly::IPAddressV6> segList1{
      folly::IPAddressV6("2001:db8::1"), folly::IPAddressV6("2001:db8::2")};
  std::vector<folly::IPAddressV6> segList2{
      folly::IPAddressV6("2001:db8::3"), folly::IPAddressV6("2001:db8::4")};

  auto nh13 = makeSrv6UnresolvedNextHop(
      addr, 10, segList1, TunnelType::SRV6_ENCAP, "tunnel_1");
  auto nh14 = makeSrv6UnresolvedNextHop(
      addr, 10, segList1, TunnelType::SRV6_ENCAP, "tunnel_1");
  auto nh15 = makeSrv6UnresolvedNextHop(
      addr, 10, segList2, TunnelType::SRV6_ENCAP, "tunnel_1");

  EXPECT_EQ(nh13, nh14);
  EXPECT_NE(nh13, nh15);

  // Test with tunnelType
  auto nh16 = makeSrv6UnresolvedNextHop(addr, 10, {}, TunnelType::SRV6_ENCAP);
  auto nh17 = makeSrv6UnresolvedNextHop(addr, 10, {}, TunnelType::SRV6_ENCAP);
  auto nh18 =
      makeSrv6UnresolvedNextHop(addr, 10, {}, TunnelType::IP_IN_IP_DECAP);

  EXPECT_EQ(nh16, nh17);
  EXPECT_NE(nh16, nh18);

  // Test with tunnelId
  auto nh19 = makeSrv6UnresolvedNextHop(addr, 10, {}, std::nullopt, "tunnel_1");
  auto nh20 = makeSrv6UnresolvedNextHop(addr, 10, {}, std::nullopt, "tunnel_1");
  auto nh21 = makeSrv6UnresolvedNextHop(addr, 10, {}, std::nullopt, "tunnel_2");

  EXPECT_EQ(nh19, nh20);
  EXPECT_NE(nh19, nh21);
}

TEST(RouteNextHopTest, ResolvedVsUnresolvedInequality) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  ResolvedNextHop resolved(addr, intfID, 10);
  UnresolvedNextHop unresolved(addr, 10);

  // These should be different types, but we test via NextHop polymorphic
  // interface
  NextHop resolvedNh = resolved;
  NextHop unresolvedNh = unresolved;
  EXPECT_NE(resolvedNh, unresolvedNh);
}

TEST(RouteNextHopTest, NextHopRole) {
  auto addr = folly::IPAddress("2401:db00::1");
  ResolvedNextHop primary(addr, InterfaceID(1), 10);
  ResolvedNextHop backup(
      addr,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      std::nullopt,
      NextHopRole::BACKUP);

  EXPECT_EQ(primary.role(), NextHopRole::PRIMARY);
  EXPECT_EQ(backup.role(), NextHopRole::BACKUP);
  EXPECT_NE(primary, backup);

  NextHop backupNextHop = backup;
  auto thrift = backupNextHop.toThrift();
  EXPECT_EQ(*thrift.role(), NextHopRole::BACKUP);
  EXPECT_EQ(util::fromThrift(thrift, true), backupNextHop);

  UnresolvedNextHop unresolvedBackup(
      addr,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      std::nullopt,
      NextHopRole::BACKUP);
  NextHop unresolvedBackupNextHop = unresolvedBackup;
  EXPECT_EQ(
      util::fromThrift(unresolvedBackupNextHop.toThrift()),
      unresolvedBackupNextHop);
}

// Hash function tests for NetworkTopologyInformation
TEST(RouteNextHopTest, NetworkTopologyInformationHashFunction) {
  std::hash<NetworkTopologyInformation> hasher;

  // Test that equal objects have the same hash
  NetworkTopologyInformation topo1;
  topo1.rack_id() = 1;
  topo1.plane_id() = 2;
  topo1.remote_rack_capacity() = 3;
  topo1.spine_capacity() = 4;
  topo1.local_rack_capacity() = 5;
  topo1.spine_id() = 6;

  NetworkTopologyInformation topo2;
  topo2.rack_id() = 1;
  topo2.plane_id() = 2;
  topo2.remote_rack_capacity() = 3;
  topo2.spine_capacity() = 4;
  topo2.local_rack_capacity() = 5;
  topo2.spine_id() = 6;

  EXPECT_EQ(hasher(topo1), hasher(topo2));

  // Test that different objects have different hashes
  NetworkTopologyInformation topo3;
  topo3.rack_id() = 2; // Different rack_id
  topo3.plane_id() = 2;
  topo3.remote_rack_capacity() = 3;
  topo3.spine_capacity() = 4;
  topo3.local_rack_capacity() = 5;
  topo3.spine_id() = 6;

  EXPECT_NE(hasher(topo1), hasher(topo3));

  NetworkTopologyInformation topo4 = topo1;
  topo4.spine_id() = 7;
  EXPECT_NE(hasher(topo1), hasher(topo4));
}

TEST(RouteNextHopTest, NetworkTopologyInformationHashWithOptionalFields) {
  std::hash<NetworkTopologyInformation> hasher;

  // Test with only some fields set
  NetworkTopologyInformation topo1;
  topo1.rack_id() = 1;
  topo1.plane_id() = 2;

  NetworkTopologyInformation topo2;
  topo2.rack_id() = 1;
  topo2.plane_id() = 2;

  EXPECT_EQ(hasher(topo1), hasher(topo2));

  // Test with all fields vs subset
  NetworkTopologyInformation topo3;
  topo3.rack_id() = 1;
  topo3.plane_id() = 2;
  topo3.remote_rack_capacity() = 10;

  EXPECT_NE(hasher(topo1), hasher(topo3));

  // Test empty topology info
  NetworkTopologyInformation topo4;
  NetworkTopologyInformation topo5;
  EXPECT_EQ(hasher(topo4), hasher(topo5));
}

TEST(RouteNextHopTest, NetworkTopologyInformationHashUnorderedSet) {
  std::unordered_set<NetworkTopologyInformation> topoSet;

  NetworkTopologyInformation topo1;
  topo1.rack_id() = 1;
  topo1.plane_id() = 2;

  NetworkTopologyInformation topo2;
  topo2.rack_id() = 2;
  topo2.plane_id() = 1;

  NetworkTopologyInformation topo3;
  topo3.rack_id() = 1;
  topo3.plane_id() = 2;
  topo3.remote_rack_capacity() = 100;

  topoSet.insert(topo1);
  topoSet.insert(topo2);
  topoSet.insert(topo3);

  EXPECT_EQ(topoSet.size(), 3);

  // Try to insert duplicate
  topoSet.insert(topo1);
  EXPECT_EQ(topoSet.size(), 3);

  // Test find operations
  EXPECT_NE(topoSet.find(topo1), topoSet.end());

  NetworkTopologyInformation notFound;
  notFound.rack_id() = 999;
  EXPECT_EQ(topoSet.find(notFound), topoSet.end());
}

// Hash function tests for NextHop
TEST(RouteNextHopTest, NextHopHashFunction) {
  std::hash<NextHop> hasher;

  auto addr1 = folly::IPAddress("2401:db00::1");
  auto addr2 = folly::IPAddress("2401:db00::2");
  auto intfID = InterfaceID(1);

  // Test resolved nexthops
  ResolvedNextHop resolved1(addr1, intfID, 10);
  ResolvedNextHop resolved2(addr1, intfID, 10);

  NextHop nh1 = resolved1;
  NextHop nh2 = resolved2;

  EXPECT_EQ(hasher(nh1), hasher(nh2));

  // Test different addresses
  ResolvedNextHop resolved3(addr2, intfID, 10);
  NextHop nh3 = resolved3;

  EXPECT_NE(hasher(nh1), hasher(nh3));

  // Test unresolved nexthops
  UnresolvedNextHop unresolved1(addr1, 10);
  UnresolvedNextHop unresolved2(addr1, 10);

  NextHop nh4 = unresolved1;
  NextHop nh5 = unresolved2;

  EXPECT_EQ(hasher(nh4), hasher(nh5));

  // Test resolved vs unresolved
  EXPECT_NE(hasher(nh1), hasher(nh4));
}

TEST(RouteNextHopTest, NextHopHashWithComplexFields) {
  std::hash<NextHop> hasher;

  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  // Test with MPLS action
  LabelForwardingAction action1(MplsActionCode::PUSH, MplsLabelStack{1001});
  LabelForwardingAction action2(MplsActionCode::PUSH, MplsLabelStack{1002});

  ResolvedNextHop resolved1(addr, intfID, 10, action1);
  ResolvedNextHop resolved2(addr, intfID, 10, action1);
  ResolvedNextHop resolved3(addr, intfID, 10, action2);

  NextHop nh1 = resolved1;
  NextHop nh2 = resolved2;
  NextHop nh3 = resolved3;

  EXPECT_EQ(hasher(nh1), hasher(nh2));
  EXPECT_NE(hasher(nh1), hasher(nh3));

  // Test with topology info
  NetworkTopologyInformation topo1;
  topo1.rack_id() = 1;
  topo1.plane_id() = 2;

  NetworkTopologyInformation topo2;
  topo2.rack_id() = 2;
  topo2.plane_id() = 1;

  ResolvedNextHop resolved4(
      addr, intfID, 10, std::nullopt, std::nullopt, topo1);
  ResolvedNextHop resolved5(
      addr, intfID, 10, std::nullopt, std::nullopt, topo1);
  ResolvedNextHop resolved6(
      addr, intfID, 10, std::nullopt, std::nullopt, topo2);

  NextHop nh4 = resolved4;
  NextHop nh5 = resolved5;
  NextHop nh6 = resolved6;

  EXPECT_EQ(hasher(nh4), hasher(nh5));
  EXPECT_NE(hasher(nh4), hasher(nh6));

  // Test with SRv6 fields
  std::vector<folly::IPAddressV6> segList1{folly::IPAddressV6("2001:db8::1")};
  std::vector<folly::IPAddressV6> segList2{folly::IPAddressV6("2001:db8::2")};

  auto resolved7 = makeResolvedSrv6NextHop(
      addr, intfID, 10, segList1, TunnelType::SRV6_ENCAP, "tunnel_1");
  auto resolved8 = makeResolvedSrv6NextHop(
      addr, intfID, 10, segList1, TunnelType::SRV6_ENCAP, "tunnel_1");
  auto resolved9 = makeResolvedSrv6NextHop(
      addr, intfID, 10, segList2, TunnelType::SRV6_ENCAP, "tunnel_1");

  NextHop nh7 = resolved7;
  NextHop nh8 = resolved8;
  NextHop nh9 = resolved9;

  EXPECT_EQ(hasher(nh7), hasher(nh8));
  EXPECT_NE(hasher(nh7), hasher(nh9));
}

TEST(RouteNextHopTest, NextHopHashUnorderedSet) {
  std::unordered_set<NextHop> nhSet;

  auto addr1 = folly::IPAddress("2401:db00::1");
  auto addr2 = folly::IPAddress("2401:db00::2");
  auto intfID = InterfaceID(1);

  ResolvedNextHop resolved1(addr1, intfID, 10);
  ResolvedNextHop resolved2(addr2, intfID, 20);
  UnresolvedNextHop unresolved1(addr1, 30);
  UnresolvedNextHop unresolved2(addr2, 40);

  nhSet.insert(NextHop(resolved1));
  nhSet.insert(NextHop(resolved2));
  nhSet.insert(NextHop(unresolved1));
  nhSet.insert(NextHop(unresolved2));

  EXPECT_EQ(nhSet.size(), 4);

  // Try to insert duplicate
  nhSet.insert(NextHop(resolved1));
  EXPECT_EQ(nhSet.size(), 4);

  // Test find operations
  EXPECT_NE(nhSet.find(NextHop(resolved1)), nhSet.end());
  EXPECT_NE(nhSet.find(NextHop(unresolved1)), nhSet.end());
}

// Hash function tests for ResolvedNextHop
TEST(RouteNextHopTest, ResolvedNextHopHashFunction) {
  std::hash<ResolvedNextHop> hasher;

  auto addr1 = folly::IPAddress("2401:db00::1");
  auto addr2 = folly::IPAddress("2401:db00::2");
  auto intfID1 = InterfaceID(1);
  auto intfID2 = InterfaceID(2);

  // Test basic equality gives same hash
  ResolvedNextHop nh1(addr1, intfID1, 10);
  ResolvedNextHop nh2(addr1, intfID1, 10);

  EXPECT_EQ(hasher(nh1), hasher(nh2));

  // Test different fields give different hashes
  ResolvedNextHop nh3(addr2, intfID1, 10); // Different address
  ResolvedNextHop nh4(addr1, intfID2, 10); // Different interface
  ResolvedNextHop nh5(addr1, intfID1, 20); // Different weight

  EXPECT_NE(hasher(nh1), hasher(nh3));
  EXPECT_NE(hasher(nh1), hasher(nh4));
  EXPECT_NE(hasher(nh1), hasher(nh5));
}

TEST(RouteNextHopTest, ResolvedNextHopHashWithOptionalFields) {
  std::hash<ResolvedNextHop> hasher;

  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  // Test with MPLS action
  LabelForwardingAction action1(MplsActionCode::PUSH, MplsLabelStack{1001});
  LabelForwardingAction action2(MplsActionCode::PUSH, MplsLabelStack{1002});

  ResolvedNextHop nh1(addr, intfID, 10, action1);
  ResolvedNextHop nh2(addr, intfID, 10, action1);
  ResolvedNextHop nh3(addr, intfID, 10, action2);

  EXPECT_EQ(hasher(nh1), hasher(nh2));
  EXPECT_NE(hasher(nh1), hasher(nh3));

  // Test with TTL decrement setting
  ResolvedNextHop nh4(addr, intfID, 10, std::nullopt, true);
  ResolvedNextHop nh5(addr, intfID, 10, std::nullopt, true);
  ResolvedNextHop nh6(addr, intfID, 10, std::nullopt, false);

  EXPECT_EQ(hasher(nh4), hasher(nh5));
  EXPECT_NE(hasher(nh4), hasher(nh6));

  // Test with topology info
  NetworkTopologyInformation topo1;
  topo1.rack_id() = 1;

  NetworkTopologyInformation topo2;
  topo2.rack_id() = 2;

  ResolvedNextHop nh7(addr, intfID, 10, std::nullopt, std::nullopt, topo1);
  ResolvedNextHop nh8(addr, intfID, 10, std::nullopt, std::nullopt, topo1);
  ResolvedNextHop nh9(addr, intfID, 10, std::nullopt, std::nullopt, topo2);

  EXPECT_EQ(hasher(nh7), hasher(nh8));
  EXPECT_NE(hasher(nh7), hasher(nh9));

  // Test with adjusted weight
  ResolvedNextHop nh10(
      addr, intfID, 10, std::nullopt, std::nullopt, std::nullopt, 100);
  ResolvedNextHop nh11(
      addr, intfID, 10, std::nullopt, std::nullopt, std::nullopt, 100);
  ResolvedNextHop nh12(
      addr, intfID, 10, std::nullopt, std::nullopt, std::nullopt, 200);

  EXPECT_EQ(hasher(nh10), hasher(nh11));
  EXPECT_NE(hasher(nh10), hasher(nh12));

  // Test with srv6SegmentList
  std::vector<folly::IPAddressV6> segList1{folly::IPAddressV6("2001:db8::1")};
  std::vector<folly::IPAddressV6> segList2{folly::IPAddressV6("2001:db8::2")};

  auto nh13 = makeResolvedSrv6NextHop(
      addr, intfID, 10, segList1, TunnelType::SRV6_ENCAP, "tunnel_1");
  auto nh14 = makeResolvedSrv6NextHop(
      addr, intfID, 10, segList1, TunnelType::SRV6_ENCAP, "tunnel_1");
  auto nh15 = makeResolvedSrv6NextHop(
      addr, intfID, 10, segList2, TunnelType::SRV6_ENCAP, "tunnel_1");

  EXPECT_EQ(hasher(nh13), hasher(nh14));
  EXPECT_NE(hasher(nh13), hasher(nh15));

  // Test with tunnelType
  auto nh16 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, TunnelType::SRV6_ENCAP);
  auto nh17 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, TunnelType::SRV6_ENCAP);
  auto nh18 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, TunnelType::IP_IN_IP_DECAP);

  EXPECT_EQ(hasher(nh16), hasher(nh17));
  EXPECT_NE(hasher(nh16), hasher(nh18));

  // Test with tunnelId
  auto nh19 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, std::nullopt, "tunnel_1");
  auto nh20 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, std::nullopt, "tunnel_1");
  auto nh21 =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, std::nullopt, "tunnel_2");

  EXPECT_EQ(hasher(nh19), hasher(nh20));
  EXPECT_NE(hasher(nh19), hasher(nh21));
}

TEST(RouteNextHopTest, ResolvedNextHopHashConsistency) {
  std::hash<ResolvedNextHop> hasher;

  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  ResolvedNextHop nh(addr, intfID, 10);

  // Test hash consistency across multiple calls
  size_t hash1 = hasher(nh);
  size_t hash2 = hasher(nh);
  size_t hash3 = hasher(nh);

  EXPECT_EQ(hash1, hash2);
  EXPECT_EQ(hash2, hash3);
}

TEST(RouteNextHopTest, ResolvedNextHopHashUnorderedMap) {
  std::unordered_map<ResolvedNextHop, std::string> nhMap;

  auto addr1 = folly::IPAddress("2401:db00::1");
  auto addr2 = folly::IPAddress("2401:db00::2");
  auto intfID = InterfaceID(1);

  ResolvedNextHop nh1(addr1, intfID, 10);
  ResolvedNextHop nh2(addr2, intfID, 20);

  nhMap[nh1] = "nexthop1";
  nhMap[nh2] = "nexthop2";

  EXPECT_EQ(nhMap.size(), 2);
  EXPECT_EQ(nhMap[nh1], "nexthop1");
  EXPECT_EQ(nhMap[nh2], "nexthop2");

  // Test with duplicate key
  nhMap[nh1] = "updated_nexthop1";
  EXPECT_EQ(nhMap.size(), 2);
  EXPECT_EQ(nhMap[nh1], "updated_nexthop1");
}

TEST(RouteNextHopTest, HashFunctionCompleteness) {
  // This test ensures that all fields are considered in the hash function
  // by creating two objects that differ only in one field at a time
  std::hash<ResolvedNextHop> hasher;

  auto addr1 = folly::IPAddress("10.0.0.1");
  auto addr2 = folly::IPAddress("10.0.0.2");
  auto intfID1 = InterfaceID(1);
  auto intfID2 = InterfaceID(2);

  ResolvedNextHop base(addr1, intfID1, 10);

  // Different address
  ResolvedNextHop diffAddr(addr2, intfID1, 10);
  EXPECT_NE(hasher(base), hasher(diffAddr));

  // Different interface
  ResolvedNextHop diffIntf(addr1, intfID2, 10);
  EXPECT_NE(hasher(base), hasher(diffIntf));

  // Different weight
  ResolvedNextHop diffWeight(addr1, intfID1, 20);
  EXPECT_NE(hasher(base), hasher(diffWeight));

  // Different MPLS action
  LabelForwardingAction action(MplsActionCode::PUSH, MplsLabelStack{1001});
  ResolvedNextHop diffAction(addr1, intfID1, 10, action);
  EXPECT_NE(hasher(base), hasher(diffAction));

  // Different TTL decrement
  ResolvedNextHop diffTTL(addr1, intfID1, 10, std::nullopt, true);
  EXPECT_NE(hasher(base), hasher(diffTTL));

  // Different topology info
  NetworkTopologyInformation topo;
  topo.rack_id() = 42;
  ResolvedNextHop diffTopo(
      addr1, intfID1, 10, std::nullopt, std::nullopt, topo);
  EXPECT_NE(hasher(base), hasher(diffTopo));

  // Different adjusted weight
  ResolvedNextHop diffAdjWeight(
      addr1, intfID1, 10, std::nullopt, std::nullopt, std::nullopt, 50);
  EXPECT_NE(hasher(base), hasher(diffAdjWeight));

  // Different srv6SegmentList
  std::vector<folly::IPAddressV6> segList{folly::IPAddressV6("2001:db8::1")};
  auto diffSegList = makeResolvedSrv6NextHop(
      addr1, intfID1, 10, segList, TunnelType::SRV6_ENCAP, "tunnel_1");
  EXPECT_NE(hasher(base), hasher(diffSegList));

  // Different tunnelType
  auto diffTunnelType =
      makeResolvedSrv6NextHop(addr1, intfID1, 10, {}, TunnelType::SRV6_ENCAP);
  EXPECT_NE(hasher(base), hasher(diffTunnelType));

  // Different tunnelId
  auto diffTunnelId =
      makeResolvedSrv6NextHop(addr1, intfID1, 10, {}, std::nullopt, "tunnel_1");
  EXPECT_NE(hasher(base), hasher(diffTunnelId));
}

TEST(RouteNextHopTest, Srv6FieldsThriftRoundTrip) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1"), folly::IPAddressV6("2001:db8::2")};

  // ResolvedNextHop round-trip via thrift preserves SRv6 fields
  auto resolved = makeResolvedSrv6NextHop(
      addr, intfID, 10, segList, TunnelType::SRV6_ENCAP, "tunnel_42");

  NextHop nh = resolved;
  auto thrift = nh.toThrift();
  auto deserialized = util::fromThrift(thrift);

  EXPECT_EQ(deserialized.addr(), addr);
  EXPECT_EQ(deserialized.weight(), 10);
  EXPECT_EQ(deserialized.srv6SegmentList(), segList);
  EXPECT_EQ(deserialized.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(deserialized.tunnelId(), "tunnel_42");

  // UnresolvedNextHop round-trip
  auto unresolved = makeSrv6UnresolvedNextHop(
      addr, 10, segList, TunnelType::SRV6_ENCAP, "tunnel_99");

  NextHop unh = unresolved;
  auto uthrift = unh.toThrift();
  auto udeserialized = util::fromThrift(uthrift);

  EXPECT_EQ(unh, udeserialized);
  EXPECT_EQ(udeserialized.srv6SegmentList(), segList);
  EXPECT_EQ(udeserialized.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(udeserialized.tunnelId(), "tunnel_99");
}

TEST(RouteNextHopTest, Srv6FieldsDefaultEmpty) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  ResolvedNextHop resolved(addr, intfID, 10);
  EXPECT_TRUE(resolved.srv6SegmentList().empty());
  EXPECT_EQ(resolved.tunnelType(), std::nullopt);
  EXPECT_EQ(resolved.tunnelId(), std::nullopt);

  UnresolvedNextHop unresolved(addr, 10);
  EXPECT_TRUE(unresolved.srv6SegmentList().empty());
  EXPECT_EQ(unresolved.tunnelType(), std::nullopt);
  EXPECT_EQ(unresolved.tunnelId(), std::nullopt);
}

TEST(RouteNextHopTest, NextHopComparisonWithSrv6Fields) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  std::vector<folly::IPAddressV6> segListA{folly::IPAddressV6("2001:db8::1")};
  std::vector<folly::IPAddressV6> segListB{folly::IPAddressV6("2001:db8::2")};

  // NextHops differing only by srv6SegmentList
  NextHop nhA = makeResolvedSrv6NextHop(
      addr, intfID, 10, segListA, TunnelType::SRV6_ENCAP, "tunnel_1");
  NextHop nhB = makeResolvedSrv6NextHop(
      addr, intfID, 10, segListB, TunnelType::SRV6_ENCAP, "tunnel_1");

  EXPECT_NE(nhA, nhB);
  EXPECT_TRUE(nhA < nhB || nhB < nhA);

  // NextHops differing only by tunnelType
  NextHop nhC =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, TunnelType::IP_IN_IP_DECAP);
  NextHop nhD =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, TunnelType::SRV6_ENCAP);

  EXPECT_NE(nhC, nhD);
  EXPECT_TRUE(nhC < nhD || nhD < nhC);

  // NextHops differing only by tunnelId
  NextHop nhE =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, std::nullopt, "aaa");
  NextHop nhF =
      makeResolvedSrv6NextHop(addr, intfID, 10, {}, std::nullopt, "bbb");

  EXPECT_NE(nhE, nhF);
  EXPECT_TRUE(nhE < nhF || nhF < nhE);
}

TEST(RouteNextHopTest, Srv6ValidationMissingTunnelType) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);
  std::vector<folly::IPAddressV6> segList{folly::IPAddressV6("2001:db8::1")};

  EXPECT_THROW(
      makeResolvedSrv6NextHop(
          addr, intfID, 10, segList, std::nullopt, "tunnel_1"),
      FbossError);

  EXPECT_THROW(
      makeSrv6UnresolvedNextHop(addr, 10, segList, std::nullopt, "tunnel_1"),
      FbossError);
}

TEST(RouteNextHopTest, Srv6ValidationWrongTunnelType) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);
  std::vector<folly::IPAddressV6> segList{folly::IPAddressV6("2001:db8::1")};

  EXPECT_THROW(
      makeResolvedSrv6NextHop(
          addr, intfID, 10, segList, TunnelType::IP_IN_IP_DECAP, "tunnel_1"),
      FbossError);

  EXPECT_THROW(
      makeSrv6UnresolvedNextHop(
          addr, 10, segList, TunnelType::IP_IN_IP_DECAP, "tunnel_1"),
      FbossError);
}

TEST(RouteNextHopTest, Srv6ValidationEmptyTunnelId) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);
  std::vector<folly::IPAddressV6> segList{folly::IPAddressV6("2001:db8::1")};

  EXPECT_THROW(
      makeResolvedSrv6NextHop(
          addr, intfID, 10, segList, TunnelType::SRV6_ENCAP, ""),
      FbossError);

  EXPECT_THROW(
      makeSrv6UnresolvedNextHop(addr, 10, segList, TunnelType::SRV6_ENCAP, ""),
      FbossError);
}

TEST(RouteNextHopTest, CostFieldDefaultEmpty) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  ResolvedNextHop resolved(addr, intfID, 10);
  EXPECT_EQ(resolved.cost(), std::nullopt);

  UnresolvedNextHop unresolved(addr, 10);
  EXPECT_EQ(unresolved.cost(), std::nullopt);
}

TEST(RouteNextHopTest, ResolvedNextHopCostEquality) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  ResolvedNextHop nh1(
      addr,
      intfID,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(100));
  ResolvedNextHop nh2(
      addr,
      intfID,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(100));
  ResolvedNextHop nh3(
      addr,
      intfID,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(200));
  ResolvedNextHop nhNoCost(addr, intfID, 10);

  EXPECT_EQ(nh1, nh2);
  EXPECT_NE(nh1, nh3);
  EXPECT_NE(nh1, nhNoCost);
}

TEST(RouteNextHopTest, UnresolvedNextHopCostEquality) {
  auto addr = folly::IPAddress("2401:db00::1");

  UnresolvedNextHop nh1(
      addr,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(100));
  UnresolvedNextHop nh2(
      addr,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(100));
  UnresolvedNextHop nh3(
      addr,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(200));
  UnresolvedNextHop nhNoCost(addr, 10);

  EXPECT_EQ(nh1, nh2);
  EXPECT_NE(nh1, nh3);
  EXPECT_NE(nh1, nhNoCost);
}

TEST(RouteNextHopTest, CostSetAndUnset) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  ResolvedNextHop nh(addr, intfID, 10);
  EXPECT_EQ(nh.cost(), std::nullopt);

  nh.setCost(42);
  EXPECT_EQ(nh.cost(), 42);

  nh.setCost(std::nullopt);
  EXPECT_EQ(nh.cost(), std::nullopt);
}

TEST(RouteNextHopTest, CostThriftRoundTrip) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  // ResolvedNextHop with cost
  ResolvedNextHop resolved(
      addr,
      intfID,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(42));

  NextHop nh = resolved;
  auto thrift = nh.toThrift();
  EXPECT_EQ(thrift.cost(), 42);

  auto deserialized = util::fromThrift(thrift);
  EXPECT_EQ(deserialized.cost(), int64_t(42));
  EXPECT_EQ(deserialized.addr(), addr);
  EXPECT_EQ(deserialized.weight(), 10);

  // UnresolvedNextHop with cost
  UnresolvedNextHop unresolved(
      addr,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(99));

  NextHop unh = unresolved;
  auto uthrift = unh.toThrift();
  auto udeserialized = util::fromThrift(uthrift);
  EXPECT_EQ(udeserialized.cost(), int64_t(99));

  // Without cost
  ResolvedNextHop noCost(addr, intfID, 10);
  NextHop nhNoCost = noCost;
  auto thriftNoCost = nhNoCost.toThrift();
  EXPECT_FALSE(thriftNoCost.cost().has_value());
  auto deserializedNoCost = util::fromThrift(thriftNoCost);
  EXPECT_EQ(deserializedNoCost.cost(), std::nullopt);
}

TEST(RouteNextHopTest, CostHashCompleteness) {
  std::hash<ResolvedNextHop> hasher;

  auto addr = folly::IPAddress("10.0.0.1");
  auto intfID = InterfaceID(1);

  ResolvedNextHop base(addr, intfID, 10);
  ResolvedNextHop withCost(
      addr,
      intfID,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(100));

  EXPECT_NE(hasher(base), hasher(withCost));
}

TEST(RouteNextHopTest, CostComparison) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);

  NextHop nhA = ResolvedNextHop(
      addr,
      intfID,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(100));
  NextHop nhB = ResolvedNextHop(
      addr,
      intfID,
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(200));

  EXPECT_NE(nhA, nhB);
  EXPECT_TRUE(nhA < nhB);
  EXPECT_TRUE(nhB > nhA);
}

TEST(RouteNextHopTest, Srv6ValidationMissingTunnelId) {
  auto addr = folly::IPAddress("2401:db00::1");
  auto intfID = InterfaceID(1);
  std::vector<folly::IPAddressV6> segList{folly::IPAddressV6("2001:db8::1")};

  EXPECT_THROW(
      makeResolvedSrv6NextHop(
          addr, intfID, 10, segList, TunnelType::SRV6_ENCAP),
      FbossError);

  EXPECT_THROW(
      makeSrv6UnresolvedNextHop(addr, 10, segList, TunnelType::SRV6_ENCAP),
      FbossError);
}

TEST(RouteNextHopTest, OverlayDstMacThriftRoundTrip) {
  UnresolvedNextHop nh(
      folly::IPAddress("10.0.0.1"),
      1,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      TunnelType::VXLAN,
      "vxlan_tunnel_1",
      std::nullopt,
      NextHopRole::PRIMARY,
      folly::MacAddress("02:00:00:00:00:01"));
  auto thrift = NextHop(nh).toThrift();
  ASSERT_TRUE(thrift.overlayDstMac().has_value());
  EXPECT_EQ(*thrift.overlayDstMac(), "02:00:00:00:00:01");
  auto roundTripped = util::fromThrift(thrift);
  EXPECT_EQ(
      roundTripped.overlayDstMac(), folly::MacAddress("02:00:00:00:00:01"));
  EXPECT_EQ(roundTripped.tunnelType(), TunnelType::VXLAN);
  EXPECT_EQ(roundTripped.tunnelId(), "vxlan_tunnel_1");

  // A different MAC must not compare or hash equal: the SAI tunnel NH
  // key includes the MAC.
  UnresolvedNextHop otherMac(
      folly::IPAddress("10.0.0.1"),
      1,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      TunnelType::VXLAN,
      "vxlan_tunnel_1",
      std::nullopt,
      NextHopRole::PRIMARY,
      folly::MacAddress("02:00:00:00:00:02"));
  EXPECT_NE(NextHop(nh), NextHop(otherMac));
  EXPECT_NE(
      std::hash<NextHop>{}(NextHop(nh)),
      std::hash<NextHop>{}(NextHop(otherMac)));
}

TEST(RouteNextHopTest, OverlayDstMacValidation) {
  // overlayDstMac without a VXLAN tunnel type is an error, never dropped.
  EXPECT_THROW(
      UnresolvedNextHop(
          folly::IPAddress("10.0.0.1"),
          1,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          {},
          std::nullopt,
          std::nullopt,
          std::nullopt,
          NextHopRole::PRIMARY,
          folly::MacAddress("02:00:00:00:00:01")),
      std::exception);
  EXPECT_THROW(
      UnresolvedNextHop(
          folly::IPAddress("10.0.0.1"),
          1,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          {},
          TunnelType::VXLAN,
          std::nullopt,
          std::nullopt,
          NextHopRole::PRIMARY,
          folly::MacAddress("02:00:00:00:00:01")),
      std::exception);
  NextHopThrift nht;
  *nht.address() =
      facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.1"));
  nht.tunnelType() = TunnelType::VXLAN;
  nht.tunnelId() = "vxlan_tunnel_1";
  nht.overlayDstMac() = "not-a-mac";
  EXPECT_THROW(util::fromThrift(nht), std::exception);
}
