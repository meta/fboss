// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiInSegEntryManager.h"

#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/state/LabelForwardingAction.h"
#include "fboss/agent/state/SwitchState.h"

namespace {
using namespace facebook::fboss;
RouterID kDefaultRouterID = RouterID(0);
SaiInSegEntryHandle::NextHopHandle getNextHopHandle(
    SaiManagerTable* managerTable,
    const std::shared_ptr<LabelForwardingEntry>& swLabelFibEntry,
    const std::shared_ptr<SwitchState>& newState) {
  const auto nexthops =
      getMplsNextHops(newState, swLabelFibEntry->getForwardInfo());
  if (nexthops.size() > 0 &&
      nexthops.begin()->labelForwardingAction()->type() ==
          LabelForwardingAction::LabelForwardingType::POP_AND_LOOKUP) {
    // MPLS next hops or label fib entry for POP doesn't mention which VRF to
    // look at for POP. Following alternatives are possible.
    // 1. either label itself could be RouterID
    // 2. separate mapping can be provided between label to RouterID,
    // 3. RouterID can be computed from label using some
    // bitwise/logical/arithmatic operations.
    // 4. router ID can be part of MPLS route (ignored for all except
    // POP_AND_LOOKUP)
    // 5. POP_AND_LOOKUP next hop can carry router ID to look at.
    auto virtualRouterHandle =
        managerTable->virtualRouterManager().getVirtualRouterHandle(
            RouterID(kDefaultRouterID));
    return virtualRouterHandle->mplsRouterInterface;
  }
  if (nexthops.size() == 1) {
    SaiInSegTraits::InSegEntry inSegEntry{
        managerTable->switchManager().getSwitchSaiId(),
        static_cast<sai_label_id_t>(swLabelFibEntry->getID())};
    auto managedNextHop = managerTable->nextHopManager().addManagedSaiNextHop(
        folly::poly_cast<ResolvedNextHop>(*nexthops.begin()));
    if (auto* ipNextHop =
            std::get_if<std::shared_ptr<ManagedIpNextHop>>(&managedNextHop)) {
      auto nexthop = std::make_shared<ManagedInSegIpNextHop>(
          &managerTable->inSegEntryManager(), inSegEntry, *ipNextHop);
      SaiObjectEventPublisher::getInstance()
          ->get<SaiIpNextHopTraits>()
          .subscribe(nexthop);
      return nexthop;
    } else if (
        auto* mplsNextHop =
            std::get_if<std::shared_ptr<ManagedMplsNextHop>>(&managedNextHop)) {
      auto nexthop = std::make_shared<ManagedInSegMplsNextHop>(
          &managerTable->inSegEntryManager(), inSegEntry, *mplsNextHop);
      SaiObjectEventPublisher::getInstance()
          ->get<SaiMplsNextHopTraits>()
          .subscribe(nexthop);
      return nexthop;
    }
  }
  return managerTable->nextHopGroupManager().incRefOrAddNextHopGroup(
      SaiNextHopGroupKey(nexthops, std::nullopt));
}
} // namespace
namespace facebook::fboss {

void SaiInSegEntryManager::processAddedInSegEntry(
    const std::shared_ptr<LabelForwardingEntry>& addedEntry,
    const std::shared_ptr<SwitchState>& newState) {
  SaiInSegTraits::InSegEntry inSegEntry{
      managerTable_->switchManager().getSwitchSaiId(),
      static_cast<sai_label_id_t>(addedEntry->getID())};
  if (saiInSegEntryTable_.find(inSegEntry) != saiInSegEntryTable_.end()) {
    throw FbossError(
        "label fib entry already exists for ", addedEntry->getID());
  }
  auto iter = saiInSegEntryTable_.emplace(inSegEntry, SaiInSegEntryHandle{});
  auto& handle = iter.first->second;
  handle.nexthopHandle = getNextHopHandle(managerTable_, addedEntry, newState);
  SaiInSegTraits::CreateAttributes createAttributes{
      SAI_PACKET_ACTION_FORWARD, // not supporting any other action now except
                                 // forward
      1, // always pop 1 label even for swap and push another label
      handle.nextHopAdapterKey()};

  auto& store = saiStore_->get<SaiInSegTraits>();
  handle.inSegEntry = store.setObject(inSegEntry, createAttributes);
}

void SaiInSegEntryManager::processChangedInSegEntry(
    const std::shared_ptr<LabelForwardingEntry>& /*oldEntry*/,
    const std::shared_ptr<LabelForwardingEntry>& newEntry,
    const std::shared_ptr<SwitchState>& newState) {
  SaiInSegTraits::InSegEntry inSegEntry{
      managerTable_->switchManager().getSwitchSaiId(),
      static_cast<sai_label_id_t>(newEntry->getID())};
  auto itr = saiInSegEntryTable_.find(inSegEntry);
  if (itr == saiInSegEntryTable_.end()) {
    throw FbossError(
        "label fib entry already does not exist for ", newEntry->getID());
  }

  itr->second.nexthopHandle =
      getNextHopHandle(managerTable_, newEntry, newState);

  SaiInSegTraits::CreateAttributes newAttributes{
      SAI_PACKET_ACTION_FORWARD, 1, itr->second.nextHopAdapterKey()};
  auto& store = saiStore_->get<SaiInSegTraits>();
  itr->second.inSegEntry = store.setObject(inSegEntry, newAttributes);
}

void SaiInSegEntryManager::processRemovedInSegEntry(
    const std::shared_ptr<LabelForwardingEntry>& removedEntry) {
  auto inSegEntry = SaiInSegTraits::InSegEntry(
      managerTable_->switchManager().getSwitchSaiId(), removedEntry->getID());
  auto itr = saiInSegEntryTable_.find(inSegEntry);
  if (itr == saiInSegEntryTable_.end()) {
    throw FbossError(
        "label fib entry already does not exist for ", removedEntry->getID());
  }
  saiInSegEntryTable_.erase(inSegEntry);
}

SaiInSegEntryHandle* SaiInSegEntryManager::getInSegEntryHandle(Label label) {
  auto inSegEntry = SaiInSegTraits::InSegEntry(
      managerTable_->switchManager().getSwitchSaiId(), label.label());
  auto itr = saiInSegEntryTable_.find(inSegEntry);
  if (itr == saiInSegEntryTable_.end()) {
    return nullptr;
  }
  return &(itr->second);
}

std::shared_ptr<SaiObject<SaiInSegTraits>> SaiInSegEntryManager::getInSegObject(
    SaiInSegTraits::AdapterHostKey inSegKey) {
  return saiStore_->get<SaiInSegTraits>().get(inSegKey);
}

template <typename NextHopTraitsT>
ManagedInSegNextHop<NextHopTraitsT>::ManagedInSegNextHop(
    SaiInSegEntryManager* inSegEntryManager,
    SaiInSegTraits::AdapterHostKey inSegKey,
    std::shared_ptr<ManagedNextHop<NextHopTraitsT>> managedNextHop)
    : detail::SaiObjectEventSubscriber<NextHopTraitsT>(
          managedNextHop->adapterHostKey()),
      inSegEntryManager_(inSegEntryManager),
      inSegKey_(std::move(inSegKey)),
      managedNextHop_(managedNextHop) {}

template <typename NextHopTraitsT>
void ManagedInSegNextHop<NextHopTraitsT>::afterCreate(
    ManagedInSegNextHop<NextHopTraitsT>::PublisherObject nexthop) {
  this->setPublisherObject(nexthop);
  auto entry = inSegEntryManager_->getInSegObject(inSegKey_);
  if (!entry) {
    // managed next hop is created before inseg entry.
    // while creating inseg entry, managed next hop id must be used in create
    // attributes
    return;
  }
  // point entry to next hop
  entry->setOptionalAttribute(
      SaiInSegTraits::Attributes::NextHopId{nexthop->adapterKey()});
}

template <typename NextHopTraitsT>
void ManagedInSegNextHop<NextHopTraitsT>::beforeRemove() {
  auto entry = inSegEntryManager_->getInSegObject(inSegKey_);
  if (entry) {
    // point entry to drop
    entry->setOptionalAttribute(
        SaiInSegTraits::Attributes::NextHopId{SAI_NULL_OBJECT_ID});
  }
  this->setPublisherObject(nullptr);
}

template <typename NextHopTraitsT>
sai_object_id_t ManagedInSegNextHop<NextHopTraitsT>::adapterKey() const {
  if (auto* nexthop = managedNextHop_->getSaiObject()) {
    return nexthop->adapterKey();
  }
  return SAI_NULL_OBJECT_ID;
}

sai_object_id_t SaiInSegEntryHandle::nextHopAdapterKey() const {
  return std::visit(
      [](const auto& handle) -> sai_object_id_t {
        return handle->adapterKey();
      },
      nexthopHandle);
}

std::shared_ptr<SaiNextHopGroupHandle> SaiInSegEntryHandle::nextHopGroupHandle()
    const {
  auto* nextHopGroupHandle =
      std::get_if<std::shared_ptr<SaiNextHopGroupHandle>>(&nexthopHandle);
  if (!nextHopGroupHandle) {
    return nullptr;
  }
  return *nextHopGroupHandle;
}
} // namespace facebook::fboss
