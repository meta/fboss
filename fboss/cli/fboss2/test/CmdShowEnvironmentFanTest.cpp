// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <ctime>
#include <iomanip>
#include <sstream>

#include "fboss/cli/fboss2/commands/show/environment/fan/CmdShowEnvironmentFan.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_types.h"

using namespace ::testing;

namespace facebook::fboss {

std::string toTimestamp(int64_t ts) {
  auto t = static_cast<std::time_t>(ts);
  std::tm tm{};
  localtime_r(&t, &tm);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %T");
  return oss.str();
}

std::map<std::string, platform::fan_service::FanStatus> createFanStatuses() {
  std::map<std::string, platform::fan_service::FanStatus> statuses;

  platform::fan_service::FanStatus fan0;
  fan0.fanFailed() = false;
  fan0.pwmToProgram() = 45.0f;
  fan0.rpm() = 12000;
  fan0.lastSuccessfulAccessTime() = 1700000000;
  statuses["fan0"] = fan0;

  platform::fan_service::FanStatus fan1;
  fan1.fanFailed() = true;
  fan1.pwmToProgram() = 100.0f;
  fan1.lastSuccessfulAccessTime() = 1700000010;
  // rpm not set (fan failed)
  statuses["fan1"] = fan1;

  platform::fan_service::FanStatus fan2;
  fan2.fanFailed() = false;
  fan2.pwmToProgram() = 60.5f;
  fan2.rpm() = 15000;
  fan2.lastSuccessfulAccessTime() = 1700000020;
  statuses["fan2"] = fan2;

  return statuses;
}

class CmdShowEnvironmentFanTestFixture : public CmdHandlerTestBase {
 public:
  std::map<std::string, platform::fan_service::FanStatus> fanStatuses;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    fanStatuses = createFanStatuses();
  }
};

TEST_F(CmdShowEnvironmentFanTestFixture, printOutput) {
  // localtime_r is timezone-dependent so we cannot hardcode the strings.
  const std::string ts0 = toTimestamp(1700000000);
  const std::string ts1 = toTimestamp(1700000010);
  const std::string ts2 = toTimestamp(1700000020);

  auto cmd = CmdShowEnvironmentFan();

  std::stringstream ss;
  cmd.printOutput(fanStatuses, ss);

  const std::string expectOutput =
      " Fan Name  Status  Target PWM (%)  RPM    Last Accessed       \n"
      "--------------------------------------------------------------------\n"
      " fan0      OK      45.0            12000  " +
      ts0 +
      " \n"
      " fan1      FAILED  100.0           N/A    " +
      ts1 +
      " \n"
      " fan2      OK      60.5            15000  " +
      ts2 + " \n\n";

  EXPECT_EQ(ss.str(), expectOutput);
}
} // namespace facebook::fboss
