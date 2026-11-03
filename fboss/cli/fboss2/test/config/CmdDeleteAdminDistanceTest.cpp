// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "fboss/cli/fboss2/commands/config/switch/admin_distance/CmdConfigAdminDistance.h"
#include "fboss/cli/fboss2/commands/delete/switch/admin_distance/CmdDeleteAdminDistance.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdDeleteAdminDistanceTestFixture : public CmdConfigTestBase {
 public:
  // clientIdToAdminDistance mirrors the map production devices carry
  // (0=BGPD, 1=STATIC_ROUTE, 700=STATIC_INTERNAL, 786=OPENR).
  CmdDeleteAdminDistanceTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_admin_distance_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "clientIdToAdminDistance": {
      "0": 20,
      "1": 1,
      "700": 255,
      "786": 10
    }
  }
})") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession("delete switch admin-distance", "786");
  }

  static const std::map<int32_t, int32_t>& adminDistanceMap() {
    return *ConfigSession::getInstance()
                .getAgentConfig()
                .sw()
                ->clientIdToAdminDistance();
  }
};

// Argument validation

TEST_F(CmdDeleteAdminDistanceTestFixture, argValid) {
  AdminDistanceDeleteArg arg({"786"});
  EXPECT_EQ(arg.getClientId(), 786);
}

TEST_F(CmdDeleteAdminDistanceTestFixture, argWrongArity) {
  EXPECT_THROW(AdminDistanceDeleteArg({}), std::invalid_argument);
  EXPECT_THROW(AdminDistanceDeleteArg({"0", "20"}), std::invalid_argument);
}

TEST_F(CmdDeleteAdminDistanceTestFixture, argNonInteger) {
  EXPECT_THROW(AdminDistanceDeleteArg({"bgp"}), std::invalid_argument);
}

TEST_F(CmdDeleteAdminDistanceTestFixture, argNegative) {
  EXPECT_THROW(AdminDistanceDeleteArg({"-1"}), std::invalid_argument);
}

// Client-ids whose admin distance the agent hardcodes are refused here exactly
// as they are by `config switch admin-distance`.
TEST_F(CmdDeleteAdminDistanceTestFixture, argForbiddenClients) {
  for (const auto& entry : forbiddenAdminDistanceClients()) {
    EXPECT_THROW(
        AdminDistanceDeleteArg({std::to_string(entry.first)}),
        std::invalid_argument)
        << "client-id " << entry.first << " should be refused";
  }
}

// Client-id 1 is present in the fixture config, so this covers the case where
// the entry exists and is still refused rather than erased.
TEST_F(CmdDeleteAdminDistanceTestFixture, forbiddenClientEntryKept) {
  ASSERT_TRUE(adminDistanceMap().count(1));

  EXPECT_THROW(AdminDistanceDeleteArg({"1"}), std::invalid_argument);

  EXPECT_TRUE(adminDistanceMap().count(1));
  EXPECT_EQ(adminDistanceMap().size(), 4);
}

// queryClient

TEST_F(CmdDeleteAdminDistanceTestFixture, deleteExistingEntry) {
  ASSERT_TRUE(adminDistanceMap().count(786));

  auto cmd = CmdDeleteAdminDistance();
  auto result = cmd.queryClient(localhost(), AdminDistanceDeleteArg({"786"}));

  EXPECT_THAT(result, HasSubstr("removed admin distance entry"));
  EXPECT_THAT(result, HasSubstr("786"));

  EXPECT_FALSE(adminDistanceMap().count(786));
  // The other entries are untouched.
  EXPECT_EQ(adminDistanceMap().size(), 3);
  EXPECT_EQ(adminDistanceMap().at(0), 20);
}

TEST_F(CmdDeleteAdminDistanceTestFixture, deleteAbsentEntryIsNoop) {
  const auto sessionBefore = sessionConfigText();
  ASSERT_FALSE(adminDistanceMap().count(42));

  auto cmd = CmdDeleteAdminDistance();
  auto result = cmd.queryClient(localhost(), AdminDistanceDeleteArg({"42"}));
  EXPECT_THAT(result, HasSubstr("Warning"));
  EXPECT_THAT(result, HasSubstr("nothing to delete"));
  EXPECT_EQ(adminDistanceMap().size(), 4);
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

} // namespace facebook::fboss
