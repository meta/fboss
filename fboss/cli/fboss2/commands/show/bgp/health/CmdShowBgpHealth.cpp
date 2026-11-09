/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/health/CmdShowBgpHealth.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"

namespace facebook::fboss {

using namespace facebook::neteng::fboss::bgp::thrift;

namespace {

std::string categoryToString(HealthCheckCategory category) {
  switch (category) {
    case HealthCheckCategory::GLOBAL_SYSTEM:
      return "Global: System";
    case HealthCheckCategory::GLOBAL_TASK_THREAD:
      return "Global: Task/Thread";
    case HealthCheckCategory::GLOBAL_CONVERGENCE:
      return "Global: Convergence";
    case HealthCheckCategory::SESSION_MANAGER:
      return "Session Manager";
    case HealthCheckCategory::PEER_MANAGER:
      return "Peer Manager";
    case HealthCheckCategory::RIB:
      return "RIB";
    case HealthCheckCategory::NETLINK_WRAPPER:
      return "Netlink Wrapper";
    case HealthCheckCategory::NEXTHOP_TRACKER:
      return "Nexthop Tracker";
    case HealthCheckCategory::FIB_AGENT:
      return "FIB Agent";
    case HealthCheckCategory::THRIFT_ENDPOINT:
      return "Thrift Endpoint";
    case HealthCheckCategory::UNKNOWN:
      return "Unknown";
  }
  return "Unknown";
}

std::string moduleDisplayStatus(const TModuleHealthReport& mod) {
  return apache::thrift::util::enumNameSafe(*mod.overallStatus());
}

} // namespace

CmdShowBgpHealth::RetType CmdShowBgpHealth::queryClient(
    const HostInfo& hostInfo) {
  THealthReport report;
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
  client->sync_getHealthReport(report);
  return report;
}

void CmdShowBgpHealth::printOutput(const RetType& report, std::ostream& out) {
  int32_t total = *report.passCount() + *report.warnCount() +
      *report.failCount() + *report.skipCount();

  out << std::endl;
  out << "BGP++ Health Report" << std::endl;
  out << "===================" << std::endl;

  out << fmt::format(
             "Overall: {}",
             apache::thrift::util::enumNameSafe(*report.overallStatus()))
      << std::endl;

  out << fmt::format(
             "Checks: {} total | {} PASS | {} WARN | {} FAIL | {} SKIP",
             total,
             *report.passCount(),
             *report.warnCount(),
             *report.failCount(),
             *report.skipCount())
      << std::endl;

  out << std::endl;
  out << "Legend:" << std::endl;
  out << "  PASS = Check ran, value within healthy range" << std::endl;
  out << "  WARN = Check ran, value degraded but not critical" << std::endl;
  out << "  FAIL = Check is in unhealthy state (bad value or could not execute)"
      << std::endl;
  out << "  SKIP = Check not applicable or deferred" << std::endl;

  for (const auto& mod : *report.modules()) {
    if (mod.checks()->empty()) {
      continue;
    }

    out << std::endl;
    out << fmt::format(
               "--- {} ({}) ---",
               categoryToString(*mod.category()),
               moduleDisplayStatus(mod))
        << std::endl;

    utils::Table table;
    table.setHeader({"Status", "Check", "Details"});

    for (const auto& check : *mod.checks()) {
      auto status = *check.status();
      auto statusName = apache::thrift::util::enumNameSafe(status);
      std::string statusTag = fmt::format("[{}]", statusName);

      const auto& detail = *check.message();
      auto checkName = apache::thrift::util::enumNameSafe(*check.checkId());
      table.addRow({statusTag, checkName, detail});
    }

    out << table;
  }

  out << std::endl;
}

std::string_view CmdShowBgpHealthTraits::description() {
  return "Runs the BGP++ daemon's built-in health checks and prints the result grouped by subsystem: global system (thrift reachability, RSS memory, CPU, planned exit), task/thread heartbeats, convergence, session manager, peer manager, RIB, netlink wrapper, nexthop tracker, FIB agent and thrift endpoint. The header gives the overall verdict and the PASS/WARN/FAIL/SKIP tally, followed by a legend and one table per subsystem showing each check's status, identifier and a detail string carrying the observed value and, where the check has one, the threshold it was compared against. SKIP means the check is not applicable on this switch or is not yet implemented, so a skipped check is not a failure. Modules with no checks are omitted entirely. Use this as a single-shot triage of daemon health before digging into individual sessions or the RIB.";
}

CmdShowBgpHealth::RetType CmdShowBgpHealth::sampleModel() {
  auto check = [](HealthCheckId id,
                  HealthCheckCategory category,
                  HealthCheckStatus status,
                  const std::string& message) {
    THealthCheckResult result;
    result.checkId() = id;
    result.category() = category;
    result.status() = status;
    result.message() = message;
    return result;
  };

  TModuleHealthReport globalSystem;
  globalSystem.category() = HealthCheckCategory::GLOBAL_SYSTEM;
  globalSystem.checks() = {
      check(
          HealthCheckId::GLOBAL_SYSTEM_THRIFT_REACHABLE,
          HealthCheckCategory::GLOBAL_SYSTEM,
          HealthCheckStatus::PASS,
          "daemon alive, uptime = 39702s"),
      check(
          HealthCheckId::GLOBAL_SYSTEM_RSS_MEMORY,
          HealthCheckCategory::GLOBAL_SYSTEM,
          HealthCheckStatus::PASS,
          "rss = 444MB / 5120MB (8.7%)"),
      check(
          HealthCheckId::GLOBAL_SYSTEM_CPU_USAGE,
          HealthCheckCategory::GLOBAL_SYSTEM,
          HealthCheckStatus::PASS,
          "cpu = 2%")};

  TModuleHealthReport sessionManager;
  sessionManager.category() = HealthCheckCategory::SESSION_MANAGER;
  sessionManager.checks() = {
      check(
          HealthCheckId::SESSION_PORT_179,
          HealthCheckCategory::SESSION_MANAGER,
          HealthCheckStatus::PASS,
          "Port 179 is listening"),
      check(
          HealthCheckId::SESSION_ESTABLISHED,
          HealthCheckCategory::SESSION_MANAGER,
          HealthCheckStatus::PASS,
          "runningSessions = 18"),
      check(
          HealthCheckId::SESSION_FLAPS,
          HealthCheckCategory::SESSION_MANAGER,
          HealthCheckStatus::PASS,
          "sessionFlaps(10min) = 0 (threshold = 9, 50% of 18 sessions)"),
      check(
          HealthCheckId::SESSION_HOLD_TIMER_EXPIRY,
          HealthCheckCategory::SESSION_MANAGER,
          HealthCheckStatus::SKIPPED,
          "Per-peer hold timer check deferred (needs session iteration)")};

  TModuleHealthReport peerManager;
  peerManager.category() = HealthCheckCategory::PEER_MANAGER;
  peerManager.checks() = {
      check(
          HealthCheckId::PEER_ZERO_ROUTES,
          HealthCheckCategory::PEER_MANAGER,
          HealthCheckStatus::PASS,
          "peersWithNoRouteExchange = 0"),
      // Deliberately degraded so the example also shows how a WARN row and a
      // module in a non-PASS state render. The capture this sample is drawn
      // from was entirely healthy, so this row is constructed.
      check(
          HealthCheckId::PEER_POLICY_CACHE,
          HealthCheckCategory::PEER_MANAGER,
          HealthCheckStatus::WARN,
          "hit=18422 miss=9107 rate=0.67 (threshold = 0.90)")};

  // Worst status wins, so a module header can never contradict its own rows.
  auto worstStatus = [](const std::vector<THealthCheckResult>& checks) {
    auto verdict = HealthCheckStatus::PASS;
    for (const auto& result : checks) {
      switch (*result.status()) {
        case HealthCheckStatus::FAIL:
          return HealthCheckStatus::FAIL;
        case HealthCheckStatus::WARN:
          verdict = HealthCheckStatus::WARN;
          break;
        case HealthCheckStatus::PASS:
        case HealthCheckStatus::SKIPPED:
        case HealthCheckStatus::UNKNOWN:
          break;
      }
    }
    return verdict;
  };
  globalSystem.overallStatus() = worstStatus(*globalSystem.checks());
  sessionManager.overallStatus() = worstStatus(*sessionManager.checks());
  peerManager.overallStatus() = worstStatus(*peerManager.checks());

  RetType report;
  report.modules() = {globalSystem, sessionManager, peerManager};
  report.timestampMs() = 1788455780000;

  /*
   * Count the checks rather than hardcoding the tally, so an edit to the
   * sample above cannot leave the header contradicting its own tables.
   * UNKNOWN has no counter in the thrift struct, so the sample must not
   * contain one - printOutput would list the row but leave it out of the
   * total. The test asserts the tally equals the listed checks, which is what
   * catches that.
   */
  int32_t passCount = 0;
  int32_t warnCount = 0;
  int32_t failCount = 0;
  int32_t skipCount = 0;
  for (const auto& mod : *report.modules()) {
    for (const auto& result : *mod.checks()) {
      switch (*result.status()) {
        case HealthCheckStatus::PASS:
          ++passCount;
          break;
        case HealthCheckStatus::WARN:
          ++warnCount;
          break;
        case HealthCheckStatus::FAIL:
          ++failCount;
          break;
        case HealthCheckStatus::SKIPPED:
          ++skipCount;
          break;
        case HealthCheckStatus::UNKNOWN:
          break;
      }
    }
  }
  report.passCount() = passCount;
  report.warnCount() = warnCount;
  report.failCount() = failCount;
  report.skipCount() = skipCount;
  // Worst status wins here too: a FAIL must not render "Overall: PASS".
  report.overallStatus() = failCount > 0 ? HealthCheckStatus::FAIL
      : warnCount > 0                    ? HealthCheckStatus::WARN
                                         : HealthCheckStatus::PASS;
  return report;
}

template void CmdHandler<CmdShowBgpHealth, CmdShowBgpHealthTraits>::run();

} // namespace facebook::fboss
