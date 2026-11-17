// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/json/json.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.h"

using namespace facebook::fboss;

class ShowConfigRunningAgentTest : public Fboss2IntegrationTest {};

TEST_F(ShowConfigRunningAgentTest, JsonMatchesDaemonRunningConfig) {
  auto result = runCli({"show", "config", "running", "agent"});
  ASSERT_EQ(result.exitCode, 0) << result.stderr;
  ASSERT_TRUE(result.stderr.empty()) << result.stderr;
  auto hosts = folly::parseJson(result.stdout);
  ASSERT_TRUE(hosts.isObject());
  ASSERT_EQ(hosts.size(), 1);
  const auto& config = hosts.items().begin()->second;
  ASSERT_TRUE(config.isObject());
  EXPECT_EQ(config, getRunningConfig())
      << "'show config running agent' must match agent getRunningConfig";
}
