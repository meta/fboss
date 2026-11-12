/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <sstream>
#include <string>

#include <folly/json/json.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/commands/show/bgp/policy/CmdShowBgpPolicy.h"

using namespace ::testing;

namespace facebook::fboss {

TEST(CmdShowBgpPolicyTest, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpPolicyTraits::description().empty());

  /*
   * Pin the PORTABLE field names and enum values emitted for a complete
   * Thrift policy, not the arbitrary names in this synthetic example.
   */
  const auto model = CmdShowBgpPolicy::sampleModel();
  ASSERT_TRUE(model.isObject());
  EXPECT_EQ("BGP policy", model["source"].asString());
  const auto& config = model["policy"];
  for (const auto* field : {"communities", "localprefs", "policies"}) {
    ASSERT_TRUE(config.count(field)) << "missing policy field " << field;
  }

  const auto& statements = config["policies"]["bgp_policy_statements"];
  ASSERT_EQ(statements.size(), 1);
  const auto& statement = statements[0];
  for (const auto* field :
       {"name", "policy_version", "policy_entries", "result"}) {
    ASSERT_TRUE(statement.count(field)) << "missing statement field " << field;
  }
  EXPECT_EQ(statement["result"].asString(), "DENY");

  ASSERT_EQ(statement["policy_entries"].size(), 1);
  const auto& term = statement["policy_entries"][0];
  for (const auto* field :
       {"name",
        "policy_matches",
        "policy_action_entries",
        "term_miss_action"}) {
    ASSERT_TRUE(term.count(field)) << "missing term field " << field;
  }
  EXPECT_EQ(term["term_miss_action"].asString(), "NEXT_TERM");

  // A match and an action inside the term - the two halves the prose says to
  // read a term by.
  ASSERT_EQ(term["policy_matches"].size(), 1);
  const auto& matchEntries = term["policy_matches"][0]["match_entries"];
  ASSERT_EQ(matchEntries.size(), 1);
  EXPECT_EQ(matchEntries[0]["type"].asString(), "COMMUNITY_LIST");
  ASSERT_EQ(term["policy_action_entries"].size(), 2);
  EXPECT_EQ(
      term["policy_action_entries"][0]["type"].asString(), "SET_LOCAL_PREF");

  std::ostringstream output;
  CmdShowBgpPolicy().printOutput(model, output);
  EXPECT_THAT(output.str(), StartsWith("Policy source: BGP policy\n"));
  const auto newline = output.str().find('\n');
  ASSERT_NE(std::string::npos, newline);
  EXPECT_EQ(config, folly::parseJson(output.str().substr(newline + 1)));
}

} // namespace facebook::fboss
