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
#include <vector>
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/agent/AddressUtil.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/received/BgpNeighborsReceivedPrePolicy.h"
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
using facebook::neteng::fboss::bgp_attr::TIpPrefix;
namespace facebook::fboss {

class NeighborsReceivedPrePolicyTestFixture : public CmdHandlerTestBase {
 public:
  std::map<TIpPrefix, std::vector<TBgpPath>> receivedNetworks_;
  std::string lookableIp_;
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    receivedNetworks_ = getReceivedNetworks(
        "8.0.0.0/32", // prefixAddress
        "8.0.0.1", // nextHopAddress
        true, // setCommunity
        true, // setAsPath
        false, // setExtCommunity
        std::nullopt, // clusterList
        std::nullopt, // originatorId
        false); // setPolicy
    lookableIp_ = "1.2.3.4";
  }
};

class NeighborsReceivedPrePolicyTestFixtureWithoutMed
    : public CmdHandlerTestBase {
 public:
  std::map<TIpPrefix, std::vector<TBgpPath>> receivedNetworks_;
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    receivedNetworks_ = getReceivedNetworks(
        "8.0.0.0/32", // prefixAddress
        "8.0.0.1", // nextHopAddress
        true, // setCommunity
        true, // setAsPath
        false, // setExtCommunity
        std::nullopt, // clusterList
        std::nullopt, // originatorId
        false, // setPolicy
        false); // setMed
  }
};

TEST_F(NeighborsReceivedPrePolicyTestFixture, queryClient) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getPrefilterReceivedNetworks2(_, _))
      .WillOnce(Invoke([&](std::map<TIpPrefix, std::vector<TBgpPath>>& networks,
                           std::unique_ptr<std::string> queriedIp) {
        networks = receivedNetworks_;
        queriedIp = std::make_unique<std::string>(lookableIp_);
      }));
  auto results = BgpNeighborsReceivedPrePolicy().queryClient(
      localhost(), {lookableIp_}, {});
  ASSERT_EQ(results.networkPath()->size(), receivedNetworks_.size());
  for (const auto& [prefix, paths] : receivedNetworks_) {
    ASSERT_TRUE(results.networkPath()->count(prefix));
    EXPECT_THRIFT_EQ_VECTOR(results.networkPath()->at(prefix), paths);
  }
}

TEST_F(NeighborsReceivedPrePolicyTestFixture, printOutputWithUnsetValues) {
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
  BgpNeighborsReceivedPrePolicy().printOutput(networkPathWithHost, ss);
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
      "LastModified: 2021-10-26 13:07:40.724 PDT\n";
  EXPECT_EQ(output, expectedOutput);
}

TEST_F(NeighborsReceivedPrePolicyTestFixture, printOutputWithAllValues) {
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
  receivedNetworks_ = getReceivedNetworks(
      "8.0.0.0/32", "8.0.0.1", true, true, true, 0x02010101, 0x03020202, false);

  std::stringstream ss;
  NetworkPathWithHost networkPathWithHost;
  networkPathWithHost.networkPath() = receivedNetworks_;
  networkPathWithHost.host() = localhost().getName();
  networkPathWithHost.oobName() = localhost().getOobName();
  networkPathWithHost.ip() = localhost().getIpStr();
  BgpNeighborsReceivedPrePolicy().printOutput(networkPathWithHost, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "---\n"
      "Network: 8.0.0.0/32\n"
      "Nexthop: 8.0.0.1\n"
      "Router/OriginatorId: 2.2.2.3\n"
      "ClusterList: [1.1.1.2]\n"
      "Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "AsPath: 64712\n"
      "LocalPref: SAMPL1/25\n"
      "Origin: INCOMPLETE\n"
      "MED: 10\n"
      "LastModified: 2021-10-26 13:07:40.724 PDT\n";
  EXPECT_EQ(output, expectedOutput);
}

TEST_F(
    NeighborsReceivedPrePolicyTestFixtureWithoutMed,
    printOutputWithUnsetValues) {
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
  BgpNeighborsReceivedPrePolicy().printOutput(networkPathWithHost, ss);
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
      "LastModified: 2021-10-26 13:07:40.724 PDT\n";
  EXPECT_EQ(output, expectedOutput);
}

TEST_F(NeighborsReceivedPrePolicyTestFixture, wikiDocHooks) {
  EXPECT_FALSE(BgpNeighborsReceivedPrePolicyTraits::description().empty());

  /*
   * printRoutesInformation resolves community and local-pref mnemonics
   * through the MODEL's own host/ip, so point the copy under test at the
   * mocked server rather than the canned documentation host.
   */
  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  auto model = BgpNeighborsReceivedPrePolicy::sampleModel();
  EXPECT_EQ(model.networkPath()->size(), 2);
  model.host() = localhost().getName();
  model.oobName() = localhost().getOobName();
  model.ip() = localhost().getIpStr();

  std::stringstream ss;
  BgpNeighborsReceivedPrePolicy().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Network: 0.0.0.0/0"));
  // The received direction must render the UPSTREAM confed ASN (65221), not
  // the downstream one (64650) the advertised views use - sampleNetworkPaths()
  // once ignored its ASN argument and rendered the wrong one here.
  EXPECT_THAT(output, HasSubstr("AsPath: (65221)"));
  // Pre-policy views pass showPolicy=false, so no Policy line.
  EXPECT_THAT(output, Not(HasSubstr("Policy:")));
}

} // namespace facebook::fboss
