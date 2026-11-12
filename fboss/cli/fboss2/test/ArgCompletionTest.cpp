/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/utils/ArgCompletion.h"

namespace facebook::fboss::utils::completion {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace {
AttrGrammar arpGrammar() {
  return {.attrs = {"timeout", "max-probes", "stale-interval"}};
}
} // namespace

TEST(ArgCompletionTest, attrGrammarOffersAttrsFirst) {
  EXPECT_THAT(
      completeAttrGrammar({}, arpGrammar()),
      ElementsAre("max-probes", "stale-interval", "timeout"));
}

TEST(ArgCompletionTest, attrGrammarOffersNothingForFreeFormValue) {
  EXPECT_THAT(completeAttrGrammar({"timeout"}, arpGrammar()), IsEmpty());
}

TEST(ArgCompletionTest, attrGrammarSkipsUsedAttrs) {
  EXPECT_THAT(
      completeAttrGrammar({"timeout", "30"}, arpGrammar()),
      ElementsAre("max-probes", "stale-interval"));
  EXPECT_THAT(
      completeAttrGrammar(
          {"timeout", "30", "max-probes", "3", "stale-interval", "10"},
          arpGrammar()),
      IsEmpty());
}

TEST(ArgCompletionTest, attrGrammarMatchesCaseInsensitively) {
  EXPECT_THAT(
      completeAttrGrammar({"TIMEOUT", "30"}, arpGrammar()),
      ElementsAre("max-probes", "stale-interval"));
}

TEST(ArgCompletionTest, attrGrammarOffersNothingAfterJunk) {
  EXPECT_THAT(completeAttrGrammar({"bogus", "30"}, arpGrammar()), IsEmpty());
}

TEST(ArgCompletionTest, attrGrammarOffersEnumValues) {
  AttrGrammar g{
      .attrs = {"sample-dest", "ingress-rate"},
      .values = {{"sample-dest", {"cpu", "mirror"}}}};
  EXPECT_THAT(
      completeAttrGrammar({"sample-dest"}, g), ElementsAre("cpu", "mirror"));
  EXPECT_THAT(
      completeAttrGrammar({"sample-dest", "cpu"}, g),
      ElementsAre("ingress-rate"));
}

TEST(ArgCompletionTest, attrGrammarValuelessAttrTakesNoValue) {
  AttrGrammar g{
      .attrs = {"mtu", "shutdown", "no-shutdown"},
      .valueless = {"shutdown", "no-shutdown"}};
  EXPECT_THAT(
      completeAttrGrammar({"shutdown"}, g), ElementsAre("mtu", "no-shutdown"));
}

TEST(ArgCompletionTest, attrGrammarLeadingObjects) {
  AttrGrammar g{.attrs = {"mtu", "description"}, .minObjects = 1};
  // The port list is free-form.
  EXPECT_THAT(completeAttrGrammar({}, g), IsEmpty());
  EXPECT_THAT(
      completeAttrGrammar({"eth1/1/1"}, g), ElementsAre("description", "mtu"));
  // Several object tokens before the first attr are fine.
  EXPECT_THAT(
      completeAttrGrammar({"eth1/1/1", "eth1/2/1", "mtu", "9000"}, g),
      ElementsAre("description"));
}

TEST(ArgCompletionTest, attrGrammarNoRepeat) {
  AttrGrammar g{.attrs = {"router-id", "local-asn"}, .repeat = false};
  EXPECT_THAT(
      completeAttrGrammar({}, g), ElementsAre("local-asn", "router-id"));
  EXPECT_THAT(completeAttrGrammar({"router-id", "1.2.3.4"}, g), IsEmpty());
}

TEST(ArgCompletionTest, attrGrammarMultiWordAttrs) {
  AttrGrammar g{
      .attrs = {"remote-asn", "timers hold-time", "timers keepalive"},
      .minObjects = 1,
      .repeat = false};
  // First words only, deduplicated.
  EXPECT_THAT(
      completeAttrGrammar({"peer1"}, g), ElementsAre("remote-asn", "timers"));
  // After the first word, the second words of the matching attrs.
  EXPECT_THAT(
      completeAttrGrammar({"peer1", "timers"}, g),
      ElementsAre("hold-time", "keepalive"));
  // The full attr then takes a free-form value.
  EXPECT_THAT(
      completeAttrGrammar({"peer1", "timers", "hold-time"}, g), IsEmpty());
  EXPECT_THAT(
      completeAttrGrammar({"peer1", "timers", "hold-time", "90"}, g),
      IsEmpty());
}

TEST(ArgCompletionTest, attrList) {
  const std::vector<std::string> attrs = {"timeout", "max-probes"};
  EXPECT_THAT(
      completeAttrList({}, attrs), ElementsAre("max-probes", "timeout"));
  EXPECT_THAT(completeAttrList({"timeout"}, attrs), ElementsAre("max-probes"));
  EXPECT_THAT(completeAttrList({"timeout", "max-probes"}, attrs), IsEmpty());
  EXPECT_THAT(completeAttrList({}, attrs, /* minObjects */ 1), IsEmpty());
  EXPECT_THAT(
      completeAttrList({"tunnel1"}, attrs, /* minObjects */ 1),
      ElementsAre("max-probes", "timeout"));
}

TEST(ArgCompletionTest, positions) {
  const std::vector<std::vector<std::string>> positions = {
      {}, {"queue"}, {}, {"order"}};
  EXPECT_THAT(completePositions({}, positions), IsEmpty());
  EXPECT_THAT(completePositions({"arp"}, positions), ElementsAre("queue"));
  EXPECT_THAT(completePositions({"arp", "queue"}, positions), IsEmpty());
  EXPECT_THAT(
      completePositions({"arp", "queue", "0"}, positions),
      ElementsAre("order"));
  EXPECT_THAT(
      completePositions({"arp", "queue", "0", "order"}, positions), IsEmpty());
}

TEST(ArgCompletionTest, toWords) {
  constexpr auto kAttrs = std::to_array<std::string_view>({"a", "b"});
  EXPECT_THAT(toWords(kAttrs), ElementsAre("a", "b"));
}

} // namespace facebook::fboss::utils::completion
