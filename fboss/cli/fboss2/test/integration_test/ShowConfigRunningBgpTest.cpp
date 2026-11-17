// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * End-to-end test for 'show config running bgp'.
 *
 * 'show config running <daemon>' is the canonical spelling for running-config
 * views, with 'show bgp config running' kept as an alias. Both must reach the
 * same handler and return the config bgpd itself reports over its
 * getRunningConfig thrift RPC.
 *
 * Requirements:
 * - bgpd must be running and answering its thrift RPC; otherwise the tests
 *   are skipped (ConfigBgpTestBase::SetUp).
 */

#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/test/integration_test/ConfigBgpTestBase.h"

using namespace facebook::fboss;

class ShowConfigRunningBgpTest : public ConfigBgpTestBase {
 protected:
  // --fmt json output is keyed by host; these tests only query localhost.
  folly::dynamic showRunningBgp(const std::vector<std::string>& args) const {
    auto result = runCli(args);
    EXPECT_EQ(result.exitCode, 0) << result.stderr;
    EXPECT_TRUE(result.stderr.empty()) << result.stderr;
    if (result.exitCode != 0 || !result.stderr.empty()) {
      return nullptr;
    }
    auto json = folly::parseJson(result.stdout);
    EXPECT_EQ(json.size(), 1) << "expected exactly one host entry";
    if (json.size() != 1) {
      return nullptr;
    }
    return json.items().begin()->second;
  }
};

TEST_F(ShowConfigRunningBgpTest, MatchesDaemonRunningConfig) {
  auto cliConfig = showRunningBgp({"show", "config", "running", "bgp"});
  ASSERT_TRUE(cliConfig.isObject());
  EXPECT_EQ(cliConfig, readRunningBgpConfigViaRpc())
      << "'show config running bgp' must match bgpd getRunningConfig";
}

TEST_F(ShowConfigRunningBgpTest, MatchesShowBgpConfigRunningAlias) {
  auto canonical = showRunningBgp({"show", "config", "running", "bgp"});
  auto alias = showRunningBgp({"show", "bgp", "config", "running"});
  ASSERT_TRUE(canonical.isObject());
  EXPECT_EQ(canonical, alias)
      << "'show config running bgp' must match 'show bgp config running'";
}
