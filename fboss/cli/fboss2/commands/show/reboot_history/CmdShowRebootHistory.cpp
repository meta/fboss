/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/reboot_history/CmdShowRebootHistory.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/utils/Table.h"
#include "fboss/platform/reboot_cause_finder/RebootHistory.h"
#include "fboss/platform/reboot_cause_finder/if/gen-cpp2/reboot_cause_config_constants.h"

#include <thrift/lib/cpp/util/EnumUtils.h>
#include <algorithm>
#include <array>
#include <ctime>

namespace facebook::fboss {

using utils::Table;
namespace rcc = platform::reboot_cause_config;

namespace {

// Whole seconds in the switch's local time zone, as `last reboot` shows them.
std::string formatBootTime(int64_t bootTimeMs) {
  if (bootTimeMs <= 0) {
    return "-";
  }
  const std::time_t secs = bootTimeMs / 1000;
  std::tm tm{};
  localtime_r(&secs, &tm);
  std::array<char, 64> buf{};
  std::strftime(buf.data(), buf.size(), "%Y-%m-%d %H:%M:%S %Z", &tm);
  return buf.data();
}

// Same shape as the uptime column of `last reboot`: the two largest units.
std::string formatDuration(int64_t secs) {
  const auto days = secs / 86400;
  const auto hours = (secs % 86400) / 3600;
  const auto minutes = (secs % 3600) / 60;
  if (days > 0) {
    return fmt::format("{}d {}h", days, hours);
  }
  if (hours > 0) {
    return fmt::format("{}h {}m", hours, minutes);
  }
  return fmt::format("{}m", minutes);
}

std::string causeText(const rcc::RebootCauseRecord& record) {
  if (record.determinedCause().has_value()) {
    const auto& description = *record.determinedCause()->cause()->description();
    return description.empty() ? "-" : description;
  }
  // Unset on records written before the finder recorded it.
  if (!record.causeStatus().has_value()) {
    return "-";
  }
  switch (*record.causeStatus()) {
    case rcc::RebootCauseStatus::FOUND:
      return "-";
    case rcc::RebootCauseStatus::UNEXPLAINED:
      return "Unexplained";
    case rcc::RebootCauseStatus::INCONCLUSIVE:
      return "Inconclusive";
    case rcc::RebootCauseStatus::INTERRUPTED:
      return "Interrupted";
  }
  return apache::thrift::util::enumNameSafe(*record.causeStatus());
}

} // namespace

CmdShowRebootHistory::RetType CmdShowRebootHistory::queryClient(
    const HostInfo& hostInfo) {
  if (!hostInfo.isLocalHost()) {
    throw std::runtime_error(
        "show reboot-history reads the records on the local switch; run it on " +
        hostInfo.getName() + " instead of with --host");
  }
  return readHistory(rcc::reboot_cause_config_constants::REBOOT_HISTORY_DIR());
}

CmdShowRebootHistory::RetType CmdShowRebootHistory::readHistory(
    const std::string& historyDir) {
  // The reader sensor_service uses, so both show the same records: newest
  // boot first, enums by name or number, and anything unreadable counted and
  // explained in warnings rather than thrown.
  platform::reboot_cause_finder::RebootHistory history;
  try {
    platform::reboot_cause_finder::readRebootHistory(historyDir, history);
  } catch (const std::exception& ex) {
    platform::reboot_cause_finder::addWarning(
        history,
        fmt::format("Reboot history could not be read: {}", ex.what()));
  }
  RetType model;
  model.records() = std::move(history.records);
  model.skippedCount() = history.skippedCount;
  model.warnings() = std::move(history.warnings);
  return model;
}

void CmdShowRebootHistory::printOutput(
    const RetType& model,
    std::ostream& out) {
  const auto& records = *model.records();
  if (records.empty()) {
    out << "No reboot records on this switch" << std::endl;
  } else {
    Table table;
    table.setHeader({"Boot Time", "Up For", "Kernel", "Cause"});
    for (size_t i = 0; i < records.size(); ++i) {
      const auto& record = records[i];
      // How long this boot ran: until the next recorded boot. Blank for the
      // newest, which a record cannot confirm is the running boot.
      const int64_t bootMs = *record.bootTimeMs();
      const int64_t nextBootMs = i > 0 ? *records[i - 1].bootTimeMs() : 0;
      table.addRow(
          {formatBootTime(bootMs),
           bootMs > 0 && nextBootMs > 0
               ? formatDuration((nextBootMs - bootMs) / 1000)
               : "-",
           record.kernelRelease()->empty() ? "-" : *record.kernelRelease(),
           causeText(record)});
    }
    out << table << std::endl;
  }
  if (*model.skippedCount() > 0) {
    out << *model.skippedCount() << " record(s) could not be read" << std::endl;
  }
  for (const auto& warning : *model.warnings()) {
    out << "Warning: " << warning << std::endl;
  }
}

std::string_view CmdShowRebootHistoryTraits::description() {
  return "Displays the local switch's reboot history, newest boot first: each boot's start time, how long it ran, its kernel, and the cause reboot_cause_finder recorded, or Unexplained, Inconclusive or Interrupted when it found none. Use it to see why a switch rebooted. Runs on the switch itself, not with --host.";
}

CmdShowRebootHistory::RetType CmdShowRebootHistory::sampleModel() {
  RetType model;
  const int64_t newestBootMs = 1792089290000;
  const struct {
    int64_t bootTimeMs;
    const char* cause;
    const char* provider;
    rcc::RebootCauseStatus status;
  } boots[] = {
      {newestBootMs,
       "X86 Reboot Command",
       "X86RebootCommand",
       rcc::RebootCauseStatus::FOUND},
      {newestBootMs - 63150000,
       "ChassisResetFromBmc",
       "BMCWedgePower",
       rcc::RebootCauseStatus::FOUND},
      {newestBootMs - 174600000, "", "", rcc::RebootCauseStatus::UNEXPLAINED},
      {newestBootMs - 260000000, "", "", rcc::RebootCauseStatus::INCONCLUSIVE},
      {newestBootMs - 345000000, "", "", rcc::RebootCauseStatus::INTERRUPTED},
  };
  for (size_t i = 0; i < std::size(boots); ++i) {
    rcc::RebootCauseRecord record;
    record.bootId() = fmt::format("00000000-0000-0000-0000-00000000000{}", i);
    record.bootTimeMs() = boots[i].bootTimeMs;
    record.kernelRelease() = "6.4.3-0_fbk15_2630_gf27365f948db";
    record.causeStatus() = boots[i].status;
    if (*boots[i].cause != '\0') {
      rcc::DeterminedCause determined;
      determined.providerName() = boots[i].provider;
      determined.cause()->description() = boots[i].cause;
      record.determinedCause() = std::move(determined);
    }
    model.records()->push_back(std::move(record));
  }
  return model;
}

// Explicit template instantiation
template void
CmdHandler<CmdShowRebootHistory, CmdShowRebootHistoryTraits>::run();

} // namespace facebook::fboss
