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
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"
#include "fboss/cli/fboss2/commands/show/bgp/shadowrib/CmdShowBgpShadowRib.h"
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
class CmdShowBgpShadowRibTestFixture : public CmdHandlerTestBase {
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

TEST_F(CmdShowBgpShadowRibTestFixture, queryClient) {
  setupMockedBgpServer();
  auto canonicalV4 = buildCanonicalRibState();
  auto canonicalV6 = buildCanonicalRibState(
      "2001::1/64", "2001::2", "2001::3", "two00one::three");
  EXPECT_CALL(getMockBgp(), getShadowRibEntriesCanonical(_, TBgpAfi::AFI_IPV4))
      .WillOnce(
          [&](TCanonicalRibState& state, TBgpAfi) { state = canonicalV4; });
  EXPECT_CALL(getMockBgp(), getShadowRibEntriesCanonical(_, TBgpAfi::AFI_IPV6))
      .WillOnce(
          [&](TCanonicalRibState& state, TBgpAfi) { state = canonicalV6; });

  auto result = CmdShowBgpShadowRib().queryClient(localhost());

  std::vector<TRibEntry> expected = resolveCanonicalRibState(canonicalV4);
  auto expectedV6 = resolveCanonicalRibState(canonicalV6);
  expected.insert(expected.end(), expectedV6.begin(), expectedV6.end());
  EXPECT_THRIFT_EQ_VECTOR(*result.tRibEntries(), expected);
}

TEST_F(CmdShowBgpShadowRibTestFixture, printOutput) {
  combinedEntries_.emplace_back(
      buildEntry("8.0.0.1/32", "8.0.0.2", "8.1.2.8", "eight.one.two.eight"));
  combinedEntries_.emplace_back(
      buildEntry("2001::3/64", "2001::4", "2001::5", "two00one::five"));

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
  CmdShowBgpShadowRib().printOutput(tRibEntryWithHost, ss);
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
// Test the CPS annotation (Meta-internal feature)
TEST_F(CmdShowBgpShadowRibTestFixture, printCPSOutput) {
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
  CmdShowBgpShadowRib().printOutput(tRibEntryWithHost, ss);
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

TEST_F(CmdShowBgpShadowRibTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpShadowRibTraits::description().empty());

  /*
   * printRIBEntries looks up the community/local-pref mnemonics through the
   * MODEL's own host/ip. sampleModel() carries the canned documentation host
   * the wiki renders under, so point the copy under test at the mocked server
   * -- otherwise this is a real connect to an unroutable address. The mock
   * returns an empty config, so the render falls back to raw asn:value
   * communities and numeric local prefs.
   */
  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  auto model = CmdShowBgpShadowRib::sampleModel();
  EXPECT_FALSE(model.tRibEntries()->empty());
  model.host() = localhost().getName();
  model.oobName() = localhost().getOobName();
  model.ip() = localhost().getIpStr();
  std::stringstream ss;
  CmdShowBgpShadowRib().printOutput(model, ss);
  const std::string output = ss.str();

  // Same listing and markers as 'show bgp table', which is the point of the
  // shadow RIB being directly comparable against it.
  EXPECT_THAT(output, testing::HasSubstr("*@  from 192.0.2.11"));
  EXPECT_THAT(output, testing::HasSubstr("> 0.0.0.0/0, Selected 2/3 paths"));
}

} // namespace facebook::fboss
