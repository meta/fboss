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

#include <folly/FileUtil.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <filesystem>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/session/FbossServiceUtil.h"
#include "fboss/cli/fboss2/test/TestableConfigSession.h"
#include "fboss/cli/fboss2/test/config/MockFbossServiceUtil.h"
#include "fboss/cli/fboss2/test/config/MockSystemdInterface.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using ::testing::_; // NOLINT(bugprone-reserved-identifier)
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Throw;

namespace facebook::fboss {

// Test: isSplitMode() returns true when multi_switch flag is set
TEST(FbossServiceUtilTest, IsSplitMode_ReturnsTrueWhenMultiSwitchSet) {
  auto mockSystemd = std::make_unique<MockSystemdInterface>();

  FbossServiceUtil util(
      std::vector<int>{0}, /*multiSwitch=*/true, std::move(mockSystemd));
  EXPECT_TRUE(util.isSplitMode());
}

// Test: isSplitMode() returns false when multi_switch flag is not set
TEST(FbossServiceUtilTest, IsSplitMode_ReturnsFalseWhenMultiSwitchNotSet) {
  auto mockSystemd = std::make_unique<MockSystemdInterface>();

  FbossServiceUtil util(
      std::vector<int>{}, /*multiSwitch=*/false, std::move(mockSystemd));
  EXPECT_FALSE(util.isSplitMode());
}

// Test: Monolithic mode warmboot restart
TEST(FbossServiceUtilTest, RestartService_MonolithicMode_Warmboot) {
  auto mockSystemd = std::make_unique<MockSystemdInterface>();

  EXPECT_CALL(*mockSystemd, restartService("wedge_agent")).Times(1);
  EXPECT_CALL(*mockSystemd, waitForServiceActive("wedge_agent", _, _)).Times(1);

  FbossServiceUtil util(
      std::vector<int>{}, /*multiSwitch=*/false, std::move(mockSystemd));

  auto services = util.restartService(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  EXPECT_EQ(services.size(), 1);
  EXPECT_EQ(services[0], "wedge_agent");
}

// Test: Monolithic mode coldboot restart
TEST(FbossServiceUtilTest, RestartService_MonolithicMode_Coldboot) {
  auto mockSystemd = std::make_unique<MockSystemdInterface>();

  // Expect marker creation, then restart -> wait for coldboot
  InSequence seq;
  EXPECT_CALL(*mockSystemd, restartService("wedge_agent")).Times(1);
  EXPECT_CALL(*mockSystemd, waitForServiceActive("wedge_agent", _, _)).Times(1);

  FbossServiceUtil util(
      std::vector<int>{}, /*multiSwitch=*/false, std::move(mockSystemd));

  auto services = util.restartService(
      cli::ServiceType::AGENT,
      cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);

  EXPECT_EQ(services.size(), 1);
  EXPECT_EQ(services[0], "wedge_agent");
}

// Test: Split mode warmboot restart (single hw_agent)
// hw_agent must be restarted before sw_agent
TEST(FbossServiceUtilTest, RestartService_SplitMode_Warmboot_SingleHwAgent) {
  auto mockSystemd = std::make_unique<StrictMock<MockSystemdInterface>>();

  InSequence seq;
  EXPECT_CALL(*mockSystemd, getMatchingServices("fboss_hw_agent@0.service"))
      .WillOnce(Return(std::vector<std::string>{"fboss_hw_agent@0.service"}));
  EXPECT_CALL(*mockSystemd, getMatchingServices("fboss_sw_agent.service"))
      .WillOnce(Return(std::vector<std::string>{"fboss_sw_agent.service"}));
  EXPECT_CALL(*mockSystemd, restartService("fboss_hw_agent@0")).Times(1);
  EXPECT_CALL(*mockSystemd, waitForServiceActive("fboss_hw_agent@0", _, _))
      .Times(1);
  EXPECT_CALL(*mockSystemd, restartService("fboss_sw_agent")).Times(1);
  EXPECT_CALL(*mockSystemd, waitForServiceActive("fboss_sw_agent", _, _))
      .Times(1);

  FbossServiceUtil util(
      std::vector<int>{0}, /*multiSwitch=*/true, std::move(mockSystemd));

  auto services = util.restartService(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  EXPECT_EQ(services.size(), 2);
  EXPECT_EQ(services[0], "fboss_hw_agent@0");
  EXPECT_EQ(services[1], "fboss_sw_agent");
}

// Test: Split mode coldboot restart (single hw_agent)
// hw_agent must be fully coldbooted before sw_agent
TEST(FbossServiceUtilTest, RestartService_SplitMode_Coldboot_SingleHwAgent) {
  auto mockSystemd = std::make_unique<MockSystemdInterface>();

  EXPECT_CALL(*mockSystemd, getMatchingServices("fboss_hw_agent@0.service"))
      .WillOnce(Return(std::vector<std::string>{"fboss_hw_agent@0.service"}));
  EXPECT_CALL(*mockSystemd, getMatchingServices("fboss_sw_agent.service"))
      .WillOnce(Return(std::vector<std::string>{"fboss_sw_agent.service"}));

  // Expect sequential coldboot: create marker -> restart -> wait for each
  // service
  InSequence seq;
  EXPECT_CALL(*mockSystemd, restartService("fboss_hw_agent@0")).Times(1);
  EXPECT_CALL(*mockSystemd, waitForServiceActive("fboss_hw_agent@0", _, _))
      .Times(1);
  EXPECT_CALL(*mockSystemd, restartService("fboss_sw_agent")).Times(1);
  EXPECT_CALL(*mockSystemd, waitForServiceActive("fboss_sw_agent", _, _))
      .Times(1);

  FbossServiceUtil util(
      std::vector<int>{0}, /*multiSwitch=*/true, std::move(mockSystemd));

  auto services = util.restartService(
      cli::ServiceType::AGENT,
      cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);

  EXPECT_EQ(services.size(), 2);
  EXPECT_EQ(services[0], "fboss_hw_agent@0");
  EXPECT_EQ(services[1], "fboss_sw_agent");
}

// Test: Split mode warmboot restart (multiple hw_agents)
// All hw_agents must be restarted before sw_agent
TEST(FbossServiceUtilTest, RestartService_SplitMode_Warmboot_MultipleHwAgents) {
  auto mockSystemd = std::make_unique<MockSystemdInterface>();

  EXPECT_CALL(*mockSystemd, getMatchingServices("fboss_hw_agent@0.service"))
      .WillOnce(Return(std::vector<std::string>{"fboss_hw_agent@0.service"}));
  EXPECT_CALL(*mockSystemd, getMatchingServices("fboss_hw_agent@1.service"))
      .WillOnce(Return(std::vector<std::string>{"fboss_hw_agent@1.service"}));
  EXPECT_CALL(*mockSystemd, getMatchingServices("fboss_sw_agent.service"))
      .WillOnce(Return(std::vector<std::string>{"fboss_sw_agent.service"}));

  // Each service: restart -> wait, in hw-before-sw order
  InSequence seq;
  EXPECT_CALL(*mockSystemd, restartService("fboss_hw_agent@0")).Times(1);
  EXPECT_CALL(*mockSystemd, waitForServiceActive("fboss_hw_agent@0", _, _))
      .Times(1);
  EXPECT_CALL(*mockSystemd, restartService("fboss_hw_agent@1")).Times(1);
  EXPECT_CALL(*mockSystemd, waitForServiceActive("fboss_hw_agent@1", _, _))
      .Times(1);
  EXPECT_CALL(*mockSystemd, restartService("fboss_sw_agent")).Times(1);
  EXPECT_CALL(*mockSystemd, waitForServiceActive("fboss_sw_agent", _, _))
      .Times(1);

  FbossServiceUtil util(
      std::vector<int>{0, 1}, /*multiSwitch=*/true, std::move(mockSystemd));

  auto services = util.restartService(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  EXPECT_EQ(services.size(), 3);
  EXPECT_EQ(services[0], "fboss_hw_agent@0");
  EXPECT_EQ(services[1], "fboss_hw_agent@1");
  EXPECT_EQ(services[2], "fboss_sw_agent");
}

TEST(FbossServiceUtilTest, RestartService_SplitMode_UsesNetosServices) {
  auto mockSystemd = std::make_unique<MockSystemdInterface>();

  EXPECT_CALL(*mockSystemd, getMatchingServices(_))
      .WillRepeatedly(Return(std::vector<std::string>{}));
  EXPECT_CALL(
      *mockSystemd,
      getMatchingServices("netos.service.fboss_wedge_agent_*_0.service"))
      .WillOnce(Return(
          std::vector<std::string>{
              "netos.service.fboss_wedge_agent_brcm_0.service"}));
  EXPECT_CALL(
      *mockSystemd, getMatchingServices("netos.service.fboss_sw_agent.service"))
      .WillOnce(Return(
          std::vector<std::string>{"netos.service.fboss_sw_agent.service"}));

  InSequence seq;
  EXPECT_CALL(
      *mockSystemd, restartService("netos.service.fboss_wedge_agent_brcm_0"));
  EXPECT_CALL(
      *mockSystemd,
      waitForServiceActive("netos.service.fboss_wedge_agent_brcm_0", _, _));
  EXPECT_CALL(*mockSystemd, restartService("netos.service.fboss_sw_agent"));
  EXPECT_CALL(
      *mockSystemd, waitForServiceActive("netos.service.fboss_sw_agent", _, _));

  FbossServiceUtil util(
      std::vector<int>{0}, /*multiSwitch=*/true, std::move(mockSystemd));

  const std::vector<std::string> expectedServices = {
      "netos.service.fboss_wedge_agent_brcm_0", "netos.service.fboss_sw_agent"};
  EXPECT_EQ(
      util.restartService(
          cli::ServiceType::AGENT,
          cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART),
      expectedServices);
}

TEST(FbossServiceUtilTest, RestartService_BgpUsesNetosService) {
  auto mockSystemd = std::make_unique<MockSystemdInterface>();

  EXPECT_CALL(*mockSystemd, getMatchingServices("bgpd.service"))
      .WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(
      *mockSystemd, getMatchingServices("netos.service.fboss_bgp.service"))
      .WillOnce(
          Return(std::vector<std::string>{"netos.service.fboss_bgp.service"}));
  EXPECT_CALL(*mockSystemd, restartService("netos.service.fboss_bgp"));
  EXPECT_CALL(
      *mockSystemd, waitForServiceActive("netos.service.fboss_bgp", _, _));

  FbossServiceUtil util(
      std::vector<int>{}, /*multiSwitch=*/false, std::move(mockSystemd));

  EXPECT_EQ(
      util.restartService(
          cli::ServiceType::BGP, cli::ConfigActionLevel::SERVICE_RESTART),
      std::vector<std::string>{"netos.service.fboss_bgp"});
}

TEST(FbossServiceUtilTest, RestartService_ThrowsWhenServiceIsMissing) {
  auto mockSystemd = std::make_unique<StrictMock<MockSystemdInterface>>();

  InSequence seq;
  EXPECT_CALL(*mockSystemd, getMatchingServices("bgpd.service"))
      .WillOnce(Return(std::vector<std::string>{}));
  EXPECT_CALL(
      *mockSystemd, getMatchingServices("netos.service.fboss_bgp.service"))
      .WillOnce(Return(std::vector<std::string>{}));

  FbossServiceUtil util(
      std::vector<int>{}, /*multiSwitch=*/false, std::move(mockSystemd));

  try {
    util.restartService(
        cli::ServiceType::BGP, cli::ConfigActionLevel::SERVICE_RESTART);
    FAIL() << "Expected restartService() to reject missing systemd services";
  } catch (const std::runtime_error& ex) {
    EXPECT_EQ(
        std::string(ex.what()),
        "No systemd service found for bgpd and netos.service.fboss_bgp");
  }
}

// Test: Service restart failure is propagated
TEST(FbossServiceUtilTest, RestartService_PropagatesFailure) {
  auto mockSystemd = std::make_unique<MockSystemdInterface>();

  EXPECT_CALL(*mockSystemd, restartService("wedge_agent"))
      .WillOnce(Throw(std::runtime_error("Failed to restart wedge_agent")));

  FbossServiceUtil util(
      std::vector<int>{}, /*multiSwitch=*/false, std::move(mockSystemd));

  EXPECT_THROW(
      util.restartService(
          cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART),
      std::runtime_error);
}

// Test: Service fails to become active within timeout
TEST(FbossServiceUtilTest, RestartService_ServiceFailsToStart) {
  auto mockSystemd = std::make_unique<MockSystemdInterface>();

  EXPECT_CALL(*mockSystemd, restartService("wedge_agent")).Times(1);
  EXPECT_CALL(*mockSystemd, waitForServiceActive("wedge_agent", _, _))
      .WillOnce(Throw(
          std::runtime_error(
              "wedge_agent did not become active within 60 seconds")));

  FbossServiceUtil util(
      std::vector<int>{}, /*multiSwitch=*/false, std::move(mockSystemd));

  EXPECT_THROW(
      util.restartService(
          cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART),
      std::runtime_error);
}

// ============================================================
// Test: waitForAgentConfigured() polls the given host until the agent is
// configured.
TEST(FbossServiceUtilTest, WaitForAgentConfigured_PollsUntilConfigured) {
  MockFbossServiceUtil util;
  HostInfo hostInfo("dut1", "dut1-oob", folly::IPAddress("10.0.0.1"));

  EXPECT_CALL(
      util, isAgentConfigured(::testing::Property(&HostInfo::getName, "dut1")))
      .WillOnce(Return(false))
      .WillOnce(Return(false))
      .WillOnce(Return(true));

  util.waitForAgentConfigured(
      hostInfo, /*maxWaitSeconds=*/5, /*pollIntervalMs=*/10);
}

// Test: waitForAgentConfigured() throws if the agent never becomes configured.
TEST(FbossServiceUtilTest, WaitForAgentConfigured_ThrowsOnTimeout) {
  MockFbossServiceUtil util;
  HostInfo hostInfo(
      "localhost", "localhost-oob", folly::IPAddress("127.0.0.1"));

  EXPECT_CALL(util, isAgentConfigured(_)).WillRepeatedly(Return(false));

  EXPECT_THROW(
      util.waitForAgentConfigured(
          hostInfo, /*maxWaitSeconds=*/1, /*pollIntervalMs=*/10),
      std::runtime_error);
}

// ConfigSession integration tests using MockFbossServiceUtil
// These verify that ConfigSession::applyServiceActions() correctly
// delegates to fbossServiceUtil_ without touching real systemd or thrift.
// ============================================================

// Test: applyServiceActions() delegates SERVICE_RESTART to
// FbossServiceUtil::restartService()
TEST(
    ConfigSessionServiceTest,
    ApplyServiceActions_Warmboot_DelegatesToRestartService) {
  auto mock = std::make_unique<MockFbossServiceUtil>();
  auto* mockPtr = mock.get();

  HostInfo hostInfo(
      "localhost", "localhost-oob", folly::IPAddress("127.0.0.1"));

  EXPECT_CALL(
      *mockPtr,
      restartService(
          cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART))
      .WillOnce(::testing::Return(std::vector<std::string>{"wedge_agent"}));
  EXPECT_CALL(*mockPtr, isAgentConfigured(_)).WillOnce(Return(true));

  TestableConfigSession session(
      "/tmp/test_session", "/tmp/test_system", std::move(mock));

  std::map<cli::ServiceType, cli::ConfigActionLevel> actions = {
      {cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART}};
  auto serviceNames = session.applyServiceActions(actions, hostInfo);

  ASSERT_EQ(serviceNames[cli::ServiceType::AGENT].size(), 1);
  EXPECT_EQ(serviceNames[cli::ServiceType::AGENT][0], "wedge_agent");
}

// Test: applyServiceActions() delegates DISRUPTIVE_SERVICE_RESTART to
// FbossServiceUtil::restartService()
TEST(
    ConfigSessionServiceTest,
    ApplyServiceActions_Coldboot_DelegatesToRestartService) {
  auto mock = std::make_unique<MockFbossServiceUtil>();
  auto* mockPtr = mock.get();

  HostInfo hostInfo(
      "localhost", "localhost-oob", folly::IPAddress("127.0.0.1"));

  EXPECT_CALL(
      *mockPtr,
      restartService(
          cli::ServiceType::AGENT,
          cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART))
      .WillOnce(
          ::testing::Return(
              std::vector<std::string>{"fboss_hw_agent@0", "fboss_sw_agent"}));
  EXPECT_CALL(*mockPtr, isAgentConfigured(_)).WillOnce(Return(true));

  TestableConfigSession session(
      "/tmp/test_session", "/tmp/test_system", std::move(mock));

  std::map<cli::ServiceType, cli::ConfigActionLevel> actions = {
      {cli::ServiceType::AGENT,
       cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART}};
  auto serviceNames = session.applyServiceActions(actions, hostInfo);

  ASSERT_EQ(serviceNames[cli::ServiceType::AGENT].size(), 2);
  EXPECT_EQ(serviceNames[cli::ServiceType::AGENT][0], "fboss_hw_agent@0");
  EXPECT_EQ(serviceNames[cli::ServiceType::AGENT][1], "fboss_sw_agent");
}

// Test: ConfigSession::applyServiceActions() delegates HITLESS to
// FbossServiceUtil::reloadConfig() (monolithic mode)
TEST(
    ConfigSessionServiceTest,
    ApplyServiceActions_Hitless_DelegatesToReloadConfig_Monolithic) {
  auto mock = std::make_unique<MockFbossServiceUtil>();
  auto* mockPtr = mock.get();

  HostInfo hostInfo(
      "localhost", "localhost-oob", folly::IPAddress("127.0.0.1"));

  EXPECT_CALL(*mockPtr, reloadConfig(cli::ServiceType::AGENT, ::testing::_))
      .WillOnce(::testing::Return(std::vector<std::string>{"wedge_agent"}));
  EXPECT_CALL(*mockPtr, isAgentConfigured(_)).Times(0);

  TestableConfigSession session(
      "/tmp/test_session", "/tmp/test_system", std::move(mock));

  std::map<cli::ServiceType, cli::ConfigActionLevel> actions = {
      {cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS}};

  auto serviceNames = session.applyServiceActions(actions, hostInfo);

  ASSERT_EQ(serviceNames.size(), 1);
  ASSERT_EQ(serviceNames[cli::ServiceType::AGENT].size(), 1);
  EXPECT_EQ(serviceNames[cli::ServiceType::AGENT][0], "wedge_agent");
}

// Test: ConfigSession::applyServiceActions() delegates HITLESS to
// FbossServiceUtil::reloadConfig() (split mode)
TEST(
    ConfigSessionServiceTest,
    ApplyServiceActions_Hitless_DelegatesToReloadConfig_SplitMode) {
  auto mock = std::make_unique<MockFbossServiceUtil>();
  auto* mockPtr = mock.get();

  HostInfo hostInfo(
      "localhost", "localhost-oob", folly::IPAddress("127.0.0.1"));

  EXPECT_CALL(*mockPtr, reloadConfig(cli::ServiceType::AGENT, ::testing::_))
      .WillOnce(::testing::Return(std::vector<std::string>{"fboss_sw_agent"}));

  TestableConfigSession session(
      "/tmp/test_session", "/tmp/test_system", std::move(mock));

  std::map<cli::ServiceType, cli::ConfigActionLevel> actions = {
      {cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS}};

  auto serviceNames = session.applyServiceActions(actions, hostInfo);

  ASSERT_EQ(serviceNames.size(), 1);
  ASSERT_EQ(serviceNames[cli::ServiceType::AGENT].size(), 1);
  EXPECT_EQ(serviceNames[cli::ServiceType::AGENT][0], "fboss_sw_agent");
}

// ============================================================
// Tests for ensureFbossServiceUtil Thrift-based detection
// Verify that multi-switch mode is detected via Thrift RPC (runtime state),
// not from the config file's defaultCommandLineArgs.
// ============================================================

// Test: Config file has no multi_switch flag, but agent is running in
// multi-switch mode. ensureFbossServiceUtil should detect multi-switch
// via Thrift and restart fboss_sw_agent + fboss_hw_agent@N.
TEST(
    ConfigSessionServiceTest,
    EnsureFbossServiceUtil_DetectsMultiSwitchViaThrift) {
  // Write a minimal config so initializeSession() succeeds.
  // ensureFbossServiceUtil no longer reads the config — it gets everything
  // from the Thrift RPC (simulated via overrides here).
  cfg::AgentConfig agentConfig;
  auto configJson =
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(agentConfig);

  std::filesystem::create_directories("/tmp/test_system_thrift");
  std::filesystem::create_directories("/tmp/test_session_thrift");
  folly::writeFile(configJson, "/tmp/test_system_thrift/agent.conf");

  TestableConfigSession session(
      "/tmp/test_session_thrift", "/tmp/test_system_thrift");

  // Simulate Thrift RPC reporting multi-switch with 1 hw_agent at index 0
  session.setMultiSwitchOverride(true, {0});
  session.setMockSystemdFactory([] {
    auto mock = std::make_unique<NiceMock<MockSystemdInterface>>();
    ON_CALL(*mock, getMatchingServices(_))
        .WillByDefault([](const std::string& pattern) {
          return std::vector<std::string>{pattern};
        });
    return mock;
  });

  HostInfo hostInfo(
      "localhost", "localhost-oob", folly::IPAddress("127.0.0.1"));

  std::map<cli::ServiceType, cli::ConfigActionLevel> actions = {
      {cli::ServiceType::AGENT,
       cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART}};

  auto serviceNames = session.applyServiceActions(actions, hostInfo);

  // Should restart multi-switch services, not wedge_agent
  ASSERT_EQ(serviceNames[cli::ServiceType::AGENT].size(), 2);
  EXPECT_EQ(serviceNames[cli::ServiceType::AGENT][0], "fboss_hw_agent@0");
  EXPECT_EQ(serviceNames[cli::ServiceType::AGENT][1], "fboss_sw_agent");

  std::filesystem::remove_all("/tmp/test_session_thrift");
  std::filesystem::remove_all("/tmp/test_system_thrift");
}

} // namespace facebook::fboss
