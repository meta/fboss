/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/DsfNodeUtils.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/HwSwitchFb303Stats.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/api/SwitchApi.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/switch/SaiAclTableGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiBufferManager.h"
#include "fboss/agent/hw/sai/switch/SaiDebugCounterManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortUtils.h"
#include "fboss/agent/hw/sai/switch/SaiUdfManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/switch_asics/Jericho3Asic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/state/LoadBalancer.h"

#include <folly/logging/xlog.h>

#include <re2/re2.h>

extern "C" {
#include <sai.h>
}

DEFINE_bool(
    skip_setting_src_mac,
    false,
    "Flag to indicate whether to skip setting source mac in Sai switch during wb");

namespace {
using namespace facebook::fboss;
sai_hash_algorithm_t toSaiHashAlgo(cfg::HashingAlgorithm algo) {
  switch (algo) {
    case cfg::HashingAlgorithm::CRC:
      return SAI_HASH_ALGORITHM_CRC;
    case cfg::HashingAlgorithm::CRC16_CCITT:
      return SAI_HASH_ALGORITHM_CRC_CCITT;
    case cfg::HashingAlgorithm::CRC32_LO:
      return SAI_HASH_ALGORITHM_CRC_32LO;
    case cfg::HashingAlgorithm::CRC32_HI:
      return SAI_HASH_ALGORITHM_CRC_32HI;
    case cfg::HashingAlgorithm::CRC32_ETHERNET_LO:
    case cfg::HashingAlgorithm::CRC32_ETHERNET_HI:
    case cfg::HashingAlgorithm::CRC32_KOOPMAN_LO:
    case cfg::HashingAlgorithm::CRC32_KOOPMAN_HI:
    default:
      throw FbossError("Unsupported hash algorithm :", algo);
  }
}

bool isJerichoAsic(cfg::AsicType asicType) {
  return asicType == cfg::AsicType::ASIC_TYPE_JERICHO2 ||
      asicType == cfg::AsicType::ASIC_TYPE_JERICHO3 ||
      asicType == cfg::AsicType::ASIC_TYPE_JERICHO4;
}

void fillHwSwitchDropStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchDropStats& hwSwitchDropStats,
    cfg::AsicType asicType) {
  auto fillAsicSpecificCounter = [](auto counterId,
                                    auto val,
                                    auto asicType,
                                    auto& dropStats) {
    if (!isJerichoAsic(asicType)) {
      throw FbossError("Configured drop reason stats only supported for J2/J3");
    }
    switch (counterId) {
      /*
       * SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_0_DROPPED_PKTS -
       * FDR cell drops
       * SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_1_DROPPED_PKTS -
       * Reassembly drops due to corrupted cells
       * SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_2_DROPPED_PKTS -
       * Reassembly drops due to missing cells
       */
      case SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_0_DROPPED_PKTS:
        dropStats.fdrCellDrops() = val;
        break;
      case SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_1_DROPPED_PKTS:
        dropStats.corruptedCellPacketIntegrityDrops() = val;
        break;
      case SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_2_DROPPED_PKTS:
        dropStats.missingCellPacketIntegrityDrops() = val;
        break;
      /*
       * From CS00012306170
       * SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_0_DROPPED_PKTS - VOQ
       * resource exhaustion drops
       * SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_1_DROPPED_PKTS - Global
       * resource exhaustion drops
       * SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_2_DROPPED_PKTS - SRAM
       * resource exhaustion drops
       * SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_3_DROPPED_PKTS - VSQ
       * resource exhaustion drops
       * SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_4_DROPPED_PKTS - Drop
       * precedence drops
       * SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_5_DROPPED_PKTS - Queue
       * resolution drops
       * SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_6_DROPPED_PKTS - Ingress PP
       * VOQ drops due to PP reject bit
       */
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_0_DROPPED_PKTS:
        dropStats.voqResourceExhaustionDrops() = val;
        break;
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_1_DROPPED_PKTS:
        dropStats.globalResourceExhaustionDrops() = val;
        break;
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_2_DROPPED_PKTS:
        dropStats.sramResourceExhaustionDrops() = val;
        break;
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_3_DROPPED_PKTS:
        dropStats.vsqResourceExhaustionDrops() = val;
        break;
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_4_DROPPED_PKTS:
        dropStats.dropPrecedenceDrops() = val;
        break;
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_5_DROPPED_PKTS:
        dropStats.queueResolutionDrops() = val;
        break;
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_6_DROPPED_PKTS:
        dropStats.ingressPacketPipelineRejectDrops() = val;
        break;
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
      case SAI_SWITCH_STAT_TC0_RATE_LIMIT_DROPPED_PACKETS:
        dropStats.tc0RateLimitDrops() = val;
        break;
#endif
      default:
        throw FbossError("Unexpected configured counter id: ", counterId);
    }
  };
  for (auto counterIdAndValue : counterId2Value) {
    auto [counterId, value] = counterIdAndValue;
    switch (counterId) {
      case SAI_SWITCH_STAT_REACHABILITY_DROP:
        hwSwitchDropStats.globalReachabilityDrops() = value;
        break;
      case SAI_SWITCH_STAT_GLOBAL_DROP:
        hwSwitchDropStats.globalDrops() = value;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      case SAI_SWITCH_STAT_PACKET_INTEGRITY_DROP:
        hwSwitchDropStats.packetIntegrityDrops() = value;
        break;
#endif
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_0_DROPPED_PKTS:
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_1_DROPPED_PKTS:
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_2_DROPPED_PKTS:
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_3_DROPPED_PKTS:
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_4_DROPPED_PKTS:
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_5_DROPPED_PKTS:
      case SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_6_DROPPED_PKTS:
      case SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_0_DROPPED_PKTS:
      case SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_1_DROPPED_PKTS:
      case SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_2_DROPPED_PKTS:
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
      case SAI_SWITCH_STAT_TC0_RATE_LIMIT_DROPPED_PACKETS:
#endif
        fillAsicSpecificCounter(counterId, value, asicType, hwSwitchDropStats);
        break;
      default:
        throw FbossError("Got unexpected switch counter id: ", counterId);
    }
  }
}

void fillHwSwitchTemperatureStats(
    const folly::F14FastMap<sai_attr_id_t, sai_attribute_value_t>& attrId2Value,
    HwSwitchTemperatureStats& hwSwitchTemperatureStats) {
  for (auto attrIdAndValue : attrId2Value) {
    auto [attrId, value] = attrIdAndValue;
    if (attrId == SAI_SWITCH_ATTR_TEMP_LIST) {
      auto timeStamp =
          std::chrono::system_clock::now().time_since_epoch().count();
      for (uint32_t i = 0; i < value.s32list.count; i++) {
        auto sensorName = std::to_string(i);
        hwSwitchTemperatureStats.timeStamp()->insert({sensorName, timeStamp});
        hwSwitchTemperatureStats.value()->insert(
            {sensorName, value.s32list.list[i]});
      }
    }
  }
}

} // namespace

namespace facebook::fboss {

SaiSwitchManager::SaiSwitchManager(
    SaiManagerTable* managerTable,
    SaiPlatform* platform,
    BootType bootType,
    cfg::SwitchType switchType,
    std::optional<int64_t> switchId)
    : managerTable_(managerTable), platform_(platform) {
  int64_t swId = switchId.value_or(0);
  if (bootType == BootType::WARM_BOOT) {
    // Extract switch adapter key and create switch only with the mandatory
    // init attribute (warm boot path)
    auto& switchApi = SaiApiTable::getInstance()->switchApi();
    auto newSwitchId = switchApi.create<SaiSwitchTraits>(
        platform->getSwitchAttributes(true, switchType, switchId, bootType),
        swId);
    // Load all switch attributes
    switch_ = std::make_unique<SaiSwitchObj>(newSwitchId);
    if (switchType != cfg::SwitchType::FABRIC &&
        switchType != cfg::SwitchType::PHY) {
      if (!FLAGS_skip_setting_src_mac) {
        switch_->setOptionalAttribute(
            SaiSwitchTraits::Attributes::SrcMac{platform->getLocalMac()});
      }
      switch_->setOptionalAttribute(
          SaiSwitchTraits::Attributes::MacAgingTime{
              platform->getDefaultMacAgingTime()});
    }
    if (switchType == cfg::SwitchType::VOQ) {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
      // We learnt of the need to set a non-default max switch id on J3 only
      // later. And we incorporated it in create switch attributes.
      // However, BRCM-SAI does not look at create switch attributes on
      // warm boot. So, to cater to the systems which will only
      // WB to this change, set the attribute post WB. It should
      // be a no-op if already set.
      //
      // Only J3 constrains MaxSwitchId below the HW default (R3/J3 HW bug,
      // see getSwitchAttributes() in SaiPlatform.cpp which gates the create
      // attribute to R3/J3). Mirror that condition here: standalone DNX
      // devices such as Q4D are not part of a DSF cluster and their SDK
      // rejects a constrained MaxSwitchId, so leave them at the HW default.
      if (platform_->getAsic()->getAsicType() ==
          cfg::AsicType::ASIC_TYPE_JERICHO3) {
        switch_->setOptionalAttribute(
            SaiSwitchTraits::Attributes::MaxSwitchId{
                utility::getDsfVoqSwitchMaxSwitchId()});
      }
#endif
    }
  } else {
    switch_ = std::make_unique<SaiSwitchObj>(
        std::monostate(),
        platform->getSwitchAttributes(false, switchType, switchId, bootType),
        swId);

    const auto& asic = platform_->getAsic();
    if (asic->isSupported(HwAsic::Feature::ECMP_HASH_V4)) {
      resetLoadBalancer<SaiSwitchTraits::Attributes::EcmpHashV4>();
    }
    if (asic->isSupported(HwAsic::Feature::ECMP_HASH_V6)) {
      resetLoadBalancer<SaiSwitchTraits::Attributes::EcmpHashV6>();
    }
    if (asic->isSupported(HwAsic::Feature::SAI_LAG_HASH)) {
      resetLoadBalancer<SaiSwitchTraits::Attributes::LagHashV4>();
      resetLoadBalancer<SaiSwitchTraits::Attributes::LagHashV6>();
    }
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
    if (asic->isSupported(HwAsic::Feature::ECMP_MEMBER_WIDTH_INTROSPECTION)) {
      auto maxEcmpCount = SaiApiTable::getInstance()->switchApi().getAttribute(
          switch_->adapterKey(),
          SaiSwitchTraits::Attributes::MaxEcmpMemberCount{});
      XLOG(DBG2) << "Got max ecmp member count " << maxEcmpCount;
      // Reserving the ASIC maximum wastes ECMP member table entries, since
      // groups are never programmed wider than FLAGS_ecmp_width.
      auto ecmpMemberCount =
          std::min<sai_uint32_t>(FLAGS_ecmp_width, maxEcmpCount);
      XLOG(DBG2) << "Setting ecmp member count to " << ecmpMemberCount;
      switch_->setOptionalAttribute(
          SaiSwitchTraits::Attributes::EcmpMemberCount{ecmpMemberCount});
    }
#endif
  }

  if (platform_->getAsic()->isSupported(HwAsic::Feature::CPU_PORT)) {
    initCpuPort();
  }
  // TODO (Q4D/J4/R4): Enable switch pipeline stats after SDK support in 15.x
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0) && !defined(BRCM_SAI_SDK_DNX_GTE_15_0)
  // load switch pipeline sai ids
  int numPipelines = SaiApiTable::getInstance()->switchApi().getAttribute(
      switch_->adapterKey(), SaiSwitchTraits::Attributes::NumberOfPipes{});
  std::vector<sai_object_id_t> pipelineList;
  pipelineList.resize(numPipelines);
  SaiSwitchTraits::Attributes::PipelineObjectList pipelineListAttribute{
      pipelineList};
  auto pipelineSaiIdList = SaiApiTable::getInstance()->switchApi().getAttribute(
      switch_->adapterKey(), pipelineListAttribute);
  if (pipelineSaiIdList.size() == 0) {
    throw FbossError("no pipeline exists");
  }
  std::vector<SwitchPipelineSaiId> pipelineSaiIds;
  pipelineSaiIds.reserve(pipelineSaiIdList.size());
  std::transform(
      pipelineSaiIdList.begin(),
      pipelineSaiIdList.end(),
      std::back_inserter(pipelineSaiIds),
      [](sai_object_id_t pipelineId) -> SwitchPipelineSaiId {
        return SwitchPipelineSaiId(pipelineId);
      });
  // create switch  pipeline sai objects
  for (auto pipelineSaiId : pipelineSaiIds) {
    std::shared_ptr switchPipeline =
        std::make_shared<SaiSwitchPipeline>(pipelineSaiId);
    switchPipeline->setOwnedByAdapter(true);
    XLOG(DBG2) << "loaded switch pipeline sai object " << pipelineSaiId
               << " with idx " << switchPipeline->adapterHostKey().value();
    switchPipelines_.push_back(switchPipeline);
  }
#endif
  isMplsQosSupported_ = isMplsQoSMapSupported();
}

void SaiSwitchManager::initCpuPort() {
  cpuPort_ = SaiApiTable::getInstance()->switchApi().getAttribute(
      switch_->adapterKey(), SaiSwitchTraits::Attributes::CpuPort{});
}

PortSaiId SaiSwitchManager::getCpuPort() const {
  if (cpuPort_) {
    return *cpuPort_;
  }
  throw FbossError("getCpuPort not supported on Phy");
}

void SaiSwitchManager::setCpuRecyclePort(PortSaiId portSaiId) {
  cpuRecyclePort_ = portSaiId;
}

std::optional<PortSaiId> SaiSwitchManager::getCpuRecyclePort() const {
  return cpuRecyclePort_;
}

SwitchSaiId SaiSwitchManager::getSwitchSaiId() const {
  if (!switch_) {
    throw FbossError("failed to get switch id: switch not initialized");
  }
  return switch_->adapterKey();
}

void SaiSwitchManager::resetHashes() {
  if (ecmpV4Hash_) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::EcmpHashV4{SAI_NULL_OBJECT_ID});
  }
  if (ecmpV6Hash_) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::EcmpHashV6{SAI_NULL_OBJECT_ID});
  }
  if (lagV4Hash_) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::LagHashV4{SAI_NULL_OBJECT_ID});
  }
  if (lagV6Hash_) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::LagHashV6{SAI_NULL_OBJECT_ID});
  }
  ecmpV4Hash_.reset();
  ecmpV6Hash_.reset();
  lagV4Hash_.reset();
  lagV6Hash_.reset();
}

void SaiSwitchManager::resetQosMaps() {
  // Since Platform owns Asic, as well as SaiSwitch, which results
  // in blowing up asic before switch (due to destructor order details)
  // as a result, we can only rely on the validity of the global map pointer
  // to gate reset. This should only be true if resetting is supported and
  // would do something meaningful.
  if (globalDscpToTcQosMap_) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::QosDscpToTcMap{SAI_NULL_OBJECT_ID});
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::QosTcToQueueMap{SAI_NULL_OBJECT_ID});
    globalDscpToTcQosMap_.reset();
    globalTcToQueueQosMap_.reset();
  }
  if (isMplsQosSupported_) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::QosExpToTcMap{SAI_NULL_OBJECT_ID});
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::QosTcToExpMap{SAI_NULL_OBJECT_ID});
    if (globalExpToTcQosMap_) {
      globalExpToTcQosMap_.reset();
      globalTcToExpQosMap_.reset();
    }
  }
}

void SaiSwitchManager::programEcmpLoadBalancerParams(
    std::optional<sai_uint32_t> seed,
    std::optional<cfg::HashingAlgorithm> algo) {
  auto hashSeed = seed ? seed.value() : 0;
  auto hashAlgo = algo ? toSaiHashAlgo(algo.value()) : SAI_HASH_ALGORITHM_CRC;
  size_t kMaskLimit = sizeof(int) * kBitsPerByte;
  auto maxHashSeedLength = platform_->getAsic()->getMaxHashSeedLength();
  CHECK(maxHashSeedLength <= kMaskLimit);
  int mask = (maxHashSeedLength == kMaskLimit)
      ? -1
      : static_cast<int>(pow(2.0, maxHashSeedLength) - 1);
  hashSeed &= mask;
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::EcmpDefaultHashSeed{hashSeed});
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SAI_ECMP_HASH_ALGORITHM)) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::EcmpDefaultHashAlgorithm{hashAlgo});
  }
}

template <typename HashAttrT>
SaiHashTraits::CreateAttributes SaiSwitchManager::getProgrammedHashAttr() {
  SaiHashTraits::CreateAttributes hashCreateAttrs{std::nullopt, std::nullopt};
  auto& [nativeHashFieldList, udfGroupList] = hashCreateAttrs;

  auto programmedHash =
      std::get<std::optional<HashAttrT>>(switch_->attributes());
  if (!programmedHash ||
      (programmedHash.value().value() == SAI_NULL_OBJECT_ID)) {
    return hashCreateAttrs;
  }

  auto programmedNativeHashFieldList =
      SaiApiTable::getInstance()->hashApi().getAttribute(
          HashSaiId{programmedHash.value().value()},
          SaiHashTraits::Attributes::NativeHashFieldList{});
  if (!programmedNativeHashFieldList.empty()) {
    nativeHashFieldList = programmedNativeHashFieldList;
  }

  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::UDF_HASH_FIELD_QUERY)) {
    auto programmedUDFGroupList =
        SaiApiTable::getInstance()->hashApi().getAttribute(
            HashSaiId{programmedHash.value().value()},
            SaiHashTraits::Attributes::UDFGroupList{});

    if (!programmedUDFGroupList.empty()) {
      udfGroupList = programmedUDFGroupList;
    }
  }

  return hashCreateAttrs;
}

template <typename HashAttrT>
void SaiSwitchManager::setLoadBalancer(
    const std::shared_ptr<SaiHash>& hash,
    SaiHashTraits::CreateAttributes& programmedHashCreateAttrs) {
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SAI_HASH_FIELDS_CLEAR_BEFORE_SET)) {
    // This code patch is called during cold boot as well as warm boot but the
    // if check is true only in following cases:
    //     - during cold boot (very first time setting load balancer),
    //     - during warm boot (if config has load balancer changed post warm
    //     boot),
    //     - during reload config if load balancer config is updated.
    //
    // The check is false during warm boot if the config does not carry any
    // changes to the load balancer (common case).
    auto newHashCreateAttrs = hash->attributes();
    if (programmedHashCreateAttrs != newHashCreateAttrs) {
      resetLoadBalancer<HashAttrT>();
    }
  }

  switch_->setOptionalAttribute(HashAttrT{hash->adapterKey()});
}

template <typename HashAttrT>
void SaiSwitchManager::resetLoadBalancer() {
  // On some SAI implementations, setting new hash field has the effect of
  // setting hash fields to union of the current hash fields and the new
  // hash fields. This behavior is incorrect and will be fixed in the long
  // run (CS00012187635). In the meanwhile, clear and set desired fields as a
  // workaround.
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SAI_HASH_FIELDS_CLEAR_BEFORE_SET)) {
    switch_->setOptionalAttribute(HashAttrT{SAI_NULL_OBJECT_ID});
  }
}

void SaiSwitchManager::addOrUpdateEcmpLoadBalancer(
    const std::shared_ptr<LoadBalancer>& newLb) {
  programEcmpLoadBalancerParams(newLb->getSeed(), newLb->getAlgorithm());

  // Get UdfGroup Ids if supported.
  auto udfGroupIds = getUdfGroupIds(newLb);

  if (newLb->getIPv4Fields().begin() != newLb->getIPv4Fields().end()) {
    // v4 ECMP
    auto programmedLoadBalancer =
        getProgrammedHashAttr<SaiSwitchTraits::Attributes::EcmpHashV4>();

    cfg::Fields v4EcmpHashFields;
    std::for_each(
        newLb->getIPv4Fields().begin(),
        newLb->getIPv4Fields().end(),
        [&v4EcmpHashFields](const auto& entry) {
          v4EcmpHashFields.ipv4Fields()->insert(entry->cref());
        });
    std::for_each(
        newLb->getTransportFields().begin(),
        newLb->getTransportFields().end(),
        [&v4EcmpHashFields](const auto& entry) {
          v4EcmpHashFields.transportFields()->insert(entry->cref());
        });

    auto hash =
        managerTable_->hashManager().getOrCreate(v4EcmpHashFields, udfGroupIds);

    // Set the new ecmp v4 hash attribute on switch obj
    setLoadBalancer<SaiSwitchTraits::Attributes::EcmpHashV4>(
        hash, programmedLoadBalancer);

    ecmpV4Hash_ = std::move(hash);
  }
  if (newLb->getIPv6Fields().begin() != newLb->getIPv6Fields().end()) {
    // v6 ECMP
    auto programmedLoadBalancer =
        getProgrammedHashAttr<SaiSwitchTraits::Attributes::EcmpHashV6>();

    cfg::Fields v6EcmpHashFields;
    std::for_each(
        newLb->getIPv6Fields().begin(),
        newLb->getIPv6Fields().end(),
        [&v6EcmpHashFields](const auto& entry) {
          v6EcmpHashFields.ipv6Fields()->insert(entry->cref());
        });

    std::for_each(
        newLb->getTransportFields().begin(),
        newLb->getTransportFields().end(),
        [&v6EcmpHashFields](const auto& entry) {
          v6EcmpHashFields.transportFields()->insert(entry->cref());
        });
    auto hash =
        managerTable_->hashManager().getOrCreate(v6EcmpHashFields, udfGroupIds);

    // Set the new ecmp v6 hash attribute on switch obj
    setLoadBalancer<SaiSwitchTraits::Attributes::EcmpHashV6>(
        hash, programmedLoadBalancer);

    ecmpV6Hash_ = std::move(hash);
  }
}

void SaiSwitchManager::programLagLoadBalancerParams(
    std::optional<sai_uint32_t> seed,
    std::optional<cfg::HashingAlgorithm> algo) {
  auto hashSeed = seed ? seed.value() : 0;
  auto hashAlgo = algo ? toSaiHashAlgo(algo.value()) : SAI_HASH_ALGORITHM_CRC;
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::LagDefaultHashSeed{hashSeed});
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::LagDefaultHashAlgorithm{hashAlgo});
}

void SaiSwitchManager::addOrUpdateLagLoadBalancer(
    const std::shared_ptr<LoadBalancer>& newLb) {
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::SAI_LAG_HASH)) {
    XLOG(WARN) << "Skip programming SAI_LAG_HASH, feature not supported ";
    return;
  }
  programLagLoadBalancerParams(newLb->getSeed(), newLb->getAlgorithm());

  if (newLb->getIPv4Fields().begin() != newLb->getIPv4Fields().end()) {
    // v4 LAG
    auto programmedLoadBalancer =
        getProgrammedHashAttr<SaiSwitchTraits::Attributes::LagHashV4>();

    cfg::Fields v4LagHashFields;
    std::for_each(
        newLb->getIPv4Fields().begin(),
        newLb->getIPv4Fields().end(),
        [&v4LagHashFields](const auto& entry) {
          v4LagHashFields.ipv4Fields()->insert(entry->cref());
        });

    std::for_each(
        newLb->getTransportFields().begin(),
        newLb->getTransportFields().end(),
        [&v4LagHashFields](const auto& entry) {
          v4LagHashFields.transportFields()->insert(entry->cref());
        });
    auto hash = managerTable_->hashManager().getOrCreate(v4LagHashFields);

    // Set the new lag v4 hash attribute on switch obj
    setLoadBalancer<SaiSwitchTraits::Attributes::LagHashV4>(
        hash, programmedLoadBalancer);

    // Drop the reference to the previous hash only after the switch stopped
    // referring to it, else SAI rejects its removal with OBJECT IN USE.
    lagV4Hash_ = std::move(hash);
  }
  if (newLb->getIPv6Fields().begin() != newLb->getIPv6Fields().end()) {
    // v6 LAG
    auto programmedLoadBalancer =
        getProgrammedHashAttr<SaiSwitchTraits::Attributes::LagHashV6>();

    cfg::Fields v6LagHashFields;
    std::for_each(
        newLb->getIPv6Fields().begin(),
        newLb->getIPv6Fields().end(),
        [&v6LagHashFields](const auto& entry) {
          v6LagHashFields.ipv6Fields()->insert(entry->cref());
        });

    std::for_each(
        newLb->getTransportFields().begin(),
        newLb->getTransportFields().end(),
        [&v6LagHashFields](const auto& entry) {
          v6LagHashFields.transportFields()->insert(entry->cref());
        });
    auto hash = managerTable_->hashManager().getOrCreate(v6LagHashFields);

    // Set the new lag v6 hash attribute on switch obj
    setLoadBalancer<SaiSwitchTraits::Attributes::LagHashV6>(
        hash, programmedLoadBalancer);

    // Drop the reference to the previous hash only after the switch stopped
    // referring to it, else SAI rejects its removal with OBJECT IN USE.
    lagV6Hash_ = std::move(hash);
  }
}

void SaiSwitchManager::addOrUpdateLoadBalancer(
    const std::shared_ptr<LoadBalancer>& newLb) {
  XLOG(DBG2) << "Add load balancer : " << newLb->getID();
  if (newLb->getID() == cfg::LoadBalancerID::AGGREGATE_PORT) {
    return addOrUpdateLagLoadBalancer(newLb);
  }
  addOrUpdateEcmpLoadBalancer(newLb);
}

void SaiSwitchManager::changeLoadBalancer(
    const std::shared_ptr<LoadBalancer>& /*oldLb*/,
    const std::shared_ptr<LoadBalancer>& newLb) {
  XLOG(DBG2) << "Change load balancer : " << newLb->getID();
  return addOrUpdateLoadBalancer(newLb);
}

void SaiSwitchManager::removeLoadBalancer(
    const std::shared_ptr<LoadBalancer>& oldLb) {
  XLOG(DBG2) << "Remove load balancer : " << oldLb->getID();
  if (oldLb->getID() == cfg::LoadBalancerID::AGGREGATE_PORT) {
    programLagLoadBalancerParams(std::nullopt, std::nullopt);
    resetLoadBalancer<SaiSwitchTraits::Attributes::LagHashV4>();
    resetLoadBalancer<SaiSwitchTraits::Attributes::LagHashV6>();
    lagV4Hash_.reset();
    lagV6Hash_.reset();
    return;
  }
  programEcmpLoadBalancerParams(std::nullopt, std::nullopt);
  resetLoadBalancer<SaiSwitchTraits::Attributes::EcmpHashV4>();
  resetLoadBalancer<SaiSwitchTraits::Attributes::EcmpHashV6>();
  ecmpV4Hash_.reset();
  ecmpV6Hash_.reset();
}

void SaiSwitchManager::setQosPolicy() {
  auto& qosMapManager = managerTable_->qosMapManager();
  XLOG(DBG2) << "Set default qos map";
  auto qosMapHandle = qosMapManager.getQosMap();
  // set switch attrs to oids
  if (isGlobalQoSMapSupported()) {
    globalDscpToTcQosMap_ = qosMapHandle->dscpToTcMap;
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::QosDscpToTcMap{
            globalDscpToTcQosMap_->adapterKey()});
    globalTcToQueueQosMap_ = qosMapHandle->tcToQueueMap;
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::QosTcToQueueMap{
            globalTcToQueueQosMap_->adapterKey()});
  }
  if (!isMplsQosSupported_) {
    return;
  }
  globalExpToTcQosMap_ = qosMapHandle->expToTcMap;
  if (globalExpToTcQosMap_) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::QosExpToTcMap{
            globalExpToTcQosMap_->adapterKey()});
  }
  globalTcToExpQosMap_ = qosMapHandle->tcToExpMap;
  if (globalTcToExpQosMap_) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::QosTcToExpMap{
            globalTcToExpQosMap_->adapterKey()});
  }
}

void SaiSwitchManager::clearQosPolicy() {
  XLOG(DBG2) << "Reset default qos map";
  resetQosMaps();
}

void SaiSwitchManager::setIngressAcl() {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::SWITCH_ATTR_INGRESS_ACL)) {
    return;
  }
  auto aclTableGroupHandle =
      managerTable_->aclTableGroupManager()
          .getAclTableGroupHandle(
              SAI_ACL_STAGE_INGRESS, cfg::AclTableGroupBindPoint::SWITCH)
          ->aclTableGroup;
  setIngressAcl(aclTableGroupHandle->adapterKey());
}

void SaiSwitchManager::setIngressAcl(sai_object_id_t id) {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::SWITCH_ATTR_INGRESS_ACL)) {
    return;
  }
  XLOG(DBG2) << "Set ingress ACL; " << id;
  switch_->setOptionalAttribute(SaiSwitchTraits::Attributes::IngressAcl{id});
}

void SaiSwitchManager::resetIngressAcl() {
  // Since resetIngressAcl() will be called in SaiManagerTable destructor.,
  // we can't call pure virtual function HwAsic::isSupported() to check
  // whether an asic supports SAI_ACL_STAGE_INGRESS
  // Therefore, we will try to read from SaiObject to see whether there's a
  // ingress acl set. If there's a not null id set, we set it to null
  auto ingressAcl =
      std::get<std::optional<SaiSwitchTraits::Attributes::IngressAcl>>(
          switch_->attributes());
  if (ingressAcl && ingressAcl->value() != SAI_NULL_OBJECT_ID) {
    XLOG(DBG2) << "Reset current ingress acl:" << ingressAcl->value()
               << " back to null";
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::IngressAcl{SAI_NULL_OBJECT_ID});
  }
}

void SaiSwitchManager::setEgressAcl() {
  CHECK(platform_->getAsic()->isSupported(
      HwAsic::Feature::INGRESS_POST_LOOKUP_ACL_TABLE))
      << "INGRESS_POST_LOOKUP_ACL_TABLE ACL not supported";
  auto aclTableGroupHandle =
      managerTable_->aclTableGroupManager()
          .getAclTableGroupHandle(
              SAI_ACL_STAGE_EGRESS, cfg::AclTableGroupBindPoint::SWITCH)
          ->aclTableGroup;
  setEgressAcl(aclTableGroupHandle->adapterKey());
  isIngressPostLookupAclSupported_ = true;
}

void SaiSwitchManager::setEgressAcl(sai_object_id_t id) {
  XLOG(DBG2) << "Set egress ACL; " << id;
  switch_->setOptionalAttribute(SaiSwitchTraits::Attributes::EgressAcl{id});
}

void SaiSwitchManager::resetEgressAcl() {
  if (!isIngressPostLookupAclSupported_) {
    return;
  }
  // Since resetIngressAcl() will be called in SaiManagerTable destructor.,
  // we can't call pure virtual function HwAsic::isSupported() to check
  // whether an asic supports INGRESS_POST_LOOKUP_ACL_TABLE
  // Therefore, we will try to read from SaiObject to see whether there's a
  // egress acl set. If there's a not null id set, we set it to null
  auto egressAcl =
      std::get<std::optional<SaiSwitchTraits::Attributes::EgressAcl>>(
          switch_->attributes());
  if (egressAcl && egressAcl->value() != SAI_NULL_OBJECT_ID) {
    XLOG(DBG2) << "Reset current egress acl:" << egressAcl->value()
               << " back to null";
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::EgressAcl{SAI_NULL_OBJECT_ID});
  }
}

void SaiSwitchManager::gracefulExit() {
#if defined(BRCM_PAI_SDK)
  // PAI's remove_switch frees the PHY/CFG_PHY/CFG_LANE records that warm boot
  // restores. PAI warm shutdown is RESTART_WARM (set by SaiSwitch) followed by
  // process exit, so the switch object is intentionally not destroyed.
  __attribute__((unused)) auto leakedSwitch = switch_.release();
#else
  // On graceful exit we trigger the warm boot path on
  // ASIC by destroying the switch (and thus calling the
  // remove switch function
  // https://github.com/opencomputeproject/SAI/blob/master/inc/saiswitch.h#L2514
  // Other objects are left intact to preserve data plane
  // forwarding during warm boot
  switch_.reset();
#endif
}

void SaiSwitchManager::setMacAgingSeconds(sai_uint32_t agingSeconds) {
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::MacAgingTime{agingSeconds});
}
sai_uint32_t SaiSwitchManager::getMacAgingSeconds() const {
  return GET_OPT_ATTR(Switch, MacAgingTime, switch_->attributes());
}

void SaiSwitchManager::setTamObject(std::vector<sai_object_id_t> tamObject) {
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::TamObject{std::move(tamObject)});
}

void SaiSwitchManager::resetTamObject() {
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::TamObject{
          std::vector<sai_object_id_t>{SAI_NULL_OBJECT_ID}});
}

void SaiSwitchManager::setArsProfile(
    [[maybe_unused]] ArsProfileSaiId arsProfileSaiId) {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  if (FLAGS_flowletSwitchingEnable &&
      platform_->getAsic()->isSupported(HwAsic::Feature::ARS)) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::ArsProfile{arsProfileSaiId});
  }
#endif
}

void SaiSwitchManager::resetArsProfile() {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  if (FLAGS_flowletSwitchingEnable) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::ArsProfile{SAI_NULL_OBJECT_ID});
  }
#endif
}

void SaiSwitchManager::setupCounterRefreshInterval() {
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::CounterRefreshInterval{
          FLAGS_counter_refresh_interval});
}

sai_object_id_t SaiSwitchManager::getDefaultVlanAdapterKey() const {
  return SaiApiTable::getInstance()->switchApi().getAttribute(
      switch_->adapterKey(), SaiSwitchTraits::Attributes::DefaultVlanId{});
}

void SaiSwitchManager::setPtpTcEnabled(bool ptpEnable) {
  isPtpTcEnabled_ = ptpEnable;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  if (platform_->getAsic()->getAsicVendor() ==
      HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
    auto ptpMode = utility::getSaiPortPtpMode(ptpEnable);
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::PtpMode{ptpMode});
  }
#endif
}

std::optional<bool> SaiSwitchManager::getPtpTcEnabled() {
  return isPtpTcEnabled_;
}

void SaiSwitchManager::setSwitchingMode(cfg::PacketForwardingMode mode) {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::CUT_THROUGH_FORWARDING)) {
    throw FbossError("setSwitchingMode not supported on this ASIC");
  }
  sai_int32_t saiMode;
  switch (mode) {
    case cfg::PacketForwardingMode::CUT_THROUGH:
      saiMode = SAI_SWITCH_SWITCHING_MODE_CUT_THROUGH;
      break;
    case cfg::PacketForwardingMode::STORE_AND_FORWARD:
      saiMode = SAI_SWITCH_SWITCHING_MODE_STORE_AND_FORWARD;
      break;
  }
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::SwitchingMode{saiMode});
}

bool SaiSwitchManager::isGlobalQoSMapSupported() const {
#if defined(BRCM_SAI_SDK_XGS_AND_DNX)
  return false;
#endif
  return platform_->getAsic()->isSupported(HwAsic::Feature::QOS_MAP_GLOBAL);
}

bool SaiSwitchManager::isMplsQoSMapSupported() const {
#if defined(TAJO_SDK_VERSION_1_42_8)
  return false;
#endif
  return platform_->getAsic()->isSupported(HwAsic::Feature::SAI_MPLS_QOS);
}

const std::vector<sai_stat_id_t>& SaiSwitchManager::supportedDropStats() const {
  static std::vector<sai_stat_id_t> stats;
  if (stats.size()) {
    // initialized
    return stats;
  }
  if (platform_->getAsic()->isSupported(HwAsic::Feature::SWITCH_DROP_STATS)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::CounterIdsToRead.begin(),
        SaiSwitchTraits::CounterIdsToRead.end());
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::PACKET_INTEGRITY_DROP_STATS)) {
#if not defined(BRCM_SAI_SDK_DNX_GTE_12_0) && \
    SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      stats.push_back(SAI_SWITCH_STAT_PACKET_INTEGRITY_DROP);
#endif
    }
    if (isJerichoAsic(platform_->getAsic()->getAsicType())) {
      static const std::vector<sai_stat_id_t> kJerichoConfigDropStats{
          SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_0_DROPPED_PKTS,
          SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_1_DROPPED_PKTS,
          SAI_SWITCH_STAT_OUT_CONFIGURED_DROP_REASONS_2_DROPPED_PKTS,
      };
      stats.insert(
          stats.end(),
          kJerichoConfigDropStats.begin(),
          kJerichoConfigDropStats.end());
    }
    if (platform_->getAsic()->getAsicType() ==
            cfg::AsicType::ASIC_TYPE_JERICHO3 ||
        platform_->getAsic()->getAsicType() ==
            cfg::AsicType::ASIC_TYPE_JERICHO4) {
      static const std::vector<sai_stat_id_t> kJericho3ConfigDropStats{
          // IN configured drop reasons
          SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_0_DROPPED_PKTS,
          SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_1_DROPPED_PKTS,
          SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_2_DROPPED_PKTS,
          SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_3_DROPPED_PKTS,
          SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_4_DROPPED_PKTS,
          SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_5_DROPPED_PKTS,
          SAI_SWITCH_STAT_IN_CONFIGURED_DROP_REASONS_6_DROPPED_PKTS,
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
          SAI_SWITCH_STAT_TC0_RATE_LIMIT_DROPPED_PACKETS,
#endif
      };
      stats.insert(
          stats.end(),
          kJericho3ConfigDropStats.begin(),
          kJericho3ConfigDropStats.end());
    }
  }
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchManager::supportedCustomDropBitmapStats() const {
  static std::vector<sai_stat_id_t> stats;
  if (stats.size()) {
    return stats;
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SWITCH_CUSTOM_DROP_BITMAP_SUPPORT)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::customDropBitmapStats().begin(),
        SaiSwitchTraits::customDropBitmapStats().end());
  }
  return stats;
}

void SaiSwitchManager::updateDropReasonStats() {
#if defined(BRCM_SAI_SDK_XGS_GTE_15_0)
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::SWITCH_DROP_REASON_LIST_SUPPORT)) {
    return;
  }
  auto& switchApi = SaiApiTable::getInstance()->switchApi();
  auto ingressDropReasons = switchApi.getAttribute(
      switch_->adapterKey(),
      SaiSwitchTraits::Attributes::PacketDropTypeIngressList{
          std::vector<sai_int32_t>{}});
  auto egressDropReasons = switchApi.getAttribute(
      switch_->adapterKey(),
      SaiSwitchTraits::Attributes::PacketDropTypeEgressList{
          std::vector<sai_int32_t>{}});

  logDropReasons(ingressDropReasons, egressDropReasons);
#endif
}

const std::vector<sai_attr_id_t>& SaiSwitchManager::supportedTemperatureStats()
    const {
  static std::vector<sai_stat_id_t> stats;
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::TEMPERATURE_MONITORING)) {
    stats = {
        SAI_SWITCH_ATTR_MAX_NUMBER_OF_TEMP_SENSORS, SAI_SWITCH_ATTR_TEMP_LIST};
    return stats;
  } else {
    stats = {};
    return stats;
  }
}

const std::vector<sai_stat_id_t>& SaiSwitchManager::supportedErrorStats()
    const {
  static std::vector<sai_stat_id_t> stats;
  if (stats.size()) {
    // initialized
    return stats;
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::EGRESS_CELL_ERROR_STATS)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::egressFabricCellError().begin(),
        SaiSwitchTraits::egressFabricCellError().end());
    stats.insert(
        stats.end(),
        SaiSwitchTraits::egressNonFabricCellError().begin(),
        SaiSwitchTraits::egressNonFabricCellError().end());
    stats.insert(
        stats.end(),
        SaiSwitchTraits::egressNonFabricCellUnpackError().begin(),
        SaiSwitchTraits::egressNonFabricCellUnpackError().end());
    stats.insert(
        stats.end(),
        SaiSwitchTraits::egressParityCellError().begin(),
        SaiSwitchTraits::egressParityCellError().end());
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::DRAM_DATAPATH_PACKET_ERROR_STATS)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::ddpPacketError().begin(),
        SaiSwitchTraits::ddpPacketError().end());
  }
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchManager::supportedSaiExtensionDropStats() const {
  static std::vector<sai_stat_id_t> stats;
  if (stats.size()) {
    // initialized
    return stats;
  }

#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  if (platform_->getAsic()->isSupported(HwAsic::Feature::SWITCH_DROP_STATS) &&
      platform_->getAsic()->isSupported(
          HwAsic::Feature::PACKET_INTEGRITY_DROP_STATS)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::packetIntegrityError().begin(),
        SaiSwitchTraits::packetIntegrityError().end());
  }
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchManager::supportedDramStats() const {
  static std::vector<sai_stat_id_t> stats;
  if (stats.size()) {
    // initialized
    return stats;
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::DRAM_ENQUEUE_DEQUEUE_STATS)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::dramStats().begin(),
        SaiSwitchTraits::dramStats().end());
  }
  if (platform_->getAsic()->isSupported(HwAsic::Feature::DRAM_BLOCK_TIME)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::dramBlockTime().begin(),
        SaiSwitchTraits::dramBlockTime().end());
  }
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchManager::supportedDramReadOnlyStats()
    const {
  static std::vector<sai_stat_id_t> stats;
  if (stats.size()) {
    // initialized
    return stats;
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::DRAM_QUARANTINED_BUFFER_STATS)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::dramQuarantinedBufferStats().begin(),
        SaiSwitchTraits::dramQuarantinedBufferStats().end());
  }
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchManager::supportedWatermarkStats()
    const {
  static std::vector<sai_stat_id_t> stats;
  if (stats.size()) {
    // initialized
    return stats;
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::RCI_WATERMARK_COUNTER)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::rciWatermarkStats().begin(),
        SaiSwitchTraits::rciWatermarkStats().end());
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::DTL_WATERMARK_COUNTER)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::dtlWatermarkStats().begin(),
        SaiSwitchTraits::dtlWatermarkStats().end());
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::EGRESS_CORE_BUFFER_WATERMARK)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::egressCoreBufferWatermarkBytes().begin(),
        SaiSwitchTraits::egressCoreBufferWatermarkBytes().end());
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::INGRESS_SRAM_MIN_BUFFER_WATERMARK)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::sramMinBufferWatermarkBytes().begin(),
        SaiSwitchTraits::sramMinBufferWatermarkBytes().end());
  }
  if (platform_->getAsic()->isSupported(HwAsic::Feature::FDR_FIFO_WATERMARK)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::fdrFifoWatermarkBytes().begin(),
        SaiSwitchTraits::fdrFifoWatermarkBytes().end());
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::FABRIC_INTER_CELL_JITTER_WATERMARK)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::fabricInterCellJitterWatermarkStats().begin(),
        SaiSwitchTraits::fabricInterCellJitterWatermarkStats().end());
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::DEVICE_WATERMARK_SUPPORT)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::deviceWatermarkBytes().begin(),
        SaiSwitchTraits::deviceWatermarkBytes().end());
  }
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchManager::supportedCreditStats()
    const {
  static std::vector<sai_stat_id_t> stats;
  if (stats.size()) {
    // initialized
    return stats;
  }
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::DELETED_CREDITS_STAT)) {
    stats.insert(
        stats.end(),
        SaiSwitchTraits::deletedCredits().begin(),
        SaiSwitchTraits::deletedCredits().end());
  }
  return stats;
}

const HwSwitchWatermarkStats SaiSwitchManager::getHwSwitchWatermarkStats()
    const {
  HwSwitchWatermarkStats switchWatermarkStats;
  // Get NPU specific watermark stats!
  auto supportedStats = supportedWatermarkStats();
  if (supportedStats.size()) {
    switch_->updateStats(supportedStats, SAI_STATS_MODE_READ_AND_CLEAR);
  }
  auto statsMap = switch_->getStats(supportedStats);
  fillHwSwitchWatermarkStats(statsMap, switchWatermarkStats);
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::DEVICE_WATERMARK_SUPPORT)) {
    if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
            switchWatermarkStats.deviceWatermarkBytes())) {
      switchWatermarkStats.deviceWatermarkBytes() = 0;
    }
  } else {
    switchWatermarkStats.deviceWatermarkBytes() =
        managerTable_->bufferManager().getDeviceWatermarkBytes();
  }
  switchWatermarkStats.globalHeadroomWatermarkBytes()->insert(
      managerTable_->bufferManager().getGlobalHeadroomWatermarkBytes().begin(),
      managerTable_->bufferManager().getGlobalHeadroomWatermarkBytes().end());
  switchWatermarkStats.globalSharedWatermarkBytes()->insert(
      managerTable_->bufferManager().getGlobalSharedWatermarkBytes().begin(),
      managerTable_->bufferManager().getGlobalSharedWatermarkBytes().end());
  return switchWatermarkStats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchManager::supportedPipelineWatermarkStats() const {
  static std::vector<sai_stat_id_t> stats;
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  if (stats.size()) {
    // initialized
    return stats;
  }
  // TODO(daiweix): enable watermark stats after CS00012366726 is resolved
  /*if (platform_->getAsic()->getSwitchType() == cfg::SwitchType::FABRIC) {
    stats.insert(
        stats.end(),
        SaiSwitchPipelineTraits::watermarkLevels().begin(),
        SaiSwitchPipelineTraits::watermarkLevels().end());
  }*/
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchManager::supportedPipelineStats()
    const {
  static std::vector<sai_stat_id_t> stats;
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  if (stats.size()) {
    // initialized
    return stats;
  }
  if (platform_->getAsic()->getSwitchType() == cfg::SwitchType::FABRIC) {
    stats.insert(
        stats.end(),
        SaiSwitchPipelineTraits::currOccupancyBytes().begin(),
        SaiSwitchPipelineTraits::currOccupancyBytes().end());
    stats.insert(
        stats.end(),
        SaiSwitchPipelineTraits::txCells().begin(),
        SaiSwitchPipelineTraits::txCells().end());
    stats.insert(
        stats.end(),
        SaiSwitchPipelineTraits::rxCells().begin(),
        SaiSwitchPipelineTraits::rxCells().end());
    stats.insert(
        stats.end(),
        SaiSwitchPipelineTraits::globalDrop().begin(),
        SaiSwitchPipelineTraits::globalDrop().end());
  } else if (platform_->getAsic()->getSwitchType() == cfg::SwitchType::VOQ) {
    stats.insert(
        stats.end(),
        SaiSwitchPipelineTraits::rxCells().begin(),
        SaiSwitchPipelineTraits::rxCells().end());
  }
#endif
  return stats;
}

const HwSwitchPipelineStats SaiSwitchManager::getHwSwitchPipelineStats(
    bool updateWatermarks) const {
  HwSwitchPipelineStats switchPipelineStats;
  // TODO (Q4D/J4/R4): Enable switch pipeline stats after SDK support in 15.x
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0) && !defined(BRCM_SAI_SDK_DNX_GTE_15_0)
  auto watermarkStats = supportedPipelineWatermarkStats();
  if (updateWatermarks && watermarkStats.size() > 0) {
    for (const auto& pipeline : switchPipelines_) {
      pipeline->updateStats(watermarkStats, SAI_STATS_MODE_READ_AND_CLEAR);
      auto idx = pipeline->adapterHostKey().value();
      fillHwSwitchPipelineStats(
          pipeline->getStats(watermarkStats), idx, switchPipelineStats);
    }
  }
  auto stats = supportedPipelineStats();
  if (stats.size()) {
    for (const auto& pipeline : switchPipelines_) {
      pipeline->updateStats(stats, SAI_STATS_MODE_READ);
      auto idx = pipeline->adapterHostKey().value();
      fillHwSwitchPipelineStats(
          pipeline->getStats(stats), idx, switchPipelineStats);
    }
  }
#endif
  return switchPipelineStats;
}

const HwSwitchTemperatureStats SaiSwitchManager::getHwSwitchTemperatureStats()
    const {
  // Get temperature stats
  HwSwitchTemperatureStats switchTemperatureStats;
  static sai_int32_t NumTemperatureSensors = -1;
  static auto lastReadTimestamp =
      std::chrono::system_clock::now().time_since_epoch().count();
  // Read temperature stats is expensive, so read it as often as fsdb would
  // stream stats to its client defined by FLAGS_fsdbStatsStreamIntervalSeconds
  const uint64_t kReadInterval =
      static_cast<int64_t>(FLAGS_fsdbStatsStreamIntervalSeconds) * 1000000000;

  auto currReadTimestamp =
      std::chrono::system_clock::now().time_since_epoch().count();

  if (currReadTimestamp - lastReadTimestamp < kReadInterval) {
    return switchTemperatureStats_;
  }
  lastReadTimestamp = currReadTimestamp;

  if (!supportedTemperatureStats().empty()) {
    folly::F14FastMap<sai_attr_id_t, sai_attribute_value_t> attrValues;
    for (auto attrId : supportedTemperatureStats()) {
      try {
        if (attrId == SAI_SWITCH_ATTR_MAX_NUMBER_OF_TEMP_SENSORS) {
          if (NumTemperatureSensors < 0) {
            NumTemperatureSensors =
                SaiApiTable::getInstance()->switchApi().getAttribute(
                    switch_->adapterKey(),
                    SaiSwitchTraits::Attributes::NumTemperatureSensors{});
          }
          sai_attribute_value_t value;
          value.u8 = NumTemperatureSensors;
          attrValues[attrId] = value;
        } else if (attrId == SAI_SWITCH_ATTR_TEMP_LIST) {
          if (NumTemperatureSensors < 0) {
            NumTemperatureSensors =
                SaiApiTable::getInstance()->switchApi().getAttribute(
                    switch_->adapterKey(),
                    SaiSwitchTraits::Attributes::NumTemperatureSensors{});
          }
          std::vector<sai_int32_t> temperatureList;
          XLOG(DBG5) << "# temperature sensor: " << NumTemperatureSensors;
          temperatureList.resize(NumTemperatureSensors);
          SaiSwitchTraits::Attributes::AsicTemperatureList
              temperatureListAttribute{temperatureList};

          auto temperatureU32List =
              SaiApiTable::getInstance()->switchApi().getAttribute(
                  switch_->adapterKey(), temperatureListAttribute);

          sai_attribute_value_t value;
          value.u32list.count = temperatureU32List.size();
          value.u32list.list =
              reinterpret_cast<uint32_t*>(temperatureU32List.data());

          attrValues[attrId] = value;
          fillHwSwitchTemperatureStats(attrValues, switchTemperatureStats);
        }
      } catch (const std::exception& ex) {
        XLOG(ERR) << "Failed to get temperature attribute " << attrId << ": "
                  << ex.what();
      }
    }
  }
  return switchTemperatureStats;
}

void SaiSwitchManager::updateStats(bool updateWatermarks) {
  auto switchDropStats = supportedDropStats();
  if (switchDropStats.size()) {
    switch_->updateStats(switchDropStats, SAI_STATS_MODE_READ);
    HwSwitchDropStats dropStats;
    fillHwSwitchDropStats(
        switch_->getStats(switchDropStats),
        dropStats,
        platform_->getAsic()->getAsicType());
    // Accumulate switch drop stats
    switchDropStats_.globalDrops() =
        switchDropStats_.globalDrops().value_or(0) +
        dropStats.globalDrops().value_or(0);
    switchDropStats_.globalReachabilityDrops() =
        switchDropStats_.globalReachabilityDrops().value_or(0) +
        dropStats.globalReachabilityDrops().value_or(0);
    switchDropStats_.packetIntegrityDrops() =
        switchDropStats_.packetIntegrityDrops().value_or(0) +
        dropStats.packetIntegrityDrops().value_or(0);
    switchDropStats_.fdrCellDrops() =
        switchDropStats_.fdrCellDrops().value_or(0) +
        dropStats.fdrCellDrops().value_or(0);
    switchDropStats_.voqResourceExhaustionDrops() =
        switchDropStats_.voqResourceExhaustionDrops().value_or(0) +
        dropStats.voqResourceExhaustionDrops().value_or(0);
    switchDropStats_.globalResourceExhaustionDrops() =
        switchDropStats_.globalResourceExhaustionDrops().value_or(0) +
        dropStats.globalResourceExhaustionDrops().value_or(0);
    switchDropStats_.sramResourceExhaustionDrops() =
        switchDropStats_.sramResourceExhaustionDrops().value_or(0) +
        dropStats.sramResourceExhaustionDrops().value_or(0);
    switchDropStats_.vsqResourceExhaustionDrops() =
        switchDropStats_.vsqResourceExhaustionDrops().value_or(0) +
        dropStats.vsqResourceExhaustionDrops().value_or(0);
    switchDropStats_.dropPrecedenceDrops() =
        switchDropStats_.dropPrecedenceDrops().value_or(0) +
        dropStats.dropPrecedenceDrops().value_or(0);
    switchDropStats_.queueResolutionDrops() =
        switchDropStats_.queueResolutionDrops().value_or(0) +
        dropStats.queueResolutionDrops().value_or(0);
    switchDropStats_.ingressPacketPipelineRejectDrops() =
        switchDropStats_.ingressPacketPipelineRejectDrops().value_or(0) +
        dropStats.ingressPacketPipelineRejectDrops().value_or(0);
    switchDropStats_.tc0RateLimitDrops() =
        switchDropStats_.tc0RateLimitDrops().value_or(0) +
        dropStats.tc0RateLimitDrops().value_or(0);
  }
  auto switchSaiExtensionDropStats = supportedSaiExtensionDropStats();
  if (switchSaiExtensionDropStats.size()) {
    switch_->updateStats(switchSaiExtensionDropStats, SAI_STATS_MODE_READ);
    HwSwitchDropStats dropStats;
    fillHwSwitchSaiExtensionDropStats(
        switch_->getStats(switchSaiExtensionDropStats), dropStats);
    // Packet integrity drop stats is supported in 11.7 with
    // SAI_SWITCH_STAT_PACKET_INTEGRITY_DROP and in 12.2 with
    // SAI_SWITCH_STAT_EGR_RX_PACKET_ERROR which is a SAI
    // extension hence switchDropStats_.packetIntegrityDrops is
    // incremented in multiple places.
    switchDropStats_.packetIntegrityDrops() =
        switchDropStats_.packetIntegrityDrops().value_or(0) +
        dropStats.packetIntegrityDrops().value_or(0);
  }
  auto errorDropStats = supportedErrorStats();
  if (errorDropStats.size()) {
    switch_->updateStats(errorDropStats, SAI_STATS_MODE_READ);
    HwSwitchDropStats errorStats;
    // Fill error stats and update drops stats
    fillHwSwitchErrorStats(switch_->getStats(errorDropStats), errorStats);
    switchDropStats_.rqpFabricCellCorruptionDrops() =
        switchDropStats_.rqpFabricCellCorruptionDrops().value_or(0) +
        errorStats.rqpFabricCellCorruptionDrops().value_or(0);
    switchDropStats_.rqpNonFabricCellCorruptionDrops() =
        switchDropStats_.rqpNonFabricCellCorruptionDrops().value_or(0) +
        errorStats.rqpNonFabricCellCorruptionDrops().value_or(0);
    switchDropStats_.rqpNonFabricCellMissingDrops() =
        switchDropStats_.rqpNonFabricCellMissingDrops().value_or(0) +
        errorStats.rqpNonFabricCellMissingDrops().value_or(0);
    switchDropStats_.rqpParityErrorDrops() =
        switchDropStats_.rqpParityErrorDrops().value_or(0) +
        errorStats.rqpParityErrorDrops().value_or(0);
    switchDropStats_.dramDataPathPacketError() =
        switchDropStats_.dramDataPathPacketError().value_or(0) +
        errorStats.dramDataPathPacketError().value_or(0);
  }

  // Read switch debug counter stats (L2, L3, Tunnel drops)
  auto& debugCounterMgr = managerTable_->debugCounterManager();
  auto switchDebugStatIds = debugCounterMgr.getConfiguredSwitchDebugStatIds();
  if (switchDebugStatIds.size()) {
    std::vector<sai_stat_id_t> debugStatsVec(
        switchDebugStatIds.begin(), switchDebugStatIds.end());
    switch_->updateStats(debugStatsVec, SAI_STATS_MODE_READ);
    auto debugStatValues = switch_->getStats(debugStatsVec);
    for (const auto& [statId, value] : debugStatValues) {
      if (statId == debugCounterMgr.getL2SwitchDropCounterStatId()) {
        switchDropStats_.switchL2InDrops() =
            switchDropStats_.switchL2InDrops().value_or(0) + value;
      } else if (statId == debugCounterMgr.getL3SwitchDropCounterStatId()) {
        switchDropStats_.switchL3InDrops() =
            switchDropStats_.switchL3InDrops().value_or(0) + value;
      } else if (statId == debugCounterMgr.getTunnelSwitchDropCounterStatId()) {
        switchDropStats_.switchTunnelInDrops() =
            switchDropStats_.switchTunnelInDrops().value_or(0) + value;
      }
    }
  }

  if (switchDropStats.size() || errorDropStats.size() ||
      switchDebugStatIds.size()) {
    platform_->getHwSwitch()->getSwitchStats()->update(switchDropStats_);
  }
  auto switchDramStats = supportedDramStats();
  if (switchDramStats.size()) {
    switch_->updateStats(switchDramStats, SAI_STATS_MODE_READ_AND_CLEAR);
    HwSwitchDramStats dramStats;
    fillHwSwitchDramStats(switch_->getStats(switchDramStats), dramStats);
    platform_->getHwSwitch()->getSwitchStats()->update(dramStats);
  }
  auto switchDramReadOnlyStats = supportedDramReadOnlyStats();
  if (switchDramReadOnlyStats.size()) {
    switch_->updateStats(switchDramReadOnlyStats, SAI_STATS_MODE_READ);
    HwSwitchDramStats dramReadOnlyStats;
    fillHwSwitchDramStats(
        switch_->getStats(switchDramReadOnlyStats), dramReadOnlyStats);
    platform_->getHwSwitch()->getSwitchStats()->update(dramReadOnlyStats);
  }
  auto switchCreditStats = supportedCreditStats();
  if (switchCreditStats.size()) {
    switch_->updateStats(switchCreditStats, SAI_STATS_MODE_READ_AND_CLEAR);
    HwSwitchCreditStats creditStats;
    fillHwSwitchCreditStats(switch_->getStats(switchCreditStats), creditStats);
    platform_->getHwSwitch()->getSwitchStats()->update(creditStats);
  }
  if (updateWatermarks) {
    switchWatermarkStats_ = getHwSwitchWatermarkStats();
    publishSwitchWatermarks(switchWatermarkStats_);
    updateSramLowBufferLimitHitCounter();
    // Read drop cause bitmaps in the watermark block (once per ~60s) with
    // READ_AND_CLEAR so each read captures only fresh drops since the last
    // collection. This cadence is intentional: reading more frequently
    // risks flooding logs with repeated drop reasons in high-drop
    // scenarios, while aggregate drop counters already provide real-time
    // visibility. The per-cause bitmaps supplement those counters by
    // narrowing down the specific drop reason.
    const auto& customDropBitmapStatIds = supportedCustomDropBitmapStats();
    if (customDropBitmapStatIds.size()) {
      switch_->updateStats(
          customDropBitmapStatIds, SAI_STATS_MODE_READ_AND_CLEAR);
      fillHwSwitchDropBitmapStats(
          switch_->getStats(customDropBitmapStatIds), switchDropBitmapStats_);
      logDropBitmapReasons(switchDropBitmapStats_);
    }
    // Broadcom XGS reports the same information as a list of active drop
    // reasons rather than a per stage bitmap. These are read only SAI
    // attributes, so unlike the bitmap stats above they cannot go through
    // updateStats()/SAI_STATS_MODE_READ_AND_CLEAR; the adapter clears them
    // on attribute read instead. Same ~60s cadence and same rationale.
    updateDropReasonStats();
  }
  updateSdkDumpSuppressedCounter();
  switchTemperatureStats_ = getHwSwitchTemperatureStats();
  publishSwitchTemperatureStats(switchTemperatureStats_);
  switchPipelineStats_ = getHwSwitchPipelineStats(updateWatermarks);
  publishSwitchPipelineStats(switchPipelineStats_);
}

void SaiSwitchManager::updateSdkDumpSuppressedCounter() {
#if defined(SAI_SDK_DUMP_RATE_LIMIT_SUPPORTED)
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::SDK_REGISTER_DUMP)) {
    return;
  }
  auto suppressed = SaiApiTable::getInstance()->switchApi().getAttribute(
      switch_->adapterKey(),
      SaiSwitchTraits::Attributes::SdkDumpSuppressedCount{});
  platform_->getHwSwitch()->getSwitchStats()->sdkDumpSuppressedCount(
      suppressed);
#endif
}

void SaiSwitchManager::updateSramLowBufferLimitHitCounter() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::INGRESS_SRAM_MIN_BUFFER_WATERMARK) ||
      !switchWatermarkStats_.sramMinBufferWatermarkBytes().has_value()) {
    return;
  }
  // getSramSizeBytes() returns the SRAM buffers in all cores combined
  // and SramFreePercentXoffTh is the percentage SRAM buffer utilization
  // at which PFC will be triggered on all ports.
  auto sramFreeBufferPctToTriggerXoff = std::get<
      std::optional<SaiSwitchTraits::Attributes::SramFreePercentXoffTh>>(
      switch_->attributes());
  if (!sramFreeBufferPctToTriggerXoff.has_value()) {
    // Optional attribute not configured
    return;
  }

  uint64_t sramBufferLowerLimit = platform_->getAsic()->getSramSizeBytes() *
      sramFreeBufferPctToTriggerXoff.value().value() /
      (platform_->getAsic()->getNumCores() * 100);
  if (*switchWatermarkStats_.sramMinBufferWatermarkBytes() <=
      sramBufferLowerLimit) {
    // Low buffer limit in SRAM was hit and all ports in the core will
    // be sending PFC XOFF, increment counter to flag this event.
    platform_->getHwSwitch()->getSwitchStats()->sramLowBufferLimitHitCount();
  }
#endif
}

void SaiSwitchManager::setSwitchIsolate(bool isolate) {
  if (platform_->getAsic()->isSupported(HwAsic::Feature::SWITCH_ISOLATE)) {
    XLOG(DBG2) << " Setting switch state to : "
               << (isolate ? "DRAINED" : "UNDRAINED");
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::SwitchIsolate{isolate});
  } else {
    XLOG(DBG2) << "Ignoring setSwitchIsolate for switch type "
               << apache::thrift::util::enumNameSafe(
                      platform_->getAsic()->getSwitchType());
  }
}

std::vector<sai_object_id_t> SaiSwitchManager::getUdfGroupIds(
    const std::shared_ptr<LoadBalancer>& newLb) const {
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  if (platform_->getAsic()->isSupported(HwAsic::Feature::SAI_UDF_HASH)) {
    return managerTable_->udfManager().getUdfGroupIds(newLb->getUdfGroupIds());
  }
#endif
  return {};
}

void SaiSwitchManager::setForceTrafficOverFabric(bool forceTrafficOverFabric) {
  auto& switchApi = SaiApiTable::getInstance()->switchApi();
  auto oldSetForceTrafficOverFabric = switchApi.getAttribute(
      switch_->adapterKey(),
      SaiSwitchTraits::Attributes::ForceTrafficOverFabric{});
  if (oldSetForceTrafficOverFabric != forceTrafficOverFabric) {
    SaiApiTable::getInstance()->switchApi().setAttribute(
        switch_->adapterKey(),
        SaiSwitchTraits::Attributes::ForceTrafficOverFabric{
            forceTrafficOverFabric});
  }
}

void SaiSwitchManager::setCreditWatchdog(bool creditWatchdog) {
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::CreditWd{creditWatchdog});
#endif
}

void SaiSwitchManager::setLocalCapsuleSwitchIds(
    const std::map<SwitchID, int>& switchIdToNumCores) {
  std::vector<sai_uint32_t> values;
  for (const auto& it : switchIdToNumCores) {
    sai_uint32_t switchId = it.first;
    int numCores = it.second;
    for (auto idx = 0; idx < numCores; idx++) {
      values.push_back(switchId + idx);
    }
  }
  XLOG(DBG2) << "set local capsule switch ids "
             << std::string(values.begin(), values.end());
  if (values.empty()) {
#if defined BRCM_SAI_SDK_DNX_GTE_12_0
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::MultiStageLocalSwitchIds{values});
#endif
    // Note: setting MultiStageLocalSwitchIds to empty, when its was not set
    // before should be a no-op, but there is a bug in pre 12.0 sai where this
    // is not correctly handled complaining invalid parameters
    return;
  }
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::MultiStageLocalSwitchIds{values});
}

void SaiSwitchManager::setReachabilityGroupList(
    const std::vector<int>& reachabilityGroups) {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  std::vector<uint32_t> groupList(
      reachabilityGroups.begin(), reachabilityGroups.end());
  std::sort(groupList.begin(), groupList.end());
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::ReachabilityGroupList{groupList});
#endif
}

void SaiSwitchManager::setSramGlobalFreePercentXoffTh(
    uint8_t sramFreePercentXoffThreshold) {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::SramFreePercentXoffTh{
          sramFreePercentXoffThreshold});
#endif
}

void SaiSwitchManager::setSramGlobalFreePercentXonTh(
    uint8_t sramFreePercentXonThreshold) {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::SramFreePercentXonTh{
          sramFreePercentXonThreshold});
#endif
}

void SaiSwitchManager::setLinkFlowControlCreditTh(
    uint16_t linkFlowControlThreshold) {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0) && !defined(BRCM_SAI_SDK_DNX_GTE_13_0)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::FabricCllfcTxCreditTh{
          linkFlowControlThreshold});
#endif
}

void SaiSwitchManager::setVoqDramBoundTh(uint32_t dramBoundThreshold) {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  // There are 3 different types of rate classes available and
  // dramBound, upper and lower limits are applied to each of
  // those. However, in our case, we just need to set the same
  // DRAM bounds value for all the rate classes.
  std::vector<uint32_t> dramBounds(6, dramBoundThreshold);
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::VoqDramBoundTh{dramBounds});
#endif
}

void SaiSwitchManager::setConditionalEntropyRehashPeriodUS(
    int conditionalEntropyRehashPeriodUS) {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::CondEntropyRehashPeriodUS{
          static_cast<uint32_t>(conditionalEntropyRehashPeriodUS)});
#endif
}

void SaiSwitchManager::setLinkUpDebounceTimeout(
    [[maybe_unused]] uint32_t linkUpDebounceTimeoutUs) {
#if defined(BRCM_SAI_SDK_GTE_15_4)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::LinkUpDebounceTimeout{
          linkUpDebounceTimeoutUs});
#else
  throw FbossError("Set switch wide link up debounce timeout not supported");
#endif
}

uint32_t SaiSwitchManager::getLinkUpDebounceTimeout() const {
#if defined(BRCM_SAI_SDK_GTE_15_4)
  const auto& attr = std::get<
      std::optional<SaiSwitchTraits::Attributes::LinkUpDebounceTimeout>>(
      switch_->attributes());
  // Unset means the switch object was created without the attribute, which
  // leaves the SDK on its own default of no debounce.
  if (attr.has_value()) {
    return attr->value();
  }
#endif
  return 0;
}

void SaiSwitchManager::setShelConfig(
    const std::optional<cfg::SelfHealingEcmpLagConfig>& shelConfig) {
  if (shelConfig.has_value()) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::ShelSrcMac{platform_->getLocalMac()});
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::ShelSrcIp{
            folly::IPAddressV6(*shelConfig.value().shelSrcIp())});
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::ShelDstIp{
            folly::IPAddressV6(*shelConfig.value().shelDstIp())});
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::ShelPeriodicInterval{static_cast<uint32_t>(
            *shelConfig.value().shelPeriodicIntervalMS())});
  }
}

void SaiSwitchManager::setLocalVoqMaxExpectedLatency(
    int localVoqMaxExpectedLatencyNsec) {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::VoqLatencyMinLocalNs{
          localVoqMaxExpectedLatencyNsec});
#endif
}

void SaiSwitchManager::setRemoteL1VoqMaxExpectedLatency(
    int remoteL1VoqMaxExpectedLatencyNsec) {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::VoqLatencyMinLevel1Ns{
          remoteL1VoqMaxExpectedLatencyNsec});
#endif
}

void SaiSwitchManager::setRemoteL2VoqMaxExpectedLatency(
    int remoteL2VoqMaxExpectedLatencyNsec) {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::VoqLatencyMinLevel2Ns{
          remoteL2VoqMaxExpectedLatencyNsec});
#endif
}

void SaiSwitchManager::setVoqOutOfBoundsLatency(int voqOutOfBoundsLatency) {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::VoqLatencyMaxLocalNs{voqOutOfBoundsLatency});
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::VoqLatencyMaxLevel1Ns{
          voqOutOfBoundsLatency});
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::VoqLatencyMaxLevel2Ns{
          voqOutOfBoundsLatency});
#endif
}

void SaiSwitchManager::setTcRateLimitList(
    const std::optional<std::map<int32_t, int32_t>>& tcToRateLimitKbps) {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  std::vector<sai_map_t> mapToValueList;
  if (tcToRateLimitKbps) {
    for (const auto& [tc, rateLimit] : *tcToRateLimitKbps) {
      sai_map_t mapping{};
      mapping.key = tc;
      mapping.value = rateLimit;
      mapToValueList.push_back(mapping);
    }
  }
  // empty list will remove rate limit
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::TcRateLimitList{mapToValueList});
#endif
}

bool SaiSwitchManager::isPtpTcEnabled() const {
  bool ptpTcEnabled =
      isPtpTcEnabled_.has_value() ? isPtpTcEnabled_.value() : false;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  if (platform_->getAsic()->getAsicVendor() ==
      HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
    ptpTcEnabled &=
        (GET_OPT_ATTR(Switch, PtpMode, switch_->attributes()) ==
         utility::getSaiPortPtpMode(true));
  }
#endif
  ptpTcEnabled &= managerTable_->portManager().isPtpTcEnabled();
  return ptpTcEnabled;
}

bool SaiSwitchManager::isMeasureCableLengthEnabled() const {
  auto& attr = std::get<std::optional<
      SaiSwitchTraits::Attributes::CablePropagationDelayMeasurement>>(
      switch_->attributes());
  return attr.has_value() && attr->value();
}

void SaiSwitchManager::setPfcWatchdogTimerGranularity(
    int pfcWatchdogTimerGranularityMsec) {
#if defined(BRCM_SAI_SDK_XGS) && defined(BRCM_SAI_SDK_GTE_11_0)
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::PFC_WATCHDOG_TIMER_GRANULARITY)) {
    // We need to set the watchdog granularity to an appropriate value,
    // otherwise the default granularity in SAI/SDK may be incompatible with
    // the requested watchdog intervals. Auto-derivation is being requested in
    // CS00012393810.
    std::vector<sai_map_t> mapToValueList(
        cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX() + 1);
    for (int pri = 0;
         pri <= cfg::switch_config_constants::PFC_PRIORITY_VALUE_MAX();
         pri++) {
      sai_map_t mapping{};
      mapping.key = pri;
      mapping.value = pfcWatchdogTimerGranularityMsec;
      mapToValueList.at(pri) = mapping;
    }
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::PfcTcDldTimerGranularityInterval{
            mapToValueList});
  }
#endif
}

void SaiSwitchManager::setEnablePfcMonitoring(bool enablePfcMonitoring) {
#if defined(BRCM_SAI_SDK_XGS) && defined(BRCM_SAI_SDK_GTE_13_0)
  switch_->setOptionalAttribute(
      SaiSwitchTraits::Attributes::PfcMonitorEnable{enablePfcMonitoring});
#endif
}

void SaiSwitchManager::setCreditRequestProfileSchedulerMode(
    cfg::QueueScheduling scheduling) {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  sai_scheduling_type_t type;
  if (scheduling == cfg::QueueScheduling::STRICT_PRIORITY) {
    type = SAI_SCHEDULING_TYPE_STRICT;
  } else if (scheduling == cfg::QueueScheduling::DEFICIT_ROUND_ROBIN) {
    type = SAI_SCHEDULING_TYPE_DWRR;
  }
  // TODO(daiweix): properly disable the feature from SP or WRR back to INTERNAL
  if (scheduling != cfg::QueueScheduling::INTERNAL) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::CreditRequestProfileSchedulerMode{type});
  }
#endif
}

void SaiSwitchManager::setModuleIdToCreditRequestProfileParam(
    const std::optional<std::map<int32_t, int32_t>>&
        moduleIdToCreditRequestProfileParam) {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  std::vector<sai_map_t> mapToValueList;
  if (moduleIdToCreditRequestProfileParam) {
    for (const auto& [moduleId, param] : *moduleIdToCreditRequestProfileParam) {
      sai_map_t mapping{};
      mapping.key = moduleId;
      mapping.value = param;
      mapToValueList.push_back(mapping);
    }
  }
  // TODO(daiweix): properly disable the feature from SP or WRR back to INTERNAL
  if (!mapToValueList.empty()) {
    switch_->setOptionalAttribute(
        SaiSwitchTraits::Attributes::ModuleIdToCreditRequestProfileParamList{
            mapToValueList});
  }
#endif
}

} // namespace facebook::fboss
