/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiSystemPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/VoqUtils.h"
#include "fboss/agent/hw/HwSysPortFb303Stats.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_constants.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/ConcurrentIndices.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include <fmt/ranges.h>

using namespace std::chrono;

namespace facebook::fboss {
namespace {
bool createOnlyAttributesChanged(
    const SaiSystemPortTraits::CreateAttributes& oldAttrs,
    const SaiSystemPortTraits::CreateAttributes& newAttrs) {
  return GET_ATTR(SystemPort, ConfigInfo, oldAttrs) !=
      GET_ATTR(SystemPort, ConfigInfo, newAttrs);
}
} // namespace

void SaiSystemPortHandle::resetQueues() {
  for (auto& cfgAndQueue : queues) {
    cfgAndQueue.second->resetQueue();
  }
}

SaiSystemPortManager::SaiSystemPortManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    SaiPlatform* platform,
    ConcurrentIndices* concurrentIndices)
    : saiStore_(saiStore),
      managerTable_(managerTable),
      platform_(platform),
      concurrentIndices_(concurrentIndices),
      tcToQueueMapAllowedOnSystemPort_(
          platform_->getAsic()->getSwitchType() == cfg::SwitchType::VOQ &&
          platform_->getAsic()->isSupported(
              HwAsic::Feature::TC_TO_QUEUE_QOS_MAP_ON_SYSTEM_PORT)) {
  // Extract platform created system ports
  if (platform_->getAsic()->getSwitchType() == cfg::SwitchType::VOQ) {
    auto& systemPortStore = saiStore_->get<SaiSystemPortTraits>();
    for (auto& sysPort : platform_->getInternalSystemPortConfig()) {
      SaiSystemPortTraits::Attributes::ConfigInfo confInfo{sysPort};
      const auto& obj = systemPortStore.get(confInfo);
      auto sysPortObj =
          systemPortStore.loadObjectOwnedByAdapter(obj->adapterKey());
      auto handle = std::make_unique<SaiSystemPortHandle>();
      handle->systemPort = std::move(sysPortObj);
      handles_.insert(
          {SystemPortID(confInfo.value().port_id), std::move(handle)});
    }
  }
}

SaiSystemPortManager::~SaiSystemPortManager() {}

std::optional<std::pair<uint32_t, uint32_t>>
SaiSystemPortManager::coreAndPortIndexFromPort(
    const std::shared_ptr<SystemPort>& swSystemPort) const {
  const auto* asic = platform_->getAsic();
  if (!asic->isSupported(HwAsic::Feature::SYSTEM_PORT_CORE_INDEX_FROM_PORT) ||
      swSystemPort->getPortType() != cfg::PortType::INTERFACE_PORT ||
      swSystemPort->getRemoteSystemPortType().has_value() ||
      !asic->getSwitchId().has_value() ||
      static_cast<uint64_t>(swSystemPort->getSwitchId()) !=
          *asic->getSwitchId()) {
    return std::nullopt;
  }
  // Inverse of getSystemPortID(): a local interface port's system port id is
  // the port id plus the switch's local or global system port offset, minus
  // the start of the switch's port id range.
  auto offset = swSystemPort->getScope() == cfg::Scope::GLOBAL
      ? asic->getGlobalSystemPortOffset()
      : asic->getLocalSystemPortOffset();
  const auto& switchIdToSwitchInfo = *platform_->getConfig()
                                          ->thrift.sw()
                                          ->switchSettings()
                                          ->switchIdToSwitchInfo();
  auto switchInfo = switchIdToSwitchInfo.find(*asic->getSwitchId());
  if (!offset.has_value() || switchInfo == switchIdToSwitchInfo.end()) {
    return std::nullopt;
  }
  PortID portId(
      static_cast<int>(
          static_cast<int64_t>(swSystemPort->getID()) - *offset +
          *switchInfo->second.portIdRange()->minimum()));
  const auto* portHandle = managerTable_->portManager().getPortHandle(portId);
  if (!portHandle) {
    XLOG(DBG2) << "System port " << swSystemPort->getID() << ": port " << portId
               << " not created yet, using the platform mapping";
    return std::nullopt;
  }
  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto saiPortId = portHandle->port->adapterKey();
  auto coreIndex = portApi.getAttribute(
      saiPortId, SaiPortTraits::Attributes::AttachedCoreIndex{});
  auto corePortIndex = portApi.getAttribute(
      saiPortId, SaiPortTraits::Attributes::AttachedCorePortIndex{});
  if (coreIndex != swSystemPort->getCoreIndex() ||
      corePortIndex != swSystemPort->getCorePortIndex()) {
    XLOG(DBG2) << "System port " << swSystemPort->getID() << ": port " << portId
               << " is on core " << coreIndex << " port index " << corePortIndex
               << ", platform mapping says core "
               << swSystemPort->getCoreIndex() << " port index "
               << swSystemPort->getCorePortIndex();
  }
  return std::make_pair(coreIndex, corePortIndex);
}

SaiSystemPortTraits::CreateAttributes
SaiSystemPortManager::attributesFromSwSystemPort(
    const std::shared_ptr<SystemPort>& swSystemPort) const {
  auto coreIndex = static_cast<uint32_t>(swSystemPort->getCoreIndex());
  auto corePortIndex = static_cast<uint32_t>(swSystemPort->getCorePortIndex());
  if (auto fromPort = coreAndPortIndexFromPort(swSystemPort)) {
    std::tie(coreIndex, corePortIndex) = *fromPort;
  }
  sai_system_port_config_t config{
      .port_id = static_cast<uint32_t>(swSystemPort->getID()),
      .attached_switch_id = static_cast<uint32_t>(swSystemPort->getSwitchId()),
      .attached_core_index = coreIndex,
      .attached_core_port_index = corePortIndex,
      .speed = static_cast<uint32_t>(swSystemPort->getSpeedMbps()),
      .num_voq = static_cast<uint32_t>(swSystemPort->getNumVoqs()),
  };
  std::optional<SaiSystemPortTraits::Attributes::QosTcToQueueMap>
      qosTcToQueueMap = std::nullopt;
  auto qosMapHandle =
      managerTable_->qosMapManager().getQosMap(swSystemPort->getQosPolicy());
  if (qosMapHandle && qosMapHandle->tcToVoqMap &&
      (swSystemPort->getPortType() != cfg::PortType::FABRIC_PORT)) {
    auto qosMap = qosMapHandle->tcToVoqMap;
    auto qosMapId = qosMap->adapterKey();
    qosTcToQueueMap =
        SaiSystemPortTraits::Attributes::QosTcToQueueMap{qosMapId};
  }
  std::optional<SaiSystemPortTraits::Attributes::TcRateLimitExclude>
      tcRateLimitExclude = std::nullopt;
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  if (isDualStage3Q2QMode() &&
      (swSystemPort->getPortType() == cfg::PortType::RECYCLE_PORT ||
       swSystemPort->getPortType() == cfg::PortType::MANAGEMENT_PORT ||
       swSystemPort->getPortType() == cfg::PortType::EVENTOR_PORT)) {
    tcRateLimitExclude =
        SaiSystemPortTraits::Attributes::TcRateLimitExclude{true};
  }
#endif
  return SaiSystemPortTraits::CreateAttributes{
      config,
      true /*enabled*/,
      qosTcToQueueMap,
      std::nullopt /* shelPktDstEnable */,
      tcRateLimitExclude,
      swSystemPort->getPushQueueEnabled()};
}

SystemPortSaiId SaiSystemPortManager::addSystemPort(
    const std::shared_ptr<SystemPort>& swSystemPort) {
  auto systemPortHandle = getSystemPortHandle(swSystemPort->getID());
  if (systemPortHandle) {
    throw FbossError(
        "Attempted to add systemPort which already exists: ",
        swSystemPort->getID(),
        " SAI id: ",
        systemPortHandle->systemPort->adapterKey());
  }
  SaiSystemPortTraits::CreateAttributes attributes =
      attributesFromSwSystemPort(swSystemPort);

  portStats_.emplace(
      swSystemPort->getID(),
      std::make_unique<HwSysPortFb303Stats>(
          swSystemPort->getName(),
          HwBasePortFb303Stats::QueueId2Name(),
          platform_->getMultiSwitchStatsPrefix(),
          swSystemPort->getRemoteSystemPortType() !=
              RemoteSystemPortType::DYNAMIC_ENTRY));
  auto handle = std::make_unique<SaiSystemPortHandle>();
  handle->qosMapsSupported =
      swSystemPort->getPortType() != cfg::PortType::FABRIC_PORT;

  auto& systemPortStore = saiStore_->get<SaiSystemPortTraits>();
  SaiSystemPortTraits::AdapterHostKey systemPortKey{
      GET_ATTR(SystemPort, ConfigInfo, attributes)};
  auto saiSystemPort = systemPortStore.setObject(
      systemPortKey, attributes, swSystemPort->getID());
  handle->systemPort = saiSystemPort;
  loadQueues(*handle, swSystemPort);
  handles_.emplace(swSystemPort->getID(), std::move(handle));
  concurrentIndices_->sysPortIds.insert(
      {saiSystemPort->adapterKey(), swSystemPort->getID()});
  concurrentIndices_->sysPortSaiIds.insert(
      {swSystemPort->getID(), saiSystemPort->adapterKey()});
  configureQueues(
      swSystemPort,
      getSystemPortHandleImpl(swSystemPort->getID()),
      swSystemPort->getPortQueues()->impl());
  return saiSystemPort->adapterKey();
}

SaiQueueHandle* FOLLY_NULLABLE SaiSystemPortManager::getQueueHandle(
    SystemPortID swId,
    const SaiQueueConfig& saiQueueConfig) const {
  auto portHandle = getSystemPortHandleImpl(swId);
  if (!portHandle) {
    XLOG(FATAL) << "Invalid null SaiPortHandle for " << swId;
  }
  auto itr = portHandle->queues.find(saiQueueConfig);
  if (itr == portHandle->queues.end()) {
    return nullptr;
  }
  if (!itr->second.get()) {
    throw FbossError("Invalid null SaiQueueHandle for ", swId);
  }
  return itr->second.get();
}

void SaiSystemPortManager::configureQueues(
    const std::shared_ptr<SystemPort>& systemPort,
    SaiSystemPortHandle* sysPortHandle,
    const QueueConfig& newQueueConfig) {
  auto swId = systemPort->getID();
  auto pitr = portStats_.find(swId);

  // Repopulate configured queues
  sysPortHandle->configuredQueues.clear();
  for (const auto& newPortQueue : std::as_const(newQueueConfig)) {
    SaiQueueConfig saiQueueConfig =
        std::make_pair(newPortQueue->getID(), newPortQueue->getStreamType());
    auto queueHandle = getQueueHandle(swId, saiQueueConfig);
    DCHECK(queueHandle);
    managerTable_->queueManager().changeQueue(
        queueHandle, *newPortQueue, nullptr, systemPort->getPortType());

    auto queueName = newPortQueue->getName()
        ? *newPortQueue->getName()
        : folly::to<std::string>("queue", newPortQueue->getID());
    if (pitr != portStats_.end()) {
      pitr->second->queueChanged(newPortQueue->getID(), queueName);
    }
    sysPortHandle->configuredQueues.push_back(queueHandle);
  }
}

void SaiSystemPortManager::changeQueue(
    const std::shared_ptr<SystemPort>& systemPort,
    const QueueConfig& oldQueueConfig,
    const QueueConfig& newQueueConfig) {
  auto systemPortHandle = getSystemPortHandleImpl(systemPort->getID());
  if (!systemPortHandle) {
    throw FbossError("Attempted to change non-existent system port!");
  }
  configureQueues(systemPort, systemPortHandle, newQueueConfig);
  auto swId = systemPort->getID();
  auto pitr = portStats_.find(swId);
  for (const auto& oldPortQueue : std::as_const(oldQueueConfig)) {
    auto portQueueIter = std::find_if(
        newQueueConfig.begin(),
        newQueueConfig.end(),
        [&](const std::shared_ptr<PortQueue> portQueue) {
          return portQueue->getID() == oldPortQueue->getID();
        });
    // Queue Remove
    if (portQueueIter == newQueueConfig.end()) {
      if (pitr != portStats_.end()) {
        pitr->second->queueRemoved(oldPortQueue->getID());
      }
    }
  }
}

void SaiSystemPortManager::changeSystemPort(
    const std::shared_ptr<SystemPort>& oldSystemPort,
    const std::shared_ptr<SystemPort>& newSystemPort) {
  CHECK_EQ(oldSystemPort->getID(), newSystemPort->getID());
  auto handle = getSystemPortHandleImpl(newSystemPort->getID());
  auto newAttributes = attributesFromSwSystemPort(newSystemPort);
  if (createOnlyAttributesChanged(
          handle->systemPort->attributes(), newAttributes)) {
    removeSystemPort(oldSystemPort);
    addSystemPort(newSystemPort);
  } else {
    if (oldSystemPort->getName() != newSystemPort->getName()) {
      // Port name changed - update stats
      portStats_.find(newSystemPort->getID())
          ->second->portNameChanged(newSystemPort->getName());
    }
    // TODO:
    // Compare qos queues changing and if so update qosmap
    // and port queue stats.
    // Note that we don't need to worry about numVoqs changing
    // since that's a create only attribute, which upon change
    // would cause a remove + add sys port sequence
    changeQueue(
        newSystemPort,
        oldSystemPort->getPortQueues()->impl(),
        newSystemPort->getPortQueues()->impl());
    changeQosPolicy(oldSystemPort, newSystemPort);
  }
}

const HwSysPortFb303Stats* SaiSystemPortManager::getLastPortStats(
    SystemPortID port) const {
  auto pitr = portStats_.find(port);
  return pitr != portStats_.end() ? pitr->second.get() : nullptr;
}

void SaiSystemPortManager::loadQueues(
    SaiSystemPortHandle& sysPortHandle,
    const std::shared_ptr<SystemPort>& swSystemPort) {
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::VOQ)) {
    return;
  }
  std::vector<sai_object_id_t> queueList;
  queueList.resize(swSystemPort->getNumVoqs());
  SaiSystemPortTraits::Attributes::QosVoqList queueListAttribute{queueList};
  auto queueSaiIdList =
      SaiApiTable::getInstance()->systemPortApi().getAttribute(
          sysPortHandle.systemPort->adapterKey(), queueListAttribute);
  if (queueSaiIdList.size() == 0) {
    throw FbossError("no queues exist for system port ");
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
  sysPortHandle.queues = managerTable_->queueManager().loadQueues(queueSaiIds);
}

void SaiSystemPortManager::removeSystemPort(
    const std::shared_ptr<SystemPort>& swSystemPort) {
  auto swId = swSystemPort->getID();
  auto itr = handles_.find(swId);
  if (itr == handles_.end()) {
    throw FbossError("Attempted to remove non-existent system port: ", swId);
  }
  clearQosPolicy(swSystemPort->getID());
  concurrentIndices_->sysPortSaiIds.erase(swId);
  concurrentIndices_->sysPortIds.erase(itr->second->systemPort->adapterKey());
  itr->second->resetQueues();
  handles_.erase(itr);
  portStats_.erase(swId);
  XLOG(DBG2) << "removed system port: " << swId;
}

// Derive the physical fabric PortID backing a fabric link monitoring system
// port. With relocation (FLAGS_fabric_ports_uniform_local_offset) fabric ports
// share the standard getSystemPortID allocation, so the reverse lookup uses
// getPortID; otherwise fall back to the legacy offset-based scheme. Both paths
// coexist until all configs move to the relocated layout.
PortID SaiSystemPortManager::getFabricLinkMonitoringPortId(
    const std::shared_ptr<SystemPort>& swSystemPort,
    const std::shared_ptr<SwitchState>& state) const {
  if (FLAGS_fabric_ports_uniform_local_offset) {
    // The fabric link monitoring system port's switchId is the fabric-link
    // switch ID, not the local ASIC's; the reverse lookup needs the local ASIC
    // switchId.
    CHECK(platform_->getAsic()->getSwitchId().has_value());
    return getPortID(
        swSystemPort->getID(),
        SwitchID(*platform_->getAsic()->getSwitchId()),
        state);
  }
  CHECK(fabricLinkMonitoringSystemPortOffset_.has_value())
      << "Fabric link monitoring system port offset is not configured!";
  return getPortIdFromFabricLinkMonSystemPortID(
      swSystemPort->getID(), *fabricLinkMonitoringSystemPortOffset_);
}

// Additional processing needed when system ports associated with fabric ports
// for fabric link monitoring are added
SystemPortSaiId SaiSystemPortManager::addFabricLinkMonitoringSystemPort(
    const std::shared_ptr<SystemPort>& swSystemPort,
    const std::shared_ptr<SwitchState>& state) {
  auto sysPortSaiId = addSystemPort(swSystemPort);
  CHECK(swSystemPort->getPortType() == cfg::PortType::FABRIC_PORT)
      << "Fabric link monitoring system ports should have port type as FABRIC!";
  auto portId = getFabricLinkMonitoringPortId(swSystemPort, state);
  managerTable_->portManager().setFabricLinkMonitoringSystemPortId(
      portId, sysPortSaiId);
  return sysPortSaiId;
}

// No special handling needed for fabric link monitoring sys ports
void SaiSystemPortManager::changeFabricLinkMonitoringSystemPort(
    const std::shared_ptr<SystemPort>& oldSystemPort,
    const std::shared_ptr<SystemPort>& newSystemPort) {
  CHECK(newSystemPort->getPortType() == cfg::PortType::FABRIC_PORT)
      << "Fabric link monitoring system ports should have port type as FABRIC!";
  changeSystemPort(oldSystemPort, newSystemPort);
}

// Additional processing needed when system ports associated with fabric ports
// for fabric link monitoring are removed
void SaiSystemPortManager::removeFabricLinkMonitoringSystemPort(
    const std::shared_ptr<SystemPort>& swSystemPort,
    const std::shared_ptr<SwitchState>& state) {
  CHECK(swSystemPort->getPortType() == cfg::PortType::FABRIC_PORT)
      << "Fabric link monitoring system ports should have port type as FABRIC!";
  auto portId = getFabricLinkMonitoringPortId(swSystemPort, state);
  managerTable_->portManager().resetFabricLinkMonitoringSystemPortId(portId);
  removeSystemPort(swSystemPort);
}

void SaiSystemPortManager::resetQueues() {
  for (auto& idAndHandle : handles_) {
    idAndHandle.second->resetQueues();
  }
}

// private const getter for use by const and non-const getters
SaiSystemPortHandle* SaiSystemPortManager::getSystemPortHandleImpl(
    SystemPortID swId) const {
  auto itr = handles_.find(swId);
  if (itr == handles_.end()) {
    return nullptr;
  }
  if (!itr->second.get()) {
    XLOG(FATAL) << "Invalid null SaiSystemPortHandle for " << swId;
  }
  return itr->second.get();
}

void SaiSystemPortManager::updateStats(
    SystemPortID portId,
    bool updateWatermarks,
    bool updateVoqStats) {
  auto handlesItr = handles_.find(portId);
  if (handlesItr == handles_.end()) {
    return;
  }

  auto portStatItr = portStats_.find(portId);
  if (portStatItr == portStats_.end()) {
    // We don't maintain port stats for disabled ports.
    return;
  }

  auto* handle = handlesItr->second.get();
  auto now = duration_cast<seconds>(system_clock::now().time_since_epoch());
  const auto& prevPortStats = portStatItr->second->portStats();
  HwSysPortStats curPortStats{prevPortStats};
  curPortStats.timestamp_() =
      duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
  if (updateVoqStats || updateWatermarks) {
    managerTable_->queueManager().updateStats(
        handle->configuredQueues,
        curPortStats,
        updateWatermarks,
        updateVoqStats);
  }
  portStats_[portId]->updateStats(curPortStats, now);
}

std::shared_ptr<SystemPortMap> SaiSystemPortManager::constructSystemPorts(
    const std::shared_ptr<MultiSwitchPortMap>& ports,
    const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo,
    int64_t switchId) {
  auto sysPortMap = std::make_shared<SystemPortMap>();
  const std::set<cfg::PortType> kCreateSysPortsFor = {
      cfg::PortType::INTERFACE_PORT,
      cfg::PortType::RECYCLE_PORT,
      cfg::PortType::MANAGEMENT_PORT,
      cfg::PortType::EVENTOR_PORT,
      cfg::PortType::HYPER_PORT,
      cfg::PortType::HYPER_PORT_MEMBER};
  for (const auto& portMap : std::as_const(*ports)) {
    for (const auto& port : std::as_const(*portMap.second)) {
      if (kCreateSysPortsFor.find(port.second->getPortType()) ==
          kCreateSysPortsFor.end()) {
        continue;
      }
      auto sysPort = std::make_shared<SystemPort>(getSystemPortID(
          port.second->getID(),
          port.second->getScope(),
          switchIdToSwitchInfo,
          SwitchID(switchId)));
      sysPort->setSwitchId(SwitchID(switchId));
      sysPort->setName(fmt::format("{}:{}", switchId, port.second->getName()));
      auto platformPort = platform_->getPlatformPort(port.second->getID());
      sysPort->setCoreIndex(*platformPort->getAttachedCoreId());
      sysPort->setCorePortIndex(*platformPort->getCorePortIndex());
      sysPort->setSpeedMbps(static_cast<int>(port.second->getSpeed()));
      sysPort->setNumVoqs(getLocalPortNumVoqs(
          port.second->getPortType(), port.second->getScope()));
      sysPort->setScope(platformPort->getScope());
      sysPort->setPortType(platformPort->getPortType());
      sysPort->setQosPolicy(port.second->getQosPolicy());
      sysPortMap->addSystemPort(std::move(sysPort));
    }
  }

  return sysPortMap;
}

void SaiSystemPortManager::setQosMapOnSystemPort(
    const SaiQosMapHandle* qosMapHandle,
    SystemPortID swId) {
  auto handle = getSystemPortHandle(swId);
  if (!handle->qosMapsSupported) {
    // QosMap not supported on specific system port
    XLOG(DBG2) << "QosMap not supported on system port " << swId;
    return;
  }
  if (qosMapHandle && qosMapHandle->tcToVoqMap) {
    // set qos policy
    auto qosMap = qosMapHandle->tcToVoqMap;
    auto qosMapId = qosMap->adapterKey();
    handle->systemPort->setOptionalAttribute(
        SaiSystemPortTraits::Attributes::QosTcToQueueMap{qosMapId});
    handle->tcToQueueQosMap = qosMap;
    handle->qosPolicy = qosMapHandle->name;
  } else {
    // clear qos policy
    auto qosMapId = QosMapSaiId(SAI_NULL_OBJECT_ID);
    handle->systemPort->setOptionalAttribute(
        SaiSystemPortTraits::Attributes::QosTcToQueueMap{qosMapId});
    handle->tcToQueueQosMap.reset();
    handle->qosPolicy.reset();
  }
}

void SaiSystemPortManager::setQosPolicy(
    SystemPortID portId,
    const std::optional<std::string>& qosPolicy) {
  if (!tcToQueueMapAllowedOnSystemPort_) {
    return;
  }
  auto handle = getSystemPortHandle(portId);
  if (!handle->qosMapsSupported) {
    // QosMap not supported on specific system port
    return;
  }
  XLOG(DBG2) << "set QoS policy " << (qosPolicy ? qosPolicy.value() : "null")
             << " for system port " << portId;
  auto qosMapHandle = managerTable_->qosMapManager().getQosMap(qosPolicy);
  if (!qosMapHandle) {
    if (qosPolicy) {
      throw FbossError(
          "QosMap handle is null for QoS policy: ", qosPolicy.value());
    }
    XLOG(DBG2)
        << "skip programming QoS policy on system port " << portId
        << " because applied QoS policy is null and default QoS policy is absent";
    return;
  }
  setQosMapOnSystemPort(qosMapHandle, portId);
}

void SaiSystemPortManager::setQosPolicy(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  for (const auto& portIdAndHandle : handles_) {
    if (portIdAndHandle.second->qosPolicy == qosPolicy->getName()) {
      setQosPolicy(portIdAndHandle.first, qosPolicy->getName());
    }
  }
}

void SaiSystemPortManager::clearQosPolicy(SystemPortID portId) {
  if (!tcToQueueMapAllowedOnSystemPort_) {
    return;
  }
  auto handle = getSystemPortHandle(portId);
  if (!handle->qosMapsSupported) {
    // QosMap not supported on specific system port
    return;
  }
  XLOG(DBG2) << "clear QoS policy for system port " << portId;
  setQosMapOnSystemPort(nullptr, portId);
}

void SaiSystemPortManager::clearQosPolicy(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  for (const auto& portIdAndHandle : handles_) {
    if (portIdAndHandle.second->qosPolicy == qosPolicy->getName()) {
      clearQosPolicy(portIdAndHandle.first);
    }
  }
}

void SaiSystemPortManager::changeQosPolicy(
    const std::shared_ptr<SystemPort>& oldSystemPort,
    const std::shared_ptr<SystemPort>& newSystemPort) {
  if (oldSystemPort->getQosPolicy() == newSystemPort->getQosPolicy()) {
    return;
  }
  clearQosPolicy(oldSystemPort->getID());
  setQosPolicy(newSystemPort->getID(), newSystemPort->getQosPolicy());
}

void SaiSystemPortManager::resetQosMaps() {
  if (!tcToQueueMapAllowedOnSystemPort_) {
    return;
  }
  for (const auto& systemPortIdAndHandle : handles_) {
    clearQosPolicy(systemPortIdAndHandle.first);
  }
}

void SaiSystemPortManager::addSystemPortShelPktDstEnable(
    const std::shared_ptr<SystemPort>& swSystemPort) const {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  if (!swSystemPort->getShelDestinationEnabled().has_value()) {
    return;
  }
  auto systemPortHandle = getSystemPortHandle(swSystemPort->getID());
  CHECK(systemPortHandle);

  // Load current SDK value into SaiStore - this will avoid unnecessary hw
  // writes.
  auto gotShelPktDstEnable =
      SaiApiTable::getInstance()->systemPortApi().getAttribute(
          systemPortHandle->systemPort->adapterKey(),
          SaiSystemPortTraits::Attributes::ShelPktDstEnable{});
  std::optional<SaiSystemPortTraits::Attributes::ShelPktDstEnable>
      shelPktDstEnable = gotShelPktDstEnable;
  systemPortHandle->systemPort->setAttribute(
      shelPktDstEnable, true /* skipHwWrite */);

  shelPktDstEnable = swSystemPort->getShelDestinationEnabled();
  systemPortHandle->systemPort->setAttribute(shelPktDstEnable);
#endif
}

void SaiSystemPortManager::changeSystemPortShelPktDstEnable(
    const std::shared_ptr<SystemPort>& oldSystemPort,
    const std::shared_ptr<SystemPort>& newSystemPort) const {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  if (oldSystemPort->getShelDestinationEnabled() !=
      newSystemPort->getShelDestinationEnabled()) {
    auto systemPortHandle = getSystemPortHandle(newSystemPort->getID());
    CHECK(systemPortHandle);

    // Load current SDK value into SaiStore - this will avoid unnecessary hw
    // writes.
    auto gotShelPktDstEnable =
        SaiApiTable::getInstance()->systemPortApi().getAttribute(
            systemPortHandle->systemPort->adapterKey(),
            SaiSystemPortTraits::Attributes::ShelPktDstEnable{});
    std::optional<SaiSystemPortTraits::Attributes::ShelPktDstEnable>
        shelPktDstEnable = gotShelPktDstEnable;
    systemPortHandle->systemPort->setAttribute(
        shelPktDstEnable, true /* skipHwWrite */);

    shelPktDstEnable = newSystemPort->getShelDestinationEnabled();
    systemPortHandle->systemPort->setAttribute(shelPktDstEnable);
  }
#endif
}

} // namespace facebook::fboss
