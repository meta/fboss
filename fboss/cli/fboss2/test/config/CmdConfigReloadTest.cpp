// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/IPAddress.h>
#include <gtest/gtest.h>
#include <sstream>
#include <stdexcept>

#include "fboss/cli/fboss2/commands/config/CmdConfigReload.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigReloadTestFixture : public ::testing::Test {
 public:
  void SetUp() override {}
};

TEST_F(CmdConfigReloadTestFixture, printOutput) {
  auto cmd = CmdConfigReload();
  std::string successMessage = "Config reloaded successfully";

  // Redirect cout to capture output
  std::stringstream buffer;
  std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());

  cmd.printOutput(successMessage);

  // Restore cout
  std::cout.rdbuf(old);

  std::string output = buffer.str();
  std::string expectedOutput = "Config reloaded successfully\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdConfigReloadTestFixture, printOutputCustomMessage) {
  auto cmd = CmdConfigReload();
  std::string customMessage = "Custom test message";

  // Redirect cout to capture output
  std::stringstream buffer;
  std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());

  cmd.printOutput(customMessage);

  // Restore cout
  std::cout.rdbuf(old);

  std::string output = buffer.str();
  std::string expectedOutput = "Custom test message\n";

  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdConfigReloadTestFixture, bootTypeArgDefault) {
  BootTypeArg arg;
  EXPECT_EQ(arg.level(), cli::ConfigActionLevel::HITLESS);
}

TEST_F(CmdConfigReloadTestFixture, bootTypeArgEmptyVector) {
  BootTypeArg arg{std::vector<std::string>{}};
  EXPECT_EQ(arg.level(), cli::ConfigActionLevel::HITLESS);
}

TEST_F(CmdConfigReloadTestFixture, bootTypeArgHitless) {
  BootTypeArg arg{std::vector<std::string>{"hitless"}};
  EXPECT_EQ(arg.level(), cli::ConfigActionLevel::HITLESS);
}

TEST_F(CmdConfigReloadTestFixture, bootTypeArgWarmboot) {
  BootTypeArg arg{std::vector<std::string>{"warmboot"}};
  EXPECT_EQ(arg.level(), cli::ConfigActionLevel::SERVICE_RESTART);
}

TEST_F(CmdConfigReloadTestFixture, bootTypeArgColdboot) {
  BootTypeArg arg{std::vector<std::string>{"coldboot"}};
  EXPECT_EQ(arg.level(), cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);
}

TEST_F(CmdConfigReloadTestFixture, bootTypeArgCaseInsensitive) {
  BootTypeArg upper{std::vector<std::string>{"COLDBOOT"}};
  EXPECT_EQ(upper.level(), cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);
  BootTypeArg mixed{std::vector<std::string>{"WarmBoot"}};
  EXPECT_EQ(mixed.level(), cli::ConfigActionLevel::SERVICE_RESTART);
}

TEST_F(CmdConfigReloadTestFixture, bootTypeArgInvalid) {
  EXPECT_THROW(
      BootTypeArg{std::vector<std::string>{"reboot"}}, std::invalid_argument);
  EXPECT_THROW(
      BootTypeArg{std::vector<std::string>{""}}, std::invalid_argument);
}

TEST_F(CmdConfigReloadTestFixture, bootTypeArgTooManyValues) {
  EXPECT_THROW(
      (BootTypeArg{std::vector<std::string>{"warmboot", "coldboot"}}),
      std::invalid_argument);
}

// warmboot / coldboot dispatch to local systemctl, so a non-loopback host
// must be rejected before the implementation tries to load AgentConfig or
// touch any service. We use a non-loopback documentation IP so HostInfo
// construction does no DNS lookup and isLocalHost() returns false.
TEST_F(CmdConfigReloadTestFixture, queryClientRejectsRemoteHostForColdboot) {
  auto cmd = CmdConfigReload();
  HostInfo remote(
      "remote-switch", "remote-switch.oob", folly::IPAddress("192.0.2.1"));
  BootTypeArg arg{std::vector<std::string>{"coldboot"}};
  EXPECT_THROW(cmd.queryClient(remote, arg), std::invalid_argument);
}

TEST_F(CmdConfigReloadTestFixture, queryClientRejectsRemoteHostForWarmboot) {
  auto cmd = CmdConfigReload();
  HostInfo remote(
      "remote-switch", "remote-switch.oob", folly::IPAddress("192.0.2.1"));
  BootTypeArg arg{std::vector<std::string>{"warmboot"}};
  EXPECT_THROW(cmd.queryClient(remote, arg), std::invalid_argument);
}

} // namespace facebook::fboss
