/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/SwitchApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/SwitchApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _SwitchMap{
    SAI_ATTR_MAP(Switch, InitSwitch),
    SAI_ATTR_MAP(Switch, HwInfo),
    SAI_ATTR_MAP(Switch, SrcMac),
    SAI_ATTR_MAP(Switch, SwitchShellEnable),
    SAI_ATTR_MAP(Switch, EcmpHashV4),
    SAI_ATTR_MAP(Switch, EcmpHashV6),
    SAI_ATTR_MAP(Switch, LagHashV4),
    SAI_ATTR_MAP(Switch, LagHashV6),
    SAI_ATTR_MAP(Switch, EcmpDefaultHashSeed),
    SAI_ATTR_MAP(Switch, LagDefaultHashSeed),
    SAI_ATTR_MAP(Switch, EcmpDefaultHashAlgorithm),
    SAI_ATTR_MAP(Switch, LagDefaultHashAlgorithm),
    SAI_ATTR_MAP(Switch, SwitchRestartWarm),
    SAI_ATTR_MAP(Switch, CpuPort),
    SAI_ATTR_MAP(Switch, SwitchPreShutdown),
    SAI_ATTR_MAP(Switch, DefaultVlanId),
    SAI_ATTR_MAP(Switch, PortNumber),
    SAI_ATTR_MAP(Switch, PortList),
    SAI_ATTR_MAP(Switch, Default1QBridgeId),
    SAI_ATTR_MAP(Switch, DefaultVirtualRouterId),
    SAI_ATTR_MAP(Switch, NumberOfMulticastQueues),
    SAI_ATTR_MAP(Switch, LagHash),
    SAI_ATTR_MAP(Switch, EcmpHash),
    SAI_ATTR_MAP(Switch, QosDscpToTcMap),
    SAI_ATTR_MAP(Switch, QosTcToQueueMap),
    SAI_ATTR_MAP(Switch, QosExpToTcMap),
    SAI_ATTR_MAP(Switch, QosTcToExpMap),
    SAI_ATTR_MAP(Switch, AclEntryMinimumPriority),
    SAI_ATTR_MAP(Switch, AclEntryMaximumPriority),
    SAI_ATTR_MAP(Switch, MacAgingTime),
    SAI_ATTR_MAP(Switch, FdbDstUserMetaDataRange),
    SAI_ATTR_MAP(Switch, RouteDstUserMetaDataRange),
    SAI_ATTR_MAP(Switch, NeighborDstUserMetaDataRange),
    SAI_ATTR_MAP(Switch, PortUserMetaDataRange),
    SAI_ATTR_MAP(Switch, AvailableIpv4RouteEntry),
    SAI_ATTR_MAP(Switch, AvailableIpv6RouteEntry),
    SAI_ATTR_MAP(Switch, AvailableIpv4NextHopEntry),
    SAI_ATTR_MAP(Switch, AvailableIpv6NextHopEntry),
    SAI_ATTR_MAP(Switch, AvailableNextHopGroupEntry),
    SAI_ATTR_MAP(Switch, AvailableNextHopGroupMemberEntry),
    SAI_ATTR_MAP(Switch, AvailableIpv4NeighborEntry),
    SAI_ATTR_MAP(Switch, AvailableIpv6NeighborEntry),
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
    SAI_ATTR_MAP(Switch, AvailableMySidEntry),
#endif
    SAI_ATTR_MAP(Switch, IngressAcl),
    SAI_ATTR_MAP(Switch, EgressAcl),
    SAI_ATTR_MAP(Switch, TamObject),
    SAI_ATTR_MAP(Switch, NumberOfFabricPorts),
    SAI_ATTR_MAP(Switch, FabricPortList),
    SAI_ATTR_MAP(Switch, UseEcnThresholds),
    SAI_ATTR_MAP(Switch, CounterRefreshInterval),
    SAI_ATTR_MAP(Switch, FirmwarePathName),
    SAI_ATTR_MAP(Switch, FirmwareLoadMethod),
    SAI_ATTR_MAP(Switch, FirmwareLoadType),
    SAI_ATTR_MAP(Switch, WarmRecover),
    SAI_ATTR_MAP(Switch, HardwareAccessBus),
    SAI_ATTR_MAP(Switch, PlatformContext),
    SAI_ATTR_MAP(Switch, SwitchProfileId),
    SAI_ATTR_MAP(Switch, FirmwareStatus),
    SAI_ATTR_MAP(Switch, FirmwareMajorVersion),
    SAI_ATTR_MAP(Switch, FirmwareMinorVersion),
    SAI_ATTR_MAP(Switch, PortConnectorList),
    SAI_ATTR_MAP(Switch, SwitchId),
    SAI_ATTR_MAP(Switch, MaxSystemCores),
    SAI_ATTR_MAP(Switch, SysPortConfigList),
    SAI_ATTR_MAP(Switch, SwitchType),
    SAI_ATTR_MAP(Switch, RegisterReadFn),
    SAI_ATTR_MAP(Switch, RegisterWriteFn),
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
    SAI_ATTR_MAP(Switch, MaxEcmpMemberCount),
    SAI_ATTR_MAP(Switch, EcmpMemberCount),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    SAI_ATTR_MAP(Switch, CreditWd),
    SAI_ATTR_MAP(Switch, CreditWdTimer),
#endif
    SAI_ATTR_MAP(Switch, PfcDlrPacketAction),
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    SAI_ATTR_MAP(Switch, ArsProfile),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    SAI_ATTR_MAP(Switch, PtpMode),
    SAI_ATTR_MAP(Switch, RegFatalSwitchAsicSdkHealthCategory),
    SAI_ATTR_MAP(Switch, RegNoticeSwitchAsicSdkHealthCategory),
#endif
    SAI_ATTR_MAP(Switch, AsicTemperatureList),
    SAI_ATTR_MAP(Switch, NumTemperatureSensors),
    SAI_ATTR_MAP(Switch, SwitchingMode),
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
    SAI_ATTR_MAP(Switch, LinkUpDebounceTimeout),
#endif

};

void handleExtensionAttributes() {
  SAI_EXT_ATTR_MAP(Switch, Led)
  SAI_EXT_ATTR_MAP(Switch, LedReset)
  SAI_EXT_ATTR_MAP(Switch, AclFieldList)
  SAI_EXT_ATTR_MAP(Switch, EgressPoolAvailableSize)
  SAI_EXT_ATTR_MAP(Switch, HwEccErrorInitiate)
  SAI_EXT_ATTR_MAP(Switch, WarmBootTargetVersion)
  SAI_EXT_ATTR_MAP(Switch, SwitchIsolate)
  SAI_EXT_ATTR_MAP(Switch, MaxCores)
  SAI_EXT_ATTR_MAP(Switch, DllPath)
  SAI_EXT_ATTR_MAP(Switch, RestartIssu)
  SAI_EXT_ATTR_MAP(Switch, ForceTrafficOverFabric)
  SAI_EXT_ATTR_MAP(Switch, WarmBootTargetVersion)
  SAI_EXT_ATTR_MAP(Switch, SwitchIsolate)
  SAI_EXT_ATTR_MAP(Switch, SdkBootTime)
  SAI_EXT_ATTR_MAP(Switch, FabricRemoteReachablePortList)
  SAI_EXT_ATTR_MAP(Switch, RouteNoImplicitMetaData)
  SAI_EXT_ATTR_MAP(Switch, RouteAllowImplicitMetaData)
  SAI_EXT_ATTR_MAP(Switch, MultiStageLocalSwitchIds)
  SAI_EXT_ATTR_MAP(Switch, VoqLatencyMinLocalNs);
  SAI_EXT_ATTR_MAP(Switch, VoqLatencyMaxLocalNs);
  SAI_EXT_ATTR_MAP(Switch, VoqLatencyMinLevel1Ns);
  SAI_EXT_ATTR_MAP(Switch, VoqLatencyMaxLevel1Ns);
  SAI_EXT_ATTR_MAP(Switch, VoqLatencyMinLevel2Ns);
  SAI_EXT_ATTR_MAP(Switch, VoqLatencyMaxLevel2Ns);
  SAI_EXT_ATTR_MAP(Switch, ReachabilityGroupList);
  SAI_EXT_ATTR_MAP(Switch, DelayDropCongThreshold);
  SAI_EXT_ATTR_MAP(Switch, FabricLinkLayerFlowControlThreshold);
  SAI_EXT_ATTR_MAP(Switch, SramFreePercentXoffTh);
  SAI_EXT_ATTR_MAP(Switch, SramFreePercentXonTh);
  SAI_EXT_ATTR_MAP(Switch, NoAclsForTraps);
  SAI_EXT_ATTR_MAP(Switch, MaxSystemPortId);
  SAI_EXT_ATTR_MAP(Switch, MaxLocalSystemPortId);
  SAI_EXT_ATTR_MAP(Switch, MaxSystemPorts);
  SAI_EXT_ATTR_MAP(Switch, MaxVoqs);
  SAI_EXT_ATTR_MAP(Switch, FabricCllfcTxCreditTh);
  SAI_EXT_ATTR_MAP(Switch, VoqDramBoundTh);
  SAI_EXT_ATTR_MAP(Switch, CondEntropyRehashPeriodUS);
  SAI_EXT_ATTR_MAP(Switch, ShelSrcIp);
  SAI_EXT_ATTR_MAP(Switch, ShelDstIp);
  SAI_EXT_ATTR_MAP(Switch, ShelSrcMac);
  SAI_EXT_ATTR_MAP(Switch, ShelPeriodicInterval);
  SAI_EXT_ATTR_MAP(Switch, FirmwareCoreToUse);
  SAI_EXT_ATTR_MAP(Switch, FirmwareLogFile);
  SAI_EXT_ATTR_MAP(Switch, MaxSwitchId);
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  SAI_EXT_ATTR_MAP(Switch, ArsAvailableFlows);
#endif
  SAI_EXT_ATTR_MAP(Switch, SflowAggrNofSamples);
  SAI_EXT_ATTR_MAP(Switch, SdkRegDumpLogPath);
  SAI_EXT_ATTR_MAP(Switch, SdkDumpRateLimitWindow);
  SAI_EXT_ATTR_MAP(Switch, SdkDumpSuppressedCount);
  SAI_EXT_ATTR_MAP(Switch, FirmwareObjectList);
  SAI_EXT_ATTR_MAP(Switch, TcRateLimitList);
  SAI_EXT_ATTR_MAP(Switch, PfcTcDldTimerGranularityInterval);
  SAI_EXT_ATTR_MAP(Switch, NumberOfPipes);
  SAI_EXT_ATTR_MAP(Switch, PipelineObjectList);
  SAI_EXT_ATTR_MAP(Switch, DisableSllAndHllTimeout);
  SAI_EXT_ATTR_MAP(Switch, AsicRevision);
  SAI_EXT_ATTR_MAP(Switch, CreditRequestProfileSchedulerMode);
  SAI_EXT_ATTR_MAP(Switch, ModuleIdToCreditRequestProfileParamList);
  SAI_EXT_ATTR_MAP(Switch, TriggerSimulatedEccCorrectableError)
  SAI_EXT_ATTR_MAP(Switch, TriggerSimulatedEccUnCorrectableError)
  SAI_EXT_ATTR_MAP(Switch, DefaultCpuEgressBufferPool)
  SAI_EXT_ATTR_MAP(Switch, TechSupportType)
  SAI_EXT_ATTR_MAP(Switch, ModuleIdFabricPortList)
  SAI_EXT_ATTR_MAP(Switch, PfcMonitorEnable);
  SAI_EXT_ATTR_MAP(Switch, CablePropagationDelayMeasurement);
  SAI_EXT_ATTR_MAP(Switch, PortCl72RetryEnable);
  SAI_EXT_ATTR_MAP(Switch, AclDropSnoopDisabled);
  SAI_EXT_ATTR_MAP(Switch, PacketDropTypeIngressList);
  SAI_EXT_ATTR_MAP(Switch, PacketDropTypeEgressList);
#if defined(BRCM_SAI_SDK_XGS_AND_DNX)
  SAI_EXT_ATTR_MAP(Switch, LocalSystemPortIdRangeList)
#endif
#if defined(BRCM_PAI_SDK)
  SAI_EXT_ATTR_MAP(Switch, SyncLock)
  SAI_EXT_ATTR_MAP(Switch, SyncUnlock)
#endif
}

} // namespace

namespace facebook::fboss {

sai_status_t wrap_create_switch(
    sai_object_id_t* switch_id,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto begin = FLAGS_enable_elapsed_time_log
      ? std::chrono::system_clock::now()
      : std::chrono::system_clock::time_point::min();
  SaiTracer::getInstance()->logSwitchCreateFn(switch_id, attr_count, attr_list);
  auto rv = SaiTracer::getInstance()->switchApi_->create_switch(
      switch_id, attr_count, attr_list);
  SaiTracer::getInstance()->logPostInvocation(rv, *switch_id, begin);
  return rv;
}

WRAP_REMOVE_FUNC(switch, SAI_OBJECT_TYPE_SWITCH, switch);
WRAP_SET_ATTR_FUNC_WITH_FILTER(
    switch,
    SAI_OBJECT_TYPE_SWITCH,
    switch,
    SAI_SWITCH_ATTR_SWITCH_SHELL_ENABLE);
WRAP_GET_ATTR_FUNC(switch, SAI_OBJECT_TYPE_SWITCH, switch);
WRAP_GET_STATS_FUNC(switch, SAI_OBJECT_TYPE_SWITCH, switch);
WRAP_GET_STATS_EXT_FUNC(switch, SAI_OBJECT_TYPE_SWITCH, switch);
WRAP_CLEAR_STATS_FUNC(switch, SAI_OBJECT_TYPE_SWITCH, switch);

sai_switch_api_t* wrappedSwitchApi() {
  handleExtensionAttributes();
  static sai_switch_api_t switchWrappers;

  switchWrappers.create_switch = &wrap_create_switch;
  switchWrappers.remove_switch = &wrap_remove_switch;
  switchWrappers.set_switch_attribute = &wrap_set_switch_attribute;
  switchWrappers.get_switch_attribute = &wrap_get_switch_attribute;
  switchWrappers.get_switch_stats = &wrap_get_switch_stats;
  switchWrappers.get_switch_stats_ext = &wrap_get_switch_stats_ext;
  switchWrappers.clear_switch_stats = &wrap_clear_switch_stats;

  return &switchWrappers;
}

SET_SAI_ATTRIBUTES(Switch)

} // namespace facebook::fboss
