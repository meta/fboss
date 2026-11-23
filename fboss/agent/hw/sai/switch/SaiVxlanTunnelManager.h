// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/hw/sai/api/TunnelApi.h"
#include "fboss/agent/hw/sai/api/TunnelMapApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/state/VxlanTunnel.h"

#include "folly/CppAttributes.h"
#include "folly/container/F14Map.h"

#include <memory>
#include <string>
#include <vector>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiStore;

using SaiVxlanTunnel = SaiObject<SaiVxlanTunnelTraits>;
using SaiVxlanTunnelTerm = SaiObject<SaiP2MPTunnelTermTraits>;
using SaiTunnelMap = SaiObject<SaiTunnelMapTraits>;
using SaiVrToVniTunnelMapEntry = SaiObject<SaiVrToVniTunnelMapEntryTraits>;
using SaiVniToVrTunnelMapEntry = SaiObject<SaiVniToVrTunnelMapEntryTraits>;

struct SaiVxlanTunnelHandle {
  std::shared_ptr<SaiTunnelMap> encapMap;
  std::shared_ptr<SaiTunnelMap> decapMap;
  // Keyed by VRF
  folly::F14FastMap<int64_t, std::shared_ptr<SaiVrToVniTunnelMapEntry>>
      encapEntries;
  // Keyed by VNI
  folly::F14FastMap<int64_t, std::shared_ptr<SaiVniToVrTunnelMapEntry>>
      decapEntries;
  std::shared_ptr<SaiVxlanTunnel> tunnel;
  std::shared_ptr<SaiVxlanTunnelTerm> tunnelTerm;
};

class SaiVxlanTunnelManager {
  using Handles =
      folly::F14FastMap<std::string, std::unique_ptr<SaiVxlanTunnelHandle>>;

 public:
  SaiVxlanTunnelManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      SaiPlatform* platform);
  ~SaiVxlanTunnelManager();

  void addVxlanTunnel(const std::shared_ptr<VxlanTunnel>& vxlanTunnel);
  void removeVxlanTunnel(const std::shared_ptr<VxlanTunnel>& vxlanTunnel);
  void changeVxlanTunnel(
      const std::shared_ptr<VxlanTunnel>& oldTunnel,
      const std::shared_ptr<VxlanTunnel>& newTunnel);
  // Removes the VNI map entries that newTunnel drops. Called before the
  // virtual routers of deleted VRFs are removed.
  void removeVniMapEntries(
      const std::shared_ptr<VxlanTunnel>& oldTunnel,
      const std::shared_ptr<VxlanTunnel>& newTunnel);

  const SaiVxlanTunnelHandle* getVxlanTunnelHandle(
      const std::string& swId) const {
    return getVxlanTunnelHandleImpl(swId);
  }

 private:
  SaiVxlanTunnelHandle* getVxlanTunnelHandleImpl(const std::string& swId) const;
  // Adds the VNI map entries of tunnel that the handle doesn't have
  void addVniMapEntries(
      SaiVxlanTunnelHandle* handle,
      const std::shared_ptr<VxlanTunnel>& tunnel);
  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  Handles handles_;
};

} // namespace facebook::fboss
