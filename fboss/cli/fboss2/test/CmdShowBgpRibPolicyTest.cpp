/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

// The rib-policy commands live under commands/show/facebook and are not part
// of the OSS build.
#ifndef IS_OSS

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sstream>

#include "fboss/cli/fboss2/commands/show/facebook/bgp/ribpolicy/CmdShowBgpRibPolicy.h"
#include "fboss/cli/fboss2/commands/show/facebook/bgp/ribpolicy/CmdShowBgpRibPolicyCps.h"
#include "fboss/cli/fboss2/commands/show/facebook/bgp/ribpolicy/CmdShowBgpRibPolicyCrf.h"
#include "fboss/cli/fboss2/commands/show/facebook/bgp/ribpolicy/CmdShowBgpRibPolicyCte.h"
#include "fboss/cli/fboss2/commands/show/facebook/bgp/ribpolicy/CmdShowBgpRibPolicyGoldenPrefixes.h"
#include "fboss/cli/fboss2/commands/show/facebook/bgp/ribpolicy/changehistory/CmdShowBgpRibPolicyChangeHistory.h"
#include "fboss/cli/fboss2/commands/show/facebook/bgp/ribpolicy/changehistory/CmdShowBgpRibPolicyChangeHistoryCps.h"
#include "fboss/cli/fboss2/commands/show/facebook/bgp/ribpolicy/changehistory/CmdShowBgpRibPolicyChangeHistoryCrf.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdShowBgpRibPolicyTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowBgpRibPolicyTestFixture, cpsWikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpRibPolicyCpsTraits::description().empty());
  EXPECT_FALSE(CmdShowBgpRibPolicyCps::sampleModel().statements()->empty());

  std::stringstream ss;
  CmdShowBgpRibPolicyCps().printOutput(
      CmdShowBgpRibPolicyCps::sampleModel(), ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("path selection policy (version 1732574075)"));
  EXPECT_THAT(output, HasSubstr("route/prefix matcher"));
  // Ordered criteria are the part of the render the description explains, so
  // the example has to show more than one.
  EXPECT_THAT(output, HasSubstr("criteria 1"));
  EXPECT_THAT(output, HasSubstr("criteria 2"));
}

TEST_F(CmdShowBgpRibPolicyTestFixture, crfWikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpRibPolicyCrfTraits::description().empty());
  EXPECT_TRUE(
      CmdShowBgpRibPolicyCrf::sampleModel().golden_prefix_policy().has_value());

  std::stringstream ss;
  CmdShowBgpRibPolicyCrf().printOutput(
      CmdShowBgpRibPolicyCrf::sampleModel(), ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("route filter policy (version 1784328957)"));
  EXPECT_THAT(output, HasSubstr("golden prefix"));
  EXPECT_THAT(output, HasSubstr("0.0.0.0/0"));
  EXPECT_THAT(output, HasSubstr("limit: 1"));
}

// An empty route-attribute policy is the documented steady state, so the
// sample is deliberately empty and must still render its header.
TEST_F(CmdShowBgpRibPolicyTestFixture, cteWikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpRibPolicyCteTraits::description().empty());
  EXPECT_TRUE(CmdShowBgpRibPolicyCte::sampleModel().statements()->empty());

  std::stringstream ss;
  CmdShowBgpRibPolicyCte().printOutput(
      CmdShowBgpRibPolicyCte::sampleModel(), ss);
  EXPECT_THAT(ss.str(), HasSubstr("route attribute policy"));
}

TEST_F(CmdShowBgpRibPolicyTestFixture, goldenPrefixesWikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpRibPolicyGoldenPrefixesTraits::description().empty());
  const auto model = CmdShowBgpRibPolicyGoldenPrefixes::sampleModel();
  EXPECT_TRUE(model.policy()->golden_prefix_policy().has_value());

  std::stringstream ss;
  CmdShowBgpRibPolicyGoldenPrefixes().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("golden prefix (version 1784328957)"));
  // The state line is the only thing this command adds over 'rib-policy crf',
  // so the example has to include it.
  EXPECT_THAT(output, HasSubstr("Golden prefix policy state:"));
}

// The golden prefixes shown here and by 'show bgp rib-policy crf' come from
// one builder, so the two wiki entries cannot describe different policies.
TEST_F(CmdShowBgpRibPolicyTestFixture, goldenPrefixesMatchCrfSample) {
  EXPECT_EQ(
      *CmdShowBgpRibPolicyGoldenPrefixes::sampleModel().policy(),
      CmdShowBgpRibPolicyCrf::sampleModel());
}

TEST_F(CmdShowBgpRibPolicyTestFixture, umbrellaWikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpRibPolicyTraits::description().empty());
  const auto model = CmdShowBgpRibPolicy::sampleModel();

  std::stringstream ss;
  CmdShowBgpRibPolicy().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("path selection policy"));
  EXPECT_THAT(output, HasSubstr("route filter policy"));
  // The route attribute policy is unset, so its section is omitted entirely -
  // the "an absent section means nothing is loaded" case the description
  // explains.
  EXPECT_FALSE(model.route_attribute_policy().has_value());
  EXPECT_THAT(output, Not(HasSubstr("route attribute policy")));
}

// The umbrella command prints what the subcommands print, so it must be built
// from the same samples.
TEST_F(CmdShowBgpRibPolicyTestFixture, umbrellaSectionsMatchSubcommands) {
  const auto model = CmdShowBgpRibPolicy::sampleModel();
  EXPECT_EQ(
      *model.path_selection_policy(), CmdShowBgpRibPolicyCps::sampleModel());
  EXPECT_EQ(
      *model.route_filter_policy(), CmdShowBgpRibPolicyCrf::sampleModel());
}

TEST_F(CmdShowBgpRibPolicyTestFixture, changeHistoryWikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpRibPolicyChangeHistoryTraits::description().empty());
  EXPECT_FALSE(CmdShowBgpRibPolicyChangeHistory::sampleModel().empty());

  std::stringstream ss;
  CmdShowBgpRibPolicyChangeHistory().printOutput(
      CmdShowBgpRibPolicyChangeHistory::sampleModel(), ss);
  const std::string output = ss.str();

  // Unfiltered: both bundles appear.
  EXPECT_THAT(output, HasSubstr("CPS 1732574075"));
  EXPECT_THAT(output, HasSubstr("CRF 1784328957"));
}

// The cps and crf variants read the same file and differ only in the filter
// they apply, which is what their descriptions tell readers.
TEST_F(CmdShowBgpRibPolicyTestFixture, changeHistoryVariantsFilterOneBundle) {
  EXPECT_FALSE(
      CmdShowBgpRibPolicyChangeHistoryCpsTraits::description().empty());
  EXPECT_FALSE(
      CmdShowBgpRibPolicyChangeHistoryCrfTraits::description().empty());
  EXPECT_EQ(
      CmdShowBgpRibPolicyChangeHistoryCps::sampleModel(),
      CmdShowBgpRibPolicyChangeHistory::sampleModel());

  std::stringstream cps;
  CmdShowBgpRibPolicyChangeHistoryCps().printOutput(
      CmdShowBgpRibPolicyChangeHistoryCps::sampleModel(), cps);
  EXPECT_THAT(cps.str(), HasSubstr("CPS"));
  EXPECT_THAT(cps.str(), Not(HasSubstr("CRF")));

  std::stringstream crf;
  CmdShowBgpRibPolicyChangeHistoryCrf().printOutput(
      CmdShowBgpRibPolicyChangeHistoryCrf::sampleModel(), crf);
  EXPECT_THAT(crf.str(), HasSubstr("CRF"));
  EXPECT_THAT(crf.str(), Not(HasSubstr("CPS")));
}

} // namespace facebook::fboss

#endif // IS_OSS
