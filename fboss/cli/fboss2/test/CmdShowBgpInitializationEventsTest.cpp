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

#include "fboss/cli/fboss2/commands/show/bgp/CmdShowBgpInitializationEvents.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdShowBgpInitializationEventsTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowBgpInitializationEventsTestFixture, wikiDocHooks) {
  const auto description = CmdShowBgpInitializationEventsTraits::description();
  EXPECT_FALSE(description.empty());
  // The three statuses are what the prose has to explain.
  for (const auto& status : {"Complete", "Skipped", "Pending"}) {
    EXPECT_THAT(std::string(description), HasSubstr(status));
  }
  // INITIALIZED is what printOutput reads to decide "Converged: Yes".
  const auto model = CmdShowBgpInitializationEvents::sampleModel();
  EXPECT_TRUE(model.contains(
      neteng::fboss::bgp::thrift::BgpInitializationEvent::INITIALIZED));
}

TEST_F(CmdShowBgpInitializationEventsTestFixture, printOutputRendersSample) {
  std::stringstream ss;
  CmdShowBgpInitializationEvents().printOutput(
      CmdShowBgpInitializationEvents::sampleModel(), ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("BGP Initialization Converged: Yes"));
  EXPECT_THAT(output, HasSubstr("Complete"));
  // Milestones the sample deliberately leaves out render as skipped with no
  // timestamp, which is the case the description calls out.
  EXPECT_THAT(output, HasSubstr("Skipped"));
  EXPECT_THAT(output, Not(HasSubstr("Pending")));
}

} // namespace facebook::fboss
