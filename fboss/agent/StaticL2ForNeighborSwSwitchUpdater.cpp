/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/StaticL2ForNeighborSwSwitchUpdater.h"

#include <tuple>

#include "fboss/agent/MacTableUtils.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/state/ArpEntry.h"
#include "fboss/agent/state/ArpTable.h"
#include "fboss/agent/state/NdpEntry.h"
#include "fboss/agent/state/NdpTable.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

namespace {

std::vector<folly::IPAddress> reachableNeighborsWithMac(
    const std::shared_ptr<SwitchState>& state,
    VlanID vlan,
    folly::MacAddress mac) {
  std::vector<folly::IPAddress> ips;
  auto collect = [&](const auto& nbrTable) {
    for (const auto& [ip, nbr] : std::as_const(*nbrTable)) {
      if (nbr->isReachable() && nbr->getMac() == mac) {
        ips.emplace_back(nbr->getIP());
      }
    }
  };
  collect(getNeighborTableForVlan<ArpTable>(state, vlan));
  collect(getNeighborTableForVlan<NdpTable>(state, vlan));
  return ips;
}

// Whether re-adding `mac` (state -> newState) stays within the per-port MAC
// limit and passes the resource accountant. The dry run must start from the
// applied state, which is what the accountant's counts reflect.
bool reAddFitsLimits(
    SwSwitch* sw,
    const std::shared_ptr<SwitchState>& state,
    const std::shared_ptr<SwitchState>& newState,
    VlanID vlan,
    folly::MacAddress mac) {
  const auto port = newState->getVlans()
                        ->getNode(vlan)
                        ->getMacTable()
                        ->getNode(mac.toString())
                        ->getPort();
  if (port.isPhysicalPort()) {
    if (const auto portNode = state->getPorts()->getNodeIf(port.phyPortID())) {
      const auto limit = MacTableUtils::getMaxL2EntriesPerPort(
          state, portNode, *sw->getScopeResolver());
      if (limit &&
          MacTableUtils::exceedsMacLimit(state, port, vlan, mac, *limit)) {
        return false;
      }
    }
  }
  return sw->isValidStateUpdateDryRun(
      StateDelta(sw->getState(), newState), sw->stats());
}

} // namespace

StaticL2ForNeighborSwSwitchUpdater::StaticL2ForNeighborSwSwitchUpdater(
    SwSwitch* sw)
    : StaticL2ForNeighborUpdater(), sw_(sw) {}

template <typename NeighborEntryT>
std::shared_ptr<SwitchState> StaticL2ForNeighborSwSwitchUpdater::ensureMacEntry(
    std::shared_ptr<SwitchState> state,
    VlanID vlan,
    const std::shared_ptr<NeighborEntryT>& neighbor) {
  CHECK(neighbor->isReachable());
  return MacTableUtils::updateOrAddStaticEntry(
      state, neighbor->getPort(), vlan, neighbor->getMac());
}

template <typename NeighborEntryT>
std::shared_ptr<SwitchState> StaticL2ForNeighborSwSwitchUpdater::pruneMacEntry(
    std::shared_ptr<SwitchState> state,
    VlanID vlanId,
    const std::shared_ptr<NeighborEntryT>& removedEntry) {
  XLOG(DBG2) << " Neighbor entry removed: " << removedEntry->str();
  auto mac = removedEntry->getMac();
  // Note that its possible that other neighbors still refer to this MAC.
  // Handle this in 2 steps
  // - Remove MAC
  // - Run ensureMacEntryIfNeighborExists
  // State passed down to HW is the composition of these 2 steps
  auto newState = MacTableUtils::removeEntry(state, vlanId, mac);
  if (newState != state) {
    // Check if another neighbor still points to this MAC
    newState = MacTableUtils::updateOrAddStaticEntryIfNbrExists(
        newState, vlanId, mac, removedEntry->getClassID());
  }
  return newState;
}

template <typename NeighborEntryT>
void StaticL2ForNeighborSwSwitchUpdater::ensureMacEntry(
    VlanID vlan,
    const std::shared_ptr<NeighborEntryT>& neighbor) {
  CHECK(neighbor->isReachable());
  auto mac = neighbor->getMac();
  auto port = neighbor->getPort();
  auto staticMacEntryFn =
      [vlan, mac, port](const std::shared_ptr<SwitchState>& state) {
        auto newState =
            MacTableUtils::updateOrAddStaticEntry(state, port, vlan, mac);
        return newState != state ? newState : nullptr;
      };

  sw_->updateState(
      "updateOrAdd static MAC: " + neighbor->str(),
      std::move(staticMacEntryFn));
}

void StaticL2ForNeighborSwSwitchUpdater::ensureMacEntryForNeighbor(
    VlanID vlan,
    const std::shared_ptr<NdpEntry>& neighbor) {
  ensureMacEntry(vlan, neighbor);
}

void StaticL2ForNeighborSwSwitchUpdater::ensureMacEntryForNeighbor(
    VlanID vlan,
    const std::shared_ptr<ArpEntry>& neighbor) {
  ensureMacEntry(vlan, neighbor);
}

template <typename NeighborEntryT>
void StaticL2ForNeighborSwSwitchUpdater::pruneMacEntry(
    VlanID vlanId,
    const std::shared_ptr<NeighborEntryT>& removedEntry) {
  XLOG(DBG2) << " Neighbor entry removed: " << removedEntry->str();
  auto mac = removedEntry->getMac();
  auto classID = removedEntry->getClassID();
  auto removeMacEntryFn =
      [vlanId, mac, classID](const std::shared_ptr<SwitchState>& state) {
        // Note that its possible that other neighbors still refer to this MAC.
        // Handle this in 2 steps
        // - Remove MAC
        // - Run ensureMacEntryIfNeighborExists
        // State passed down to HW is the composition of these 2 steps
        bool macPruned = false;
        auto newState = MacTableUtils::removeEntry(state, vlanId, mac);
        if (newState != state) {
          // Check if another neighbor still points to this MAC
          newState = MacTableUtils::updateOrAddStaticEntryIfNbrExists(
              newState, vlanId, mac, classID);
          auto vlan = newState->getVlans()->getNodeIf(vlanId);
          macPruned = vlan->getMacTable()->getMacIf(mac) == nullptr;
        }
        return macPruned ? newState : nullptr;
      };

  sw_->updateState(
      "Prune MAC if unreferenced: " + removedEntry->str(),
      std::move(removeMacEntryFn));
}

void StaticL2ForNeighborSwSwitchUpdater::pruneMacEntryForNeighbor(
    VlanID vlan,
    const std::shared_ptr<ArpEntry>& neighbor) {
  pruneMacEntry(vlan, neighbor);
}

void StaticL2ForNeighborSwSwitchUpdater::pruneMacEntryForNeighbor(
    VlanID vlan,
    const std::shared_ptr<NdpEntry>& neighbor) {
  pruneMacEntry(vlan, neighbor);
}

void StaticL2ForNeighborSwSwitchUpdater::ensureMacEntryIfNeighborExists(
    VlanID vlan,
    const std::shared_ptr<MacEntry>& macEntry) {
  auto mac = macEntry->getMac();
  // This update is not HW failure protected, so a re-add that the resource
  // accountant rejects would be fatal. If the MAC doesn't fit, flush the
  // neighbors that use it instead: the neighbor cache re-resolves them and
  // re-adds the MAC through its own update, which applies the limits.
  auto ensureMac =
      [sw = sw_, mac, vlan](const std::shared_ptr<SwitchState>& state)
      -> std::shared_ptr<SwitchState> {
    auto newState =
        MacTableUtils::updateOrAddStaticEntryIfNbrExists(state, vlan, mac);
    if (newState == state) {
      return nullptr;
    }
    if (reAddFitsLimits(sw, state, newState, vlan, mac)) {
      return newState;
    }
    XLOG_EVERY_MS(ERR, 5000)
        << "Not re-adding static MAC " << mac << " on VLAN " << vlan
        << ", flushing its neighbors to re-resolve";
    sw->stats()->neighborStaticMacReaddRejected();
    for (const auto& ip : reachableNeighborsWithMac(state, vlan, mac)) {
      // Not waited on: the flush runs on the neighbor cache thread and may
      // block on this (the update) thread.
      std::ignore = sw->getNeighborUpdater()->flushEntryForIntf(
          static_cast<InterfaceID>(vlan), ip);
    }
    return nullptr;
  };
  sw_->updateState(
      "ensure static MAC for nbr: " + macEntry->str(), std::move(ensureMac));
}

bool StaticL2ForNeighborSwSwitchUpdater::needL2EntryForNeighbor() const {
  return sw_->needL2EntryForNeighbor();
}

template std::shared_ptr<SwitchState>
StaticL2ForNeighborSwSwitchUpdater::ensureMacEntry(
    std::shared_ptr<SwitchState> state,
    VlanID vlan,
    const std::shared_ptr<NdpEntry>& neighbor);

template std::shared_ptr<SwitchState>
StaticL2ForNeighborSwSwitchUpdater::ensureMacEntry(
    std::shared_ptr<SwitchState> state,
    VlanID vlan,
    const std::shared_ptr<ArpEntry>& neighbor);

template std::shared_ptr<SwitchState>
StaticL2ForNeighborSwSwitchUpdater::pruneMacEntry(
    std::shared_ptr<SwitchState> state,
    VlanID vlanId,
    const std::shared_ptr<NdpEntry>& removedEntry);

template std::shared_ptr<SwitchState>
StaticL2ForNeighborSwSwitchUpdater::pruneMacEntry(
    std::shared_ptr<SwitchState> state,
    VlanID vlanId,
    const std::shared_ptr<ArpEntry>& removedEntry);

} // namespace facebook::fboss
