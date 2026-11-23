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

#include <CLI/App.hpp>

#include "fboss/cli/fboss2/CmdCompletion.h"
#include "fboss/cli/fboss2/CmdGlobalOptions.h"
#include "fboss/cli/fboss2/CmdList.h"
#include "fboss/cli/fboss2/CmdSubcommands.h"

namespace facebook::fboss {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::IsEmpty;
using ::testing::Not;

// Completion against the real command trees compiled into fboss2-dev, i.e.
// what `fboss2-dev __completion ...` prints.
class CmdCompletionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CmdGlobalOptions::getInstance()->init(app_);
    subcommands_.init(
        app_, kCommandTree(), kAdditionalCommandTree(), kSpecialCommands());
  }

  std::vector<std::string> complete(const std::vector<std::string>& words) {
    return completeCommandLine(app_, subcommands_, words);
  }

  CLI::App app_{"fboss2"};
  CmdSubcommands subcommands_;
};

TEST_F(CmdCompletionTest, verbs) {
  EXPECT_THAT(complete({""}), AllOf(Contains("show"), Contains("config")));
  EXPECT_THAT(complete({}), IsEmpty());
}

TEST_F(CmdCompletionTest, subcommands) {
  EXPECT_THAT(
      complete({"show", ""}), AllOf(Contains("arp"), Contains("interface")));
  EXPECT_THAT(
      complete({"config", ""}), AllOf(Contains("arp"), Contains("interface")));
  EXPECT_THAT(
      complete({"config", "interface", ""}),
      AllOf(Contains("pfc-config"), Contains("sflow")));
}

TEST_F(CmdCompletionTest, unknownSubcommandOffersNothing) {
  EXPECT_THAT(complete({"bogus", ""}), IsEmpty());
}

TEST_F(CmdCompletionTest, leafAttributesFromTraits) {
  // `config arp` keeps its attributes in a list, not as CLI11 subcommands.
  EXPECT_THAT(
      complete({"config", "arp", ""}),
      AllOf(Contains("timeout"), Contains("max-probes")));
  // A value is free-form.
  EXPECT_THAT(complete({"config", "arp", "timeout", ""}), IsEmpty());
  // Used attributes are not offered again.
  EXPECT_THAT(
      complete({"config", "arp", "timeout", "30", ""}),
      AllOf(Contains("max-probes"), Not(Contains("timeout"))));
  EXPECT_THAT(
      complete({"delete", "arp", ""}),
      AllOf(Contains("timeout"), Contains("max-probes")));
}

TEST_F(CmdCompletionTest, positionalsAndSubcommandsMix) {
  // After the port list, both the interface attributes and the sub-commands
  // that take the port list from their parent are valid.
  EXPECT_THAT(
      complete({"config", "interface", "eth1/1/1", ""}),
      AllOf(Contains("mtu"), Contains("shutdown"), Contains("pfc-config")));
  // While a value is being completed, sub-commands are not offered.
  EXPECT_THAT(
      complete({"config", "interface", "eth1/1/1", "loopback-mode", ""}),
      AllOf(Contains("PHY"), Not(Contains("pfc-config"))));
  EXPECT_THAT(
      complete({"config", "interface", "eth1/1/1", "mtu", "9000", ""}),
      AllOf(Contains("description"), Contains("pfc-config")));
  // Descending into a sub-command starts a fresh positional list.
  EXPECT_THAT(
      complete({"config", "interface", "eth1/1/1", "sflow", ""}),
      AllOf(Contains("sample-dest"), Not(Contains("mtu"))));
  EXPECT_THAT(
      complete({"config", "interface", "eth1/1/1", "sflow", "sample-dest", ""}),
      AllOf(Contains("cpu"), Contains("mirror")));
}

TEST_F(CmdCompletionTest, fixedPositions) {
  EXPECT_THAT(
      complete({"config", "copp", "reason", ""}),
      AllOf(Contains("arp"), Contains("ttl-1")));
  EXPECT_THAT(
      complete({"config", "copp", "reason", "arp", ""}), Contains("queue"));
  EXPECT_THAT(
      complete(
          {"config", "copp", "traffic-policy", "match", "r1", "action", ""}),
      Contains("send-to-queue"));
}

TEST_F(CmdCompletionTest, options) {
  EXPECT_THAT(complete({"-"}), AllOf(Contains("--host"), Contains("-H")));
  // Global options are accepted after any sub-command.
  EXPECT_THAT(complete({"show", "arp", "--"}), Contains("--host"));
  // An option's value is skipped, an inline value needs no skipping.
  EXPECT_THAT(
      complete({"--host", "switch1", "config", "arp", ""}),
      Contains("timeout"));
  EXPECT_THAT(
      complete({"--host=switch1", "config", "arp", ""}), Contains("timeout"));
  // An option's value under the cursor is free-form.
  EXPECT_THAT(complete({"--host", ""}), IsEmpty());
  EXPECT_THAT(
      complete({"config", "interface", "eth1/1/1", "--host", "sw"}), IsEmpty());
}

} // namespace facebook::fboss
