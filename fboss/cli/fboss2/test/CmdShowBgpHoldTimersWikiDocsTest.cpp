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

#include "fboss/cli/fboss2/commands/show/bgp/holdtimers/CmdShowBgpHoldTimers.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

/*
 * Wiki-doc-hook coverage for `show bgp holdtimers`. Deliberately separate from
 * test/facebook/CmdShowBgpHoldTimersTest.cpp, which covers queryClient and the
 * render against mocked bgpd data but builds into the facebook-only target --
 * this command is in the OSS build, so its doc hooks need coverage there too.
 */
class CmdShowBgpHoldTimersWikiDocsTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowBgpHoldTimersWikiDocsTestFixture, wikiDocHooks) {
  const auto description = CmdShowBgpHoldTimersTraits::description();
  EXPECT_FALSE(description.empty());
  // The prose exists to explain what the remaining time means, so a
  // description that no longer mentions the hold timer is not doing its job.
  EXPECT_THAT(std::string(description), HasSubstr("hold timer"));
  EXPECT_EQ(CmdShowBgpHoldTimers::sampleModel().size(), 4);
}

TEST_F(CmdShowBgpHoldTimersWikiDocsTestFixture, printOutputRendersSample) {
  std::stringstream ss;
  CmdShowBgpHoldTimers().printOutput(CmdShowBgpHoldTimers::sampleModel(), ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Peer Address"));
  EXPECT_THAT(output, HasSubstr("Hold Time Remaining (ms)"));
  EXPECT_THAT(output, HasSubstr("2001:db8:e11e:1062::4e"));
  EXPECT_THAT(output, HasSubstr("24676"));
}

// An empty result is the documented "no session has a running timer" case, not
// an empty table.
TEST_F(CmdShowBgpHoldTimersWikiDocsTestFixture, printOutputNoTimers) {
  std::stringstream ss;
  CmdShowBgpHoldTimers().printOutput({}, ss);
  EXPECT_EQ(ss.str(), "No hold timer information available\n");
}

} // namespace facebook::fboss
