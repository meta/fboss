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
#include <string_view>

#include "fboss/cli/fboss2/commands/show/bgp/stream/CmdShowBgpStreamSubscriber.h"
#include "fboss/cli/fboss2/commands/show/bgp/stream/subscriber/CmdShowBgpStreamSubscriberPostPolicy.h"
#include "fboss/cli/fboss2/commands/show/bgp/stream/subscriber/CmdShowBgpStreamSubscriberPrePolicy.h"
#include "fboss/cli/fboss2/test/CmdBgpTestUtils.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace ::testing;
using namespace facebook::neteng::fboss::bgp::thrift;
namespace facebook::fboss {
class CmdShowBgpStreamSubscriberTestFixture : public CmdHandlerTestBase {
 public:
  std::map<TIpPrefix, std::vector<TBgpPath>> sessions_;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
  }
};

// As the output for the subscriber command is the same as the
// neighbors policy outputs, we only need to make sure the right
// thrift call is being made in this unit test.
TEST_F(CmdShowBgpStreamSubscriberTestFixture, queryClientPrePolicy) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getSubscriberNetworkInfo(_, _, _));

  auto results =
      CmdShowBgpStreamSubscriberPrePolicy().queryClient(localhost(), {"1"}, {});
}

TEST_F(CmdShowBgpStreamSubscriberTestFixture, queryClientPostPolicy) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getSubscriberNetworkInfo(_, _, _));

  auto results = CmdShowBgpStreamSubscriberPostPolicy().queryClient(
      localhost(), {"1"}, {});
}

TEST_F(CmdShowBgpStreamSubscriberTestFixture, wikiDocHooksSubscriber) {
  EXPECT_FALSE(CmdShowBgpStreamSubscriberTraits::description().empty());
  // This level only echoes the subscriber id back; printOutput writes the
  // "missing policy argument" hint straight to std::cout and takes no stream,
  // so there is nothing renderable to assert beyond the model itself.
  EXPECT_EQ(
      CmdShowBgpStreamSubscriber::sampleModel(), std::vector<std::string>{"1"});
}

// The pre/post-policy samples render through printRoutesInformation, which
// reaches getLocalBgpConfig for the community/local-pref mnemonics and builds
// the HostInfo it connects to from the MODEL's own host/ip fields. Point the
// copy under test at the mocked server so this is not a real connect to the
// canned documentation host.
TEST_F(CmdShowBgpStreamSubscriberTestFixture, wikiDocHooksPrePolicy) {
  EXPECT_FALSE(
      CmdShowBgpStreamSubscriberPrePolicyTraits::description().empty());

  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  auto model = CmdShowBgpStreamSubscriberPrePolicy::sampleModel();
  EXPECT_EQ(model.networkPath()->size(), 2);
  model.host() = localhost().getName();
  model.oobName() = localhost().getOobName();
  model.ip() = localhost().getIpStr();

  std::stringstream ss;
  CmdShowBgpStreamSubscriberPrePolicy().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Network: 198.51.100.0/24"));
  EXPECT_THAT(output, HasSubstr("Nexthop: 192.0.2.12"));
  // pre-policy passes showPolicy=false, so no Policy line is rendered.
  EXPECT_THAT(output, Not(HasSubstr("Policy:")));
}

TEST_F(CmdShowBgpStreamSubscriberTestFixture, wikiDocHooksPostPolicy) {
  EXPECT_FALSE(
      CmdShowBgpStreamSubscriberPostPolicyTraits::description().empty());

  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  auto model = CmdShowBgpStreamSubscriberPostPolicy::sampleModel();
  EXPECT_EQ(model.networkPath()->size(), 2);
  model.host() = localhost().getName();
  model.oobName() = localhost().getOobName();
  model.ip() = localhost().getIpStr();

  std::stringstream ss;
  CmdShowBgpStreamSubscriberPostPolicy().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Network: 198.51.100.0/24"));
  // post-policy passes showPolicy=true; the term that accepted the route is
  // the only rendered difference from pre-policy.
  EXPECT_THAT(output, HasSubstr("Policy: Accepted/Modified by STREAM_EXPORT"));
}
} // namespace facebook::fboss
