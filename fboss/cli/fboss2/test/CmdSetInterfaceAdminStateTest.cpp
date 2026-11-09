// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>

#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/commands/set/interface/admin_state/CmdSetInterfaceAdminState.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdSetInterfaceAdminStateTestFixture : public CmdHandlerTestBase {
 public:
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    // Unit tests have no TTY, so bypass the confirmation prompt.
    CmdLocalOptions::getInstance()->setLocalOption(
        kSetInterfaceAdminStateCommandName,
        kSetInterfaceAdminStateYesFlag,
        "true");
  }

  void TearDown() override {
    CmdLocalOptions::getInstance()->setLocalOption(
        kSetInterfaceAdminStateCommandName,
        kSetInterfaceAdminStateYesFlag,
        "false");
    CmdHandlerTestBase::TearDown();
  }
};

TEST_F(CmdSetInterfaceAdminStateTestFixture, parsesXphyLineDisable) {
  const AdminStateArg arg({"XPHY_LINE", "Disable"});
  EXPECT_EQ(arg.component(), phy::PortComponent::GB_LINE);
  EXPECT_EQ(arg.componentName(), "xphy_line");
  EXPECT_FALSE(arg.adminUp());
}

TEST_F(CmdSetInterfaceAdminStateTestFixture, parsesXphySystemEnable) {
  const AdminStateArg arg({"xphy_system", "enable"});
  EXPECT_EQ(arg.component(), phy::PortComponent::GB_SYSTEM);
  EXPECT_TRUE(arg.adminUp());
}

TEST_F(CmdSetInterfaceAdminStateTestFixture, rejectsInvalidArgs) {
  EXPECT_THROW(AdminStateArg({"asic", "disable"}), std::exception);
  EXPECT_THROW(AdminStateArg({"transceiver_line", "disable"}), std::exception);
  EXPECT_THROW(AdminStateArg({"xphy_line", "down"}), FbossError);
  EXPECT_THROW(AdminStateArg({"xphy_line"}), FbossError);
  EXPECT_THROW(AdminStateArg({"xphy_line", "enable", "extra"}), FbossError);
}

TEST_F(CmdSetInterfaceAdminStateTestFixture, disableSendsSelectedSideOnly) {
  setupMockedAgentServer();
  EXPECT_CALL(
      getQsfpService(),
      setPortAdminState(
          Pointee(std::string("eth1/48/1")),
          phy::PortComponent::GB_LINE,
          false))
      .Times(1);

  auto model = CmdSetInterfaceAdminState().queryClient(
      localhost(),
      utils::PortList({"eth1/48/1"}),
      AdminStateArg({"xphy_line", "disable"}));
  EXPECT_EQ(model, "Set admin state xphy_line=disable on eth1/48/1\n");
}

TEST_F(CmdSetInterfaceAdminStateTestFixture, failureOnOnePortThrows) {
  setupMockedAgentServer();
  EXPECT_CALL(
      getQsfpService(),
      setPortAdminState(
          Pointee(std::string("eth1/48/1")),
          phy::PortComponent::GB_SYSTEM,
          true))
      .Times(1);
  EXPECT_CALL(
      getQsfpService(),
      setPortAdminState(
          Pointee(std::string("eth1/48/5")),
          phy::PortComponent::GB_SYSTEM,
          true))
      .WillOnce(Throw(std::runtime_error("no xphy")));

  EXPECT_THROW(
      CmdSetInterfaceAdminState().queryClient(
          localhost(),
          utils::PortList({"eth1/48/1", "eth1/48/5"}),
          AdminStateArg({"xphy_system", "enable"})),
      FbossError);
}

TEST_F(CmdSetInterfaceAdminStateTestFixture, noInterfaceReturnsUsage) {
  auto model = CmdSetInterfaceAdminState().queryClient(
      localhost(), utils::PortList(), AdminStateArg({"xphy_line", "enable"}));
  EXPECT_THAT(model, HasSubstr("No interface specified"));
}

} // namespace facebook::fboss
