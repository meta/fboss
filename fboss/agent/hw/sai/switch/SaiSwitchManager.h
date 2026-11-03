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

#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/api/VirtualRouterApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiHashManager.h"
#include "fboss/agent/hw/sai/switch/SaiQosMapManager.h"
#include "fboss/agent/types.h"

#include <folly/MacAddress.h>

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace folly {
struct dynamic;
}
namespace facebook::fboss {

class LoadBalancer;
class SaiManagerTable;
class SaiPlatform;
class StateDelta;

#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
using SaiSwitchPipeline = SaiObjectWithCounters<SaiSwitchPipelineTraits>;
#endif

class SaiSwitchManager {
  static constexpr auto kBitsPerByte = 8;

 public:
  SaiSwitchManager(
      SaiManagerTable* managerTable,
      SaiPlatform* platform,
      BootType bootType,
      cfg::SwitchType switchType,
      std::optional<int64_t> switchId);
  SwitchSaiId getSwitchSaiId() const;

  void setQosPolicy();
  void clearQosPolicy();
  void addOrUpdateLoadBalancer(const std::shared_ptr<LoadBalancer>& newLb);
  void changeLoadBalancer(
      const std::shared_ptr<LoadBalancer>& oldLb,
      const std::shared_ptr<LoadBalancer>& newLb);
  void removeLoadBalancer(const std::shared_ptr<LoadBalancer>& oldLb);

  void resetHashes();
  void resetQosMaps();
  void gracefulExit();

  void setIngressAcl();
  void setIngressAcl(sai_object_id_t id);
  void resetIngressAcl();

  void setEgressAcl();
  void resetEgressAcl();

  void setTamObject(std::vector<sai_object_id_t> tamObject);
  void resetTamObject();

  void setArsProfile(ArsProfileSaiId arsProfileSaiId);
  void resetArsProfile();

  void setMacAgingSeconds(sai_uint32_t agingSeconds);
  sai_uint32_t getMacAgingSeconds() const;

  void setupCounterRefreshInterval();

  sai_object_id_t getDefaultVlanAdapterKey() const;

  PortSaiId getCpuPort() const;
  std::optional<PortSaiId> getCpuRecyclePort() const;

  void initCpuPort();
  void setCpuRecyclePort(PortSaiId portSaiId);

  void setPtpTcEnabled(bool ptpEnable);
  std::optional<bool> getPtpTcEnabled();

  bool isGlobalQoSMapSupported() const;
  bool isMplsQoSMapSupported() const;

  void updateStats(bool updateWatermarks);

  void setSwitchIsolate(bool isolate);
  HwSwitchDropStats getSwitchDropStats() const {
    return switchDropStats_;
  }
  void setForceTrafficOverFabric(bool forceTrafficOverFabric);
  void setCreditWatchdog(bool creditWatchdog);
  HwSwitchWatermarkStats getSwitchWatermarkStats() const {
    return switchWatermarkStats_;
  }
  HwSwitchPipelineStats getSwitchPipelineStats() const {
    return switchPipelineStats_;
  }
  HwSwitchTemperatureStats getSwitchTemperatureStats() const {
    return switchTemperatureStats_;
  }
  HwSwitchDropBitmapStats getSwitchDropBitmapStats() const {
    return switchDropBitmapStats_;
  }
  void setLocalCapsuleSwitchIds(
      const std::map<SwitchID, int>& switchIdToNumCores);
  void setReachabilityGroupList(const std::vector<int>& reachabilityGroups);
  void setSramGlobalFreePercentXoffTh(uint8_t sramFreePercentXoffThreshold);
  void setSramGlobalFreePercentXonTh(uint8_t sramFreePercentXonThreshold);
  void setLinkFlowControlCreditTh(uint16_t linkFlowControlThreshold);
  void setVoqDramBoundTh(uint32_t dramBoundThreshold);
  void setConditionalEntropyRehashPeriodUS(
      int conditionalEntropyRehashPeriodUS);
  void setLinkUpDebounceTimeout(uint32_t linkUpDebounceTimeoutUs);
  uint32_t getLinkUpDebounceTimeout() const;
  void setShelConfig(
      const std::optional<cfg::SelfHealingEcmpLagConfig>& shelConfig);
  void setLocalVoqMaxExpectedLatency(int localVoqMaxExpectedLatencyNsec);
  void setRemoteL1VoqMaxExpectedLatency(int remoteL1VoqMaxExpectedLatencyNsec);
  void setRemoteL2VoqMaxExpectedLatency(int remoteL2VoqMaxExpectedLatencyNsec);
  void setVoqOutOfBoundsLatency(int voqOutOfBoundsLatencyNsec);
  void setTcRateLimitList(
      const std::optional<std::map<int32_t, int32_t>>& tcToRateLimitKbps);
  bool isPtpTcEnabled() const;
  bool isMeasureCableLengthEnabled() const;
  void setPfcWatchdogTimerGranularity(int pfcWatchdogTimerGranularityMsec);
  void setEnablePfcMonitoring(bool enablePfcMonitoring);
  void setCreditRequestProfileSchedulerMode(cfg::QueueScheduling scheduling);
  void setSwitchingMode(cfg::PacketForwardingMode mode);
  void setModuleIdToCreditRequestProfileParam(
      const std::optional<std::map<int32_t, int32_t>>&
          moduleIdToCreditRequestProfileParam);

 private:
  void programEcmpLoadBalancerParams(
      std::optional<sai_uint32_t> seed,
      std::optional<cfg::HashingAlgorithm> algo);
  void addOrUpdateEcmpLoadBalancer(const std::shared_ptr<LoadBalancer>& newLb);
  void updateSramLowBufferLimitHitCounter();
  void updateSdkDumpSuppressedCounter();

  void programLagLoadBalancerParams(
      std::optional<sai_uint32_t> seed,
      std::optional<cfg::HashingAlgorithm> algo);
  void addOrUpdateLagLoadBalancer(const std::shared_ptr<LoadBalancer>& newLb);

  std::vector<sai_object_id_t> getUdfGroupIds(
      const std::shared_ptr<LoadBalancer>& newLb) const;

  template <typename HashAttrT>
  SaiHashTraits::CreateAttributes getProgrammedHashAttr();

  template <typename HashAttrT>
  void setLoadBalancer(
      const std::shared_ptr<SaiHash>& hash,
      SaiHashTraits::CreateAttributes& hashCreateAttrs);
  template <typename HashAttrT>
  void resetLoadBalancer();

  void setEgressAcl(sai_object_id_t id);

  const std::vector<sai_stat_id_t>& supportedDropStats() const;
  const std::vector<sai_stat_id_t>& supportedDramStats() const;
  const std::vector<sai_stat_id_t>& supportedDramReadOnlyStats() const;
  const std::vector<sai_stat_id_t>& supportedWatermarkStats() const;
  const std::vector<sai_stat_id_t>& supportedCreditStats() const;
  const std::vector<sai_stat_id_t>& supportedErrorStats() const;
  const std::vector<sai_stat_id_t>& supportedSaiExtensionDropStats() const;
  const std::vector<sai_stat_id_t>& supportedPipelineWatermarkStats() const;
  const std::vector<sai_stat_id_t>& supportedPipelineStats() const;
  const std::vector<sai_stat_id_t>& supportedCustomDropBitmapStats() const;
  void updateDropReasonStats();
  const std::vector<sai_attr_id_t>& supportedTemperatureStats() const;
  const HwSwitchWatermarkStats getHwSwitchWatermarkStats() const;
  const HwSwitchPipelineStats getHwSwitchPipelineStats(
      bool updateWatermarks) const;
  const HwSwitchTemperatureStats getHwSwitchTemperatureStats() const;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
  std::unique_ptr<SaiSwitchObj> switch_;
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  std::vector<std::shared_ptr<SaiSwitchPipeline>> switchPipelines_;
#endif
  std::optional<PortSaiId> cpuPort_;
  std::optional<PortSaiId> cpuRecyclePort_;
  std::shared_ptr<SaiHash> ecmpV4Hash_;
  std::shared_ptr<SaiHash> ecmpV6Hash_;
  std::shared_ptr<SaiHash> lagV4Hash_;
  std::shared_ptr<SaiHash> lagV6Hash_;
  std::shared_ptr<SaiQosMap> globalDscpToTcQosMap_;
  std::shared_ptr<SaiQosMap> globalTcToQueueQosMap_;
  std::shared_ptr<SaiQosMap> globalExpToTcQosMap_;
  std::shared_ptr<SaiQosMap> globalTcToExpQosMap_;

  bool isMplsQosSupported_{false};
  bool isIngressPostLookupAclSupported_{false};
  // since this is an optional attribute in SAI
  std::optional<bool> isPtpTcEnabled_{std::nullopt};
  HwSwitchDropStats switchDropStats_;
  HwSwitchDropBitmapStats switchDropBitmapStats_;
  HwSwitchWatermarkStats switchWatermarkStats_;
  HwSwitchPipelineStats switchPipelineStats_;
  HwSwitchTemperatureStats switchTemperatureStats_;
};

void fillHwSwitchDramStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchDramStats& hwSwitchDramStats);
void fillHwSwitchWatermarkStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchWatermarkStats& hwSwitchWatermarkStats);
void fillHwSwitchCreditStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchCreditStats& hwSwitchCreditStats);
void fillHwSwitchErrorStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchDropStats& switchDropStats);
void fillHwSwitchPipelineStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    int idx,
    HwSwitchPipelineStats& switchPipelineStats);
void fillHwSwitchSaiExtensionDropStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchDropStats& dropStats);
void fillHwSwitchDropBitmapStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchDropBitmapStats& dropBitmapStats);
void logDropBitmapReasons(const HwSwitchDropBitmapStats& stats);
// Decode raw SAI drop reason enum values into SDK independent names. The value
// spaces are vendor specific, so this is implemented per platform.
void logDropReasons(
    const std::vector<sai_int32_t>& ingressDropReasons,
    const std::vector<sai_int32_t>& egressDropReasons);
void publishSwitchWatermarks(HwSwitchWatermarkStats& watermarkStats);
void publishSwitchPipelineStats(HwSwitchPipelineStats& pipelineStats);
void publishSwitchTemperatureStats(HwSwitchTemperatureStats& temperatureStats);
} // namespace facebook::fboss
