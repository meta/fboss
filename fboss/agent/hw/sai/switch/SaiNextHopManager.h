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

#include "fboss/agent/hw/sai/api/NextHopApi.h"
#include "fboss/agent/hw/sai/api/RouterInterfaceApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6SidListManager.h"
#include "fboss/agent/state/LabelForwardingAction.h"
#include "fboss/agent/types.h"
#include "fboss/lib/RefMap.h"

#include "fboss/agent/hw/sai/store/SaiObjectEventSubscriber-defs.h"
#include "fboss/agent/hw/sai/store/SaiObjectEventSubscriber.h"

#include "folly/IPAddress.h"

#include <memory>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class ResolvedNextHop;
class SaiNextHopManager;
class SaiStore;

using SaiIpNextHop = SaiObject<SaiIpNextHopTraits>;
using SaiMplsNextHop = SaiObject<SaiMplsNextHopTraits>;
using SaiTunnelEncapNextHop = SaiObject<SaiTunnelEncapNextHopTraits>;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
using SaiSrv6SidlistNextHop = SaiObject<SaiSrv6SidlistNextHopTraits>;
#endif
using SaiNextHop = typename ConditionSaiObjectType<SaiNextHopTraits>::type;

template <typename NextHopTraits>
class ManagedNextHop : public SaiObjectEventAggregateSubscriber<
                           ManagedNextHop<NextHopTraits>,
                           NextHopTraits,
                           SaiNeighborTraits> {
 public:
  using ObjectTraits = NextHopTraits;
  using AdapterHostKey = typename NextHopTraits::AdapterHostKey;
  using NeighborWeakPtr = std::weak_ptr<const SaiObject<SaiNeighborTraits>>;
  using PublishedObjects = std::tuple<NeighborWeakPtr>;
  using Base = SaiObjectEventAggregateSubscriber<
      ManagedNextHop<NextHopTraits>,
      NextHopTraits,
      SaiNeighborTraits>;
  // TODO: Support next hop group as underlay as well as and when
  // that use case arises. Today, for underlay we only have a use
  // case for a single nhop
  using UnderlayNextHop = std::variant<
      std::shared_ptr<ManagedNextHop<SaiIpNextHopTraits>>,
      std::shared_ptr<ManagedNextHop<SaiMplsNextHopTraits>>
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      ,
      std::shared_ptr<ManagedNextHop<SaiSrv6SidlistNextHopTraits>>
#endif
      >;
  ManagedNextHop(
      SaiNextHopManager* manager,
      SaiNeighborTraits::NeighborEntry entry,
      const typename NextHopTraits::AdapterHostKey& key,
      std::optional<bool> disableTTLDecrement)
      : Base(entry),
        manager_(manager),
        key_(key),
        disableTTLDecrement_(disableTTLDecrement) {}

  ~ManagedNextHop() {
    this->resetObject();
  }

  void createObject(PublishedObjects /*added*/);

  void removeObject(size_t index, PublishedObjects removed) {
    XLOG(DBG2) << "ManagedNeighbor::removeObject: " << toString();
    /* when neighbor is removed remove next hop */
    this->resetObject();
  }

  void handleLinkDown() {
    this->resetObject();
  }

  typename NextHopTraits::AdapterHostKey adapterHostKey() const {
    return key_;
  }

  // The neighbor this next hop resolves through. ManagedNextHop subscribes to
  // SaiNeighborTraits, so the publisher key is exactly that neighbor entry.
  SaiNeighborTraits::NeighborEntry getNeighborEntry() const {
    return this->getPublisherKey();
  }

  std::string toString() const;

  void setDisableTTLDecrement(std::optional<bool> disableTTLDecrement);

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  void setSrv6SidListHandle(
      std::shared_ptr<SaiSrv6SidListHandle> srv6SidListHandle) {
    srv6SidListHandle_ = std::move(srv6SidListHandle);
  }

  const std::shared_ptr<SaiSrv6SidListHandle>& getSrv6SidListHandle() const {
    return srv6SidListHandle_;
  }
#endif

  void setUnderlayNextHop(std::optional<UnderlayNextHop> underlayNextHop) {
    underlayNextHop_ = std::move(underlayNextHop);
  }

  const std::optional<UnderlayNextHop>& getUnderlayNextHop() const {
    return underlayNextHop_;
  }

 private:
  SaiNextHopManager* manager_;
  typename NextHopTraits::AdapterHostKey key_;
  std::optional<bool> disableTTLDecrement_{};
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  std::shared_ptr<SaiSrv6SidListHandle> srv6SidListHandle_;
#endif
  std::optional<UnderlayNextHop> underlayNextHop_;
};

using ManagedIpNextHop = ManagedNextHop<SaiIpNextHopTraits>;
using ManagedMplsNextHop = ManagedNextHop<SaiMplsNextHopTraits>;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
using ManagedSrv6NextHop = ManagedNextHop<SaiSrv6SidlistNextHopTraits>;
#endif

using ManagedSaiNextHop = std::variant<
    std::shared_ptr<ManagedIpNextHop>,
    std::shared_ptr<ManagedMplsNextHop>
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    ,
    std::shared_ptr<ManagedSrv6NextHop>
#endif
    >;

/*
 * VXLAN tunnel next hop holder.
 *
 * A dedicated holder that creates the
 * SaiTunnelEncapNextHopTraits object eagerly through the store and
 * keeps it alive by shared ownership. The SDK resolves the underlay
 * ECMP path internally, so there is no neighbor and no underlay
 * sub-next-hop to wait for; tunnels are programmed before routes,
 * so the next hop is ready at creation.
 *
 * ManagedNextHop<SaiTunnelEncapNextHopTraits> is not reused: that
 * template is an aggregate subscriber on SaiNeighborTraits and its
 * createObject CHECKs that the neighbor is alive.
 */
class SaiVxlanNextHopHandle {
 public:
  using ObjectTraits = SaiTunnelEncapNextHopTraits;
  using AdapterHostKey = typename ObjectTraits::AdapterHostKey;

  SaiVxlanNextHopHandle(
      AdapterHostKey key,
      std::shared_ptr<SaiTunnelEncapNextHop> nextHop)
      : key_(std::move(key)), nextHop_(std::move(nextHop)) {}

  AdapterHostKey adapterHostKey() const {
    return key_;
  }

  std::shared_ptr<SaiTunnelEncapNextHop> getNextHop() const {
    return nextHop_;
  }

  sai_object_id_t adapterKey() const {
    return nextHop_->adapterKey();
  }

  std::string toString() const;

 private:
  AdapterHostKey key_;
  std::shared_ptr<SaiTunnelEncapNextHop> nextHop_;
};

class SaiNextHopManager {
 public:
  SaiNextHopManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform);
  std::shared_ptr<SaiIpNextHop> addNextHop(
      RouterInterfaceSaiId routerInterfaceId,
      const folly::IPAddress& ip);

  SaiNextHopTraits::AdapterHostKey getAdapterHostKey(
      const ResolvedNextHop& swNextHop,
      std::optional<sai_object_id_t> sidListId = std::nullopt);

  ManagedSaiNextHop addManagedSaiNextHop(
      const ResolvedNextHop& swNextHop
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      ,
      std::shared_ptr<SaiSrv6SidListHandle> srv6SidListHandle = nullptr
#endif
  );

  // Returns the eagerly created tunnel next hop.
  std::shared_ptr<SaiVxlanNextHopHandle> addVxlanNextHop(
      const ResolvedNextHop& swNextHop);

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  std::shared_ptr<SaiSrv6SidList> createSrv6SidList(
      const ResolvedNextHop& swNextHop);
#endif
  const ManagedIpNextHop* getManagedNextHop(
      const ManagedIpNextHop::AdapterHostKey& key) const {
    return managedIpNextHops_.get(key);
  }
  const ManagedMplsNextHop* getManagedNextHop(
      const ManagedMplsNextHop::AdapterHostKey& key) const {
    return managedMplsNextHops_.get(key);
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  const ManagedSrv6NextHop* getManagedNextHop(
      const ManagedSrv6NextHop::AdapterHostKey& key) const {
    return managedSrv6NextHops_.get(key);
  }
#endif
  const SaiVxlanNextHopHandle* getVxlanNextHop(
      const SaiVxlanNextHopHandle::AdapterHostKey& key) const {
    return vxlanNextHopHandles_.get(key);
  }

  template <typename NextHopTraits>
  std::shared_ptr<SaiObject<NextHopTraits>> createSaiObject(
      typename NextHopTraits::AdapterHostKey adapterHostKey,
      typename NextHopTraits::CreateAttributes attributes);

  std::string listManagedObjects() const;

  const SaiPlatform* getPlatform() const {
    return platform_;
  }

 private:
  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;

  UnorderedRefMap<typename ManagedIpNextHop::AdapterHostKey, ManagedIpNextHop>
      managedIpNextHops_;
  UnorderedRefMap<
      typename ManagedMplsNextHop::AdapterHostKey,
      ManagedMplsNextHop>
      managedMplsNextHops_;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  UnorderedRefMap<
      typename ManagedSrv6NextHop::AdapterHostKey,
      ManagedSrv6NextHop>
      managedSrv6NextHops_;
#endif
  UnorderedRefMap<
      typename SaiVxlanNextHopHandle::AdapterHostKey,
      SaiVxlanNextHopHandle>
      vxlanNextHopHandles_;
};

} // namespace facebook::fboss
