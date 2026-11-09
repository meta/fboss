// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/state/LabelForwardingInformationBase.h"
#include "fboss/agent/state/NodeMapDelta.h"

#include "fboss/agent/hw/sai/api/MplsApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/switch/SaiVirtualRouterManager.h"

#include "folly/container/F14Map.h"

#include <memory>
#include <mutex>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiStore;
class SaiInSegEntryManager;
class SwitchState;
struct SaiNextHopGroupHandle;

using SaiInSegEntry = SaiObject<SaiInSegTraits>;

template <typename T>
class ManagedNextHop;

template <typename NextHopTraitsT>
class ManagedInSegNextHop
    : public detail::SaiObjectEventSubscriber<NextHopTraitsT> {
 public:
  using PublisherObject = std::shared_ptr<const SaiObject<NextHopTraitsT>>;
  ManagedInSegNextHop(
      SaiInSegEntryManager* inSegEntryManager,
      SaiInSegTraits::AdapterHostKey inSegKey,
      std::shared_ptr<ManagedNextHop<NextHopTraitsT>> managedNextHop);
  void afterCreate(PublisherObject nexthop) override;
  void beforeRemove() override;
  void linkDown() override {}
  sai_object_id_t adapterKey() const;
  using detail::SaiObjectEventSubscriber<NextHopTraitsT>::isReady;

 private:
  SaiInSegEntryManager* inSegEntryManager_;
  const SaiPlatform* platform_;
  typename SaiInSegTraits::AdapterHostKey inSegKey_;
  std::shared_ptr<ManagedNextHop<NextHopTraitsT>> managedNextHop_;
};

using ManagedInSegIpNextHop = ManagedInSegNextHop<SaiIpNextHopTraits>;
using ManagedInSegMplsNextHop = ManagedInSegNextHop<SaiMplsNextHopTraits>;

struct SaiInSegEntryHandle {
  using NextHopHandle = std::variant<
      std::shared_ptr<SaiNextHopGroupHandle>,
      std::shared_ptr<ManagedInSegIpNextHop>,
      std::shared_ptr<ManagedInSegMplsNextHop>,
      std::shared_ptr<SaiMplsRouterInterface>>;
  NextHopHandle nexthopHandle;
  std::shared_ptr<SaiInSegEntry> inSegEntry;
  sai_object_id_t nextHopAdapterKey() const;
  std::shared_ptr<SaiNextHopGroupHandle> nextHopGroupHandle() const;
};

class SaiInSegEntryManager {
 public:
  SaiInSegEntryManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform)
      : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {}

  SaiInSegEntryHandle* getInSegEntryHandle(Label label);

  // newState resolves label nexthops from their IDs.
  void processAddedInSegEntry(
      const std::shared_ptr<Route<LabelID>>& addedEntry,
      const std::shared_ptr<SwitchState>& newState);
  void processChangedInSegEntry(
      const std::shared_ptr<Route<LabelID>>& oldEntry,
      const std::shared_ptr<Route<LabelID>>& newEntry,
      const std::shared_ptr<SwitchState>& newState);
  void processRemovedInSegEntry(
      const std::shared_ptr<Route<LabelID>>& removedEntry);

  std::shared_ptr<SaiObject<SaiInSegTraits>> getInSegObject(
      SaiInSegTraits::AdapterHostKey inSegKey);

 private:
  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
  folly::
      F14FastMap<typename SaiInSegTraits::AdapterHostKey, SaiInSegEntryHandle>
          saiInSegEntryTable_;
};
} // namespace facebook::fboss
