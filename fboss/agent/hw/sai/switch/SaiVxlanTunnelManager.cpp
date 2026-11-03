// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/switch/SaiVxlanTunnelManager.h"

#include "fboss/agent/Constants.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
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
  // create attributes reference the maps by OID.
  std::optional<Attrs::EncapMappers> encapMappers;
  if (auto vrf2Vni = vxlanTunnel->getVrf2Vni(); vrf2Vni && !vrf2Vni->empty()) {
    SaiTunnelMapTraits::CreateAttributes mapAttrs{
        SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI};
    handle->encapMap =
        saiStore_->get<SaiTunnelMapTraits>().setObject(mapAttrs, mapAttrs);
    auto& entryStore = saiStore_->get<SaiVrToVniTunnelMapEntryTraits>();
    for (const auto& [vrf, vni] : *vrf2Vni) {
      SaiVrToVniTunnelMapEntryTraits::CreateAttributes entryAttrs{
          SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI,
          handle->encapMap->adapterKey(),
          getVirtualRouterSaiId(managerTable_, static_cast<uint32_t>(vrf)),
          static_cast<sai_uint32_t>(vni)};
      handle->encapEntries.push_back(
          entryStore.setObject(entryAttrs, entryAttrs));
    }
    encapMappers = Attrs::EncapMappers{std::vector<sai_object_id_t>{
        static_cast<sai_object_id_t>(handle->encapMap->adapterKey())}};
  }
  std::optional<Attrs::DecapMappers> decapMappers;
  if (auto vniToVrf = vxlanTunnel->getVniToVrf();
      vniToVrf && !vniToVrf->empty()) {
    SaiTunnelMapTraits::CreateAttributes mapAttrs{
        SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID};
    handle->decapMap =
        saiStore_->get<SaiTunnelMapTraits>().setObject(mapAttrs, mapAttrs);
    auto& entryStore = saiStore_->get<SaiVniToVrTunnelMapEntryTraits>();
    for (const auto& [vni, vrf] : *vniToVrf) {
      SaiVniToVrTunnelMapEntryTraits::CreateAttributes entryAttrs{
          SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID,
          handle->decapMap->adapterKey(),
          static_cast<sai_uint32_t>(vni),
          getVirtualRouterSaiId(managerTable_, static_cast<uint32_t>(vrf))};
      handle->decapEntries.push_back(
          entryStore.setObject(entryAttrs, entryAttrs));
    }
    decapMappers = Attrs::DecapMappers{std::vector<sai_object_id_t>{
        static_cast<sai_object_id_t>(handle->decapMap->adapterKey())}};
  }
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
  if (oldTunnel != newTunnel) {
    removeVxlanTunnel(oldTunnel);
    addVxlanTunnel(newTunnel);
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
