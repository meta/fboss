// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "configerator/distribution/api/ScopedConfigeratorFake.h"
#include "configerator/structs/neteng/fboss/push/forwarding_stack/gen-cpp2/fbpkg_map_types.h"
#include "configerator/structs/neteng/netwhoami/gen-cpp2/netwhoami_types.h"
#include "fboss/cli/fboss2/commands/show/facebook/CmdShowVersionAgent.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

DECLARE_string(netwhoami_path);

using namespace ::testing;

namespace facebook::fboss {

std::string captureVersionAgentOutput(CmdShowVersionAgent::RetType agentVer) {
  configerator::ScopedConfigeratorFake configeratorFake;
  configeratorFake.setConfigThrift(
      "neteng/fboss/push/forwarding_stack/fbpkg_map", FbpkgMap{});
  CmdShowVersionAgent cmd;
  testing::internal::CaptureStdout();
  cmd.printOutput(agentVer);
  return testing::internal::GetCapturedStdout();
}

std::string versionBlock(
    const std::string& packageName,
    const std::string& packageVersion,
    const std::string& revision) {
  return fmt::format(
      "Package Name: {}\n"
      "Package Info: \n"
      "Package Version: {}\n"
      "Build Details: \n"
      "\t Host: \n"
      "\t Time: \n"
      "\t User: \n"
      "\t Path: \n"
      "\t Platform: \n"
      "\t Revision: {}\n",
      packageName,
      packageVersion,
      revision);
}

TEST(CmdShowVersionAgentTest, printOutputSwOnlyKeepsLegacyFormat) {
  const CmdShowVersionAgent::RetType agentVer{
      {"build_package_name", "sw_agent"},
      {"build_package_version", "sw_agent:130"},
      {"build_revision", "rev-a"},
  };

  EXPECT_EQ(
      captureVersionAgentOutput(agentVer),
      versionBlock("sw_agent", "sw_agent:130", "rev-a"));
}

TEST(CmdShowVersionAgentTest, printOutputShowsEachHwAgent) {
  const CmdShowVersionAgent::RetType agentVer{
      {"build_package_name", "sw_agent"},
      {"build_package_version", "sw_agent:130"},
      {"build_revision", "rev-a"},
      {"hw_agent_0.build_package_name", "hw_agent"},
      {"hw_agent_0.build_package_version", "hw_agent:349"},
      {"hw_agent_0.build_revision", "rev-b"},
      {"hw_agent_1.error", "unreachable"},
  };

  const auto expected = "SW Agent:\n" +
      versionBlock("sw_agent", "sw_agent:130", "rev-a") + "\nHW Agent 0:\n" +
      versionBlock("hw_agent", "hw_agent:349", "rev-b") +
      "\nHW Agent 1: unreachable\n";
  EXPECT_EQ(captureVersionAgentOutput(agentVer), expected);
}

class CmdShowVersionAgentTestFixture : public CmdHandlerTestBase {
 protected:
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    setupMockedAgentServer();
    setOsVariant(netwhoami::OsVariant::NETOS);
    EXPECT_CALL(getMockAgent(), getRegexExportedValues(_, _))
        .WillOnce(Invoke([](auto& values, auto) {
          values = {{"build_revision", "rev-a"}};
        }));
  }

  void setOsVariant(netwhoami::OsVariant osVariant) {
    netwhoami::NetWhoAmI whoami;
    whoami.current().ensure().os_variant() = osVariant;
    folly::writeFile(
        apache::thrift::SimpleJSONSerializer::serialize<std::string>(whoami),
        netwhoamiFile_.path().c_str());
    FLAGS_netwhoami_path = netwhoamiFile_.path().string();
  }

  void expectSwOnlyQuery() {
    EXPECT_CALL(getMockAgent(), getMultiSwitchRunState(_)).Times(0);

    const CmdShowVersionAgent::RetType expected{{"build_revision", "rev-a"}};
    EXPECT_EQ(CmdShowVersionAgent().queryClient(localhost()), expected);
  }

  void expectMultiSwitchRunState(
      bool multiSwitchEnabled,
      const std::map<int32_t, SwitchRunState>& hwIndexToRunState) {
    EXPECT_CALL(getMockAgent(), getMultiSwitchRunState(_))
        .WillOnce(Invoke([=](auto& runState) {
          runState.multiSwitchEnabled() = multiSwitchEnabled;
          runState.hwIndexToRunState() = hwIndexToRunState;
        }));
  }

  MockFbossCtrlAgent& addHwAgent(int switchIndex) {
    auto hwAgent = std::make_shared<MockFbossCtrlAgent>();
    hwAgentServers_.push_back(
        std::make_unique<apache::thrift::ScopedServerInterfaceThread>(
            hwAgent, "::1", 0, createFastMockServerConfig()));
    CmdGlobalOptions::getInstance()->setHwAgentThriftPort(
        switchIndex, hwAgentServers_.back()->getAddress().getPort());
    return *hwAgent;
  }

 private:
  gflags::FlagSaver flagSaver_;
  folly::test::TemporaryFile netwhoamiFile_{"netwhoami"};
  std::vector<std::unique_ptr<apache::thrift::ScopedServerInterfaceThread>>
      hwAgentServers_;
};

TEST_F(CmdShowVersionAgentTestFixture, queryClientSwOnlyOnClassic) {
  setOsVariant(netwhoami::OsVariant::CLASSIC);
  expectSwOnlyQuery();
}

TEST_F(CmdShowVersionAgentTestFixture, queryClientSwOnlyOnNetosNspawn) {
  setOsVariant(netwhoami::OsVariant::NETOS_NSPAWN);
  expectSwOnlyQuery();
}

TEST_F(CmdShowVersionAgentTestFixture, queryClientSwOnlyWithoutNetwhoami) {
  FLAGS_netwhoami_path = "/nonexistent/netwhoami.json";
  expectSwOnlyQuery();
}

TEST_F(CmdShowVersionAgentTestFixture, queryClientSwOnlyWhenNotMultiSwitch) {
  expectMultiSwitchRunState(false, {{0, SwitchRunState::CONFIGURED}});

  const CmdShowVersionAgent::RetType expected{{"build_revision", "rev-a"}};
  EXPECT_EQ(CmdShowVersionAgent().queryClient(localhost()), expected);
}

TEST_F(
    CmdShowVersionAgentTestFixture,
    queryClientQueriesHwAgentsBySwitchIndex) {
  expectMultiSwitchRunState(
      true,
      {{2880, SwitchRunState::CONFIGURED}, {2882, SwitchRunState::CONFIGURED}});
  EXPECT_CALL(addHwAgent(0), getRegexExportedValues(_, _))
      .WillOnce(Invoke(
          [](auto& values, auto) { values = {{"build_revision", "rev-b"}}; }));
  EXPECT_CALL(addHwAgent(1), getRegexExportedValues(_, _))
      .WillOnce(Throw(std::runtime_error("hw agent down")));

  const CmdShowVersionAgent::RetType expected{
      {"build_revision", "rev-a"},
      {"hw_agent_0.build_revision", "rev-b"},
      {"hw_agent_1.error", "unreachable"},
  };
  EXPECT_EQ(CmdShowVersionAgent().queryClient(localhost()), expected);
}

TEST_F(
    CmdShowVersionAgentTestFixture,
    queryClientEmptyHwAgentIsNotUnreachable) {
  expectMultiSwitchRunState(true, {{2880, SwitchRunState::CONFIGURED}});
  EXPECT_CALL(addHwAgent(0), getRegexExportedValues(_, _)).WillOnce(Return());

  const CmdShowVersionAgent::RetType expected{{"build_revision", "rev-a"}};
  EXPECT_EQ(CmdShowVersionAgent().queryClient(localhost()), expected);
}

} // namespace facebook::fboss
