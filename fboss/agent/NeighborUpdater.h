/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <boost/container/flat_map.hpp>
#include <list>
#include <mutex>
#include <string>
#include <utility>
#include "fboss/agent/ArpCache.h"
#include "fboss/agent/NdpCache.h"
#include "fboss/agent/NeighborUpdaterImpl.h"
#include "fboss/agent/NeighborUpdaterNoopImpl.h"
#include "fboss/agent/StateObserver.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/types.h"

DECLARE_bool(disable_neighbor_updates);

namespace facebook::fboss {

class SwitchState;
class StateDelta;

using NeighborUpdaterVariant =
    std::variant<NeighborUpdaterImpl, NeighborUpdaterNoopImpl>;

/**
 * This class handles all updates to neighbor entries. Whenever we perform an
 * action or receive a packet that should update the Arp or Ndp tables in the
 * SwitchState, it goes through this class first, which then schedules some
 * work on the neighbor thread using the implementation methods present in
 * `NeighborUpdaterImpl`.
 *
 * Most methods in this class return a `folly::Future<>` which the caller can
 * use to wait for completion of the underlying execution.
 *
 * This class implements the StateObserver API and listens for all vlan added
 * or deleted events. It ignores all changes it receives related to arp/ndp
 * tables, as all those changes should originate from the caches stored in this
 * class. Everything else is forwarded to the `Impl` class on the neighbor
 * thread.
 */
class NeighborUpdater : public StateObserver {
  // Enable access to caches from NeighborUpdater to help imitate neighbor
  // learning during Agent HW testing.
  friend class AgentHwTest;

 private:
  std::shared_ptr<NeighborUpdaterVariant> impl_;
  SwSwitch* sw_{nullptr};

 public:
  NeighborUpdater(SwSwitch* sw, bool disableImpl);
  explicit NeighborUpdater(SwSwitch* sw);
  ~NeighborUpdater() override;

  void waitForPendingUpdates();

  void stateUpdated(const StateDelta& delta) override;

  void processInterfaceUpdates(const StateDelta& stateDelta);
  void processVlanUpdates(const StateDelta& stateDelta);

  // Zero-cost forwarders. See comment in NeighborUpdater-defs.h.
#define NEIGHBOR_UPDATER_METHOD(VISIBILITY, NAME, RETURN_TYPE, PARAMS)  \
  VISIBILITY:                                                           \
  template <typename... Args>                                           \
  folly::Future<folly::lift_unit_t<RETURN_TYPE>> NAME(Args&&... args) { \
    return folly::via(                                                  \
        sw_->getNeighborCacheEvb(),                                     \
        [impl = this->impl_,                                            \
         ... capturedArgs =                                             \
             std::forward<Args>(args)]() mutable -> RETURN_TYPE {       \
          return std::visit(                                            \
              [&](auto& updater) -> RETURN_TYPE {                       \
                return updater.NAME(std::move(capturedArgs)...);        \
              },                                                        \
              *impl);                                                   \
        });                                                             \
  }
#include "fboss/agent/NeighborUpdater-defs.h"
#undef NEIGHBOR_UPDATER_METHOD

 public:
  template <typename AddrT>
  void updateEntryClassID(
      VlanID vlan,
      AddrT ip,
      std::optional<cfg::AclLookupClass> classID = std::nullopt) {
    // VlanID always numerically equals the InterfaceID. Thus, cast
    // VlanID as InterfaceID and use it.
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      updateArpEntryClassIDForIntf(static_cast<InterfaceID>(vlan), ip, classID);
    } else {
      updateNdpEntryClassIDForIntf(static_cast<InterfaceID>(vlan), ip, classID);
    }
  }

 private:
  void portChanged(
      const std::shared_ptr<Port>& oldPort,
      const std::shared_ptr<Port>& newPort);
  void aggregatePortChanged(
      const std::shared_ptr<AggregatePort>& oldAggPort,
      const std::shared_ptr<AggregatePort>& newAggPort);
  void aggregatePortRemoved(const std::shared_ptr<AggregatePort>& oldAggPort);

  // TODO(skhare) Remove after completely migrating to intfCaches_
  void sendNeighborUpdates(const VlanDelta& delta);

  void sendNeighborUpdatesForIntf(const InterfaceDelta& delta);

  // Forbidden copy constructor and assignment operator
  NeighborUpdater(NeighborUpdater const&) = delete;
  NeighborUpdater& operator=(NeighborUpdater const&) = delete;
};

} // namespace facebook::fboss
