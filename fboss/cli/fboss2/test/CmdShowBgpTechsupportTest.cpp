/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

// show bgp techsupport lives under commands/show/facebook and is not part of
// the OSS build.
#ifndef IS_OSS

#include <folly/json/json.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sstream>

#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/commands/show/facebook/bgp/techsupport/CmdShowBgpTechsupport.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdShowBgpTechsupportTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowBgpTechsupportTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpTechsupportTraits::description().empty());

  auto model = CmdShowBgpTechsupport::sampleModel();
  EXPECT_FALSE(model.bgpVersionMap()->empty());
  EXPECT_FALSE(model.bgpRunningConfig()->empty());
  EXPECT_FALSE(model.bgpTable()->tRibEntries()->empty());
  EXPECT_FALSE(
      model.bgpRibPolicy()->path_selection_policy()->statements()->empty());
  EXPECT_FALSE(model.bgpSummary()->sessions()->empty());
}

/*
 * The bundle must show what the individual commands show.
 *
 * Byte-comparing the bundled render against a separately rendered command does
 * not work here: the RIB listing's LM field and the summary's Uptime column are
 * both derived from the wall clock, so two renders taken a moment apart can
 * differ - and once a value changes width, utils::Table re-pads the whole
 * column and its dashed separator too. Masking those fields was tried and made
 * the comparison weak enough to be worthless (it also had to mask any
 * digits-plus-unit token anywhere).
 *
 * So assert on content instead: each section must carry the distinctive,
 * time-independent data its own command renders for the same sample. That
 * still fails if a section goes missing, renders the wrong model, or gets
 * transformed on the way through createModel.
 */
TEST_F(CmdShowBgpTechsupportTestFixture, sectionsCarryTheirCommandsContent) {
  /*
   * The table section renders through printRIBEntries, which looks up
   * mnemonics through the model's own host/ip. Point the rendered copies at
   * the mocked server so this does not attempt a real connect to the canned
   * documentation address.
   */
  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  auto model = CmdShowBgpTechsupport::sampleModel();
  model.bgpTable()->host() = localhost().getName();
  model.bgpTable()->oobName() = localhost().getOobName();
  model.bgpTable()->ip() = localhost().getIpStr();
  std::stringstream ss;
  CmdShowBgpTechsupport().printOutput(model, ss);
  const std::string output = ss.str();

  // Config section: the community mnemonics the sample config defines.
  EXPECT_EQ(
      *model.bgpRunningConfig(),
      folly::toPrettyJson(CmdShowConfigRunningBgp::sampleModel()));
  EXPECT_THAT(output, HasSubstr("AS65108.DEFAULT"));
  EXPECT_THAT(output, HasSubstr("SAMPLE_LOCALPREF_100"));

  // Table section: the prefix headers and the best-path marker.
  EXPECT_THAT(output, HasSubstr("> 0.0.0.0/0, Selected 2/3 paths"));
  EXPECT_THAT(output, HasSubstr("> 2001:db8:1c00::/40"));
  EXPECT_THAT(output, HasSubstr("*@  from 192.0.2.11"));

  // Rib-policy section: both policy bodies, with their bundle versions.
  EXPECT_THAT(output, HasSubstr("path selection policy (version 1732574075)"));
  EXPECT_THAT(output, HasSubstr("route filter policy (version 1784328957)"));

  // Summary section: the global header and the peer table.
  EXPECT_THAT(output, HasSubstr("Router ID - 192.0.2.1, Local ASN - 65108"));
  EXPECT_THAT(output, HasSubstr("Peers: UP - 4, TOTAL - 4"));
  EXPECT_THAT(output, HasSubstr("Peer Groups Configured - 1"));
  EXPECT_THAT(output, HasSubstr("EOR sent (UP peers) - 4/4"));
  EXPECT_THAT(output, HasSubstr("EORs received (UP peers) - 4/4"));
  EXPECT_THAT(output, HasSubstr("198.51.100.0/24"));
}

// Only the banners are checked for the version section: its body goes to
// std::cout rather than to the stream, which the description calls out.
TEST_F(CmdShowBgpTechsupportTestFixture, printOutputRendersEverySectionBanner) {
  /*
   * The table section renders through printRIBEntries, which looks up
   * mnemonics through the model's own host/ip. Point the rendered copies at
   * the mocked server so this does not attempt a real connect to the canned
   * documentation address.
   */
  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  auto model = CmdShowBgpTechsupport::sampleModel();
  model.bgpTable()->host() = localhost().getName();
  model.bgpTable()->oobName() = localhost().getOobName();
  model.bgpTable()->ip() = localhost().getIpStr();
  std::stringstream ss;
  CmdShowBgpTechsupport().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("# show bgp version"));
  EXPECT_THAT(output, HasSubstr("# show bgp config"));
  EXPECT_THAT(output, HasSubstr("# show bgp table"));
  EXPECT_THAT(output, HasSubstr("# show bgp rib-policy"));
  EXPECT_THAT(output, HasSubstr("# show bgp summary"));
}

} // namespace facebook::fboss

#endif // IS_OSS
