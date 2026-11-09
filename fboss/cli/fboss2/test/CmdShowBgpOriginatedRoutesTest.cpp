/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "common/network/if/gen-cpp2/Address_types.h"
#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/agent/AddressUtil.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowBgpOriginatedRoutes.h"
#include "folly/IPAddress.h"
#include "folly/Range.h"
#include "folly/json/json.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace ::testing;
using facebook::neteng::fboss::bgp::thrift::TOriginatedRoute;
using facebook::neteng::fboss::bgp::thrift::TOriginatedRouteWithHost;
using facebook::neteng::fboss::bgp_attr::TBgpAfi;
using facebook::neteng::fboss::bgp_attr::TBgpCommunity;
using facebook::neteng::fboss::bgp_attr::TIpPrefix;
namespace facebook::fboss {

const auto kIpVersion = TBgpAfi::AFI_IPV4;
const auto kBinaryAddress =
    facebook::network::toBinaryAddress(folly::IPAddress("8.0.0.0"));
const auto kAddressMask = 32;
const auto kCommunityNumber = 4274352190;
const auto kSupportingRoutes = 0;

class CmdShowBgpOriginatedRoutesTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<TOriginatedRoute> routes_;
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    routes_ = getOriginatedRoutes();
  }

  std::vector<TOriginatedRoute> getOriginatedRoutes() {
    TIpPrefix ip_prefix;
    ip_prefix.afi() = kIpVersion;
    ip_prefix.prefix_bin() = kBinaryAddress.addr().value().toStdString();
    ip_prefix.num_bits() = kAddressMask;

    TBgpCommunity bgp_community;
    bgp_community.community() = kCommunityNumber;

    TOriginatedRoute queried_route;
    queried_route.prefix() = ip_prefix;
    queried_route.communities() = {bgp_community};
    queried_route.supporting_route_count() = kSupportingRoutes;
    queried_route.minimum_supporting_routes() = kSupportingRoutes;

    return {queried_route};
  }
};

TEST_F(CmdShowBgpOriginatedRoutesTestFixture, queryClient) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getOriginatedRoutes(_))
      .WillOnce(Invoke([&](auto& entries) { entries = routes_; }));

  auto results = CmdShowBgpOriginatedRoutes().queryClient(localhost());
  const auto routes = *results.tOriginatedRoutes();
  ASSERT_EQ(routes.size(), 1);

  const auto& prefix = routes[0].prefix().value();
  EXPECT_EQ(prefix.afi().value(), kIpVersion);
  EXPECT_EQ(prefix.prefix_bin().value(), kBinaryAddress.addr()->toStdString());
  EXPECT_EQ(prefix.num_bits().value(), kAddressMask);

  const auto& communites =
      apache::thrift::get_pointer(routes[0].communities())[0];
  ASSERT_EQ(communites.size(), 1);
  EXPECT_EQ(communites[0].community().value(), kCommunityNumber);
  EXPECT_EQ(routes[0].supporting_route_count().value(), kSupportingRoutes);
  EXPECT_EQ(routes[0].minimum_supporting_routes().value(), kSupportingRoutes);
}

TEST_F(CmdShowBgpOriginatedRoutesTestFixture, printOutput) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillOnce(Invoke([&](std::string& config) {
        // clang-format off
        folly::dynamic value = folly::dynamic::object
          ("communities",
          folly::dynamic::array(
          folly::dynamic::object("name", "SAMPLE_LOOPBACK_COM")
          ("description", "rsw loopback")
          ("communities", folly::dynamic::array("65221:28734"))
          )
        );
        // clang-format on
        config = folly::toPrettyJson(value);
      }));
  std::stringstream ss;
  TOriginatedRouteWithHost originatedRouteWithHost;
  originatedRouteWithHost.tOriginatedRoutes() = routes_;
  originatedRouteWithHost.host() = localhost().getName();
  originatedRouteWithHost.oobName() = localhost().getOobName();
  originatedRouteWithHost.ip() = localhost().getIpStr();
  CmdShowBgpOriginatedRoutes().printOutput(originatedRouteWithHost, ss);

  std::string output = ss.str();
  std::string expectedOutput =
      " Prefix      Communities                      Supporting Route Cnt  Minimum supporting route  Require Nexthop Resolution \n"
      "-------------------------------------------------------------------------------------------------------------------------------\n"
      " 8.0.0.0/32  SAMPLE_LOOPBACK_COM/65221:28734  0                     0                         N/A                        \n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpOriginatedRoutesTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpOriginatedRoutesTraits::description().empty());

  /*
   * printOutput reaches getCommunitySet -> getLocalBgpConfig through the
   * MODEL's own host/ip, so point the copy under test at the mocked server;
   * otherwise this is a real connect to an unroutable documentation address
   * that only ends on timeout. The mock returns an empty config, the same "no
   * mnemonics" state the offline wiki generator renders under, so the expected
   * (NA)/asn:value output below is unchanged.
   */
  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly(Invoke([](std::string& config) { config = "{}"; }));

  auto model = CmdShowBgpOriginatedRoutes::sampleModel();
  EXPECT_EQ(model.tOriginatedRoutes()->size(), 3);
  model.host() = localhost().getName();
  model.oobName() = localhost().getOobName();
  model.ip() = localhost().getIpStr();

  std::stringstream ss;
  CmdShowBgpOriginatedRoutes().printOutput(model, ss);

  /*
   * Pin the whole render rather than probing for substrings: the columns are
   * mostly small integers, so a substring check cannot tell the supporting
   * count from the minimum from the next-hop-resolution flag. Communities show
   * as "(NA)/asn:value" because no bgpd is reachable to resolve the mnemonics.
   */
  const std::string expectedOutput =
      " Prefix                       Communities     Supporting Route Cnt  Minimum supporting route  Require Nexthop Resolution \n"
      "-------------------------------------------------------------------------------------------------------------------------------\n"
      " 192.0.2.1/32                 (NA)/64873:521  0                     0                         0                          \n"
      "                              (NA)/65221:291                                                                             \n"
      " 2001:db8:e111:f162:27::/128  (NA)/64873:521  0                     0                         0                          \n"
      "                              (NA)/65221:291                                                                             \n"
      " 2001:db8:111c:6227::/64      (NA)/64873:521  12                    8                         1                          \n"
      "                              (NA)/65108:725                                                                             \n"
      "\n";

  EXPECT_EQ(ss.str(), expectedOutput);
}

} // namespace facebook::fboss
