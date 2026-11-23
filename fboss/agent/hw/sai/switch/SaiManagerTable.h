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

#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"

#include <memory>
#include <optional>

namespace folly {
struct dynamic;
}

namespace facebook::fboss {

struct ConcurrentIndices;
class SaiAclTableGroupManager;
class SaiAclTableManager;
class SaiArsManager;
class SaiArsProfileManager;
class SaiBridgeManager;
class SaiBufferManager;
class SaiCounterManager;
class SaiDebugCounterManager;
class SaiFdbManager;
class SaiHashManager;
class SaiHostifManager;
class SaiInSegEntryManager;
class SaiLagManager;
class SaiMacsecManager;
class SaiMirrorManager;
class SaiNeighborManager;
class SaiNextHopManager;
class SaiNextHopGroupManager;
class SaiPlatform;
class SaiPortManager;
class SaiQueueManager;
class SaiQosMapManager;
class SaiRouteManager;
class SaiRouterInterfaceManager;
class SaiMyMacManager;
class SaiSamplePacketManager;
class SaiSwitchManager;
class SaiSystemPortManager;
class SaiSchedulerManager;
class SaiTamManager;
class SaiTunnelManager;
class SaiSrv6SidListManager;
class SaiSrv6MySidManager;
class SaiSrv6TunnelManager;
class SaiUdfManager;
class SaiVirtualChannelManager;
class SaiVirtualRouterManager;
class SaiVlanManager;
class SaiVxlanTunnelManager;
class SaiVendorSwitchManager;
class SaiWredManager;
class SaiStore;
class UnsupportedFeatureManager;
class SaiFirmwareManager;

class SaiManagerTable {
 public:
  SaiManagerTable(
      SaiPlatform* platform,
      BootType bootType,
      cfg::SwitchType switchType,
      std::optional<int64_t> switchId);
  void createSaiTableManagers(
      SaiStore* saiStore,
      SaiPlatform* platform,
      ConcurrentIndices* concurrentIndices);
  ~SaiManagerTable();

  SaiAclTableManager& aclTableManager();
  const SaiAclTableManager& aclTableManager() const;

  SaiArsManager& arsManager();
  const SaiArsManager& arsManager() const;

  SaiArsProfileManager& arsProfileManager();
  const SaiArsProfileManager& arsProfileManager() const;

  SaiAclTableGroupManager& aclTableGroupManager();
  const SaiAclTableGroupManager& aclTableGroupManager() const;

  SaiBridgeManager& bridgeManager();
  const SaiBridgeManager& bridgeManager() const;

  SaiBufferManager& bufferManager();
  const SaiBufferManager& bufferManager() const;

  SaiCounterManager& counterManager();
  const SaiCounterManager& counterManager() const;

  SaiDebugCounterManager& debugCounterManager();
  const SaiDebugCounterManager& debugCounterManager() const;

  UnsupportedFeatureManager& teFlowEntryManager();
  const UnsupportedFeatureManager& teFlowEntryManager() const;

  SaiFdbManager& fdbManager();
  const SaiFdbManager& fdbManager() const;

  SaiHashManager& hashManager();
  const SaiHashManager& hashManager() const;

  SaiHostifManager& hostifManager();
  const SaiHostifManager& hostifManager() const;

  SaiLagManager& lagManager();
  const SaiLagManager& lagManager() const;

  SaiMacsecManager& macsecManager();
  const SaiMacsecManager& macsecManager() const;

  SaiMirrorManager& mirrorManager();
  const SaiMirrorManager& mirrorManager() const;

  SaiNeighborManager& neighborManager();
  const SaiNeighborManager& neighborManager() const;

  SaiNextHopManager& nextHopManager();
  const SaiNextHopManager& nextHopManager() const;

  SaiNextHopGroupManager& nextHopGroupManager();
  const SaiNextHopGroupManager& nextHopGroupManager() const;

  SaiPortManager& portManager();
  const SaiPortManager& portManager() const;

  SaiSystemPortManager& systemPortManager();
  const SaiSystemPortManager& systemPortManager() const;

  SaiQueueManager& queueManager();
  const SaiQueueManager& queueManager() const;

  SaiQosMapManager& qosMapManager();
  const SaiQosMapManager& qosMapManager() const;

  SaiRouteManager& routeManager();
  const SaiRouteManager& routeManager() const;

  SaiRouterInterfaceManager& routerInterfaceManager();
  const SaiRouterInterfaceManager& routerInterfaceManager() const;

  SaiMyMacManager& myMacManager();
  const SaiMyMacManager& myMacManager() const;

  SaiSamplePacketManager& samplePacketManager();
  const SaiSamplePacketManager& samplePacketManager() const;

  SaiSchedulerManager& schedulerManager();
  const SaiSchedulerManager& schedulerManager() const;

  SaiSwitchManager& switchManager();
  const SaiSwitchManager& switchManager() const;

  SaiVirtualChannelManager& virtualChannelManager();
  const SaiVirtualChannelManager& virtualChannelManager() const;
  SaiVirtualRouterManager& virtualRouterManager();
  const SaiVirtualRouterManager& virtualRouterManager() const;

  SaiVlanManager& vlanManager();
  const SaiVlanManager& vlanManager() const;

  SaiInSegEntryManager& inSegEntryManager();
  const SaiInSegEntryManager& inSegEntryManager() const;

  SaiWredManager& wredManager();
  const SaiWredManager& wredManager() const;

  SaiTamManager& tamManager();
  const SaiTamManager& tamManager() const;

  SaiTunnelManager& tunnelManager();
  const SaiTunnelManager& tunnelManager() const;

  SaiSrv6SidListManager& srv6SidListManager();
  const SaiSrv6SidListManager& srv6SidListManager() const;

  SaiSrv6MySidManager& srv6MySidManager();
  const SaiSrv6MySidManager& srv6MySidManager() const;

  SaiSrv6TunnelManager& srv6TunnelManager();
  const SaiSrv6TunnelManager& srv6TunnelManager() const;

  SaiVxlanTunnelManager& vxlanTunnelManager();
  const SaiVxlanTunnelManager& vxlanTunnelManager() const;

  SaiUdfManager& udfManager();
  const SaiUdfManager& udfManager() const;

  SaiVendorSwitchManager& vendorSwitchManager();
  const SaiVendorSwitchManager& vendorSwitchManager() const;

  SaiFirmwareManager& firmwareManager();
  const SaiFirmwareManager& firmwareManager() const;

  void reset(bool skipSwitchManager);

 private:
  std::unique_ptr<SaiAclTableGroupManager> aclTableGroupManager_;
  std::unique_ptr<SaiAclTableManager> aclTableManager_;
  std::unique_ptr<SaiArsManager> arsManager_;
  std::unique_ptr<SaiArsProfileManager> arsProfileManager_;
  std::unique_ptr<SaiBridgeManager> bridgeManager_;
  std::unique_ptr<SaiBufferManager> bufferManager_;
  std::unique_ptr<SaiCounterManager> counterManager_;
  std::unique_ptr<SaiDebugCounterManager> debugCounterManager_;

  std::unique_ptr<SaiFdbManager> fdbManager_;
  std::unique_ptr<SaiHashManager> hashManager_;
  std::unique_ptr<SaiHostifManager> hostifManager_;
  std::unique_ptr<SaiMacsecManager> macsecManager_;
  std::unique_ptr<SaiMirrorManager> mirrorManager_;
  std::unique_ptr<SaiNeighborManager> neighborManager_;
  std::unique_ptr<SaiNextHopManager> nextHopManager_;
  std::unique_ptr<SaiNextHopGroupManager> nextHopGroupManager_;
  std::unique_ptr<SaiPortManager> portManager_;
  std::unique_ptr<SaiSystemPortManager> systemPortManager_;
  std::unique_ptr<SaiQueueManager> queueManager_;
  std::unique_ptr<SaiQosMapManager> qosMapManager_;
  std::unique_ptr<SaiRouteManager> routeManager_;
  std::unique_ptr<SaiRouterInterfaceManager> routerInterfaceManager_;
  std::unique_ptr<SaiMyMacManager> myMacManager_;
  std::unique_ptr<SaiSamplePacketManager> samplePacketManager_;
  std::unique_ptr<SaiSchedulerManager> schedulerManager_;
  std::unique_ptr<SaiSwitchManager> switchManager_;
  std::unique_ptr<SaiVirtualChannelManager> virtualChannelManager_;
  std::unique_ptr<SaiVirtualRouterManager> virtualRouterManager_;
  std::unique_ptr<SaiVlanManager> vlanManager_;
  std::unique_ptr<SaiInSegEntryManager> inSegEntryManager_;
  std::unique_ptr<SaiLagManager> lagManager_;
  std::unique_ptr<SaiWredManager> wredManager_;
  std::unique_ptr<SaiTamManager> tamManager_;
  std::unique_ptr<SaiTunnelManager> tunnelManager_;
  std::unique_ptr<SaiSrv6SidListManager> srv6SidListManager_;
  std::unique_ptr<SaiSrv6MySidManager> srv6MySidManager_;
  std::unique_ptr<SaiSrv6TunnelManager> srv6TunnelManager_;
  std::unique_ptr<SaiVxlanTunnelManager> vxlanTunnelManager_;
  std::unique_ptr<UnsupportedFeatureManager> teFlowEntryManager_;
  std::unique_ptr<SaiUdfManager> udfManager_;
  std::unique_ptr<SaiVendorSwitchManager> vendorSwitchManager_;
  std::unique_ptr<SaiFirmwareManager> firmwareManager_;
};

} // namespace facebook::fboss
