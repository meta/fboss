// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/switch/SaiVxlanTunnelManager.h"

#include "fboss/agent/Constants.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiTunnelUtils.h"
#include "fboss/agent/hw/sai/switch/SaiVirtualRouterManager.h"
#include "fboss/agent/state/VxlanTunnel.h"

namespace facebook::fboss {

namespace {

VirtualRouterSaiId getVirtualRouterSaiId(
    SaiManagerTable* managerTable,
    uint32_t vrf) {
  auto* vrHandle = managerTable->virtualRouterManager().getVirtualRouterHandle(
      RouterID(vrf));
  if (!vrHandle) {
    throw FbossError(
        "Failed to create VXLAN tunnel. "
        "No virtual router for RouterID: ",
        vrf);
  }
  return VirtualRouterSaiId{vrHandle->virtualRouter->adapterKey()};
}

} // namespace

SaiVxlanTunnelManager::SaiVxlanTunnelManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    SaiPlatform* /*platform*/)
    : saiStore_(saiStore), managerTable_(managerTable) {}

SaiVxlanTunnelManager::~SaiVxlanTunnelManager() {}

void SaiVxlanTunnelManager::addVxlanTunnel(
    const std::shared_ptr<VxlanTunnel>& vxlanTunnel) {
  if (vxlanTunnel->getType() != TunnelType::VXLAN) {
    throw FbossError(
        "Attempted to add non-VXLAN tunnel to vxlan tunnel manager: ",
        vxlanTunnel->getID());
  }
  if (getVxlanTunnelHandle(vxlanTunnel->getID())) {
    throw FbossError(
        "Attempted to add vxlan tunnel which already exists: ",
        vxlanTunnel->getID());
  }
  managerTable_->switchManager().setVxlanDefaultPort();
  SaiRouterInterfaceHandle* intfHandle =
      managerTable_->routerInterfaceManager().getRouterInterfaceHandle(
          vxlanTunnel->getUnderlayIntfId());
  if (!intfHandle) {
    throw FbossError(
        "Failed to create VXLAN tunnel. "
        "No SaiRouterInterface for InterfaceID: ",
        vxlanTunnel->getUnderlayIntfId());
  }
  RouterInterfaceSaiId saiIntfId{intfHandle->adapterKey()};
  auto srcIp = vxlanTunnel->getSrcIP();
  if (!srcIp) {
    throw FbossError(
        "Failed to create VXLAN tunnel. "
        "No source IP for tunnel: ",
        vxlanTunnel->getID());
  }
  std::optional<sai_tunnel_ttl_mode_t> ttlMode;
  if (auto mode = vxlanTunnel->getTTLMode()) {
    ttlMode = getSaiTtlMode(*mode);
  }
  std::optional<sai_tunnel_dscp_mode_t> dscpMode;
  if (auto mode = vxlanTunnel->getDscpMode()) {
    dscpMode = getSaiDscpMode(*mode);
  }
  std::optional<sai_tunnel_encap_ecn_mode_t> encapEcnMode;
  std::optional<sai_tunnel_decap_ecn_mode_t> decapEcnMode;
  if (auto mode = vxlanTunnel->getEcnMode()) {
    encapEcnMode = getSaiEncapEcnMode(*mode);
    decapEcnMode = getSaiDecapEcnMode(*mode);
  }
  using Attrs = SaiVxlanTunnelTraits::Attributes;
  auto handle = std::make_unique<SaiVxlanTunnelHandle>();
  // Program the VNI<->VR tunnel maps before the tunnel: the tunnel
  // create attributes reference the maps by OID. The maps are created even
  // when empty, since entries are added later as VRFs get VNIs.
  SaiTunnelMapTraits::CreateAttributes encapMapAttrs{
      SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI};
  handle->encapMap = saiStore_->get<SaiTunnelMapTraits>().setObject(
      encapMapAttrs, encapMapAttrs);
  SaiTunnelMapTraits::CreateAttributes decapMapAttrs{
      SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID};
  handle->decapMap = saiStore_->get<SaiTunnelMapTraits>().setObject(
      decapMapAttrs, decapMapAttrs);
  addVniMapEntries(handle.get(), vxlanTunnel);
  std::optional<Attrs::EncapMappers> encapMappers{std::vector<sai_object_id_t>{
      static_cast<sai_object_id_t>(handle->encapMap->adapterKey())}};
  std::optional<Attrs::DecapMappers> decapMappers{std::vector<sai_object_id_t>{
      static_cast<sai_object_id_t>(handle->decapMap->adapterKey())}};
  SaiVxlanTunnelTraits::CreateAttributes attrs{
      SAI_TUNNEL_TYPE_VXLAN,
      Attrs::UnderlayInterface(saiIntfId),
      std::optional<Attrs::EncapSrcIp>(srcIp.value()),
      ttlMode,
      dscpMode,
      encapEcnMode,
      encapMappers,
      ttlMode,
      dscpMode,
      decapEcnMode,
      decapMappers};
  auto& tunnelStore = saiStore_->get<SaiVxlanTunnelTraits>();
  handle->tunnel = tunnelStore.setObject(attrs, attrs);
  if (auto termType = vxlanTunnel->getTunnelTermType(); termType) {
    if (*termType != cfg::TunnelTerminationType::P2MP) {
      throw FbossError(
          "VXLAN tunnel term type other than P2MP is not supported: ",
          vxlanTunnel->getID());
    }
    SaiP2MPTunnelTermTraits::CreateAttributes termAttrs{
        SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2MP,
        getVirtualRouterSaiId(managerTable_, kDefaultVrf),
        srcIp.value(),
        SAI_TUNNEL_TYPE_VXLAN,
        handle->tunnel->adapterKey()};
    handle->tunnelTerm = saiStore_->get<SaiP2MPTunnelTermTraits>().setObject(
        termAttrs, termAttrs);
  }
  handles_[vxlanTunnel->getID()] = std::move(handle);
}

void SaiVxlanTunnelManager::removeVxlanTunnel(
    const std::shared_ptr<VxlanTunnel>& vxlanTunnel) {
  auto swId = vxlanTunnel->getID();
  auto itr = handles_.find(swId);
  if (itr == handles_.end()) {
    throw FbossError("Failed to remove non-existent vxlan tunnel: ", swId);
  }
  handles_.erase(itr);
}

void SaiVxlanTunnelManager::changeVxlanTunnel(
    const std::shared_ptr<VxlanTunnel>& oldTunnel,
    const std::shared_ptr<VxlanTunnel>& newTunnel) {
  if (oldTunnel == newTunnel) {
    return;
  }
  auto oldWithNewMaps = oldTunnel->clone();
  oldWithNewMaps->setVrf2Vni(newTunnel->getVrf2Vni());
  oldWithNewMaps->setVniToVrf(newTunnel->getVniToVrf());
  if (*oldWithNewMaps == *newTunnel) {
    // Only the VNI maps changed: update their entries in place
    removeVniMapEntries(oldTunnel, newTunnel);
    addVniMapEntries(getVxlanTunnelHandleImpl(newTunnel->getID()), newTunnel);
    return;
  }
  removeVxlanTunnel(oldTunnel);
  addVxlanTunnel(newTunnel);
}

void SaiVxlanTunnelManager::removeVniMapEntries(
    const std::shared_ptr<VxlanTunnel>& oldTunnel,
    const std::shared_ptr<VxlanTunnel>& newTunnel) {
  auto* handle = getVxlanTunnelHandleImpl(oldTunnel->getID());
  if (!handle) {
    return;
  }
  const std::map<int64_t, int64_t> kNoMap;
  auto newVrf2Vni = newTunnel->getVrf2Vni().value_or(kNoMap);
  for (const auto& [vrf, vni] : oldTunnel->getVrf2Vni().value_or(kNoMap)) {
    auto it = newVrf2Vni.find(vrf);
    if (it == newVrf2Vni.end() || it->second != vni) {
      handle->encapEntries.erase(vrf);
    }
  }
  auto newVniToVrf = newTunnel->getVniToVrf().value_or(kNoMap);
  for (const auto& [vni, vrf] : oldTunnel->getVniToVrf().value_or(kNoMap)) {
    auto it = newVniToVrf.find(vni);
    if (it == newVniToVrf.end() || it->second != vrf) {
      handle->decapEntries.erase(vni);
    }
  }
}

void SaiVxlanTunnelManager::addVniMapEntries(
    SaiVxlanTunnelHandle* handle,
    const std::shared_ptr<VxlanTunnel>& tunnel) {
  const std::map<int64_t, int64_t> kNoMap;
  for (const auto& [vrf, vni] : tunnel->getVrf2Vni().value_or(kNoMap)) {
    if (handle->encapEntries.contains(vrf)) {
      continue;
    }
    SaiVrToVniTunnelMapEntryTraits::CreateAttributes entryAttrs{
        SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI,
        handle->encapMap->adapterKey(),
        getVirtualRouterSaiId(managerTable_, static_cast<uint32_t>(vrf)),
        static_cast<sai_uint32_t>(vni)};
    handle->encapEntries[vrf] =
        saiStore_->get<SaiVrToVniTunnelMapEntryTraits>().setObject(
            entryAttrs, entryAttrs);
  }
  for (const auto& [vni, vrf] : tunnel->getVniToVrf().value_or(kNoMap)) {
    if (handle->decapEntries.contains(vni)) {
      continue;
    }
    SaiVniToVrTunnelMapEntryTraits::CreateAttributes entryAttrs{
        SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID,
        handle->decapMap->adapterKey(),
        static_cast<sai_uint32_t>(vni),
        getVirtualRouterSaiId(managerTable_, static_cast<uint32_t>(vrf))};
    handle->decapEntries[vni] =
        saiStore_->get<SaiVniToVrTunnelMapEntryTraits>().setObject(
            entryAttrs, entryAttrs);
  }
}

SaiVxlanTunnelHandle* FOLLY_NULLABLE
SaiVxlanTunnelManager::getVxlanTunnelHandleImpl(const std::string& swId) const {
  auto itr = handles_.find(swId);
  if (itr == handles_.end()) {
    return nullptr;
  }
  if (!itr->second.get()) {
    XLOG(FATAL) << "Invalid null SaiVxlanTunnelHandle for " << swId;
  }
  return itr->second.get();
}

} // namespace facebook::fboss
