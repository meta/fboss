/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/json.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/reflection/testing.h> // NOLINT(misc-include-cleaner)
#include <sstream>
#include <string_view> // NOLINT(misc-include-cleaner)
#include <utility> // NOLINT(misc-include-cleaner)
#include <vector>
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/agent/AddressUtil.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedRejected.h"
#include "fboss/cli/fboss2/test/CmdBgpTestUtils.h"
#include "folly/IPAddress.h" // NOLINT(misc-include-cleaner)
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#ifndef IS_OSS
// Avoid EXPECT_THRIFT_EQ clash with <thrift/lib/cpp2/reflection/testing.h>
#undef EXPECT_THRIFT_EQ
#include "nettools/common/TestUtils.h"
#endif

using namespace ::testing;
using facebook::neteng::fboss::bgp::thrift::TBgpPath;
using facebook::neteng::fboss::bgp_attr::TAsPath;
using facebook::neteng::fboss::bgp_attr::TAsPathSeg;
using facebook::neteng::fboss::bgp_attr::TBgpCommunity;
using facebook::neteng::fboss::bgp_attr::TIpPrefix;
namespace facebook::fboss {

class NeighborsReceivedRejectedTestFixture : public CmdHandlerTestBase {
 public:
  std::map<TIpPrefix, std::vector<TBgpPath>> receivedNetworks_;
  std::map<TIpPrefix, std::vector<TBgpPath>> acceptedNetworks_;
  std::map<TIpPrefix, std::vector<TBgpPath>> mergedNetwoks_;
  std::string lookableIp_;
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    lookableIp_ = "8.0.0.0";
    receivedNetworks_ = getReceivedNetworks(
        lookableIp_ + "/32", // prefixAddress
        "8.0.0.1"); // nextHopAddress
    acceptedNetworks_ = getReceivedNetworks(
        "8.0.0.2/32", // prefixAddress
        "8.1.2.1"); // nextHopAddress

    mergedNetwoks_ = receivedNetworks_;
    mergedNetwoks_.insert(acceptedNetworks_.begin(), acceptedNetworks_.end());
  }

  TIpPrefix getPrefix(const std::string& ipAddress) {
    const auto kBinaryAddress =
        facebook::network::toBinaryAddress(folly::IPAddress(ipAddress));
    TIpPrefix prefix;
    prefix.prefix_bin() = kBinaryAddress.addr().value().toStdString();
    prefix.afi() = TBgpAfi::AFI_IPV4;
    prefix.num_bits() = 32;
    return prefix;
  }

 private:
  std::map<TIpPrefix, std::vector<TBgpPath>> createRoutesMap(
      const std::string& nextHop,
      long commRef,
      int asns,
      int localPref,
      int origin,
      const std::string& network) {
    const auto kNextHopAddress =
        facebook::network::toBinaryAddress(folly::IPAddress(nextHop));

    TBgpCommunity community;
    community.community() = commRef;

    TAsPathSeg pathSegment;
    pathSegment.seg_type() = TAsPathSegType::AS_SEQUENCE;
    pathSegment.asns() = {asns};

    TBgpPath path;
    path.next_hop()->prefix_bin() =
        kNextHopAddress.addr().value().toStdString();
    path.communities() = {community};
    path.extCommunities() = {};
    path.as_path() = {pathSegment};
    path.local_pref() = localPref;
    path.origin() = origin;
    path.last_modified_time() = 1635278860724 * 1000;
    path.policy_name() = "Accepted/Modified by SAMPLE_UPLINK_POLICY term N/A";

    return {{getPrefix(network), {path}}};
  }
};

class NeighborsReceivedRejectedTestFixtureWithoutMed
    : public CmdHandlerTestBase {
 public:
  std::map<TIpPrefix, std::vector<TBgpPath>> receivedNetworks_;
  std::string lookableIp_;
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    lookableIp_ = "8.0.0.0";
    receivedNetworks_ = getReceivedNetworks(
        lookableIp_ + "/32", // prefixAddress
        "8.0.0.1", // nextHopAddress
        true, // setCommunity
        true, // setAsPath
        false, // setExtCommunity
        std::nullopt, // clusterList
        std::nullopt, // originatorId
        true, // setPolicy
        false); // setMed
  }
};

TEST_F(NeighborsReceivedRejectedTestFixture, queryClient) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getPrefilterReceivedNetworks2(_, _))
      .WillOnce(Invoke([&](std::map<TIpPrefix, std::vector<TBgpPath>>& networks,
                           std::unique_ptr<std::string> queriedIp) {
        networks = receivedNetworks_;
        queriedIp = std::make_unique<std::string>(lookableIp_);
      }));

  EXPECT_CALL(getMockBgp(), getPostfilterReceivedNetworks2(_, _))
      .WillOnce(Invoke([&](std::map<TIpPrefix, std::vector<TBgpPath>>& networks,
                           std::unique_ptr<std::string> queriedIp) {
        networks = acceptedNetworks_;
        queriedIp = std::make_unique<std::string>(lookableIp_);
      }));

  auto results = BgpNeighborsReceivedRejected().queryClient(
      localhost(), {lookableIp_}, {});
  ASSERT_EQ(results.networkPath()->size(), receivedNetworks_.size());
  for (const auto& [prefix, paths] : receivedNetworks_) {
    ASSERT_TRUE(results.networkPath()->count(prefix));
    EXPECT_THRIFT_EQ_VECTOR(results.networkPath()->at(prefix), paths);
  }
}

TEST_F(NeighborsReceivedRejectedTestFixture, queryClientWithTwoReceivedRoutes) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getPrefilterReceivedNetworks2(_, _))
      .WillOnce(Invoke([&](std::map<TIpPrefix, std::vector<TBgpPath>>& networks,
                           std::unique_ptr<std::string> queriedIp) {
        networks = mergedNetwoks_;
        queriedIp = std::make_unique<std::string>(lookableIp_);
      }));

  EXPECT_CALL(getMockBgp(), getPostfilterReceivedNetworks2(_, _))
      .WillOnce(Invoke([&](std::map<TIpPrefix, std::vector<TBgpPath>>& networks,
                           std::unique_ptr<std::string> queriedIp) {
        networks = acceptedNetworks_;
        queriedIp = std::make_unique<std::string>(lookableIp_);
      }));

  auto results = BgpNeighborsReceivedRejected().queryClient(
      localhost(), {lookableIp_}, {});
  ASSERT_EQ(results.networkPath()->size(), receivedNetworks_.size());
  for (const auto& [prefix, paths] : receivedNetworks_) {
    ASSERT_TRUE(results.networkPath()->count(prefix));
    EXPECT_THRIFT_EQ_VECTOR(results.networkPath()->at(prefix), paths);
  }
}

TEST_F(NeighborsReceivedRejectedTestFixture, printOutput) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly(Invoke([&](std::string& config) {
        // clang-format off
        folly::dynamic value = folly::dynamic::object
          ("communities",
          folly::dynamic::array(
          folly::dynamic::object("name", "SAMPLE_LOOPBACK_COM")
          ("description", "rsw loopback")
          ("communities", folly::dynamic::array("65221:28734"))
          )
        )
        ("localprefs",
        folly::dynamic::array(
          folly::dynamic::object("localpref", 20)
          ("name", "LOCALPREF_SAMPLE_BKUP")
          ("description", "low-priority supplementary/backup routes from bgp controller"),
          folly::dynamic::object("localpref", 25)
          ("name", "LOCALPREF_SAMPL1")
          ("description", "deprioritized local preference value"))
        );
        // clang-format on
        config = folly::toPrettyJson(value);
      }));
  std::stringstream ss;
  NetworkPathWithHost networkPathWithHost;
  networkPathWithHost.networkPath() = receivedNetworks_;
  networkPathWithHost.host() = localhost().getName();
  networkPathWithHost.oobName() = localhost().getOobName();
  networkPathWithHost.ip() = localhost().getIpStr();
  BgpNeighborsReceivedRejected().printOutput(networkPathWithHost, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "---\n"
      "Network: 8.0.0.0/32\n"
      "Nexthop: 8.0.0.1\n"
      "Router/OriginatorId:   --  \n"
      "ClusterList: []\n"
      "Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "ExtCommunities: \n"
      "AsPath: 64712\n"
      "LocalPref: SAMPL1/25\n"
      "Origin: INCOMPLETE\n"
      "MED: 10\n"
      "LastModified: 2021-10-26 13:07:40.724 PDT\n"
      "Policy: Accepted/Modified by SAMPLE_UPLINK_POLICY term N/A\n";
  EXPECT_EQ(output, expectedOutput);
}

TEST_F(NeighborsReceivedRejectedTestFixtureWithoutMed, printOutput) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly(Invoke([&](std::string& config) {
        // clang-format off
        folly::dynamic value = folly::dynamic::object
          ("communities",
          folly::dynamic::array(
          folly::dynamic::object("name", "SAMPLE_LOOPBACK_COM")
          ("description", "rsw loopback")
          ("communities", folly::dynamic::array("65221:28734"))
          )
        )
        ("localprefs",
        folly::dynamic::array(
          folly::dynamic::object("localpref", 20)
          ("name", "LOCALPREF_SAMPLE_BKUP")
          ("description", "low-priority supplementary/backup routes from bgp controller"),
          folly::dynamic::object("localpref", 25)
          ("name", "LOCALPREF_SAMPL1")
          ("description", "deprioritized local preference value"))
        );
        // clang-format on
        config = folly::toPrettyJson(value);
      }));
  std::stringstream ss;
  NetworkPathWithHost networkPathWithHost;
  networkPathWithHost.networkPath() = receivedNetworks_;
  networkPathWithHost.host() = localhost().getName();
  networkPathWithHost.oobName() = localhost().getOobName();
  networkPathWithHost.ip() = localhost().getIpStr();
  BgpNeighborsReceivedRejected().printOutput(networkPathWithHost, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "---\n"
      "Network: 8.0.0.0/32\n"
      "Nexthop: 8.0.0.1\n"
      "Router/OriginatorId:   --  \n"
      "ClusterList: []\n"
      "Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "ExtCommunities: \n"
      "AsPath: 64712\n"
      "LocalPref: SAMPL1/25\n"
      "Origin: INCOMPLETE\n"
      "MED: Not set\n"
      "LastModified: 2021-10-26 13:07:40.724 PDT\n"
      "Policy: Accepted/Modified by SAMPLE_UPLINK_POLICY term N/A\n";
  EXPECT_EQ(output, expectedOutput);
}

TEST_F(NeighborsReceivedRejectedTestFixture, wikiDocHooks) {
  EXPECT_FALSE(BgpNeighborsReceivedRejectedTraits::description().empty());

  /*
   * printRoutesInformation resolves community and local-pref mnemonics
   * through the MODEL's own host/ip, so point the copy under test at the
   * mocked server rather than the canned documentation host.
   */
  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  auto model = BgpNeighborsReceivedRejected::sampleModel();
  EXPECT_EQ(model.networkPath()->size(), 2);
  model.host() = localhost().getName();
  model.oobName() = localhost().getOobName();
  model.ip() = localhost().getIpStr();

  std::stringstream ss;
  BgpNeighborsReceivedRejected().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Network: 0.0.0.0/0"));
  // The received direction must render the UPSTREAM confed ASN (65221), not
  // the downstream one (64650) the advertised views use - sampleNetworkPaths()
  // once ignored its ASN argument and rendered the wrong one here.
  EXPECT_THAT(output, HasSubstr("AsPath: (65221)"));
  EXPECT_THAT(output, HasSubstr("Policy: Denied by SAMPLE_UPLINK_IN"));
}

} // namespace facebook::fboss
