/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiHostifManager.h"
#include "fboss/agent/VoqUtils.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/ConcurrentIndices.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/state/ControlPlane.h"

#include "thrift/lib/cpp/util/EnumUtils.h"

#include <chrono>

extern "C" {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
#include <experimental/saihostifextensions.h>
#endif
}

#include "common/stats/DynamicStats.h"

namespace {

DEFINE_dynamic_quantile_stat(
    buffer_watermark_cpu,
    "buffer_watermark_cpu.queue{}",
    facebook::fb303::ExportTypeConsts::kNone,
    std::array<double, 1>{{1.0}});

} // unnamed namespace

using namespace std::chrono;

namespace facebook::fboss {

SaiHostifManager::SaiHostifManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform,
    ConcurrentIndices* concurrentIndices)
    : saiStore_(saiStore),
      managerTable_(managerTable),
      platform_(platform),
      concurrentIndices_(concurrentIndices),
      cpuStats_(HwCpuFb303Stats(
          {} /*queueId2Name*/,
          platform->getMultiSwitchStatsPrefix())),
      cpuSysPortStats_(HwSysPortFb303Stats(
          "cpu.sysport",
          {},
          platform->getMultiSwitchStatsPrefix())) {
  if (platform_->getAsic()->isSupported(HwAsic::Feature::CPU_PORT)) {
    loadCpuPort();
  }
}

SaiHostifManager::~SaiHostifManager() {
  if (qosPolicy_) {
    clearQosPolicy();
  }
  // clear all user defined trap
  auto& store = saiStore_->get<SaiHostifUserDefinedTrapTraits>();
  store.release();
}

std::optional<std::pair<sai_int32_t, sai_packet_action_t>>
SaiHostifManager::packetReasonToHostifTrap(
    cfg::PacketRxReason reason,
    const SaiPlatform* platform) {
  /*
   * Traps such as ARP request, ARP response and IPv6 ND are configured with
   * packet action COPY:
   *  - One copy reaches the CPU if the ARP/NDP is for the switch.
   *  - Also flooded to the vlan so that the hosts on the vlan receive it.
   * For eg, all rsw downlinks will be in the same vlan. Pinging between hosts
   * will generate an ARP/NdP which has to be flooded to the vlan members.
   * IP2ME, BGP and BGPV6 are destined to the switch and hence configured as
   * TRAP. LLDP and DHCP is link local and hence configured as TRAP.
   *
   * On DNX platforms, use action trap for arp/ndp only, because
   * 1) there is no vlan member in DNX user case
   * 2) two copies of packet would be punt to CPU if the ARP/NDP pkt is
   * destined to the switch itself
   */
  sai_packet_action_t ndpAction;
  if ((platform->getAsic()->getAsicType() ==
       cfg::AsicType::ASIC_TYPE_JERICHO2) ||
      (platform->getAsic()->getAsicType() ==
       cfg::AsicType::ASIC_TYPE_JERICHO3) ||
      (platform->getAsic()->getAsicType() ==
       cfg::AsicType::ASIC_TYPE_JERICHO4)) {
    ndpAction = SAI_PACKET_ACTION_TRAP;
  } else {
    ndpAction = SAI_PACKET_ACTION_COPY;
  }
  switch (reason) {
    case cfg::PacketRxReason::ARP:
      return std::make_pair(SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST, ndpAction);
    case cfg::PacketRxReason::ARP_RESPONSE:
      return std::make_pair(SAI_HOSTIF_TRAP_TYPE_ARP_RESPONSE, ndpAction);
    case cfg::PacketRxReason::NDP:
      return std::make_pair(
          SAI_HOSTIF_TRAP_TYPE_IPV6_NEIGHBOR_DISCOVERY, ndpAction);
    case cfg::PacketRxReason::CPU_IS_NHOP:
      return std::make_pair(SAI_HOSTIF_TRAP_TYPE_IP2ME, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::DHCP:
      return std::make_pair(SAI_HOSTIF_TRAP_TYPE_DHCP, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::LLDP:
      return std::make_pair(SAI_HOSTIF_TRAP_TYPE_LLDP, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::BGP:
      return std::make_pair(SAI_HOSTIF_TRAP_TYPE_BGP, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::BGPV6:
      return std::make_pair(SAI_HOSTIF_TRAP_TYPE_BGPV6, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::LACP:
      return std::make_pair(SAI_HOSTIF_TRAP_TYPE_LACP, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::L3_MTU_ERROR:
      return std::make_pair(
          SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::TTL_1:
      return std::make_pair(
          SAI_HOSTIF_TRAP_TYPE_TTL_ERROR, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::MPLS_TTL_1:
      return std::make_pair(
          SAI_HOSTIF_TRAP_TYPE_MPLS_TTL_ERROR, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::TTL_0:
      if (platform->getAsic()->isSupported(
              HwAsic::Feature::SAI_TTL0_PACKET_FORWARD_ENABLE)) {
        return std::make_pair(
            SAI_HOSTIF_TRAP_TYPE_TTL_ERROR, SAI_PACKET_ACTION_FORWARD);
      }
      // Trap is unsupported otherwise
      break;
    case cfg::PacketRxReason::DHCPV6:
      return std::make_pair(
          SAI_HOSTIF_TRAP_TYPE_DHCPV6, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::SAMPLEPACKET:
      return std::make_pair(
          SAI_HOSTIF_TRAP_TYPE_SAMPLEPACKET, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::EAPOL:
      return std::make_pair(SAI_HOSTIF_TRAP_TYPE_EAPOL, SAI_PACKET_ACTION_TRAP);
    case cfg::PacketRxReason::HOST_MISS:
#if SAI_API_VERSION >= SAI_VERSION(1, 15, 0)
      return std::make_pair(
          SAI_HOSTIF_TRAP_TYPE_NEIGHBOR_MISS, SAI_PACKET_ACTION_TRAP);
#else
      break;
#endif
    case cfg::PacketRxReason::PORT_MTU_ERROR:
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
      return std::make_pair(
          SAI_HOSTIF_TRAP_TYPE_PORT_MTU_ERROR, SAI_PACKET_ACTION_TRAP);
#else
      break;
#endif
    case cfg::PacketRxReason::BPDU:
#if defined(CHENAB_SAI_SDK)
      return std::make_pair(SAI_HOSTIF_TRAP_TYPE_STP, SAI_PACKET_ACTION_TRAP);
#else
      break;
#endif
    case cfg::PacketRxReason::MPLS_UNKNOWN_LABEL:
    case cfg::PacketRxReason::L3_SLOW_PATH:
    case cfg::PacketRxReason::L3_DEST_MISS:
    case cfg::PacketRxReason::UNMATCHED:
      break;
  }
  XLOG(WARN) << "Unsupported packet rx reason for this platform: "
             << apache::thrift::util::enumName(reason);
  return std::nullopt;
}

std::optional<SaiHostifTrapTraits::CreateAttributes>
SaiHostifManager::makeHostifTrapAttributes(
    cfg::PacketRxReason trapId,
    HostifTrapGroupSaiId trapGroupId,
    uint16_t priority,
    const SaiPlatform* platform) {
  std::optional<std::pair<sai_int32_t, sai_packet_action_t>> hostifTrap =
      packetReasonToHostifTrap(trapId, platform);
  if (!hostifTrap.has_value()) {
    return std::nullopt;
  }
  auto [hostifTrapId, hostifPacketAction] = *hostifTrap;
  SaiHostifTrapTraits::Attributes::PacketAction packetAction{
      hostifPacketAction};
  SaiHostifTrapTraits::Attributes::TrapType trapType{hostifTrapId};
  std::optional<SaiHostifTrapTraits::Attributes::TrapPriority> trapPriority{};
  std::optional<SaiHostifTrapTraits::Attributes::TrapGroup> trapGroup{};
  if (hostifPacketAction == SAI_PACKET_ACTION_TRAP ||
      hostifPacketAction == SAI_PACKET_ACTION_COPY) {
    trapPriority = priority;
    trapGroup = SaiHostifTrapTraits::Attributes::TrapGroup(trapGroupId);
  }
  return SaiHostifTrapTraits::CreateAttributes{
      trapType, packetAction, trapPriority, trapGroup};
}

SaiHostifUserDefinedTrapTraits::CreateAttributes
SaiHostifManager::makeHostifUserDefinedTrapAttributes(
    HostifTrapGroupSaiId trapGroupId,
    std::optional<uint16_t> priority,
    std::optional<uint16_t> trapType) {
  SaiHostifUserDefinedTrapTraits::Attributes::TrapGroup trapGroup{trapGroupId};
  return SaiHostifUserDefinedTrapTraits::CreateAttributes{
      trapGroup, priority, trapType};
}

std::shared_ptr<SaiHostifTrapGroup> SaiHostifManager::ensureHostifTrapGroup(
    uint32_t queueId) {
  auto& store = saiStore_->get<SaiHostifTrapGroupTraits>();
  SaiHostifTrapGroupTraits::AdapterHostKey k{queueId};
  SaiHostifTrapGroupTraits::CreateAttributes attributes{queueId, std::nullopt};
  return store.setObject(k, attributes);
}

std::shared_ptr<SaiHostifUserDefinedTrapHandle>
SaiHostifManager::ensureHostifUserDefinedTrap(uint32_t queueId) {
  XLOG(DBG2) << "ensure user defined trap for cpu queue " << queueId;
  auto hostifTrapGroup = ensureHostifTrapGroup(queueId);
  auto priority =
      SaiHostifUserDefinedTrapTraits::Attributes::TrapPriority::defaultValue();
  if (platform_->getAsic()->getAsicVendor() ==
      HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
    // Chenab has following hardware limitations -
    //  - CPU queues in Chenab are 0...3.
    //  - trap priorities are in range of 0...3.
    //  - A trap group is per queue and all traps in same trap group have same
    //  priority.
    // Simplifying with queueId as priority.
    priority = queueId;
    CHECK(
        queueId >= 0 || queueId <= platform_->getAsic()->getHiPriCpuQueueId());
  }
  auto attributes = makeHostifUserDefinedTrapAttributes(
      hostifTrapGroup->adapterKey(),
      priority,
      SaiHostifUserDefinedTrapTraits::Attributes::TrapType::defaultValue());
  SaiHostifUserDefinedTrapTraits::AdapterHostKey k =
      GET_ATTR(HostifUserDefinedTrap, TrapGroup, attributes);
  auto& store = saiStore_->get<SaiHostifUserDefinedTrapTraits>();
  auto userDefinedTrap = store.setObject(k, attributes);
  auto handle = std::make_shared<SaiHostifUserDefinedTrapHandle>();
  handle->trapGroup = hostifTrapGroup;
  handle->trap = userDefinedTrap;
  return handle;
}

std::shared_ptr<SaiHostifTrapCounter> SaiHostifManager::createHostifTrapCounter(
    cfg::PacketRxReason rxReason) {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  auto rxReasonLabelStr = packetRxReasonToString(rxReason);
  SaiCharArray32 rxReasonLabel{};
  std::copy(
      rxReasonLabelStr.begin(), rxReasonLabelStr.end(), rxReasonLabel.begin());
  SaiCounterTraits::Attributes::Type type{SAI_COUNTER_TYPE_REGULAR};
  SaiCounterTraits::Attributes::Label label{rxReasonLabel};
  SaiCounterTraits::CreateAttributes attributes{label, type, std::nullopt};
  SaiCounterTraits::AdapterHostKey k{attributes};
  auto& store = saiStore_->get<SaiCounterTraits>();
  return store.setObject(k, attributes);
#else
  std::ignore = rxReason;
  return nullptr;
#endif
}

std::optional<HostifTrapSaiId> SaiHostifManager::addHostifTrap(
    cfg::PacketRxReason trapId,
    uint32_t queueId,
    uint16_t priority) {
  XLOGF(
      INFO,
      "add trap for {}, send to queue {}, with priority {}",
      apache::thrift::util::enumName(trapId),
      queueId,
      priority);
  if (handles_.find(trapId) != handles_.end()) {
    throw FbossError(
        "Attempted to re-add existing trap for rx reason: ",
        apache::thrift::util::enumName(trapId));
  }
  auto hostifTrapGroup = ensureHostifTrapGroup(queueId);
  std::optional<SaiHostifTrapTraits::CreateAttributes> attributes =
      makeHostifTrapAttributes(
          trapId, hostifTrapGroup->adapterKey(), priority, platform_);
  if (!attributes.has_value()) {
    XLOG(ERR) << "Skipping programming of unsupported packet rx reason: "
              << apache::thrift::util::enumName(trapId);
    return std::nullopt;
  }
  SaiHostifTrapTraits::AdapterHostKey k =
      GET_ATTR(HostifTrap, TrapType, *attributes);
  auto& store = saiStore_->get<SaiHostifTrapTraits>();
  auto hostifTrap = store.setObject(k, *attributes);
  auto handle = std::make_unique<SaiHostifTrapHandle>();
  handle->trap = hostifTrap;
  handle->trapGroup = hostifTrapGroup;
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::SAI_RX_REASON_COUNTER)) {
    handle->counter = createHostifTrapCounter(trapId);
  }
  concurrentIndices_->hostifTrapIds.emplace(handle->trap->adapterKey(), trapId);
  handles_.emplace(trapId, std::move(handle));
  return hostifTrap->adapterKey();
}

void SaiHostifManager::removeHostifTrap(cfg::PacketRxReason trapId) {
  auto handleItr = handles_.find(trapId);
  if (handleItr == handles_.end()) {
    // The trap may never have been programmed if the rx reason is unsupported
    // on this platform, in which case there is nothing to remove.
    XLOG(WARN) << "Attempted to remove non-existent trap for rx reason: "
               << apache::thrift::util::enumName(trapId);
    return;
  }
  concurrentIndices_->hostifTrapIds.erase(
      handleItr->second->trap->adapterKey());
  handles_.erase(trapId);
}

void SaiHostifManager::changeHostifTrap(
    cfg::PacketRxReason trapId,
    uint32_t queueId,
    uint16_t priority) {
  // An unsupported reason was never programmed by addHostifTrap(), so it has
  // no entry in handles_. Bail out on it before the lookup below, which treats
  // a missing handle as a programming error.
  auto hostifTrapGroup = ensureHostifTrapGroup(queueId);
  std::optional<SaiHostifTrapTraits::CreateAttributes> attributes =
      makeHostifTrapAttributes(
          trapId, hostifTrapGroup->adapterKey(), priority, platform_);
  if (!attributes.has_value()) {
    XLOG(ERR) << "Skipping change of unsupported packet rx reason: "
              << apache::thrift::util::enumName(trapId);
    return;
  }
  auto handleItr = handles_.find(trapId);
  if (handleItr == handles_.end()) {
    throw FbossError(
        "Attempted to change non-existent trap for rx reason: ",
        apache::thrift::util::enumName(trapId));
  }
  SaiHostifTrapTraits::AdapterHostKey k =
      GET_ATTR(HostifTrap, TrapType, *attributes);
  auto& store = saiStore_->get<SaiHostifTrapTraits>();
  auto hostifTrap = store.setObject(k, *attributes);

  handleItr->second->trap = hostifTrap;
  handleItr->second->trapGroup = hostifTrapGroup;
}

void SaiHostifManager::processQosDelta(
    const DeltaValue<ControlPlane>& controlPlaneDelta) {
  if (managerTable_->switchManager().isGlobalQoSMapSupported()) {
    // Global QOS policy applies to all ports including the CPU port
    // nothing to do here
    return;
  }
  auto oldQos = controlPlaneDelta.getOld()->getQosPolicy();
  auto newQos = controlPlaneDelta.getNew()->getQosPolicy();
  if (oldQos != newQos) {
    if (newQos) {
      setCpuQosPolicy(newQos->cref());
    } else if (oldQos) {
      clearQosPolicy();
    }
  }
}

void SaiHostifManager::processRxReasonToQueueDelta(
    const DeltaValue<ControlPlane>& controlPlaneDelta) {
  const auto& oldRxReasonToQueue =
      controlPlaneDelta.getOld()->getRxReasonToQueue();
  const auto& newRxReasonToQueue =
      controlPlaneDelta.getNew()->getRxReasonToQueue();
  /*
   * RxReasonToQueue is an ordered list and the enum values dicatates the
   * priority of the traps. Lower the enum value, lower the priority.
   */
  for (auto index = 0; index < newRxReasonToQueue->size(); index++) {
    const auto& newRxReasonEntry = newRxReasonToQueue->cref(index);
    // We'll need to skip for unmatched reason because
    // (1) we're not programming hostif trap for that, and
    // (2) If newly added rx reason entries are above the unmatched reason,
    // we'll try to call changeHostifTrap() for the unmatched reason.
    // TODO(zecheng): Fix the below logic of comparing index (instead we should
    // compare priority)
    if (newRxReasonEntry->cref<switch_config_tags::rxReason>()->toThrift() ==
        cfg::PacketRxReason::UNMATCHED) {
      // what is the trap for unmatched?
      XLOG(WARN) << "ignoring UNMATCHED packet rx reason";
      continue;
    }
    auto oldRxReasonEntryIter = std::find_if(
        oldRxReasonToQueue->cbegin(),
        oldRxReasonToQueue->cend(),
        [newRxReasonEntry](const auto& rxReasonEntry) {
          return rxReasonEntry->template cref<switch_config_tags::rxReason>()
                     ->cref() ==
              newRxReasonEntry->template cref<switch_config_tags::rxReason>()
                  ->cref();
        });
    /*
     * Lower index must have higher priority.
     */
    auto priority = newRxReasonToQueue->size() - index;
    CHECK_GT(priority, 0);
    if (platform_->getAsic()->getAsicVendor() ==
        HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
      // Chenab has following hardware limitations -
      //  - CPU queues in Chenab are 0...3.
      //  - trap priorities are in range of 0...3.
      //  - A trap group is per queue and all traps in same trap group have same
      //  priority.
      // Simplifying with queueId as priority.
      priority = newRxReasonEntry->cref<switch_config_tags::queueId>()->cref();
    }
    if (oldRxReasonEntryIter != oldRxReasonToQueue->cend()) {
      /*
       * If old reason exists and does not match the index, priority of the trap
       * group needs to be updated.
       */
      auto oldIndex =
          std::distance(oldRxReasonToQueue->cbegin(), oldRxReasonEntryIter);
      const auto& oldRxReasonEntry = *oldRxReasonEntryIter;
      if (oldIndex != index ||
          oldRxReasonEntry->cref<switch_config_tags::queueId>()->cref() !=
              newRxReasonEntry->cref<switch_config_tags::queueId>()->cref()) {
        changeHostifTrap(
            newRxReasonEntry->cref<switch_config_tags::rxReason>()->cref(),
            newRxReasonEntry->cref<switch_config_tags::queueId>()->cref(),
            priority);
      }
    } else {
      addHostifTrap(
          newRxReasonEntry->cref<switch_config_tags::rxReason>()->cref(),
          newRxReasonEntry->cref<switch_config_tags::queueId>()->cref(),
          priority);
    }
  }

  for (auto index = 0; index < oldRxReasonToQueue->size(); index++) {
    const auto& oldRxReasonEntry = oldRxReasonToQueue->cref(index);
    if (oldRxReasonEntry->cref<switch_config_tags::rxReason>()->toThrift() ==
        cfg::PacketRxReason::UNMATCHED) {
      // UNMATCHED was never added in the first place, so ignoring here
      XLOG(WARN) << "ignoring UNMATCHED packet rx reason";
      continue;
    }
    auto newRxReasonEntry = std::find_if(
        newRxReasonToQueue->cbegin(),
        newRxReasonToQueue->cend(),
        [&](const auto& rxReasonEntry) {
          return rxReasonEntry->template cref<switch_config_tags::rxReason>()
                     ->cref() ==
              oldRxReasonEntry->template cref<switch_config_tags::rxReason>()
                  ->cref();
        });
    if (newRxReasonEntry == newRxReasonToQueue->cend()) {
      removeHostifTrap(
          oldRxReasonEntry->cref<switch_config_tags::rxReason>()->cref());
    }
  }
}

void SaiHostifManager::processQueueDelta(
    const DeltaValue<ControlPlane>& controlPlaneDelta) {
  const auto& oldQueues =
      controlPlaneDelta.getOld()->cref<switch_state_tags::queues>();
  const auto& newQueues =
      controlPlaneDelta.getNew()->cref<switch_state_tags::queues>();
  changeCpuQueue(oldQueues, newQueues);
}

void SaiHostifManager::processVoqDelta(
    const DeltaValue<ControlPlane>& controlPlaneDelta) {
  const auto& newVoqs =
      controlPlaneDelta.getNew()->cref<switch_state_tags::voqs>();
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::VOQ) &&
      !newVoqs->empty()) {
    throw FbossError(
        "Got non-empty cpu voq state on platform not supporting VOQ");
  }
  if (newVoqs->empty() &&
      platform_->getAsic()->isSupported(HwAsic::Feature::VOQ)) {
    // TODO(daiweix): remove this if clause after populating cpu voq
    // configs everywhere. For now, use the same voq config from egq
    const auto& oldQueues =
        controlPlaneDelta.getOld()->cref<switch_state_tags::queues>();
    const auto& newQueues =
        controlPlaneDelta.getNew()->cref<switch_state_tags::queues>();
    changeCpuVoq(oldQueues, newQueues);
  } else {
    const auto& oldVoqs =
        controlPlaneDelta.getOld()->cref<switch_state_tags::voqs>();
    changeCpuVoq(oldVoqs, newVoqs);
  }
}

void SaiHostifManager::processHostifDelta(
    const ThriftMapDelta<MultiControlPlane>& multiSwitchControlPlaneDelta) {
  DeltaFunctions::forEachChanged(
      multiSwitchControlPlaneDelta,
      [&](const std::shared_ptr<ControlPlane>& oldCPU,
          const std::shared_ptr<ControlPlane>& newCPU) {
        auto controlPlaneDelta = DeltaValue<ControlPlane>(oldCPU, newCPU);
        processHostifEntryDelta(controlPlaneDelta);
      });
  DeltaFunctions::forEachAdded(
      multiSwitchControlPlaneDelta,
      [&](const std::shared_ptr<ControlPlane>& newCPU) {
        auto controlPlaneDelta =
            DeltaValue<ControlPlane>(std::make_shared<ControlPlane>(), newCPU);
        processHostifEntryDelta(controlPlaneDelta);
      });
  DeltaFunctions::forEachRemoved(
      multiSwitchControlPlaneDelta,
      [&](const std::shared_ptr<ControlPlane>& oldCPU) {
        auto controlPlaneDelta =
            DeltaValue<ControlPlane>(oldCPU, std::make_shared<ControlPlane>());
        processHostifEntryDelta(controlPlaneDelta);
      });
}

void SaiHostifManager::processHostifEntryDelta(
    const DeltaValue<ControlPlane>& controlPlaneDelta) {
  if (*controlPlaneDelta.getOld() == *controlPlaneDelta.getNew()) {
    return;
  }
  // TODO: Can we have reason code to a queue mapping that does not have
  // corresponding sai queue oid for cpu port ?
  processRxReasonToQueueDelta(controlPlaneDelta);
  processQueueDelta(controlPlaneDelta);
  processVoqDelta(controlPlaneDelta);
  processQosDelta(controlPlaneDelta);
}
SaiQueueHandle* SaiHostifManager::getQueueHandleImpl(
    const SaiQueueConfig& saiQueueConfig) const {
  auto itr = cpuPortHandle_->queues.find(saiQueueConfig);
  if (itr == cpuPortHandle_->queues.end()) {
    return nullptr;
  }
  if (!itr->second.get()) {
    XLOG(FATAL) << "Invalid null SaiQueueHandle";
  }
  return itr->second.get();
}

const SaiQueueHandle* SaiHostifManager::getQueueHandle(
    const SaiQueueConfig& saiQueueConfig) const {
  return getQueueHandleImpl(saiQueueConfig);
}

SaiQueueHandle* SaiHostifManager::getQueueHandle(
    const SaiQueueConfig& saiQueueConfig) {
  return getQueueHandleImpl(saiQueueConfig);
}

SaiQueueHandle* FOLLY_NULLABLE
SaiHostifManager::getVoqHandleImpl(const SaiQueueConfig& saiQueueConfig) const {
  auto itr = cpuPortHandle_->voqs.find(saiQueueConfig);
  if (itr == cpuPortHandle_->voqs.end()) {
    XLOG(FATAL) << "No cpu voq handle configured for " << saiQueueConfig.first;
  }
  if (!itr->second.get()) {
    XLOG(FATAL) << "Invalid null SaiQueueHandle for cpu voq";
  }
  return itr->second.get();
}

const SaiQueueHandle* FOLLY_NULLABLE
SaiHostifManager::getVoqHandle(const SaiQueueConfig& saiQueueConfig) const {
  return getVoqHandleImpl(saiQueueConfig);
}

SaiQueueHandle* FOLLY_NULLABLE
SaiHostifManager::getVoqHandle(const SaiQueueConfig& saiQueueConfig) {
  return getVoqHandleImpl(saiQueueConfig);
}

void SaiHostifManager::changeCpuVoq(
    const ControlPlane::PortQueues& oldVoqConfig,
    const ControlPlane::PortQueues& newVoqConfig) {
  cpuPortHandle_->configuredVoqs.clear();

  auto maxCpuVoqs =
      getLocalPortNumVoqs(cfg::PortType::CPU_PORT, cfg::Scope::LOCAL);
  for (const auto& newPortVoq : std::as_const(*newVoqConfig)) {
    // Voq create or update
    if (newPortVoq->getID() > maxCpuVoqs) {
      throw FbossError(
          "Voq ID : ",
          newPortVoq->getID(),
          " exceeds max supported CPU queues: ",
          maxCpuVoqs);
    }
    SaiQueueConfig saiVoqConfig =
        std::make_pair(newPortVoq->getID(), newPortVoq->getStreamType());
    auto portVoq = newPortVoq->clone();
    auto voqHandle = getVoqHandle(saiVoqConfig);
    if (newPortVoq->getMaxDynamicSharedBytes()) {
      portVoq->setMaxDynamicSharedBytes(
          *newPortVoq->getMaxDynamicSharedBytes());
    }
    CHECK_NOTNULL(voqHandle);
    XLOG(DBG2) << "set maxDynamicSharedBytes "
               << (portVoq->getMaxDynamicSharedBytes().has_value()
                       ? folly::to<std::string>(static_cast<int>(
                             portVoq->getMaxDynamicSharedBytes().value()))
                       : "None")
               << " for cpu voq " << portVoq->getID();
    managerTable_->queueManager().changeQueue(
        voqHandle, *portVoq, nullptr /*swPort*/, cfg::PortType::CPU_PORT);
    if (newPortVoq->getName().has_value()) {
      auto voqName = *newPortVoq->getName();
      cpuSysPortStats_.queueChanged(newPortVoq->getID(), voqName);
      CHECK_NOTNULL(voqHandle);
      XLOG(DBG2) << "add configured cpu voq " << newPortVoq->getID();
      cpuPortHandle_->configuredVoqs.push_back(voqHandle);
    }
  }
  for (const auto& oldPortVoq : std::as_const(*oldVoqConfig)) {
    auto portVoqIter = std::find_if(
        newVoqConfig->cbegin(),
        newVoqConfig->cend(),
        [&](const std::shared_ptr<PortQueue> portVoq) {
          return portVoq->getID() == oldPortVoq->getID();
        });
    // Voq Remove
    if (portVoqIter == newVoqConfig->cend()) {
      cpuSysPortStats_.queueRemoved(oldPortVoq->getID());
      XLOG(DBG2) << "remove configured cpu voq " << oldPortVoq->getID();
    }
  }
}

void SaiHostifManager::changeCpuQueue(
    const ControlPlane::PortQueues& oldQueueConfig,
    const ControlPlane::PortQueues& newQueueConfig) {
  cpuPortHandle_->configuredQueues.clear();

  const auto asic = platform_->getAsic();
  auto maxCpuQueues = getMaxCpuQueues();
  for (const auto& newPortQueue : std::as_const(*newQueueConfig)) {
    // Queue create or update
    if (newPortQueue->getID() > maxCpuQueues) {
      throw FbossError(
          "Queue ID : ",
          newPortQueue->getID(),
          " exceeds max supported CPU queues: ",
          maxCpuQueues);
    }
    SaiQueueConfig saiQueueConfig =
        std::make_pair(newPortQueue->getID(), newPortQueue->getStreamType());
    auto queueHandle = getQueueHandle(saiQueueConfig);
    auto portQueue = newPortQueue->clone();
    if (auto reservedBytes = newPortQueue->getReservedBytes()) {
      portQueue->setReservedBytes(*reservedBytes);
    } else if (
        auto defaultReservedBytes = asic->getDefaultReservedBytes(
            newPortQueue->getStreamType(), cfg::PortType::CPU_PORT)) {
      portQueue->setReservedBytes(*defaultReservedBytes);
    }
    if (auto scalingFactor = newPortQueue->getScalingFactor()) {
      portQueue->setScalingFactor(*scalingFactor);
    } else if (
        auto defaultScalingFactor = asic->getDefaultScalingFactor(
            newPortQueue->getStreamType(), true /*cpu port*/)) {
      portQueue->setScalingFactor(*defaultScalingFactor);
    }
    // TODO(pshaikh) : this is where cpu port queue is being changed
    managerTable_->queueManager().changeQueue(
        queueHandle, *portQueue, nullptr /*swPort*/, cfg::PortType::CPU_PORT);
    if (newPortQueue->getName().has_value()) {
      auto queueName = *newPortQueue->getName();
      cpuStats_.queueChanged(newPortQueue->getID(), queueName);
      cpuPortHandle_->configuredQueues.push_back(queueHandle);
    }
  }
  for (const auto& oldPortQueue : std::as_const(*oldQueueConfig)) {
    auto portQueueIter = std::find_if(
        newQueueConfig->cbegin(),
        newQueueConfig->cend(),
        [&](const std::shared_ptr<PortQueue> portQueue) {
          return portQueue->getID() == oldPortQueue->getID();
        });
    // Queue Remove
    if (portQueueIter == newQueueConfig->cend()) {
      SaiQueueConfig saiQueueConfig =
          std::make_pair(oldPortQueue->getID(), oldPortQueue->getStreamType());
      cpuPortHandle_->queues.erase(saiQueueConfig);
      cpuStats_.queueRemoved(oldPortQueue->getID());
    }
  }
}

void SaiHostifManager::loadCpuPortQueues() {
  std::vector<sai_object_id_t> queueList;
  queueList.resize(1);
  SaiPortTraits::Attributes::QosQueueList queueListAttribute{queueList};
  auto queueSaiIdList = SaiApiTable::getInstance()->portApi().getAttribute(
      cpuPortHandle_->cpuPortId, queueListAttribute);
  if (queueSaiIdList.size() == 0) {
    throw FbossError("no queues exist for cpu port ");
  }
  std::vector<QueueSaiId> queueSaiIds;
  queueSaiIds.reserve(queueSaiIdList.size());
  std::transform(
      queueSaiIdList.begin(),
      queueSaiIdList.end(),
      std::back_inserter(queueSaiIds),
      [](sai_object_id_t queueId) -> QueueSaiId {
        return QueueSaiId(queueId);
      });
  cpuPortHandle_->queues =
      managerTable_->queueManager().loadQueues(queueSaiIds);
}

void SaiHostifManager::loadCpuSystemPortVoqs() {
  std::vector<sai_object_id_t> voqList;
  voqList.resize(1);
  SaiSystemPortTraits::Attributes::QosVoqList voqListAttribute{voqList};
  auto voqSaiIdList = SaiApiTable::getInstance()->systemPortApi().getAttribute(
      cpuPortHandle_->cpuSystemPortId.value(), voqListAttribute);
  if (voqSaiIdList.size() == 0) {
    throw FbossError("no voqs exist for cpu port ");
  }
  std::vector<QueueSaiId> voqSaiIds;
  voqSaiIds.reserve(voqSaiIdList.size());
  std::transform(
      voqSaiIdList.begin(),
      voqSaiIdList.end(),
      std::back_inserter(voqSaiIds),
      [](sai_object_id_t voqId) -> QueueSaiId { return QueueSaiId(voqId); });
  cpuPortHandle_->voqs = managerTable_->queueManager().loadQueues(voqSaiIds);
}

void SaiHostifManager::loadCpuPort() {
  cpuPortHandle_ = std::make_unique<SaiCpuPortHandle>();
  cpuPortHandle_->cpuPortId = managerTable_->switchManager().getCpuPort();
  XLOG(DBG2) << "Got cpu sai port ID " << cpuPortHandle_->cpuPortId;
  const auto& portApi = SaiApiTable::getInstance()->portApi();
  if (platform_->getAsic()->isSupported(HwAsic::Feature::VOQ)) {
    auto attr = SaiPortTraits::Attributes::SystemPort{};
    cpuPortHandle_->cpuSystemPortId =
        portApi.getAttribute(cpuPortHandle_->cpuPortId, attr);
    XLOG(DBG2) << "Got cpu sai system port ID "
               << cpuPortHandle_->cpuSystemPortId.value();
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
    auto& systemPortApi = SaiApiTable::getInstance()->systemPortApi();
    auto oldTcRateLimitExclude = systemPortApi.getAttribute(
        cpuPortHandle_->cpuSystemPortId.value(),
        SaiSystemPortTraits::Attributes::TcRateLimitExclude{});
    // always exclude cpu system port from global tc based rate limit
    if (!oldTcRateLimitExclude && isDualStage3Q2QMode()) {
      systemPortApi.setAttribute(
          cpuPortHandle_->cpuSystemPortId.value(),
          SaiSystemPortTraits::Attributes::TcRateLimitExclude{true});
      XLOG(DBG2) << "Excluded cpu system port from global tc rate limit";
    }
#endif
  }
  // Load CPU queues only for switches with CPU queues
  if (platform_->getAsic()->isSupported(HwAsic::Feature::CPU_QUEUES)) {
    loadCpuPortQueues();
  }
  if (platform_->getAsic()->isSupported(HwAsic::Feature::VOQ)) {
    loadCpuSystemPortVoqs();
  }
}

void SaiHostifManager::updateStats(bool updateWatermarks) {
  if (updateWatermarks &&
      !platform_->getAsic()->isSupported(
          HwAsic::Feature::CPU_QUEUE_WATERMARK_STATS)) {
    throw FbossError("Watermarks are not supported on CPU queues");
  }
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  HwPortStats cpuQueueStats;
  managerTable_->queueManager().updateStats(
      cpuPortHandle_->configuredQueues, cpuQueueStats, updateWatermarks);
  cpuStats_.updateStats(cpuQueueStats, now);
  if (updateWatermarks) {
    for (const auto& queueId2Name : cpuStats_.getQueueId2Name()) {
      publishCpuQueueWatermark(
          queueId2Name.first,
          cpuQueueStats.queueWatermarkBytes_()->at(queueId2Name.first));
    }
  }
  if (platform_->getAsic()->isSupported(HwAsic::Feature::VOQ)) {
    const auto& prevPortStats = cpuSysPortStats_.portStats();
    HwSysPortStats curPortStats{prevPortStats};
    managerTable_->queueManager().updateStats(
        cpuPortHandle_->configuredVoqs, curPortStats, updateWatermarks, true);
    cpuSysPortStats_.updateStats(curPortStats, now);
  }
}

HwPortStats SaiHostifManager::getCpuPortStats() const {
  HwPortStats hwPortStats;
  managerTable_->queueManager().getStats(cpuPortHandle_->queues, hwPortStats);
  return hwPortStats;
}

uint32_t SaiHostifManager::getMaxCpuQueues() const {
  auto asic = platform_->getAsic();
  if (asic->isSupported(HwAsic::Feature::CPU_PORT) &&
      !asic->isSupported(HwAsic::Feature::CPU_QUEUES)) {
    // Switch with CPU port support, but not CPU queues
    return 0;
  }
  auto cpuQueueTypes = asic->getQueueStreamTypes(cfg::PortType::CPU_PORT);
  CHECK_EQ(cpuQueueTypes.size(), 1);
  return asic->getDefaultNumPortQueues(
      *cpuQueueTypes.begin(), cfg::PortType::CPU_PORT);
}

QueueConfig SaiHostifManager::getQueueSettings() const {
  if (!cpuPortHandle_) {
    return QueueConfig{};
  }
  auto queueConfig =
      managerTable_->queueManager().getQueueSettings(cpuPortHandle_->queues);
  auto maxCpuQueues = getMaxCpuQueues();
  QueueConfig filteredQueueConfig;
  // Prepare queue config only upto max CPU queues
  std::copy_if(
      queueConfig.begin(),
      queueConfig.end(),
      std::back_inserter(filteredQueueConfig),
      [maxCpuQueues](const auto& portQueue) {
        return portQueue->getID() < maxCpuQueues;
      });
  return filteredQueueConfig;
}

QueueConfig SaiHostifManager::getVoqSettings() const {
  if (!cpuPortHandle_) {
    return QueueConfig{};
  }
  auto voqConfig =
      managerTable_->queueManager().getQueueSettings(cpuPortHandle_->voqs);
  auto maxCpuVoqs =
      getLocalPortNumVoqs(cfg::PortType::CPU_PORT, cfg::Scope::LOCAL);
  QueueConfig filteredVoqConfig;
  // Prepare voq config only upto max CPU voqs
  std::copy_if(
      voqConfig.begin(),
      voqConfig.end(),
      std::back_inserter(filteredVoqConfig),
      [maxCpuVoqs](const auto& portVoq) {
        return portVoq->getID() < maxCpuVoqs;
      });
  return filteredVoqConfig;
}

SaiHostifTrapHandle* SaiHostifManager::getHostifTrapHandleImpl(
    cfg::PacketRxReason rxReason) const {
  auto itr = handles_.find(rxReason);
  if (itr == handles_.end()) {
    return nullptr;
  }
  if (!itr->second.get()) {
    XLOG(FATAL) << "Invalid null SaiHostifTrapHandle";
  }
  return itr->second.get();
}

const SaiHostifTrapHandle* SaiHostifManager::getHostifTrapHandle(
    cfg::PacketRxReason rxReason) const {
  return getHostifTrapHandleImpl(rxReason);
}

SaiHostifTrapHandle* SaiHostifManager::getHostifTrapHandle(
    cfg::PacketRxReason rxReason) {
  return getHostifTrapHandleImpl(rxReason);
}

void SaiHostifManager::qosPolicyUpdated(const std::string& qosPolicy) {
  if (qosPolicy_ == qosPolicy) {
    // On J3 platforms, different qos policies are applied to frontpanel
    // ports and cpu port. So, set new qos policy only when the qos policy
    // applied to cpu is updated.
    setCpuQosPolicy(qosPolicy_);
  }
}

void SaiHostifManager::setCpuQosPolicy(
    const std::optional<std::string>& qosPolicy) {
  auto& qosMapManager = managerTable_->qosMapManager();
  XLOG(DBG2) << "Set cpu qos map " << (qosPolicy ? qosPolicy.value() : "null");
  // We allow only a single QoS policy right now, so
  // pick that up.
  auto qosMapHandle = qosMapManager.getQosMap(qosPolicy);
  if (!qosMapHandle) {
    throw FbossError("empty qos map handle for cpu port");
  }
#if defined(SAI_CBFC_SUPPORTED)
  // Before DSCP_TO_TC, as on the front panel ports: the CPU port shares their
  // DSCP_TO_TC map, and brcm-sai rejects that bind with OBJECT IN USE unless
  // the CPU port carries the same TC_TO_VC.
  setCpuPortTcToVcMap(qosMapHandle->tcToVcMap);
#endif
  setCpuPortQosPolicy(
      qosMapHandle->dscpToTcMap->adapterKey(),
      qosMapHandle->tcToQueueMap->adapterKey());
  if (qosMapHandle->tcToVoqMap) {
    setCpuSystemPortQosPolicy(qosMapHandle->tcToVoqMap->adapterKey());
  }
  // update qos map shared pointers at last to keep
  // new qos map objects and release unused ones
  dscpToTcQosMap_ = qosMapHandle->dscpToTcMap;
  tcToQueueQosMap_ = qosMapHandle->tcToQueueMap;
  tcToVoqMap_ = qosMapHandle->tcToVoqMap;
  qosPolicy_ = qosMapHandle->name;
}

void SaiHostifManager::clearQosPolicy() {
  setCpuPortQosPolicy(
      QosMapSaiId(SAI_NULL_OBJECT_ID), QosMapSaiId(SAI_NULL_OBJECT_ID));
#if defined(SAI_CBFC_SUPPORTED)
  setCpuPortTcToVcMap(nullptr);
#endif
  if (tcToVoqMap_) {
    setCpuSystemPortQosPolicy(QosMapSaiId(SAI_NULL_OBJECT_ID));
  }
  dscpToTcQosMap_.reset();
  tcToQueueQosMap_.reset();
  tcToVoqMap_.reset();
  qosPolicy_.reset();
}

//
// XGS:
//  - Setting tcToQueueMap to non SAI_NULL_OBJECT_ID fails.
//  - Instead, we need to clear (SAI_NULL_OBJECT_ID) and set tcToQueueMap.
//  - CS00012322624 to debug.
void SaiHostifManager::setCpuPortQosPolicy(
    QosMapSaiId dscpToTc,
    QosMapSaiId tcToQueue) {
  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto oldDscpToTc = portApi.getAttribute(
      cpuPortHandle_->cpuPortId, SaiPortTraits::Attributes::QosDscpToTcMap{});
  auto oldTcToQueue = portApi.getAttribute(
      cpuPortHandle_->cpuPortId, SaiPortTraits::Attributes::QosTcToQueueMap{});
  if (oldDscpToTc != dscpToTc) {
    portApi.setAttribute(
        cpuPortHandle_->cpuPortId,
        SaiPortTraits::Attributes::QosDscpToTcMap{
            QosMapSaiId(SAI_NULL_OBJECT_ID)});
    portApi.setAttribute(
        cpuPortHandle_->cpuPortId,
        SaiPortTraits::Attributes::QosDscpToTcMap{dscpToTc});
  }
  if (oldTcToQueue != tcToQueue) {
    portApi.setAttribute(
        cpuPortHandle_->cpuPortId,
        SaiPortTraits::Attributes::QosTcToQueueMap{
            QosMapSaiId(SAI_NULL_OBJECT_ID)});
    portApi.setAttribute(
        cpuPortHandle_->cpuPortId,
        SaiPortTraits::Attributes::QosTcToQueueMap{tcToQueue});
  }
}

#if defined(SAI_CBFC_SUPPORTED)
void SaiHostifManager::setCpuPortTcToVcMap(
    const std::shared_ptr<SaiQosMap>& tcToVc) {
  if (tcToVc == tcToVcQosMap_) {
    return;
  }
  SaiApiTable::getInstance()->portApi().setAttribute(
      cpuPortHandle_->cpuPortId,
      SaiPortTraits::Attributes::QosTcToVcMap{
          tcToVc ? tcToVc->adapterKey() : QosMapSaiId(SAI_NULL_OBJECT_ID)});
  tcToVcQosMap_ = tcToVc;
}
#endif

void SaiHostifManager::setCpuSystemPortQosPolicy(QosMapSaiId tcToQueue) {
  if (!cpuPortHandle_->cpuSystemPortId) {
    return;
  }
  auto& systemPortApi = SaiApiTable::getInstance()->systemPortApi();
  auto oldTcToQueue = systemPortApi.getAttribute(
      cpuPortHandle_->cpuSystemPortId.value(),
      SaiSystemPortTraits::Attributes::QosTcToQueueMap{});
  if (oldTcToQueue != tcToQueue) {
    systemPortApi.setAttribute(
        cpuPortHandle_->cpuSystemPortId.value(),
        SaiSystemPortTraits::Attributes::QosTcToQueueMap{tcToQueue});
  }
}

void SaiHostifManager::publishCpuQueueWatermark(int queue, uint64_t peakBytes)
    const {
  STATS_buffer_watermark_cpu.addValue(peakBytes, queue);
}

} // namespace facebook::fboss
