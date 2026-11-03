// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/switch/SaiSrv6SidListManager.h"

#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/store/SaiObjectEventPublisher.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"

namespace facebook::fboss {

SaiSrv6SidListManager::SaiSrv6SidListManager(
    SaiStore* saiStore,
    SaiManagerTable* /*managerTable*/,
    SaiPlatform* /*platform*/)
    : saiStore_(saiStore) {}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)

ManagedSrv6SidList::ManagedSrv6SidList(
    SaiSrv6SidListManager* manager,
    SaiStore* saiStore,
    typename SaiIpNextHopTraits::AdapterHostKey nexthopKey,
    SaiSrv6SidListTraits::AdapterHostKey sidListKey,
    SaiSrv6SidListTraits::CreateAttributes attrs)
    : Base(std::move(nexthopKey)),
      manager_(manager),
      sidListKey_(std::move(sidListKey)),
      attrs_(std::move(attrs)) {
  using NextHopIdAttr =
      std::optional<SaiSrv6SidListTraits::Attributes::NextHopId>;
  auto& store = saiStore->get<SaiSrv6SidListTraits>();
  if (auto wbSidList = store.getWarmbootHandle(sidListKey_)) {
    std::get<NextHopIdAttr>(attrs_) =
        std::get<NextHopIdAttr>(wbSidList->attributes());
  }
  sidList_ = store.setObject(sidListKey_, attrs_);
}

std::shared_ptr<SaiSrv6SidListHandle>
SaiSrv6SidListManager::addOrReuseSrv6SidList(
    const SaiSrv6SidListTraits::AdapterHostKey& adapterHostKey,
    const SaiSrv6SidListTraits::CreateAttributes& createAttributes) {
  auto [handle, emplaced] = srv6SidLists_.refOrEmplace(adapterHostKey);
  if (emplaced) {
    SaiIpNextHopTraits::AdapterHostKey nexthopKey{
        std::get<2>(adapterHostKey), std::get<3>(adapterHostKey)};
    handle->managedSidList = std::make_shared<ManagedSrv6SidList>(
        this,
        saiStore_,
        std::move(nexthopKey),
        adapterHostKey,
        createAttributes);
    SaiObjectEventPublisher::getInstance()->get<SaiIpNextHopTraits>().subscribe(
        handle->managedSidList);
  }
  return handle;
}

const SaiSrv6SidListHandle* SaiSrv6SidListManager::getSrv6SidListHandle(
    const SaiSrv6SidListTraits::AdapterHostKey& adapterHostKey) const {
  return srv6SidLists_.get(adapterHostKey);
}

std::pair<
    SaiSrv6SidListTraits::AdapterHostKey,
    SaiSrv6SidListTraits::CreateAttributes>
makeSrv6SidListKeyAndAttributes(
    RouterInterfaceSaiId rifId,
    const ResolvedNextHop& swNextHop) {
  auto saiSegList = toSaiIp6List(swNextHop.srv6SegmentList());
  SaiSrv6SidListTraits::AdapterHostKey key{
      SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED, saiSegList, rifId, swNextHop.addr()};
  SaiSrv6SidListTraits::CreateAttributes attrs{
      SAI_SRV6_SIDLIST_TYPE_ENCAPS_RED, saiSegList, std::nullopt};
  return {std::move(key), std::move(attrs)};
}

#endif

} // namespace facebook::fboss
