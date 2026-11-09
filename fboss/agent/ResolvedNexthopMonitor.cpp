// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/ResolvedNexthopMonitor.h"

#include "fboss/agent/ResolvedNexthopProbeScheduler.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

#include <gflags/gflags.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

namespace {
using namespace facebook::fboss;

auto static constexpr kMonitorNexthopsForHelp =
    "comma separated list of ClientIDs. Default value is OPENR. "
    "Resolved next hops of routes programmed by these clients will be probed, "
    "if neighbor entry for these next hops is missing";

bool kValidateMonitorNextHopsFor(
    const char* /*flagname*/,
    const std::string& value) {
  std::vector<std::string> clientsStr;
  folly::split(',', value, clientsStr);
  using apache::thrift::util::tryParseEnum;

  std::set<ClientID> clients;
  for (const auto& clientStr : clientsStr) {
    ClientID client;
    if (!tryParseEnum(clientStr, &client)) {
      return false;
    }
    clients.insert(client);
  }
  ResolvedNexthopMonitor::updateMonitoredClients(std::move(clients));
  return true;
}

} // namespace

using facebook::fboss::DeltaFunctions::forEachAdded;
using facebook::fboss::DeltaFunctions::forEachChanged;
using facebook::fboss::DeltaFunctions::forEachRemoved;

/* monitoring only resolved next hops of select few clients, as opposed to
 * monitoring resolved next hops of all routes. this is to prevent flood of
 * probes in cases which has thousands of next hops which are l3 resolved but no
 * neighbor entries for such next hops exists e.g.  ILA static routes. */
DEFINE_string(monitor_nexthops_for, "OPENR", kMonitorNexthopsForHelp);
DEFINE_validator(monitor_nexthops_for, kValidateMonitorNextHopsFor);

namespace facebook::fboss {

folly::Synchronized<std::set<ClientID>>
    ResolvedNexthopMonitor::kMonitoredClients;

ResolvedNexthopMonitor::ResolvedNexthopMonitor(SwSwitch* sw) : sw_(sw) {
  sw_->registerStateObserver(this, "ResolvedNexthopMonitor");
}
ResolvedNexthopMonitor::~ResolvedNexthopMonitor() {
  sw_->unregisterStateObserver(this);
}

void ResolvedNexthopMonitor::stateUpdated(const StateDelta& delta) {
  scheduleProbes_ = false;
  auto oldState = delta.oldState();
  auto newState = delta.newState();
  forEachChangedRoute(
      delta,
      [this, &oldState, &newState](
          auto /*rid*/, const auto& oldRoute, const auto& newRoute) {
        processChangedRouteNextHops(oldRoute, newRoute, oldState, newState);
      },
      [this, &newState](auto /*rid*/, const auto& newRoute) {
        processAddedRouteNextHops(newRoute, newState);
      },
      [this, &oldState](auto /*rid*/, const auto& oldRoute) {
        processRemovedRouteNextHops(oldRoute, oldState);
      });
  forEachChanged(
      delta.getLabelForwardingInformationBaseDelta(),
      [this, &oldState, &newState](const auto& oldEntry, const auto& newEntry) {
        processChangedLabelFibEntry(oldEntry, newEntry, oldState, newState);
      },
      [this, &newState](const auto& addedEntry) {
        processAddedLabelFibEntry(addedEntry, newState);
      },
      [this, &oldState](const auto& removedEntry) {
        processRemovedLabelFibEntry(removedEntry, oldState);
      });

  if (processNeighborDelta(delta.getIntfsDelta())) {
    scheduleProbes_ = true;
  }

  if (!added_.empty() || !removed_.empty()) {
    scheduleProbes_ = true;
    sw_->getResolvedNexthopProbeScheduler()->processChangedResolvedNexthops(
        std::move(added_), std::move(removed_));
  }

  if (scheduleProbes_) {
    sw_->getResolvedNexthopProbeScheduler()->schedule();
  }
}

bool ResolvedNexthopMonitor::skipLabelFibEntry(
    const std::shared_ptr<LabelForwardingEntry>& entry,
    const std::shared_ptr<SwitchState>& state) const {
  if (entry->getForwardInfo().getAction() !=
      LabelNextHopEntry::Action::NEXTHOPS) {
    return true;
  }
  auto bestEntry = entry->getBestEntry();
  auto bestRouteNextHopEntry = bestEntry.second;
  for (auto nhop : getMplsClientNextHops(state, *bestRouteNextHopEntry)) {
    if (nhop.labelForwardingAction().has_value() &&
        nhop.labelForwardingAction()->type() ==
            LabelForwardingAction::LabelForwardingType::POP_AND_LOOKUP) {
      return true;
    }
  }
  return !isClientMonitored(bestEntry.first);
}

void ResolvedNexthopMonitor::processChangedLabelFibEntry(
    const std::shared_ptr<LabelForwardingEntry>& oldEntry,
    const std::shared_ptr<LabelForwardingEntry>& newEntry,
    const std::shared_ptr<SwitchState>& oldState,
    const std::shared_ptr<SwitchState>& newState) {
  processRemovedLabelFibEntry(oldEntry, oldState);
  processAddedLabelFibEntry(newEntry, newState);
}

void ResolvedNexthopMonitor::processAddedLabelFibEntry(
    const std::shared_ptr<LabelForwardingEntry>& addedEntry,
    const std::shared_ptr<SwitchState>& newState) {
  if (skipLabelFibEntry(addedEntry, newState)) {
    return;
  }
  const auto& fwd = addedEntry->getForwardInfo();
  for (const auto& nhop : getMplsNormalizedNextHops(newState, fwd)) {
    added_.emplace_back(nhop.addr(), nhop.intf(), 0);
  }
}

void ResolvedNexthopMonitor::processRemovedLabelFibEntry(
    const std::shared_ptr<LabelForwardingEntry>& removedEntry,
    const std::shared_ptr<SwitchState>& oldState) {
  if (skipLabelFibEntry(removedEntry, oldState)) {
    return;
  }
  const auto& fwd = removedEntry->getForwardInfo();
  for (const auto& nhop : getMplsNormalizedNextHops(oldState, fwd)) {
    removed_.emplace_back(nhop.addr(), nhop.intf(), 0);
  }
}

template <typename MapDeltaT>
bool ResolvedNexthopMonitor::processNeighborDelta(const MapDeltaT& mapDelta) {
  bool scheduleProbes = false;

  for (const auto& delta : mapDelta) {
    auto arpDelta = delta.getArpDelta();
    auto ndpDelta = delta.getNdpDelta();
    if (arpDelta.getNew() || arpDelta.getOld() || ndpDelta.getNew() ||
        ndpDelta.getOld()) {
      scheduleProbes = true;
      break;
    }
  }

  return scheduleProbes;
}

} // namespace facebook::fboss
