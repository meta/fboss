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
#include <thrift/lib/cpp2/reflection/testing.h> // NOLINT(misc-include-cleaner)
#include <string_view>

#include "fboss/cli/fboss2/commands/show/bgp/stream/CmdShowBgpStreamSummary.h"
#include "fboss/cli/fboss2/commands/show/bgp/summary/gen-cpp2/bgp_summary_types.h"
#include "fboss/cli/fboss2/test/CmdBgpTestUtils.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#ifndef IS_OSS
// Avoid EXPECT_THRIFT_EQ clash with <thrift/lib/cpp2/reflection/testing.h>
#undef EXPECT_THRIFT_EQ
#include "nettools/common/TestUtils.h"
#endif

using namespace ::testing;
using facebook::neteng::fboss::bgp::thrift::TBgpStreamSession;
namespace facebook::fboss {
class CmdShowBgpStreamSummaryTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<TBgpStreamSession> sessions_;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    sessions_ = {
        buildStreamSession(
            /*name=*/"anotherStreamName.02.efgh3.facebook.com",
            /*uptime=*/1000,
            /*peerId=*/2,
            /*peerAddr=*/"5.6.7.8",
            /*prefixCount=*/13),
        buildStreamSession()};
  }
};

TEST_F(CmdShowBgpStreamSummaryTestFixture, queryClient) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getBgpStreamSessions(_))
      .WillOnce(Invoke([&](auto& sessions) { sessions = sessions_; }));

  auto results = CmdShowBgpStreamSummary().queryClient(localhost());
  EXPECT_THRIFT_EQ_VECTOR(results, sessions_);
}

TEST_F(CmdShowBgpStreamSummaryTestFixture, printOutput) {
  std::stringstream ss;
  CmdShowBgpStreamSummary().printOutput(sessions_, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP stream summary information for subscribers\n\n"
      " Peer ID  Name                                     NumRoutes  Uptime   \n"
      "----------------------------------------------------------------------------\n"
      " 1        myStreamName.01.abcd2.facebook.com       14         0h 0m 1s \n"
      " 2        anotherStreamName.02.efgh3.facebook.com  13         0h 0m 1s \n\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpStreamSummaryTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpStreamSummaryTraits::description().empty());
  auto model = CmdShowBgpStreamSummary::sampleModel();
  EXPECT_FALSE(model.empty());
  std::stringstream ss;
  CmdShowBgpStreamSummary().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(
      output, HasSubstr("BGP stream summary information for subscribers"));
  EXPECT_THAT(output, HasSubstr("tsp_cco/netsystems/BgpMonitor_abc1/1"));
  EXPECT_THAT(output, HasSubstr("10214"));
}

} // namespace facebook::fboss
