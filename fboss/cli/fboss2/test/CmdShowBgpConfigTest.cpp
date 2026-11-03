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

#include "fboss/cli/fboss2/commands/show/bgp/config/CmdShowConfigRunningBgp.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

/*
 * Coverage for the CLI reference-wiki hooks of `show bgp config` (the hooks
 * themselves live in CmdShowConfigTraits.h and CmdShowConfigRunningBgp.h).
 * It lives here rather than in CmdShowConfigRunningBgpTest.cpp because that
 * file is registered only in the OSS CMake build, so tests added to it never
 * run under Buck.
 */
class CmdShowBgpConfigTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowBgpConfigTestFixture, wikiDocHooks) {
  /*
   * printOutputRendersSample below already proves sampleModel() renders, so
   * what is worth pinning here is the prose: it has to actually describe this
   * command, and it must not go stale on the two things a reader depends on -
   * that the document is the daemon's running config, and that the command is
   * spelled differently in the internal and OSS builds.
   */
  const std::string description(CmdShowConfigRunningBgpTraits::description());
  EXPECT_THAT(description, HasSubstr("actually running"));
  // The OSS build returns a different document shape; the prose must say so.
  EXPECT_THAT(description, HasSubstr("raw running-config JSON"));
  EXPECT_THAT(description, HasSubstr("show bgp config"));
  EXPECT_THAT(description, HasSubstr("show config running bgp"));

  // The sample must carry every section the prose walks through, or the wiki
  // page describes fields its own example does not show.
  const auto model = CmdShowConfigRunningBgp::sampleModel();
  ASSERT_TRUE(model.isObject());
  for (const auto* section :
       {"bgp_setting_config",
        "communities",
        "localprefs",
        "peer_groups",
        "policies"}) {
    EXPECT_TRUE(model.count(section)) << "sample is missing " << section;
  }
}

TEST_F(CmdShowBgpConfigTestFixture, printOutputRendersSample) {
  auto model = CmdShowConfigRunningBgp::sampleModel();
  std::stringstream ss;
  CmdShowConfigRunningBgp().printOutput(model, ss);
  const std::string output = ss.str();

  // Pretty-printed rather than the compact form the daemon returns.
  EXPECT_THAT(output, HasSubstr("\n  \"communities\""));
  // The community mnemonics that let other commands print names instead of
  // raw asn:value pairs.
  EXPECT_THAT(output, HasSubstr("AS65108.DEFAULT"));
  EXPECT_THAT(output, HasSubstr("65108:337"));
  // description() calls out the local-pref mnemonics, peer groups and policy
  // statements, so the example has to actually contain them.
  EXPECT_THAT(output, HasSubstr("SAMPLE_LOCALPREF_100"));
  EXPECT_THAT(output, HasSubstr("peer_groups"));
  EXPECT_THAT(output, HasSubstr("SAMPLE_UPLINK_IN"));
}

} // namespace facebook::fboss
