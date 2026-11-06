// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fboss/cli/fboss2/commands/show/facebook/techsupport/CmdShowTechSupport.h"
#include "fboss/cli/fboss2/commands/show/facebook/techsupport/gen-cpp2/model_types.h"

using namespace ::testing;

namespace facebook::fboss {

FabricReachabilityStats createReachabilityStats() {
  FabricReachabilityStats stats;
  stats.mismatchCount() = 0;
  stats.missingCount() = 0;
  return stats;
}

CmdShowTechSupport::RetType createTechSupportModel(
    const std::optional<std::string>& agentBootType,
    const std::map<int16_t, std::string>& hwAgentBootType,
    const std::map<int16_t, std::string>& hwAgentRevision = {},
    bool netosNative = true) {
  CmdShowTechSupport cmd;
  ServiceInfo agentInfo;
  agentInfo.version = "agent-revision";

  std::map<int16_t, bool> hwAgentStatus;
  std::map<int16_t, ServiceInfo> hwAgentInfo;
  for (const auto& hwAgentBootTypeEntry : hwAgentBootType) {
    hwAgentStatus[hwAgentBootTypeEntry.first] = true;
    hwAgentInfo[hwAgentBootTypeEntry.first].bundle = "hw-agent-version";
    hwAgentInfo[hwAgentBootTypeEntry.first].version = agentInfo.version;
  }
  for (const auto& [switchIndex, revision] : hwAgentRevision) {
    hwAgentInfo[switchIndex].version = revision;
  }

  return cmd.createModel(
      {},
      agentInfo,
      {},
      {},
      {},
      {},
      {},
      std::nullopt,
      std::nullopt,
      {},
      {},
      createReachabilityStats(),
      {},
      {},
      {},
      {},
      {},
      {},
      hwAgentStatus,
      hwAgentInfo,
      agentBootType,
      hwAgentBootType,
      netosNative);
}

std::string captureTechSupportOutput(CmdShowTechSupport::RetType model) {
  CmdShowTechSupport cmd;
  testing::internal::CaptureStdout();
  cmd.printOutput(model);
  return testing::internal::GetCapturedStdout();
}

std::vector<std::string> getLinesContaining(
    const std::string& output,
    const std::string& needle) {
  std::vector<std::string> lines;
  std::stringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.find(needle) != std::string::npos) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::string getLineContaining(
    const std::string& output,
    const std::string& needle) {
  const auto lines = getLinesContaining(output, needle);
  return lines.empty() ? "" : lines.front();
}

TEST(CmdShowTechSupportTest, createModelSetsAgentBootType) {
  auto model = createTechSupportModel("WARM_BOOT", {});

  EXPECT_EQ(model.agentBootType().value(), "WARM_BOOT");
  EXPECT_TRUE(model.hwAgentBootType()->empty());
}

TEST(CmdShowTechSupportTest, printOutputAlignsBootTypesInServiceTable) {
  auto model = createTechSupportModel(
      "WARM_BOOT",
      {
          {0, "COLD_BOOT"},
          {1, "WARM_BOOT"},
      });

  const auto output = captureTechSupportOutput(std::move(model));
  const auto header = getLineContaining(output, "Boot Type");
  const auto agent = getLineContaining(output, "Agent");
  const auto hwAgent0 = getLineContaining(output, "Hw-Agent 0");
  const auto hwAgent1 = getLineContaining(output, "Hw-Agent 1");

  ASSERT_NE(header, "");
  ASSERT_NE(agent, "");
  ASSERT_NE(hwAgent0, "");
  ASSERT_NE(hwAgent1, "");

  const auto bootTypeColumn = header.find("Boot Type");
  EXPECT_EQ(agent.find("WARM_BOOT"), bootTypeColumn);
  EXPECT_EQ(hwAgent0.find("COLD_BOOT"), bootTypeColumn);
  EXPECT_EQ(hwAgent1.find("WARM_BOOT"), bootTypeColumn);
}

TEST(CmdShowTechSupportTest, createModelSetsHwAgentVersionAndRevision) {
  auto model = createTechSupportModel(
      "WARM_BOOT", {{0, "WARM_BOOT"}, {1, "WARM_BOOT"}}, {{1, "old-revision"}});

  const std::map<int16_t, std::string> expectedVersion{
      {0, "hw-agent-version"}, {1, "hw-agent-version"}};
  const std::map<int16_t, std::string> expectedRevision{
      {0, "agent-revision"}, {1, "old-revision"}};
  EXPECT_EQ(*model.hwAgentVersion(), expectedVersion);
  EXPECT_EQ(*model.hwAgentRevision(), expectedRevision);
}

TEST(CmdShowTechSupportTest, printOutputWarnsOnlyForMismatchedHwAgent) {
  auto model = createTechSupportModel(
      "WARM_BOOT", {{0, "WARM_BOOT"}, {1, "WARM_BOOT"}}, {{1, "old-revision"}});

  const std::vector<std::string> expected{
      "Warning: Version mismatch between sw-agent and hw-agent 1 (build revision agent-revision vs old-revision)."};
  EXPECT_EQ(
      getLinesContaining(captureTechSupportOutput(std::move(model)), "Warning"),
      expected);
}

TEST(CmdShowTechSupportTest, printOutputSkipsWarningWhenRevisionUnknown) {
  auto model =
      createTechSupportModel("WARM_BOOT", {{0, "WARM_BOOT"}}, {{0, ""}});

  EXPECT_TRUE(
      getLinesContaining(captureTechSupportOutput(std::move(model)), "Warning")
          .empty());
}

TEST(
    CmdShowTechSupportTest,
    createModelOmitsHwAgentRevisionWhenNotNetosNative) {
  auto model =
      createTechSupportModel("WARM_BOOT", {{0, "WARM_BOOT"}}, {}, false);

  const std::map<int16_t, std::string> expectedVersion{{0, "hw-agent-version"}};
  EXPECT_EQ(*model.hwAgentVersion(), expectedVersion);
  EXPECT_FALSE(model.hwAgentRevision().has_value());
}

TEST(CmdShowTechSupportTest, printOutputKeepsLegacyWarningWhenNotNetosNative) {
  auto model = createTechSupportModel(
      "WARM_BOOT",
      {{0, "WARM_BOOT"}, {1, "WARM_BOOT"}},
      {{1, "old-revision"}},
      false);
  model.agentBundle() = "sw-agent-bundle";

  const std::vector<std::string> expected(
      2, "Warning: Version mismatch between sw-agent and hw-agent.");
  EXPECT_EQ(
      getLinesContaining(captureTechSupportOutput(std::move(model)), "Warning"),
      expected);
}

} // namespace facebook::fboss
