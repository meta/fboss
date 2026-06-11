/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/environment/fan/CmdShowEnvironmentFan.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/platform/fan_service/if/gen-cpp2/FanService.h"

#include <ctime>

namespace facebook::fboss {

using utils::Table;

CmdShowEnvironmentFan::RetType CmdShowEnvironmentFan::queryClient(
    const HostInfo& hostInfo) {
  auto fanService = utils::createClient<
      apache::thrift::Client<platform::fan_service::FanService>>(hostInfo);
  platform::fan_service::FanStatusesResponse response;
  fanService->sync_getFanStatuses(response);
  return response.fanStatuses().value();
}

void CmdShowEnvironmentFan::printOutput(
    const RetType& fanStatuses,
    std::ostream& out) {
  Table table;
  table.setHeader(
      {"Fan Name", "Status", "Target PWM (%)", "RPM", "Last Accessed"});

  for (const auto& [fanName, fanStatus] : fanStatuses) {
    const bool failed = fanStatus.fanFailed().value();

    std::string rpmStr = "N/A";
    if (fanStatus.rpm().has_value()) {
      rpmStr = std::to_string(fanStatus.rpm().value());
    }

    auto timestamp =
        static_cast<std::time_t>(fanStatus.lastSuccessfulAccessTime().value());
    std::tm tm{};
    localtime_r(&timestamp, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %T");
    std::string lastSuccessfulAccessTime = oss.str();

    table.addRow({
        fanName,
        failed ? Table::StyledCell("FAILED", Table::Style::ERROR)
               : Table::StyledCell("OK", Table::Style::GOOD),
        fmt::format("{:.1f}", fanStatus.pwmToProgram().value()),
        rpmStr,
        lastSuccessfulAccessTime,
    });
  }

  out << table << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdShowEnvironmentFan, CmdShowEnvironmentFanTraits>::run();

} // namespace facebook::fboss
