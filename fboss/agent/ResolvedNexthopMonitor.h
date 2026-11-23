// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/StateObserver.h"

#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/state/Route.h"

namespace facebook::fboss {

class SwSwitch;
class StateDelta;

class ResolvedNexthopMonitor : public StateObserver {
  /*
   * under certain topologies and routing protocols, resolved l3 next hop may
   * not be l2 resolved i.e. a routing protocol may indicate next hop x is
   * reachable or directly connected via interface y, but arp/ndp mechanism may
   * never trigger to discover this next hop. arp/ndp mechanism triggers only
   * when packet directed towards neighbor is trapped in cpu and
   * solicitations/probes to that neighbor are generated. but if that next hop
   * is part of "multipath" next hop, it is "skipped" from getting programmed in
   * hardware and so packets to that next hop are never trapped. further routing
   * protocol agents may not use unicast communication method. this will leave
   * resolved l3 next hop's neighbor entries unresolved until there is a
   * specific trigger (often external such as ping) to initiate ndp/arp
   * discovery. to work around such cases, resolved next hop monitor schedules
   * probes to each resolved next hop. a probe gets  added when  a new resolved
   * l3 next hop gets added through either new or changed  resolved route is
   * added or changed. a probes gets removed when a resolved l3 next hop is no
   * longer referenced by any resolved route. a probe gets "scheduled" when
   * neighbor entry does not exist for corresponding l3 resolved next hop a
   * probe gets "unscheduled" when neighbor entry exists (even if pending) for
   * corresponding l3 resolved next hop
   * when a probe is scheduled it will keep generating arp or ndp requests to
   * corresponding next hop.
   */
 public:
  explicit ResolvedNexthopMonitor(SwSwitch* sw);
  ~ResolvedNexthopMonitor() override;
  void stateUpdated(const StateDelta& delta) override;

  bool probesScheduled() const {
    return scheduleProbes_;
  }

  static void updateMonitoredClients(std::set<ClientID> clients) {
    auto monitoredClients = kMonitoredClients.wlock();
    *monitoredClients = std::move(clients);
  }

 private:
  template <typename RouteT>
  void processAddedRouteNextHops(
      const std::shared_ptr<RouteT>& route,
      const std::shared_ptr<SwitchState>& state) {
    if (skipRoute(route)) {
      return;
    }
    const auto& fwd = route->getForwardInfo();
    for (auto nhop : getNormalizedNextHops(state, fwd)) {
      if (nhop.resolvedInNextLevel()) {
        continue;
      }
      added_.emplace_back(nhop.addr(), nhop.intf(), 0);
    }
  }

  template <typename RouteT>
  void processRemovedRouteNextHops(
      const std::shared_ptr<RouteT>& route,
      const std::shared_ptr<SwitchState>& state) {
    if (skipRoute(route)) {
      return;
    }
    const auto& fwd = route->getForwardInfo();
    for (auto nhop : getNormalizedNextHops(state, fwd)) {
      if (nhop.resolvedInNextLevel()) {
        continue;
      }
      removed_.emplace_back(nhop.addr(), nhop.intf(), 0);
    }
  }

  template <typename RouteT>
  void processChangedRouteNextHops(
      const std::shared_ptr<RouteT>& oldRoute,
      const std::shared_ptr<RouteT>& newRoute,
      const std::shared_ptr<SwitchState>& oldState,
      const std::shared_ptr<SwitchState>& newState) {
    processRemovedRouteNextHops(oldRoute, oldState);
    processAddedRouteNextHops(newRoute, newState);
  }

  template <typename RouteT>
  bool skipRoute(const std::shared_ptr<RouteT>& route) const {
    return !route->isResolved() ||
        route->getForwardInfo().getAction() !=
        RouteNextHopEntry::Action::NEXTHOPS ||
        !isMonitored(route);
  }

  template <typename RouteT>
  bool isMonitored(const std::shared_ptr<RouteT>& route) const {
    return isClientMonitored(route->getBestEntry().first);
  }

  bool isClientMonitored(ClientID clientId) const {
    {
      auto monitoredClientsLocked = kMonitoredClients.rlock();
      for (auto client : *monitoredClientsLocked) {
        if (clientId == client) {
          /* route is programmed by client and it's next hops are preferred */
          return true;
        }
      }
    }
    return false;
  }

  template <typename MapDeltaT>
  bool processNeighborDelta(const MapDeltaT& mapDelta);

  // added resolves against newState, removed against oldState.
  bool skipLabelFibEntry(
      const std::shared_ptr<LabelForwardingEntry>& entry,
      const std::shared_ptr<SwitchState>& state) const;
  void processChangedLabelFibEntry(
      const std::shared_ptr<LabelForwardingEntry>& oldEntry,
      const std::shared_ptr<LabelForwardingEntry>& newEntry,
      const std::shared_ptr<SwitchState>& oldState,
      const std::shared_ptr<SwitchState>& newState);
  void processAddedLabelFibEntry(
      const std::shared_ptr<LabelForwardingEntry>& addedEntry,
      const std::shared_ptr<SwitchState>& newState);
  void processRemovedLabelFibEntry(
      const std::shared_ptr<LabelForwardingEntry>& removedEntry,
      const std::shared_ptr<SwitchState>& oldState);

  SwSwitch* sw_{nullptr};
  std::vector<ResolvedNextHop> added_;
  std::vector<ResolvedNextHop> removed_;
  std::atomic_bool scheduleProbes_{false}; // trigger next hop probe scheduler
  static folly::Synchronized<std::set<ClientID>> kMonitoredClients;
};

} // namespace facebook::fboss
