/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/NeighborUpdater.h"
#include <boost/container/flat_map.hpp>
#include <folly/logging/xlog.h>
#include <string>
#include <vector>
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/StateDelta.h"

using boost::container::flat_map;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using std::shared_ptr;

DEFINE_bool(
    disable_neighbor_updates,
    false,
    "Disable neighbor updater in agent");

namespace facebook::fboss {

using facebook::fboss::DeltaFunctions::forEachChanged;

NeighborUpdater::NeighborUpdater(SwSwitch* sw, bool disableImpl) : sw_(sw) {
  sw_->registerStateObserver(this, "NeighborUpdater");
  // for agent tests, we want to disable the neighbor updater
  // functionality. Simplest way to do so is to create stub version
  // of NeighborUpdaterImpl i.e. NeighborUpdaterNoopImpl
  // and invoke it with same interface across the code
  if (disableImpl) {
    XLOG(DBG2) << "Disable neighbor update implementation";
    impl_ = std::make_shared<NeighborUpdaterVariant>(
        std::in_place_type_t<NeighborUpdaterNoopImpl>(), sw);
  } else {
    impl_ = std::make_shared<NeighborUpdaterVariant>(
        std::in_place_type_t<NeighborUpdaterImpl>(), sw);
  }
}

NeighborUpdater::NeighborUpdater(SwSwitch* sw)
    : NeighborUpdater(sw, FLAGS_disable_neighbor_updates) {};

NeighborUpdater::~NeighborUpdater() {
  sw_->unregisterStateObserver(this);
  // we make sure to destroy the NeighborUpdaterImpl on the neighbor
  // cache thread to avoid racing between background entry processing
  // and destruction.

  sw_->getNeighborCacheEvb()->runImmediatelyOrRunInFbossEventBaseThreadAndWait(
      [this]() mutable { return this->impl_.reset(); });
}

void NeighborUpdater::waitForPendingUpdates() {
  folly::via(sw_->getNeighborCacheEvb(), [impl = this->impl_]() {}).get();
}

void NeighborUpdater::processInterfaceUpdates(const StateDelta& stateDelta) {
  for (const auto& delta : stateDelta.getIntfsDelta()) {
    sendNeighborUpdatesForIntf(delta);
    auto oldInterface = delta.getOld();
    auto newInterface = delta.getNew();

    if (!oldInterface && newInterface) {
      interfaceAdded(newInterface->getID(), stateDelta.newState());
    } else if (oldInterface && !newInterface) {
      interfaceRemoved(oldInterface->getID());
    } else {
      // From the neighbor cache perspective, we only care about the
      // interfaceID which cannot change for a given interface. Thus, no need
      // to process change to interface
      ;
    }
  }
}

void NeighborUpdater::processVlanUpdates(const StateDelta& stateDelta) {
  for (const auto& entry : stateDelta.getVlansDelta()) {
    sendNeighborUpdates(entry);
    auto oldEntry = entry.getOld();
    auto newEntry = entry.getNew();

    if (!newEntry) {
      vlanDeleted(oldEntry->getID());
    } else if (!oldEntry) {
      vlanAdded(newEntry->getID(), stateDelta.newState());
    } else {
      if (newEntry->getInterfaceID() != oldEntry->getInterfaceID() ||
          newEntry->getName() != oldEntry->getName()) {
        // For now we only care about changes to the interfaceID and VlanName
        vlanChanged(
            newEntry->getID(), newEntry->getInterfaceID(), newEntry->getName());
      }
    }
  }
}

void NeighborUpdater::stateUpdated(const StateDelta& delta) {
  CHECK(sw_->getUpdateEvb()->inRunningEventBaseThread());

  processInterfaceUpdates(delta);

  const auto& oldState = delta.oldState();
  const auto& newState = delta.newState();
  if (oldState->getArpTimeout() != newState->getArpTimeout() ||
      oldState->getNdpTimeout() != newState->getNdpTimeout() ||
      oldState->getMaxNeighborProbes() != newState->getMaxNeighborProbes() ||
      oldState->getStaleEntryInterval() != newState->getStaleEntryInterval()) {
    timeoutsChanged(
        newState->getArpTimeout(),
        newState->getNdpTimeout(),
        newState->getStaleEntryInterval(),
        newState->getMaxNeighborProbes());
  }

  forEachChanged(delta.getPortsDelta(), &NeighborUpdater::portChanged, this);
  forEachChanged(
      delta.getAggregatePortsDelta(),
      &NeighborUpdater::aggregatePortChanged,
      [](NeighborUpdater*, const std::shared_ptr<AggregatePort>&) {},
      &NeighborUpdater::aggregatePortRemoved,
      this);
}

template <typename T>
void collectPresenceChange(
    const T& delta,
    std::vector<std::string>* added,
    std::vector<std::string>* deleted) {
  for (const auto& entry : delta) {
    auto oldEntry = entry.getOld();
    auto newEntry = entry.getNew();
    if (oldEntry && !newEntry) {
      if (oldEntry->nonZeroPort()) {
        deleted->push_back(folly::to<std::string>(oldEntry->getIP()));
      }
    } else if (newEntry && !oldEntry) {
      if (newEntry->nonZeroPort()) {
        added->push_back(folly::to<std::string>(newEntry->getIP()));
      }
    } else {
      if (oldEntry->zeroPort() && newEntry->nonZeroPort()) {
        // Entry was resolved, add it
        added->push_back(folly::to<std::string>(newEntry->getIP()));
      } else if (oldEntry->nonZeroPort() && newEntry->zeroPort()) {
        // Entry became unresolved, prune it
        deleted->push_back(folly::to<std::string>(oldEntry->getIP()));
      }
    }
  }
}

void NeighborUpdater::sendNeighborUpdates(const VlanDelta& delta) {
  std::vector<std::string> added;
  std::vector<std::string> deleted;
  collectPresenceChange(delta.getArpDelta(), &added, &deleted);
  collectPresenceChange(delta.getNdpDelta(), &added, &deleted);
  if (!(added.empty() && deleted.empty())) {
    sw_->invokeNeighborListener(added, deleted);
  }
}

void NeighborUpdater::sendNeighborUpdatesForIntf(const InterfaceDelta& delta) {
  std::vector<std::string> added;
  std::vector<std::string> deleted;
  collectPresenceChange(delta.getArpDelta(), &added, &deleted);
  collectPresenceChange(delta.getNdpDelta(), &added, &deleted);
  if (!(added.empty() && deleted.empty())) {
    sw_->invokeNeighborListener(added, deleted);
  }
}

void NeighborUpdater::portChanged(
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  if (oldPort->getOperState() == Port::OperState::UP &&
      newPort->getOperState() == Port::OperState::DOWN) {
    // Fire explicit callback for purging neighbor entries.
    CHECK_EQ(oldPort->getID(), newPort->getID());
    auto portId = newPort->getID();

    XLOG(DBG2) << "Purging neighbor entry for physical port " << portId;
    portDown(PortDescriptor(portId));
  }
}

void NeighborUpdater::aggregatePortRemoved(
    const std::shared_ptr<AggregatePort>& oldAggPort) {
  // Entries left on a removed aggregate can never be probed, and keep
  // re-adding a static MacEntry that names the dead LAG.
  XLOG(DBG2) << "Flushing neighbor entries for removed aggregate port "
             << oldAggPort->getID();
  portFlushEntries(PortDescriptor(oldAggPort->getID()));
}

void NeighborUpdater::aggregatePortChanged(
    const std::shared_ptr<AggregatePort>& oldAggPort,
    const std::shared_ptr<AggregatePort>& newAggPort) {
  CHECK_EQ(oldAggPort->getID(), newAggPort->getID());

  auto oldSubportRange = oldAggPort->sortedSubports();
  auto newSubport = newAggPort->sortedSubports();

  bool transitionedToDown = oldAggPort->isUp() && !newAggPort->isUp();
  bool membershipChanged = !std::equal(
      oldSubportRange.begin(), oldSubportRange.end(), newSubport.begin());

  if (transitionedToDown || membershipChanged) {
    auto aggregatePortID = newAggPort->getID();
    XLOG(DBG2) << "Purging neighbor entry for aggregate port "
               << aggregatePortID;
    portDown(PortDescriptor(aggregatePortID));
  }

  /*
   Subports of aggport might have stale neighbor entries.
   Flush them when LACP is established
   */
  if (!oldAggPort->isUp() && newAggPort->isUp()) {
    for (const auto& subPort : newAggPort->sortedSubports()) {
      portFlushEntries(PortDescriptor(subPort.portID));
    }
  }
}

} // namespace facebook::fboss
