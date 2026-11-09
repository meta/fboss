// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/reflection/testing.h>
#include "fboss/cli/fboss2/commands/show/mymac/CmdShowMyMac.h"
#include "fboss/cli/fboss2/commands/show/mymac/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {

cfg::MacAndVlan makeMacAndVlan(int32_t vlanID, const std::string& macAddress) {
  cfg::MacAndVlan macAndVlan;
  macAndVlan.vlanID() = vlanID;
  macAndVlan.macAddress() = macAddress;
  return macAndVlan;
}

cli::MyMacEntry makeMyMacEntry(int32_t vlanID, const std::string& macAddress) {
  cli::MyMacEntry entry;
  entry.vlanID() = vlanID;
  entry.macAddress() = macAddress;
  return entry;
}

} // namespace

class CmdShowMyMacTestFixture : public CmdHandlerTestBase {
 public:
  // Deliberately unsorted, to check the model is sorted by VLAN then MAC.
  std::vector<cfg::MacAndVlan> mockMyMacs{
      makeMacAndVlan(2001, "02:00:00:00:00:02"),
      makeMacAndVlan(1, "02:00:00:00:00:03"),
      makeMacAndVlan(2001, "02:00:00:00:00:01"),
  };
  cli::ShowMyMacModel normalizedModel;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    normalizedModel.myMacs() = {
        makeMyMacEntry(1, "02:00:00:00:00:03"),
        makeMyMacEntry(2001, "02:00:00:00:00:01"),
        makeMyMacEntry(2001, "02:00:00:00:00:02"),
    };
  }
};

TEST_F(CmdShowMyMacTestFixture, queryClient) {
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), getMyMacs(_)).WillOnce(Invoke([&](auto& myMacs) {
    myMacs = mockMyMacs;
  }));

  auto model = CmdShowMyMac().queryClient(localhost());

  EXPECT_THRIFT_EQ(normalizedModel, model);
}

TEST_F(CmdShowMyMacTestFixture, queryClientEmpty) {
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), getMyMacs(_)).WillOnce(Invoke([](auto& myMacs) {
    myMacs.clear();
  }));

  auto model = CmdShowMyMac().queryClient(localhost());

  EXPECT_TRUE(model.myMacs()->empty());
}

TEST_F(CmdShowMyMacTestFixture, printOutput) {
  std::stringstream ss;
  CmdShowMyMac().printOutput(normalizedModel, ss);

  std::string expectOutput =
      " VLAN  MAC Address       \n"
      "----------------------------\n"
      " 1     02:00:00:00:00:03 \n"
      " 2001  02:00:00:00:00:01 \n"
      " 2001  02:00:00:00:00:02 \n\n";
  EXPECT_EQ(ss.str(), expectOutput);
}

TEST_F(CmdShowMyMacTestFixture, printOutputEmpty) {
  std::stringstream ss;
  CmdShowMyMac().printOutput(cli::ShowMyMacModel(), ss);

  EXPECT_EQ(ss.str(), "No MY_MAC entries configured\n");
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowMyMacTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowMyMacTraits::description().empty());
  EXPECT_FALSE(CmdShowMyMac::sampleModel().myMacs()->empty());
}

} // namespace facebook::fboss
