/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
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
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/show/bgp/neighbors_by_name/CmdShowBgpNeighborsByName.h"

using namespace ::testing;
using facebook::neteng::fboss::bgp::thrift::TBgpSession;

namespace facebook::fboss {

namespace {
TBgpSession sessionWithDescription(
    const std::string& peerAddr,
    const std::string& description) {
  TBgpSession session;
  session.peer_addr() = peerAddr;
  session.description() = description;
  return session;
}
} // namespace

// A description-less session, notably a configured listen range nobody has
// connected on, is excluded by any pattern requiring text - but PartialMatch
// still accepts it for a pattern that can match the empty string, which is the
// distinction description() now draws. Broader filter coverage (regex, no
// match, invalid regex) lives in
// test/facebook/CmdShowBgpNeighborsByNameAdvertisedRejectedTest.cpp.
TEST(CmdShowBgpNeighborsByNameTest, filterAndEmptyDescription) {
  const std::vector<TBgpSession> sessions = {
      sessionWithDescription("2001:db8:1ff:c100::/56", "")};

  EXPECT_TRUE(
      CmdShowBgpNeighborsByName::filterNeighborsByNameRegex(sessions, "fsw")
          .empty());
  EXPECT_EQ(
      CmdShowBgpNeighborsByName::filterNeighborsByNameRegex(sessions, ".*"),
      sessions);
}

TEST(CmdShowBgpNeighborsByNameTest, printOutputNoMatch) {
  std::stringstream ss;
  CmdShowBgpNeighborsByName().printOutput({}, ss);
  EXPECT_EQ(ss.str(), "No neighbors matched the given pattern.\n");
}

TEST(CmdShowBgpNeighborsByNameTest, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpNeighborsByNameTraits::description().empty());

  // The sample runs the command's own filter over the 'show bgp neighbors'
  // sample, so the listen range there must drop out and the one described peer
  // must survive.
  const auto model = CmdShowBgpNeighborsByName::sampleModel();
  ASSERT_EQ(model.size(), 1);
  EXPECT_EQ(*model[0].description(), "fsw001.p001.f01.abc1");

  // Render the sample the way the wiki generator does; a property-only check
  // would still pass on a sample missing a field printOutput reads via
  // .value().
  std::stringstream ss;
  CmdShowBgpNeighborsByName().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Description: fsw001.p001.f01.abc1"));
  EXPECT_THAT(output, HasSubstr("BGP state is ESTABLISHED"));
  EXPECT_THAT(output, HasSubstr("Prefix Telemetry"));
  // A single match renders without the 'neighbor N of M' separator.
  EXPECT_THAT(output, Not(HasSubstr("neighbor 1 of")));
}

} // namespace facebook::fboss
