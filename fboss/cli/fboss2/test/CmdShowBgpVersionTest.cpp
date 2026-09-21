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

#include "fboss/cli/fboss2/commands/show/bgp/CmdShowVersionBgp.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "thrift/lib/cpp/TApplicationException.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdShowBgpVersionTestFixture : public CmdHandlerTestBase {};

TEST_F(CmdShowBgpVersionTestFixture, QueryClientReturnsRunningBuildInfo) {
  const CmdShowVersionBgp::RetType expected = {
      {"build_package_name", "bgp"},
      {"build_package_version", "0530bb98"},
      {"build_revision", "0530bb98"},
  };
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getBuildInfo(_)).WillOnce(Invoke([&](auto& info) {
    info = expected;
  }));

  EXPECT_EQ(CmdShowVersionBgp().queryClient(localhost()), expected);
}

TEST_F(CmdShowBgpVersionTestFixture, QueryClientRejectsOldDaemon) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getBuildInfo(_))
      .WillOnce(Throw(
          apache::thrift::TApplicationException(
              apache::thrift::TApplicationException::UNKNOWN_METHOD,
              "Unknown function getBuildInfo")));

  EXPECT_THROW(
      CmdShowVersionBgp().queryClient(localhost()), std::runtime_error);
}

} // namespace facebook::fboss
