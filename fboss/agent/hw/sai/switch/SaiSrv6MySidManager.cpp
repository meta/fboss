// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/switch/SaiSrv6MySidManager.h"

#include <tuple>
#include <vector>

#include <folly/logging/xlog.h>

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
#include "fboss/agent/FbossError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6SidListManager.h"
#include "fboss/agent/hw/sai/switch/SaiSrv6TunnelManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiVirtualRouterManager.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/lib/HwWriteBehavior.h"
#endif

namespace facebook::fboss {

SaiSrv6MySidManager::SaiSrv6MySidManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    SaiPlatform* /*platform*/)
    : saiStore_(saiStore), managerTable_(managerTable) {}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)

namespace {

#if defined(BRCM_SAI_SDK_XGS_GTE_15_0)
// Broadcom XGS accepts TunnelId only during MySid creation. Keep it out of the
// canonical traits so warm-boot reload does not query TunnelId on non-decap
// MySids, for which Broadcom returns SAI_STATUS_ITEM_NOT_FOUND.
struct BcmSaiMySidEntryCreateTraits : SaiMySidEntryTraits {
  using CreateAttributes = std::tuple<
      Attributes::EndpointBehavior,
      Attributes::EndpointBehaviorFlavor,
      Attributes::NextHopId,
      std::optional<Attributes::Vrf>,
      Attributes::PacketAction,
      Attributes::TunnelId>;
};
#endif

SaiMySidEntryTraits::CreateAttributes getMySidCreateAttributes(
    const MySid& mySid,
    const std::optional<SaiMySidEntryHandle::NextHopHandle>& nexthopHandle,
    SaiManagerTable* managerTable) {
  sai_int32_t endpointBehavior;
  sai_int32_t endpointBehaviorFlavor =
      SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_NONE;
  std::optional<SaiMySidEntryTraits::Attributes::Vrf> vrId;
  switch (mySid.getType()) {
    case MySidType::ADJACENCY_MICRO_SID:
      endpointBehavior = SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UA;
      endpointBehaviorFlavor =
          SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_FLAVOR_PSP_AND_USD;
      break;
    case MySidType::NODE_MICRO_SID:
      endpointBehavior = SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UN;
      break;
    case MySidType::DECAPSULATE_AND_LOOKUP: {
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 1)
      endpointBehavior = SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_UDT46;
      auto* vrHandle =
          managerTable->virtualRouterManager().getVirtualRouterHandle(
              RouterID(0));
      CHECK(vrHandle) << "No default virtual router";
      vrId = SaiMySidEntryTraits::Attributes::Vrf{
          vrHandle->virtualRouter->adapterKey()};
      break;
#else
      throw FbossError("Decapsulate with uSids requires SAI >= 1.16.0");
#endif
    }
    case MySidType::BINDING_MICRO_SID:
      endpointBehavior = SAI_MY_SID_ENTRY_ENDPOINT_BEHAVIOR_B6_ENCAPS_RED;
      break;
  }

  sai_object_id_t nextHopId = SAI_NULL_OBJECT_ID;
  if (nexthopHandle) {
    if (auto* nhgHandle = std::get_if<std::shared_ptr<SaiNextHopGroupHandle>>(
            &nexthopHandle.value())) {
      nextHopId = (*nhgHandle)->nextHopGroup->adapterKey();
    } else if (
        auto* managedIpNh = std::get_if<
            std::shared_ptr<ManagedMySidNextHop<SaiIpNextHopTraits>>>(
            &nexthopHandle.value())) {
      nextHopId = (*managedIpNh)->adapterKey();
    } else if (
        auto* managedSrv6Nh = std::get_if<
            std::shared_ptr<ManagedMySidNextHop<SaiSrv6SidlistNextHopTraits>>>(
            &nexthopHandle.value())) {
      nextHopId = (*managedSrv6Nh)->adapterKey();
    }
  }

  sai_int32_t packetAction = SAI_PACKET_ACTION_FORWARD;
  // For uA / uN, drop traffic when the next hop isn't resolved.
  if (mySid.getType() != MySidType::DECAPSULATE_AND_LOOKUP &&
      nextHopId == SAI_NULL_OBJECT_ID) {
    packetAction = SAI_PACKET_ACTION_DROP;
  }

  return SaiMySidEntryTraits::CreateAttributes{
      endpointBehavior, endpointBehaviorFlavor, nextHopId, vrId, packetAction};
}

#if defined(BRCM_SAI_SDK_XGS_GTE_15_0)
std::shared_ptr<SaiMySid> createBcmDecapMySidEntry(
    SaiObjectStore<SaiMySidEntryTraits>& store,
    const SaiMySidEntryTraits::AdapterHostKey& adapterHostKey,
    const SaiMySidEntryTraits::CreateAttributes& createAttributes,
    TunnelSaiId tunnelId) {
  if (getHwWriteBehavior() == HwWriteBehavior::SKIP) {
    return store.setObject(adapterHostKey, createAttributes);
  }
  BcmSaiMySidEntryCreateTraits::CreateAttributes bcmCreateAttributes =
      std::tuple_cat(
          createAttributes,
          std::make_tuple(SaiMySidEntryTraits::Attributes::TunnelId{tunnelId}));
  auto& srv6Api = SaiApiTable::getInstance()->srv6Api();
  srv6Api.create<BcmSaiMySidEntryCreateTraits>(
      adapterHostKey, bcmCreateAttributes);
  try {
    // Adopt the entry using the canonical attributes, which intentionally
    // exclude TunnelId from warm-boot reload and subsequent updates.
    return store.reloadObject(adapterHostKey);
  } catch (const SaiApiError&) {
    try {
      srv6Api.remove(adapterHostKey);
    } catch (const SaiApiError& error) {
      XLOG(ERR) << "Failed to remove Broadcom MySid after store reload "
                   "failure: "
                << error.what();
    }
    throw;
  }
}
#endif

} // namespace

SaiMySidEntryTraits::AdapterHostKey getMySidAdapterHostKey(
    const MySid& mySid,
    SaiManagerTable* managerTable) {
  auto switchId = managerTable->switchManager().getSwitchSaiId();
  auto* vrHandle =
      managerTable->virtualRouterManager().getVirtualRouterHandle(RouterID(0));
  CHECK(vrHandle) << "No default virtual router";
  auto vrId = vrHandle->virtualRouter->adapterKey();
  auto [ip, maskLen] = mySid.getMySid();
  auto sid = ip.asV6();
  constexpr uint8_t kLocatorBlockLen = 32;
  uint8_t nonLocatorMaskLen = maskLen - kLocatorBlockLen;
  uint8_t nodeLen{0}, functionLen{0};

  switch (mySid.getType()) {
    case MySidType::ADJACENCY_MICRO_SID:
    case MySidType::DECAPSULATE_AND_LOOKUP:
    case MySidType::BINDING_MICRO_SID:
      functionLen = nonLocatorMaskLen;
      break;
    case MySidType::NODE_MICRO_SID:
      nodeLen = nonLocatorMaskLen;
      break;
  }
  return SaiMySidEntryTraits::AdapterHostKey(
      switchId,
      vrId,
      kLocatorBlockLen,
      nodeLen,
      functionLen,
      0 /* argsLen */,
      sid);
}

template <typename NextHopTraits>
ManagedMySidNextHop<NextHopTraits>::ManagedMySidNextHop(
    SaiSrv6MySidManager* manager,
    SaiMySidEntryTraits::AdapterHostKey mySidKey,
    std::shared_ptr<ManagedNextHop<NextHopTraits>> managedNextHop)
    : detail::SaiObjectEventSubscriber<NextHopTraits>(
          managedNextHop->adapterHostKey()),
      manager_(manager),
      mySidKey_(std::move(mySidKey)),
      managedNextHop_(std::move(managedNextHop)) {}

template <typename NextHopTraits>
void ManagedMySidNextHop<NextHopTraits>::afterCreate(
    ManagedMySidNextHop::PublisherObject nexthop) {
  this->setPublisherObject(nexthop);
  auto entry = manager_->getMySidObject(mySidKey_);
  if (!entry) {
    // managed next hop is created before my_sid entry.
    // while creating my_sid entry, managed next hop id must be used in create
    return;
  }
  entry->setAttribute(
      SaiMySidEntryTraits::Attributes::NextHopId{nexthop->adapterKey()});
  entry->setAttribute(
      SaiMySidEntryTraits::Attributes::PacketAction{SAI_PACKET_ACTION_FORWARD});
}

template <typename NextHopTraits>
void ManagedMySidNextHop<NextHopTraits>::beforeRemove() {
  auto entry = manager_->getMySidObject(mySidKey_);
  if (entry) {
    entry->setAttribute(
        SaiMySidEntryTraits::Attributes::NextHopId{SAI_NULL_OBJECT_ID});
    entry->setAttribute(
        SaiMySidEntryTraits::Attributes::PacketAction{SAI_PACKET_ACTION_DROP});
  }
  this->setPublisherObject(nullptr);
}

template <typename NextHopTraits>
sai_object_id_t ManagedMySidNextHop<NextHopTraits>::adapterKey() const {
  if (auto* nexthop = managedNextHop_->getSaiObject()) {
    return nexthop->adapterKey();
  }
  return SAI_NULL_OBJECT_ID;
}

template class ManagedMySidNextHop<SaiIpNextHopTraits>;
template class ManagedMySidNextHop<SaiSrv6SidlistNextHopTraits>;

std::shared_ptr<SaiObject<SaiMySidEntryTraits>>
SaiSrv6MySidManager::getMySidObject(
    const SaiMySidEntryTraits::AdapterHostKey& key) {
  return saiStore_->get<SaiMySidEntryTraits>().get(key);
}

std::optional<SaiMySidEntryHandle::NextHopHandle>
SaiSrv6MySidManager::getNextHopHandle(
    const SaiMySidEntryTraits::AdapterHostKey& adapterHostKey,
    const MySid& mySid,
    const std::shared_ptr<SwitchState>& state) {
  const auto resolvedNextHopsId = mySid.getResolvedNextHopsId();
  const auto backupResolvedNextHopsId = mySid.getBackupResolvedNextHopsId();
  if (!resolvedNextHopsId && !backupResolvedNextHopsId) {
    return std::nullopt;
  }
  std::vector<NextHop> nhops;
  for (const auto& nextHopsId :
       {resolvedNextHopsId, backupResolvedNextHopsId}) {
    if (nextHopsId) {
      const auto resolvedNextHops =
          getNextHops(state, static_cast<int64_t>(*nextHopsId));
      nhops.insert(
          nhops.end(), resolvedNextHops.begin(), resolvedNextHops.end());
    }
  }
  if (nhops.empty()) {
    throw FbossError("Resolved nhops Id set, but no next hops found");
  }
  RouteNextHopSet nhopSet(nhops.begin(), nhops.end());
  const auto nextHopGroupType = getNextHopGroupType(nhopSet);
  // A lone next hop is normally programmed directly, but not when it is a
  // backup: the protection group is what carries standby semantics into
  // hardware. Collapsing a single backup to a plain next hop would forward
  // over it as if it were the primary path.
  if (nhops.size() > 1 || isProtectionNextHopGroupType(nextHopGroupType)) {
    return managerTable_->nextHopGroupManager().incRefOrAddNextHopGroup(
        SaiNextHopGroupKey(nhopSet, std::nullopt, nextHopGroupType));
  }
  auto resolvedNh = folly::poly_cast<ResolvedNextHop>(nhops.front());
  std::shared_ptr<SaiSrv6SidListHandle> sidListHandle;
  if (!resolvedNh.srv6SegmentList().empty()) {
    auto interfaceId = resolvedNh.intfID().value();
    auto* routerInterfaceHandle =
        managerTable_->routerInterfaceManager().getRouterInterfaceHandle(
            interfaceId);
    CHECK(routerInterfaceHandle)
        << "Missing SAI router interface for " << interfaceId;
    auto [sidListKey, sidListAttrs] = makeSrv6SidListKeyAndAttributes(
        routerInterfaceHandle->adapterKey(), resolvedNh);
    sidListHandle = managerTable_->srv6SidListManager().addOrReuseSrv6SidList(
        sidListKey, sidListAttrs);
  }
  auto managedSaiNextHop = managerTable_->nextHopManager().addManagedSaiNextHop(
      resolvedNh, std::move(sidListHandle));
  if (auto* ipNextHop =
          std::get_if<std::shared_ptr<ManagedIpNextHop>>(&managedSaiNextHop)) {
    auto managedMySidNextHop =
        std::make_shared<ManagedMySidNextHop<SaiIpNextHopTraits>>(
            this, adapterHostKey, *ipNextHop);
    SaiObjectEventPublisher::getInstance()->get<SaiIpNextHopTraits>().subscribe(
        managedMySidNextHop);
    return managedMySidNextHop;
  }
  if (auto* srv6NextHop = std::get_if<std::shared_ptr<ManagedSrv6NextHop>>(
          &managedSaiNextHop)) {
    auto managedMySidNextHop =
        std::make_shared<ManagedMySidNextHop<SaiSrv6SidlistNextHopTraits>>(
            this, adapterHostKey, *srv6NextHop);
    SaiObjectEventPublisher::getInstance()
        ->get<SaiSrv6SidlistNextHopTraits>()
        .subscribe(managedMySidNextHop);
    return managedMySidNextHop;
  }
  throw FbossError(
      "Expected IP or SRv6 next hop for MySid entry ", mySid.getID());
}

void SaiSrv6MySidManager::addMySidEntry(
    const std::shared_ptr<MySid>& mySid,
    const std::shared_ptr<SwitchState>& state) {
  auto adapterHostKey = getMySidAdapterHostKey(*mySid, managerTable_);
  if (handles_.contains(adapterHostKey)) {
    throw FbossError("MySid entry already exists for ", mySid->getID());
  }

  auto nexthopHandle = getNextHopHandle(adapterHostKey, *mySid, state);

  std::shared_ptr<SaiObject<SaiSrv6TunnelTraits>> decapTunnel;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  if (mySid->getType() == MySidType::DECAPSULATE_AND_LOOKUP) {
    const auto* decapTunnelHandle =
        managerTable_->srv6TunnelManager().getDecapTunnelHandle();
    if (decapTunnelHandle && decapTunnelHandle->tunnel) {
      decapTunnel = decapTunnelHandle->tunnel;
    }
  }
#endif

  auto createAttributes =
      getMySidCreateAttributes(*mySid, nexthopHandle, managerTable_);
  auto& store = saiStore_->get<SaiMySidEntryTraits>();
  std::shared_ptr<SaiMySid> mySidEntry;
#if defined(BRCM_SAI_SDK_XGS_GTE_15_0)
  if (decapTunnel && !store.get(adapterHostKey)) {
    mySidEntry = createBcmDecapMySidEntry(
        store, adapterHostKey, createAttributes, decapTunnel->adapterKey());
  } else {
    // Existing entries were loaded during warm boot. Claim and reconcile them
    // without setting Broadcom's create-only TunnelId attribute.
    mySidEntry = store.setObject(adapterHostKey, createAttributes);
  }
#else
  mySidEntry = store.setObject(adapterHostKey, createAttributes);
  if (decapTunnel) {
    // TunnelId is not part of CreateAttributes so warm-boot reload does not
    // query it for MySID behaviors where the attribute is not applicable.
    SaiApiTable::getInstance()->srv6Api().setAttribute(
        mySidEntry->adapterKey(),
        SaiMySidEntryTraits::Attributes::TunnelId{decapTunnel->adapterKey()});
  }
#endif

  auto handle = std::make_unique<SaiMySidEntryHandle>();
  if (nexthopHandle) {
    handle->nexthopHandle = nexthopHandle.value();
  }
  handle->mySidEntry = mySidEntry;
  handle->decapTunnel = decapTunnel;
  handles_.emplace(adapterHostKey, std::move(handle));
}

void SaiSrv6MySidManager::removeMySidEntry(
    const std::shared_ptr<MySid>& mySid,
    const std::shared_ptr<SwitchState>& /*state*/) {
  auto key = getMySidAdapterHostKey(*mySid, managerTable_);
  auto itr = handles_.find(key);
  if (itr == handles_.end()) {
    throw FbossError("MySid entry does not exist for ", mySid->getID());
  }
  handles_.erase(itr);
}

void SaiSrv6MySidManager::changeMySidEntry(
    const std::shared_ptr<MySid>& oldMySid,
    const std::shared_ptr<MySid>& newMySid,
    const std::shared_ptr<SwitchState>& state) {
  const auto adapterHostKey = getMySidAdapterHostKey(*newMySid, managerTable_);
  if (getMySidAdapterHostKey(*oldMySid, managerTable_) != adapterHostKey ||
      oldMySid->getType() != newMySid->getType() ||
      newMySid->getType() == MySidType::DECAPSULATE_AND_LOOKUP) {
    removeMySidEntry(oldMySid, state);
    addMySidEntry(newMySid, state);
    return;
  }
  auto itr = handles_.find(adapterHostKey);
  if (itr == handles_.end()) {
    throw FbossError("MySid entry does not exist for ", newMySid->getID());
  }
  auto& handle = itr->second;
  // Repoint before releasing the old next hop (make-before-break).
  auto nexthopHandle = getNextHopHandle(adapterHostKey, *newMySid, state);
  saiStore_->get<SaiMySidEntryTraits>().setObject(
      adapterHostKey,
      getMySidCreateAttributes(*newMySid, nexthopHandle, managerTable_));
  handle->nexthopHandle =
      nexthopHandle.value_or(SaiMySidEntryHandle::NextHopHandle{});
}

#endif

} // namespace facebook::fboss
