/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <folly/IPAddress.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/TApplicationException.h>

#include "fboss/cli/fboss2/commands/show/bgp/fibnexthops/CmdShowBgpFibNexthops.h"

using facebook::neteng::fboss::bgp::thrift::TFibNexthopDatabase;
using facebook::neteng::fboss::bgp::thrift::TFibNexthopSet;
using facebook::neteng::fboss::bgp::thrift::TFibOutNextHop;
using facebook::neteng::fboss::bgp::thrift::TFibOutNextHopRole;
using facebook::neteng::fboss::bgp_attr::TBgpAfi;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Throw;

namespace facebook::fboss {
namespace {

/** Mock only the RPC used by the FIB nexthop command. */
class MockFibNexthopRpcClient {
 public:
  MOCK_METHOD(
      void,
      sync_getFibNexthopDatabase,
      (TFibNexthopDatabase & database));
};

/** Build one normalized nexthop for renderer tests. */
TFibOutNextHop makeNexthop(
    std::string_view address,
    int64_t weight,
    TFibOutNextHopRole role,
    std::optional<std::string_view> interfaceName = std::nullopt) {
  const auto ip = folly::IPAddress(address);
  TFibOutNextHop nexthop;
  nexthop.next_hop()->afi() = ip.isV4() ? TBgpAfi::AFI_IPV4 : TBgpAfi::AFI_IPV6;
  nexthop.next_hop()->prefix_bin() =
      std::string(reinterpret_cast<const char*>(ip.bytes()), ip.byteCount());
  nexthop.next_hop()->num_bits() = ip.bitCount();
  nexthop.weight() = weight;
  nexthop.role() = role;
  if (interfaceName) {
    nexthop.interface_name() = *interfaceName;
  }
  return nexthop;
}

/** Build a representative canonical-set database for renderer tests. */
TFibNexthopDatabase makeDatabase() {
  TFibNexthopSet multipathSet;
  multipathSet.next_hops() = {
      makeNexthop("192.0.2.1", 10, TFibOutNextHopRole::PRIMARY),
      makeNexthop("192.0.2.2", 20, TFibOutNextHopRole::BACKUP),
  };
  multipathSet.ref_count() = 3;

  auto linkLocal =
      makeNexthop("fe80::1", 30, TFibOutNextHopRole::PRIMARY, "eth1/1");
  linkLocal.is_connected() = true;
  TFibNexthopSet linkLocalSet;
  linkLocalSet.next_hops() = {std::move(linkLocal)};
  linkLocalSet.ref_count() = 7;

  TFibNexthopSet emptySet;
  emptySet.ref_count() = 2;

  TFibNexthopDatabase database;
  database.enabled() = true;
  database.nexthop_sets() = {
      std::move(multipathSet), std::move(linkLocalSet), std::move(emptySet)};
  return database;
}

} // namespace

TEST(CmdShowBgpFibNexthopsTest, PrintOutput) {
  auto database = makeDatabase();
  std::stringstream out;
  CmdShowBgpFibNexthops().printOutput(database, out);

  EXPECT_EQ(
      out.str(),
      "Unique FIB-out nexthop sets: 3\n"
      "Nexthop Set | Route References: 3\n"
      "    192.0.2.1 | Weight: 10 | Role: PRIMARY\n"
      "    192.0.2.2 | Weight: 20 | Role: BACKUP\n"
      "Nexthop Set | Route References: 7\n"
      "    fe80::1 | Weight: 30 | Role: PRIMARY | Connected: true | Interface: eth1/1\n"
      "Nexthop Set | Route References: 2\n"
      "    (empty)\n");
}

TEST(CmdShowBgpFibNexthopsTest, ReportsOldBinary) {
  MockFibNexthopRpcClient client;
  EXPECT_CALL(client, sync_getFibNexthopDatabase(_))
      .WillOnce(Throw(
          apache::thrift::TApplicationException(
              apache::thrift::TApplicationException::UNKNOWN_METHOD,
              "Method name getFibNexthopDatabase not found")));

  try {
    queryFibNexthopDatabaseWithCompatibility(client);
    FAIL() << "Expected unsupported RPC failure";
  } catch (const FbossError& ex) {
    EXPECT_THAT(ex.what(), HasSubstr("does not support FIB-out tracking"));
  }
}

TEST(CmdShowBgpFibNexthopsTest, PreservesRealRpcFailure) {
  MockFibNexthopRpcClient client;
  EXPECT_CALL(client, sync_getFibNexthopDatabase(_))
      .WillOnce(Throw(
          apache::thrift::TApplicationException(
              apache::thrift::TApplicationException::INTERNAL_ERROR,
              "FIB nexthop database timed out")));

  EXPECT_THROW(
      queryFibNexthopDatabaseWithCompatibility(client),
      apache::thrift::TApplicationException);
}

TEST(CmdShowBgpFibNexthopsTest, Disabled) {
  TFibNexthopDatabase database;
  database.enabled() = false;
  std::stringstream out;

  CmdShowBgpFibNexthops().printOutput(database, out);

  EXPECT_THAT(out.str(), HasSubstr("disabled"));
  EXPECT_THAT(out.str(), Not(HasSubstr("Unique FIB-out nexthop sets:")));
}

TEST(CmdShowBgpFibNexthopsTest, EnabledWithoutNexthopSets) {
  TFibNexthopDatabase database;
  database.enabled() = true;
  std::stringstream out;

  CmdShowBgpFibNexthops().printOutput(database, out);

  EXPECT_EQ(
      out.str(),
      "Unique FIB-out nexthop sets: 0\n"
      "FIB-out tracking is enabled; no nexthop sets are currently "
      "referenced.\n");
}

TEST(CmdShowBgpFibNexthopsTest, InvalidAddressPrintsPlaceholder) {
  TFibNexthopDatabase database;
  database.enabled() = true;
  TFibNexthopSet set;
  set.ref_count() = 1;
  TFibOutNextHop nexthop;
  nexthop.next_hop()->prefix_bin() = "invalid";
  nexthop.weight() = 1;
  nexthop.role() = TFibOutNextHopRole::PRIMARY;
  set.next_hops()->push_back(std::move(nexthop));
  database.nexthop_sets()->push_back(std::move(set));
  std::stringstream out;

  CmdShowBgpFibNexthops().printOutput(database, out);

  EXPECT_THAT(out.str(), HasSubstr("<invalid>"));
}

} // namespace facebook::fboss
