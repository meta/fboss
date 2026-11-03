/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/commands/config/gen/agent/AgentConfigComparisonUtils.h"
#include "fboss/cli/fboss2/commands/config/gen/agent/AgentConfigGenUtils.h"

namespace facebook::fboss::configgen {
namespace {

namespace fs = std::filesystem;

struct AgentConfigParityCandidate {
  std::string_view referencePath;
  std::set<std::string> ignoredPaths;
  std::optional<std::string_view> asicConfigVariant;
};

const std::map<
    std::pair<std::string_view, std::string_view>,
    AgentConfigParityCandidate>
    kParityCandidates{
        {{"wedge800bact", "hw_test"},
         {
             .referencePath =
                 "oss/hw_test_configs/wedge800bact.agent.materialized_JSON",
         }},
        {{"wedge800bact", "benchmark"},
         {
             .referencePath =
                 "oss/hw_bench_configs/wedge800bact.agent.materialized_JSON",
             .ignoredPaths =
                 {
                     "defaultCommandLineArgs.enable_acl_table_group",
                     "defaultCommandLineArgs.use_raw_platform_mapping",
                     "platform.portIdToPortAssignment",
                     "sw.aclTableGroups",
                     "sw.switchSettings.needL2EntryForNeighbor",
                 },
             .asicConfigVariant = "default",
         }},
    };

std::string readFile(const fs::path& path) {
  std::string contents;
  if (!folly::readFile(path.c_str(), contents)) {
    throw std::runtime_error("Unable to read Agent config " + path.string());
  }
  return contents;
}

TEST(AgentConfigParityTest, AllCandidatesMatchMaterializedConfigs) {
  const fs::path fbossRoot{"fboss"};
  ASSERT_TRUE(fs::is_directory(fbossRoot / "configs"));
  for (const auto& [platformProfile, candidate] : kParityCandidates) {
    const auto& [platform, profile] = platformProfile;
    SCOPED_TRACE(std::string(platform) + "/" + std::string(profile));
    folly::test::TemporaryDirectory outputDirectory;
    const auto generatedPath = generateAgentConfig(
        platform,
        profile,
        fbossRoot,
        fs::path(outputDirectory.path().string()),
        std::nullopt,
        std::nullopt,
        candidate.asicConfigVariant);
    AgentConfigComparisonOptions options;
    options.ignoredPaths = candidate.ignoredPaths;

    const auto result = compareAgentConfigContents(
        readFile(generatedPath),
        readFile(fbossRoot / candidate.referencePath),
        options);

    EXPECT_EQ(result.status, AgentConfigComparisonStatus::SEMANTIC_MATCH)
        << "Generated config:\n"
        << result.normalizedGenerated << "\nReference config:\n"
        << result.normalizedReference;
  }
}

} // namespace
} // namespace facebook::fboss::configgen
