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
#include <thrift/lib/cpp/TApplicationException.h>
#include <thrift/lib/cpp2/reflection/testing.h> // NOLINT(misc-include-cleaner)
#include <vector>
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include <folly/json/json.h>

#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableDetail.h"
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
class CmdShowBgpTableDetailTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<TRibEntry> entriesIPv4_;
  std::vector<TRibEntry> entriesIPv6_;
  std::vector<TRibEntry> combinedEntries_;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    entriesIPv4_ = {buildEntry()};
    entriesIPv6_ = {
        buildEntry("2001::1/64", "2001::2", "2001::3", "two00one::three", 7)};
    entriesIPv6_.front()
        .paths()
        ->at(entriesIPv6_.front().best_group().value())
        .front()
        .backup_addr() = getPrefix("2001:db8::1");
    combineEntries();
  }

  void combineEntries() {
    this->combinedEntries_ = this->entriesIPv4_;
    this->combinedEntries_.insert(
        combinedEntries_.end(), entriesIPv6_.begin(), entriesIPv6_.end());
  }
};

TEST_F(CmdShowBgpTableDetailTestFixture, queryClient) {
  setupMockedBgpServer();
  auto canonicalV4 = buildCanonicalRibState();
  auto canonicalV6 = buildCanonicalRibState(
      "2001::1/64", "2001::2", "2001::3", "two00one::three");
  EXPECT_CALL(getMockBgp(), getRibEntriesCanonical(_, TBgpAfi::AFI_IPV4))
      .WillOnce(
          [&](TCanonicalRibState& state, TBgpAfi) { state = canonicalV4; });
  EXPECT_CALL(getMockBgp(), getRibEntriesCanonical(_, TBgpAfi::AFI_IPV6))
      .WillOnce(
          [&](TCanonicalRibState& state, TBgpAfi) { state = canonicalV6; });

  auto result = CmdShowBgpTableDetail().queryClient(localhost());

  std::vector<TRibEntry> expected = resolveCanonicalRibState(canonicalV4);
  auto expectedV6 = resolveCanonicalRibState(canonicalV6);
  expected.insert(expected.end(), expectedV6.begin(), expectedV6.end());
  EXPECT_THRIFT_EQ_VECTOR(*result.tRibEntries(), expected);
}

TEST_F(CmdShowBgpTableDetailTestFixture, printOutput) {
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
  tRibEntryWithHost.tRibEntries() = combinedEntries_;
  tRibEntryWithHost.host() = localhost().getName();
  tRibEntryWithHost.oobName() = localhost().getOobName();
  tRibEntryWithHost.ip() = localhost().getIpStr();
  CmdShowBgpTableDetail().printOutput(tRibEntryWithHost, ss);
  std::string output = ss.str();

  std::string expectedOutput = kRibEntryMarkersHeader +
      "\n> 8.0.0.0/32, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 1.2.3.4 (one.two.three.four) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: [1.1.1.2]\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n"
      "\n> 2001::1/64, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 2001::3 (two00one::three) via 2001::2 (backup: 2001:db8::1) | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: 7 | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: [1.1.1.2]\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n";

  maskDateInOutput(output);
  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpTableDetailTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpTableDetailTraits::description().empty());
  EXPECT_FALSE(CmdShowBgpTableDetail::sampleModel().tRibEntries()->empty());
  /*
   * printRIBEntries reaches getLocalBgpConfig for the community/local-pref
   * mnemonics, and builds the HostInfo it connects to from the MODEL's own
   * host/ip fields. sampleModel() carries the canned documentation host the
   * wiki renders under, so point the copy under test at the mocked server
   * instead - otherwise this is a real connect to an unroutable address that
   * only ends on timeout. The mock returns an empty config, so the render falls
   * back to raw asn:value communities and numeric local prefs, which is what
   * the expected output below reflects.
   */
  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  auto model = CmdShowBgpTableDetail::sampleModel();
  model.host() = localhost().getName();
  model.oobName() = localhost().getOobName();
  model.ip() = localhost().getIpStr();
  std::stringstream ss;
  CmdShowBgpTableDetail().printOutput(model, ss);
  const std::string output = ss.str();

  // Marker semantics the description explains: best path, other ECMP member,
  // and a lower-local-pref path outside the best group.
  EXPECT_THAT(output, HasSubstr("*@  from 192.0.2.11"));
  EXPECT_THAT(output, HasSubstr("*   from 192.0.2.12"));
  EXPECT_THAT(output, HasSubstr("    from 192.0.2.13"));
  EXPECT_THAT(output, HasSubstr("> 0.0.0.0/0, Selected 2/3 paths"));
  // detail adds the originator, cluster list and community lines, plus the
  // tie-break that rejected the non-best ECMP path.
  EXPECT_THAT(output, HasSubstr("Router/Originator: 192.0.2.102"));
  EXPECT_THAT(output, HasSubstr("Communities:"));
  EXPECT_THAT(output, HasSubstr("BestPath Rejection Reason:"));
}

} // namespace facebook::fboss
