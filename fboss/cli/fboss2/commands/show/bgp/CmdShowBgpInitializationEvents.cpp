/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/CmdShowBgpInitializationEvents.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include <string>

#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {

CmdShowBgpInitializationEvents::RetType
CmdShowBgpInitializationEvents::queryClient(const HostInfo& hostInfo) {
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

  // Get initialization events and their timestamps
  RetType events;
  client->sync_getInitializationEvents(events);

  return events;
}

void CmdShowBgpInitializationEvents::printOutput(
    const RetType& events,
    std::ostream& out) {
  using facebook::fboss::utils::Table;

  // prevent printing number with comma, e.g: 65,000
  out.imbue(std::locale("C"));

  // Check if BGP has converged based on the presence of INITIALIZED event
  bool converged =
      events.find(
          neteng::fboss::bgp::thrift::BgpInitializationEvent::INITIALIZED) !=
      events.end();
  out << "BGP Initialization Converged: " << (converged ? "Yes" : "No")
      << std::endl
      << std::endl;

  Table table;
  table.setHeader({"Event", "Status", "Time From Start"});

  // Track which events actually occurred (have timestamps)
  std::set<neteng::fboss::bgp::thrift::BgpInitializationEvent> completedEvents;
  int32_t highestCompletedEvent = -1;
  for (const auto& [event, timestamp] : events) {
    if (timestamp > 0) {
      completedEvents.insert(event);
      int32_t eventValue = static_cast<int32_t>(event);
      if (eventValue > highestCompletedEvent) {
        highestCompletedEvent = eventValue;
      }
    }
  }

  // Get all enum values dynamically
  const auto& enumValues = apache::thrift::TEnumTraits<
      neteng::fboss::bgp::thrift::BgpInitializationEvent>::values;

  // Add rows to the table for each event
  for (const auto& enumValue : enumValues) {
    auto event =
        static_cast<neteng::fboss::bgp::thrift::BgpInitializationEvent>(
            enumValue);
    std::string eventName = apache::thrift::util::enumNameSafe(event);
    std::string status;
    if (completedEvents.count(event)) {
      status = "Complete";
    } else if (converged) {
      // BGP has converged, so this event was skipped in this initialization
      // path
      status = "Skipped";
    } else if (static_cast<int32_t>(enumValue) < highestCompletedEvent) {
      // BGP has progressed past this event, so it was skipped
      status = "Skipped";
    } else {
      // BGP is still initializing and hasn't reached this event yet
      status = "Pending";
    }

    std::string duration = "-";

    if (events.count(event) && events.at(event) > 0) {
      // Use BGP service timestamp directly (already represents time from
      // start)
      int64_t durationMs = events.at(event);

      // Format duration with millisecond precision
      if (durationMs == 0) {
        duration = "0ms";
      } else {
        int64_t totalSeconds = durationMs / 1000;
        int64_t milliseconds = durationMs % 1000;

        int64_t hours = totalSeconds / 3600;
        int64_t minutes = (totalSeconds % 3600) / 60;
        int64_t seconds = totalSeconds % 60;

        if (hours > 0) {
          duration = fmt::format(
              "{}h {}m {}s {}ms", hours, minutes, seconds, milliseconds);
        } else if (minutes > 0) {
          duration =
              fmt::format("{}m {}s {}ms", minutes, seconds, milliseconds);
        } else if (seconds > 0) {
          duration = fmt::format("{}s {}ms", seconds, milliseconds);
        } else {
          duration = fmt::format("{}ms", milliseconds);
        }
      }
    }

    table.addRow({eventName, status, duration});
  }

  out << table << std::endl;
}

std::string_view CmdShowBgpInitializationEventsTraits::description() {
  return "Displays the BGP++ daemon's startup timeline: every initialization milestone in enum order, whether it completed, and how long after process start it happened. The header line says whether initialization converged, which is true once the INITIALIZED milestone is reached. An event shows 'Complete' when the daemon recorded a timestamp for it, 'Skipped' when the daemon took a path that bypasses it (for example EOR_TIMER_EXPIRED on a switch that received all end-of-RIB markers naturally, or FSDB_SUBSCRIBED where nexthop tracking is not in use), and 'Pending' when initialization is still in flight and has not reached that milestone yet. Skipped milestones have no timestamp and render '-'. Use this to answer how long a start took and which phase dominated it - the gaps between consecutive timestamps are the per-phase costs. This is the timeline of whatever initialization the daemon last performed; it is not specific to a cold start.";
}

CmdShowBgpInitializationEvents::RetType
CmdShowBgpInitializationEvents::sampleModel() {
  using neteng::fboss::bgp::thrift::BgpInitializationEvent;

  // Milliseconds since process start. FSDB_SUBSCRIBED and EOR_TIMER_EXPIRED are
  // deliberately absent: this switch received all end-of-RIB markers naturally,
  // so printOutput renders them as "Skipped".
  RetType events;
  events[BgpInitializationEvent::INITIALIZING] = 1;
  events[BgpInitializationEvent::AGENT_CONFIGURED] = 2;
  events[BgpInitializationEvent::PEER_INFO_LOADED] = 400;
  events[BgpInitializationEvent::ALL_EOR_RECEIVED] = 1571;
  events[BgpInitializationEvent::RIB_COMPUTED] = 1766;
  events[BgpInitializationEvent::FIB_SYNCED] = 1908;
  events[BgpInitializationEvent::EOR_SENT] = 1910;
  events[BgpInitializationEvent::INITIALIZED] = 2127;
  return events;
}

template void CmdHandler<
    CmdShowBgpInitializationEvents,
    CmdShowBgpInitializationEventsTraits>::run();

} // namespace facebook::fboss
