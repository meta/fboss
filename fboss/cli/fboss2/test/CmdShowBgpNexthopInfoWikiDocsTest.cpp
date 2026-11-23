/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sstream>

#include "fboss/cli/fboss2/commands/show/bgp/nexthopinfo/CmdShowBgpNexthopInfo.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

/*
 * Wiki-doc-hook coverage for `show bgp nexthopinfo`. Deliberately separate from
 * test/facebook/CmdShowBgpNexthopInfoTest.cpp, which covers queryClient, cache
 * misses and the empty cache but builds into the facebook-only target -- this
 * command is in the OSS build, so its doc hooks need coverage there too.
 */
class CmdShowBgpNexthopInfoWikiDocsTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowBgpNexthopInfoWikiDocsTestFixture, wikiDocHooks) {
  const auto description = CmdShowBgpNexthopInfoTraits::description();
  EXPECT_FALSE(description.empty());
  // The state the prose exists to point operators at.
  EXPECT_THAT(std::string(description), HasSubstr("resolved for selection"));
  EXPECT_EQ(CmdShowBgpNexthopInfo::sampleModel().entries()->size(), 3);
}

TEST_F(CmdShowBgpNexthopInfoWikiDocsTestFixture, printOutputRendersSampleList) {
  std::stringstream ss;
  CmdShowBgpNexthopInfo().printOutput(CmdShowBgpNexthopInfo::sampleModel(), ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Resolved For Selection"));
  // Pin each row whole rather than probing for bare "Yes"/"No", which also
  // occur in the empty-cache message and are not tied to a row. Column widths
  // come from utils::Table, so collapse runs of spaces.
  EXPECT_THAT(output, ContainsRegex("192\\.0\\.2\\.11 +Yes +10 +Yes +171"));
  EXPECT_THAT(output, ContainsRegex("192\\.0\\.2\\.12 +Yes +N/A +No +171"));
  // The unreachable entry has no IGP cost, so its cost column renders N/A.
  EXPECT_THAT(output, ContainsRegex("192\\.0\\.2\\.13 +No +N/A +No +0"));
}

// The same entries rendered in detail mode must produce the per-nexthop view,
// which is the other half of what the description promises.
/*
 * Detail mode. queryClient only ever sets detailed() while pushing one queried
 * address per entry, so mirror that invariant rather than testing a shape the
 * daemon path never produces.
 *
 * Each entry renders its own table, so assert per entry: a bare
 * ContainsRegex over the concatenated render would pass even if the values
 * were swapped between next hops.
 */
TEST_F(CmdShowBgpNexthopInfoWikiDocsTestFixture, printOutputRendersDetailView) {
  const auto listModel = CmdShowBgpNexthopInfo::sampleModel();

  auto renderDetail = [](const TNexthopInfo& entry,
                         const std::string& queried) {
    CmdShowBgpNexthopInfo::RetType model;
    model.detailed() = true;
    model.entries() = {entry};
    model.queried_nexthops() = {queried};
    std::stringstream ss;
    CmdShowBgpNexthopInfo().printOutput(model, ss);
    return ss.str();
  };

  // Healthy multi-hop next hop.
  const auto healthy = renderDetail(listModel.entries()->at(0), "192.0.2.11");
  EXPECT_THAT(healthy, ContainsRegex("Reachable +Yes"));
  EXPECT_THAT(healthy, ContainsRegex("IGP Cost +10"));
  EXPECT_THAT(healthy, ContainsRegex("Directly Connected +No"));
  EXPECT_THAT(healthy, ContainsRegex("Resolved For Selection +Yes"));
  EXPECT_THAT(healthy, ContainsRegex("Dependent Routes +171"));

  // Reachable but not resolved for selection - the state description()
  // highlights, and the reason the detail view is worth opening.
  const auto unresolved =
      renderDetail(listModel.entries()->at(1), "192.0.2.12");
  EXPECT_THAT(unresolved, ContainsRegex("Reachable +Yes"));
  EXPECT_THAT(unresolved, ContainsRegex("IGP Cost +N/A"));
  EXPECT_THAT(unresolved, ContainsRegex("Resolved For Selection +No"));
  EXPECT_THAT(unresolved, ContainsRegex("Dependent Routes +171"));

  // Unreachable: no IGP cost, so the column renders N/A.
  const auto unreachable =
      renderDetail(listModel.entries()->at(2), "192.0.2.13");
  EXPECT_THAT(unreachable, ContainsRegex("Reachable +No"));
  EXPECT_THAT(unreachable, ContainsRegex("IGP Cost +N/A"));
  EXPECT_THAT(unreachable, ContainsRegex("Directly Connected +Yes"));
  EXPECT_THAT(unreachable, ContainsRegex("Resolved For Selection +No"));
}

// A queried address the cache does not hold is reported by name rather than
// silently skipped - the branch the index-parallel queried_nexthops exists for.
TEST_F(CmdShowBgpNexthopInfoWikiDocsTestFixture, printOutputNamesCacheMiss) {
  CmdShowBgpNexthopInfo::RetType model;
  model.detailed() = true;
  model.entries() = {TNexthopInfo{}};
  model.queried_nexthops() = {"192.0.2.99"};

  std::stringstream ss;
  CmdShowBgpNexthopInfo().printOutput(model, ss);
  EXPECT_EQ(ss.str(), "Nexthop 192.0.2.99 not found in the nexthop cache\n");
}

} // namespace facebook::fboss
