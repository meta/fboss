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

#include "fboss/agent/hw/sai/fake/FakeSaiAcl.h"
#include "fboss/agent/hw/sai/fake/FakeSaiArs.h"
#include "fboss/agent/hw/sai/fake/FakeSaiArsProfile.h"
#include "fboss/agent/hw/sai/fake/FakeSaiBridge.h"
#include "fboss/agent/hw/sai/fake/FakeSaiBuffer.h"
#include "fboss/agent/hw/sai/fake/FakeSaiCounter.h"
#include "fboss/agent/hw/sai/fake/FakeSaiDebugCounter.h"
#include "fboss/agent/hw/sai/fake/FakeSaiFdb.h"
#include "fboss/agent/hw/sai/fake/FakeSaiHash.h"
#include "fboss/agent/hw/sai/fake/FakeSaiHostif.h"
#include "fboss/agent/hw/sai/fake/FakeSaiInSegEntryManager.h"
#include "fboss/agent/hw/sai/fake/FakeSaiIsolationGroup.h"
#include "fboss/agent/hw/sai/fake/FakeSaiLag.h"
#include "fboss/agent/hw/sai/fake/FakeSaiMacsec.h"
#include "fboss/agent/hw/sai/fake/FakeSaiMirror.h"
#include "fboss/agent/hw/sai/fake/FakeSaiMyMac.h"
#include "fboss/agent/hw/sai/fake/FakeSaiNeighbor.h"
#include "fboss/agent/hw/sai/fake/FakeSaiNextHop.h"
#include "fboss/agent/hw/sai/fake/FakeSaiNextHopGroup.h"
#include "fboss/agent/hw/sai/fake/FakeSaiPort.h"
#include "fboss/agent/hw/sai/fake/FakeSaiQosMap.h"
#include "fboss/agent/hw/sai/fake/FakeSaiQueue.h"
#include "fboss/agent/hw/sai/fake/FakeSaiRoute.h"
#include "fboss/agent/hw/sai/fake/FakeSaiRouterInterface.h"
#include "fboss/agent/hw/sai/fake/FakeSaiSamplePacket.h"
#include "fboss/agent/hw/sai/fake/FakeSaiScheduler.h"
#include "fboss/agent/hw/sai/fake/FakeSaiSrv6.h"
#include "fboss/agent/hw/sai/fake/FakeSaiSwitch.h"
#include "fboss/agent/hw/sai/fake/FakeSaiSystemPort.h"
#include "fboss/agent/hw/sai/fake/FakeSaiTam.h"
#include "fboss/agent/hw/sai/fake/FakeSaiTunnel.h"
#include "fboss/agent/hw/sai/fake/FakeSaiUdf.h"
#include "fboss/agent/hw/sai/fake/FakeSaiVirtualChannel.h"
#include "fboss/agent/hw/sai/fake/FakeSaiVirtualRouter.h"
#include "fboss/agent/hw/sai/fake/FakeSaiVlan.h"
#include "fboss/agent/hw/sai/fake/FakeSaiWred.h"

#include <memory>
#include <set>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

struct FakeSai {
  static std::shared_ptr<FakeSai> getInstance();
  static void clear();

  FakeAclTableGroupManager aclTableGroupManager;
  FakeAclEntryManager aclEntryManager;
  FakeAclCounterManager aclCounterManager;
  FakeAclRangeManager aclRangeManager;
  FakeAclTableManager aclTableManager;
  FakeArsManager arsManager;
  FakeArsProfileManager arsProfileManager;
  FakeBridgeManager bridgeManager;
  FakeBufferPoolManager bufferPoolManager;
  FakeBufferProfileManager bufferProfileManager;
  FakeIngressPriorityGroupManager ingressPriorityGroupManager;
  FakeCounterManager counterManager;
  FakeDebugCounterManager debugCounterManager;
  FakeFdbManager fdbManager;
  FakeHashManager hashManager;
  FakeHostifTrapManager hostIfTrapManager;
  FakeHostifUserDefinedTrapManager hostIfUserDefinedTrapManager;
  FakeHostifTrapGroupManager hostifTrapGroupManager;
  FakeInSegEntryManager inSegEntryManager;
  FakeIsolationGroupManager isolationGroupManager;
  FakeNeighborManager neighborManager;
  FakeMirrorManager mirrorManager;
  FakeLagManager lagManager;
  FakeNextHopManager nextHopManager;
  FakeNextHopGroupManager nextHopGroupManager;
  FakePortManager portManager;
  FakePortSerdesManager portSerdesManager;
  FakePortConnectorManager portConnectorManager;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  FakePortLlrProfileManager portLlrProfileManager;
#endif
  FakeQosMapManager qosMapManager;
  FakeQueueManager queueManager;
  FakeRouteManager routeManager;
  FakeRouterInterfaceManager routeInterfaceManager;
  FakeMyMacManager myMacManager;
  FakeSamplePacketManager samplePacketManager;
  FakeSchedulerManager scheduleManager;
  FakeSwitchManager switchManager;
  FakeUdfManager udfManager;
  FakeUdfGroupManager udfGroupManager;
  FakeUdfMatchManager udfMatchManager;
  FakeVirtualRouterManager virtualRouteManager;
  FakeVlanManager vlanManager;
  FakeVirtualChannelManager virtualChannelManager;
  FakeCbfcCreditProfileManager cbfcCreditProfileManager;
  FakeWredManager wredManager;
  FakeTamManager tamManager;
  FakeTamEventManager tamEventManager;
  FakeTamEventActionManager tamEventActionManager;
  FakeTamReportManager tamReportManager;
  FakeTamTransportManager tamTransportManager;
  FakeTamCollectorManager tamCollectorManager;
  FakeTamEventThresholdManager tamEventThresholdManager;
  FakeTunnelManager tunnelManager;
  FakeTunnelTermManager tunnelTermManager;
  FakeTunnelMapManager tunnelMapManager;
  FakeTunnelMapEntryManager tunnelMapEntryManager;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  FakeSrv6SidListManager srv6SidListManager;
  FakeMySidEntryManager mySidEntryManager;
#endif
  FakeMacsecManager macsecManager;
  FakeMacsecPortManager macsecPortManager;
  FakeMacsecSAManager macsecSAManager;
  FakeMacsecSCManager macsecSCManager;
  FakeMacsecFlowManager macsecFlowManager;
  FakeSystemPortManager systemPortManager;
  bool initialized = false;
  sai_object_id_t cpuPortId;
  sai_object_id_t cpuSystemPortId;
  sai_object_id_t getCpuPort();
  sai_object_id_t getCpuSystemPort();
};

} // namespace facebook::fboss

sai_status_t sai_api_initialize(
    uint64_t flags,
    const sai_service_method_table_t* services);

sai_status_t sai_api_uninitialize(void);

sai_status_t sai_api_query(sai_api_t sai_api_id, void** api_method_table);

sai_status_t sai_log_set(sai_api_t api, sai_log_level_t log_level);

sai_status_t sai_dbg_generate_dump(const char* dump_file_name);

sai_status_t sai_object_type_get_availability(
    sai_object_id_t switch_id,
    sai_object_type_t object_type,
    uint32_t attr_count,
    const sai_attribute_t* attr_list,
    uint64_t* count);
