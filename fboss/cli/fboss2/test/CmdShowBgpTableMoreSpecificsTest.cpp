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
#include <folly/json.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/TApplicationException.h>
#include <thrift/lib/cpp2/reflection/testing.h> // NOLINT(misc-include-cleaner)
#include <memory>
#include <vector>
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableMoreSpecifics.h"
#include "fboss/cli/fboss2/test/CmdBgpTestUtils.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#ifndef IS_OSS
// Avoid EXPECT_THRIFT_EQ clash with <thrift/lib/cpp2/reflection/testing.h>
#undef EXPECT_THRIFT_EQ
#include "nettools/common/TestUtils.h"
#endif

using namespace ::testing;
using namespace facebook::neteng::fboss::bgp::thrift;
namespace facebook::fboss {
class CmdShowBgpTableMoreSpecificsTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<TRibEntry> queriedEntry_;
  const std::string kPrefixToQuery = "8.0.0.0/32";

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    queriedEntry_ = {
        buildEntry(kPrefixToQuery, "8.0.0.1", "1.2.3.4", "one.two.three.four")};
  }
};

TEST_F(CmdShowBgpTableMoreSpecificsTestFixture, queryClient) {
  setupMockedBgpServer();
  auto canonical = buildCanonicalRibState(kPrefixToQuery);
  EXPECT_CALL(getMockBgp(), getRibSubprefixesCanonical(_, _))
      .WillOnce([&](TCanonicalRibState& state, std::unique_ptr<std::string>) {
        state = canonical;
      });

  auto result =
      CmdShowBgpTableMoreSpecifics().queryClient(localhost(), {kPrefixToQuery});
  EXPECT_THRIFT_EQ_VECTOR(
      *result.tRibEntries(), resolveCanonicalRibState(canonical));
}

TEST_F(CmdShowBgpTableMoreSpecificsTestFixture, printOutput) {
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
  TRibEntryWithHost tRibEntryWithHost;
  tRibEntryWithHost.tRibEntries() = queriedEntry_;
  tRibEntryWithHost.host() = localhost().getName();
  tRibEntryWithHost.oobName() = localhost().getOobName();
  tRibEntryWithHost.ip() = localhost().getIpStr();
  CmdShowBgpTableMoreSpecifics().printOutput(tRibEntryWithHost, ss);
  std::string output = ss.str();

  std::string expectedOutput = kRibEntryMarkersHeader +
      "\n> 8.0.0.0/32, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 1.2.3.4 (one.two.three.four) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: [1.1.1.2]\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n";

  maskDateInOutput(output);
  EXPECT_EQ(output, expectedOutput);
}
TEST_F(CmdShowBgpTableMoreSpecificsTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpTableMoreSpecificsTraits::description().empty());

  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  auto model = CmdShowBgpTableMoreSpecifics::sampleModel();
  ASSERT_EQ(model.tRibEntries()->size(), 3);
  model.host() = localhost().getName();
  model.oobName() = localhost().getOobName();
  model.ip() = localhost().getIpStr();

  std::stringstream ss;
  CmdShowBgpTableMoreSpecifics().printOutput(model, ss);
  const std::string output = ss.str();

  // The covering prefix is part of the result, which is what the description
  // says and what isSubnet() on the server actually does.
  EXPECT_THAT(output, HasSubstr("> 2001:db8:1c00::/40"));
  EXPECT_THAT(output, HasSubstr("> 2001:db8:1c00::/44"));
  EXPECT_THAT(output, HasSubstr("> 2001:db8:1c10::/44"));
  // Each more-specific names the peer contributing it, which is the question
  // the description says this command answers.
  EXPECT_THAT(
      output, HasSubstr("from 2001:db8:e11e:1062::5f (fsw002.p001.f01.abc1)"));
  // The originator moves with the peer, so the second /44 must not report
  // fsw001's router ID.
  EXPECT_THAT(output, HasSubstr("Router/Originator: 192.0.2.102"));
}

} // namespace facebook::fboss
