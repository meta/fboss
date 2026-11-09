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
#include <vector>
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"
#include "fboss/cli/fboss2/commands/show/bgp/changelist/CmdShowBgpChangelist.h"
#include "fboss/cli/fboss2/test/CmdBgpTestUtils.h"
#ifndef IS_OSS
// Avoid EXPECT_THRIFT_EQ clash with <thrift/lib/cpp2/reflection/testing.h>
#undef EXPECT_THRIFT_EQ
#include "nettools/common/TestUtils.h"
#endif

using facebook::neteng::fboss::bgp::thrift::TRibEntry;
using facebook::neteng::fboss::bgp::thrift::TRibEntryWithHost;
using facebook::neteng::fboss::bgp_attr::TBgpAfi;
using ::testing::_;
using ::testing::Invoke;

namespace facebook::fboss {
class CmdShowBgpChangelistTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<TRibEntry> entriesIPv4_;
  std::vector<TRibEntry> entriesIPv6_;
  std::vector<TRibEntry> combinedEntries_;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    entriesIPv4_ = {buildEntry()};
    entriesIPv6_ = {
        buildEntry("2001::1/64", "2001::2", "2001::3", "two00one::three")};
    combineEntries();
  }

  void combineEntries() {
    this->combinedEntries_ = this->entriesIPv4_;
    this->combinedEntries_.insert(
        combinedEntries_.end(), entriesIPv6_.begin(), entriesIPv6_.end());
  }
};

TEST_F(CmdShowBgpChangelistTestFixture, queryClient) {
  setupMockedBgpServer();
  auto canonicalV4 = buildCanonicalRibState();
  auto canonicalV6 = buildCanonicalRibState(
      "2001::1/64", "2001::2", "2001::3", "two00one::three");
  EXPECT_CALL(getMockBgp(), getChangeListEntriesCanonical(_, TBgpAfi::AFI_IPV4))
      .WillOnce(
          [&](TCanonicalRibState& state, TBgpAfi) { state = canonicalV4; });
  EXPECT_CALL(getMockBgp(), getChangeListEntriesCanonical(_, TBgpAfi::AFI_IPV6))
      .WillOnce(
          [&](TCanonicalRibState& state, TBgpAfi) { state = canonicalV6; });

  auto result = CmdShowBgpChangelist().queryClient(localhost());

  std::vector<TRibEntry> expected = resolveCanonicalRibState(canonicalV4);
  auto expectedV6 = resolveCanonicalRibState(canonicalV6);
  expected.insert(expected.end(), expectedV6.begin(), expectedV6.end());
  EXPECT_THRIFT_EQ_VECTOR(*result.tRibEntries(), expected);
}

TEST_F(CmdShowBgpChangelistTestFixture, printOutput) {
  combinedEntries_.emplace_back(
      buildEntry("8.0.0.1/32", "8.0.0.2", "8.1.2.8", "eight.one.two.eight"));
  combinedEntries_.emplace_back(
      buildEntry("2001::3/64", "2001::4", "2001::5", "two00one::five"));

  // clang-format off
  folly::dynamic cpsConfigValue = folly::dynamic::object
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

  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly(Invoke([cpsConfigValue](std::string& config) {
        config = folly::toPrettyJson(cpsConfigValue);
      }));
  std::stringstream ss;
  TRibEntryWithHost tRibEntryWithHost;
  tRibEntryWithHost.tRibEntries() = combinedEntries_;
  tRibEntryWithHost.host() = localhost().getName();
  tRibEntryWithHost.oobName() = localhost().getOobName();
  tRibEntryWithHost.ip() = localhost().getIpStr();
  CmdShowBgpChangelist().printOutput(tRibEntryWithHost, ss);
  std::string output = ss.str();

  std::string expectedOutput = kRibEntryMarkersHeader +
      "\n> 8.0.0.0/32, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 1.2.3.4 (one.two.three.four) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100\n"
      "\n> 8.0.0.1/32, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 8.1.2.8 (eight.one.two.eight) via 8.0.0.2 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100\n"
      "\n> 2001::1/64, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 2001::3 (two00one::three) via 2001::2 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100\n"
      "\n> 2001::3/64, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 2001::5 (two00one::five) via 2001::4 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100\n";

  maskDateInOutput(output);
  EXPECT_EQ(output, expectedOutput);
}

#ifndef IS_OSS
// Test the CPS annotation (Meta-internal feature, not available in OSS)
TEST_F(CmdShowBgpChangelistTestFixture, printCPSOutput) {
  // PathSelector contains the info for the active criteria
  bgp::rib_policy::TPathSelector activeCriteria1;
  activeCriteria1.bgp_native_path_selection_min_nexthop() = 20;
  auto pathOverriddenEntry1 = buildEntry(
      "8.0.0.1/32",
      "8.0.0.2",
      "8.1.2.8",
      "eight.one.two.eight",
      std::nullopt,
      true /* omit best path */,
      true /* omit best paths */);
  pathOverriddenEntry1.active_cps_criteria() = std::move(activeCriteria1);
  combinedEntries_.push_back(std::move(pathOverriddenEntry1));
  auto pathOverriddenEntry3 = buildEntry(
      "8.0.0.3/32",
      "8.0.0.2",
      "8.1.2.8",
      "eight.one.two.eight",
      std::nullopt,
      true /* omit best path */);
  bgp::rib_policy::TPathSelector activeCriteria3;
  activeCriteria3.bgp_native_path_selection_min_nexthop() = 20;
  activeCriteria3.drain_on_min_nexthop_violation() = true;
  pathOverriddenEntry3.active_cps_criteria() = std::move(activeCriteria3);
  combinedEntries_.push_back(std::move(pathOverriddenEntry3));

  bgp::rib_policy::TPathSelector activeCriteria2;
  bgp::rib_policy::TPathSelectionCriteria tCriteria;
  tCriteria.min_nexthop() = 13;
  activeCriteria2.criteria_list()->push_back(std::move(tCriteria));
  auto pathOverriddenEntry2 =
      buildEntry("2001::3/64", "2001::4", "2001::5", "two00one::five");
  pathOverriddenEntry2.active_cps_criteria() = std::move(activeCriteria2);
  combinedEntries_.push_back(std::move(pathOverriddenEntry2));
  // clang-format off
  folly::dynamic cpsConfigValue2 = folly::dynamic::object
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

  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly(Invoke([cpsConfigValue2](std::string& config) {
        config = folly::toPrettyJson(cpsConfigValue2);
      }));
  std::stringstream ss;
  TRibEntryWithHost tRibEntryWithHost;
  tRibEntryWithHost.tRibEntries() = combinedEntries_;
  tRibEntryWithHost.host() = localhost().getName();
  tRibEntryWithHost.oobName() = localhost().getOobName();
  tRibEntryWithHost.ip() = localhost().getIpStr();
  CmdShowBgpChangelist().printOutput(tRibEntryWithHost, ss);
  std::string output = ss.str();

  std::string expectedOutput = kRibEntryMarkersHeader +
      "\n> 8.0.0.0/32, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 1.2.3.4 (one.two.three.four) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100\n"
      "\n> 8.0.0.1/32, Selected 0/1 paths (1 active, 0 inactive)\n"
      "Path overridden by CPS:\n"
      "  prefix: 8.0.0.1/32\n"
      "    default BGP multipath selector\n"
      "    BGP native min nexthop: 20\n"
      "    from 8.1.2.8 (eight.one.two.eight) via 8.0.0.2 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100\n"
      "\n> 8.0.0.3/32, Selected 1/1 paths (1 active, 0 inactive)\n"
      "Path overridden by CPS:\n"
      "  prefix: 8.0.0.3/32\n"
      "    default BGP multipath selector\n"
      "    BGP native min nexthop: 20, partial drain: true\n"
      "*   from 8.1.2.8 (eight.one.two.eight) via 8.0.0.2 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100\n"
      "\n> 2001::1/64, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 2001::3 (two00one::three) via 2001::2 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100\n"
      "\n> 2001::3/64, Selected 1/1 paths (1 active, 0 inactive)\n"
      "Path overridden by CPS:\n"
      "  prefix: 2001::3/64\n"
      "    active criteria:\n"
      "      path_matchers:\n"
      "      min nexthop: 13\n"
      "*@  from 2001::5 (two00one::five) via 2001::4 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100\n";

  maskDateInOutput(output);
  EXPECT_EQ(output, expectedOutput);
}
#endif // IS_OSS
TEST_F(CmdShowBgpChangelistTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpChangelistTraits::description().empty());

  /*
   * printRIBEntries reaches getLocalBgpConfig for the community/local-pref
   * mnemonics and builds the HostInfo it connects to from the MODEL's own
   * host/ip fields, so point the copy under test at the mocked server rather
   * than at the canned documentation host.
   */
  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  auto model = CmdShowBgpChangelist::sampleModel();
  EXPECT_FALSE(model.tRibEntries()->empty());
  model.host() = localhost().getName();
  model.oobName() = localhost().getOobName();
  model.ip() = localhost().getIpStr();

  std::stringstream ss;
  CmdShowBgpChangelist().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, ::testing::HasSubstr("> 0.0.0.0/0, Selected 2/3 paths"));
  // printOutput leaves detail off, so the per-path detail lines are absent -
  // this renders like 'show bgp table', not 'table detail'.
  EXPECT_THAT(
      output, ::testing::Not(::testing::HasSubstr("Router/Originator:")));
  EXPECT_THAT(output, ::testing::Not(::testing::HasSubstr("Communities:")));
}

} // namespace facebook::fboss
