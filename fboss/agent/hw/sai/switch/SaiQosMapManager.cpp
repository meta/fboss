/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiQosMapManager.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/api/QosMapApi.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiHostifManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiSystemPortManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

namespace facebook::fboss {

SaiQosMapManager::SaiQosMapManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {}

std::shared_ptr<SaiQosMap> SaiQosMapManager::setDscpToTcQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  const auto& newDscpMap = qosPolicy->getDscpMap();
  std::vector<sai_qos_map_t> mapToValueList;
  const auto& entries = newDscpMap->get<switch_state_tags::from>();
  mapToValueList.reserve(entries->size());
  for (const auto& entry : std::as_const(*entries)) {
    sai_qos_map_t mapping{};
    mapping.key.dscp = entry->cref<switch_state_tags::attr>()->cref();
    mapping.value.tc = entry->cref<switch_state_tags::trafficClass>()->cref();
    mapToValueList.push_back(mapping);
  }
  // set the dscp -> tc mapping in SAI
  SaiQosMapTraits::Attributes::Type typeAttribute{SAI_QOS_MAP_TYPE_DSCP_TO_TC};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  SaiQosMapTraits::CreateAttributes c = k;
  return store.setObject(k, c);
}

std::shared_ptr<SaiQosMap> SaiQosMapManager::setExpToTcQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  if (!managerTable_->switchManager().isMplsQoSMapSupported()) {
    return nullptr;
  }
  std::vector<sai_qos_map_t> mapToValueList;
  const auto& newExpMap = qosPolicy->getExpMap();
  const auto& entries = newExpMap->cref<switch_state_tags::from>();
  mapToValueList.reserve(entries->size());
  for (const auto& entry : std::as_const(*entries)) {
    sai_qos_map_t mapping{};
    mapping.key.mpls_exp = entry->cref<switch_state_tags::attr>()->cref();
    mapping.value.tc = entry->cref<switch_state_tags::trafficClass>()->cref();
    mapToValueList.push_back(mapping);
  }
  // set the exp -> tc mapping in SAI
  SaiQosMapTraits::Attributes::Type typeAttribute{
      SAI_QOS_MAP_TYPE_MPLS_EXP_TO_TC};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  SaiQosMapTraits::CreateAttributes c = k;
  return store.setObject(k, c);
}

std::shared_ptr<SaiQosMap> SaiQosMapManager::setTcToExpQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  if (!managerTable_->switchManager().isMplsQoSMapSupported()) {
    return nullptr;
  }
  std::vector<sai_qos_map_t> mapToValueList;
  const auto& newExpMap = qosPolicy->getExpMap();
  const auto& entries = newExpMap->cref<switch_state_tags::to>();
  mapToValueList.reserve(entries->size());
  for (const auto& entry : std::as_const(*entries)) {
    sai_qos_map_t mapping{};
    mapping.key.tc = entry->cref<switch_state_tags::trafficClass>()->cref();
    mapping.key.color = SAI_PACKET_COLOR_GREEN;
    mapping.value.mpls_exp = entry->cref<switch_state_tags::attr>()->cref();
    mapToValueList.push_back(mapping);
  }
  // set the tc -> exp mapping in SAI
  SaiQosMapTraits::Attributes::Type typeAttribute{
      SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_MPLS_EXP};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  SaiQosMapTraits::CreateAttributes c = k;
  return store.setObject(k, c);
}

std::shared_ptr<SaiQosMap> SaiQosMapManager::setPcpToTcQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  auto pcpMap = qosPolicy->getPcpMap();
  if (!pcpMap) {
    return nullptr;
  }

  std::vector<sai_qos_map_t> mapToValueList;
  const auto& entries = pcpMap->from();
  mapToValueList.reserve(entries.size());
  for (const auto& entry : entries) {
    sai_qos_map_t mapping{};
    mapping.key.dot1p = *entry.attr();
    mapping.value.tc = *entry.trafficClass();
    mapToValueList.push_back(mapping);
  }
  // set the dot1p (pcp) -> tc mapping in SAI
  SaiQosMapTraits::Attributes::Type typeAttribute{SAI_QOS_MAP_TYPE_DOT1P_TO_TC};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  const SaiQosMapTraits::CreateAttributes& c = k;
  return store.setObject(k, c);
}

std::shared_ptr<SaiQosMap> SaiQosMapManager::setTcToPcpQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  auto pcpMap = qosPolicy->getPcpMap();
  if (!pcpMap) {
    return nullptr;
  }

  std::vector<sai_qos_map_t> mapToValueList;
  const auto& entries = pcpMap->to();
  mapToValueList.reserve(entries.size());
  for (const auto& entry : entries) {
    sai_qos_map_t mapping{};
    mapping.key.tc = *entry.trafficClass();
    mapping.key.color = SAI_PACKET_COLOR_GREEN;
    mapping.value.dot1p = *entry.attr();
    mapToValueList.push_back(mapping);
  }
  // set the tc -> dot1p (pcp) mapping in SAI
  SaiQosMapTraits::Attributes::Type typeAttribute{
      SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DOT1P};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  const SaiQosMapTraits::CreateAttributes& c = k;
  return store.setObject(k, c);
}

std::shared_ptr<SaiQosMap> SaiQosMapManager::setTcToQueueQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy,
    bool voq = false) {
  const auto& newTcToQueueIdMap = voq ? qosPolicy->getTrafficClassToVoqId()
                                      : qosPolicy->getTrafficClassToQueueId();
  std::vector<sai_qos_map_t> mapToValueList;
  mapToValueList.reserve(newTcToQueueIdMap->size());
  for (const auto& [tc, queue] : std::as_const(*newTcToQueueIdMap)) {
    const auto& q = queue->ref();
    sai_qos_map_t mapping{};
    mapping.key.tc = tc;
    mapping.value.queue_index = q;
    mapToValueList.push_back(mapping);
  }
  // set the tc->queue mapping in SAI
  SaiQosMapTraits::Attributes::Type typeAttribute{SAI_QOS_MAP_TYPE_TC_TO_QUEUE};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  SaiQosMapTraits::CreateAttributes c = k;
  return store.setObject(k, c);
}

#if defined(SAI_CBFC_SUPPORTED)
std::shared_ptr<SaiQosMap> SaiQosMapManager::setTcToVcQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  const auto& newTcToVcMap = qosPolicy->getTrafficClassToVcId();
  std::vector<sai_qos_map_t> mapToValueList;
  mapToValueList.reserve(newTcToVcMap->size());
  for (const auto& [tc, vc] : std::as_const(*newTcToVcMap)) {
    sai_qos_map_t mapping{};
    mapping.key.tc = tc;
    mapping.value.vc = vc->cref();
    mapToValueList.push_back(mapping);
  }
  SaiQosMapTraits::Attributes::Type typeAttribute{SAI_QOS_MAP_TYPE_TC_TO_VC};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  SaiQosMapTraits::CreateAttributes c = k;
  return store.setObject(k, c);
}

std::shared_ptr<SaiQosMap> SaiQosMapManager::setQueueToVcQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  const auto& newQueueToVcMap = qosPolicy->getQueueToVcId();
  std::vector<sai_qos_map_t> mapToValueList;
  mapToValueList.reserve(newQueueToVcMap->size());
  for (const auto& [queue, vc] : std::as_const(*newQueueToVcMap)) {
    sai_qos_map_t mapping{};
    mapping.key.queue_index = queue;
    mapping.value.vc = vc->cref();
    mapToValueList.push_back(mapping);
  }
  SaiQosMapTraits::Attributes::Type typeAttribute{SAI_QOS_MAP_TYPE_QUEUE_TO_VC};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  SaiQosMapTraits::CreateAttributes c = k;
  return store.setObject(k, c);
}
#endif

std::shared_ptr<SaiQosMap> SaiQosMapManager::setTcToPgQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  const auto& newTcToPgMap = qosPolicy->getTrafficClassToPgId();
  std::vector<sai_qos_map_t> mapToValueList;
  mapToValueList.reserve(newTcToPgMap->size());
  for (const auto& [tc, pg] : std::as_const(*newTcToPgMap)) {
    sai_qos_map_t mapping{};
    mapping.key.tc = tc;
    mapping.value.pg = pg->cref();
    mapToValueList.push_back(mapping);
  }
  // set the tc->pg mapping in SAI
  SaiQosMapTraits::Attributes::Type typeAttribute{
      SAI_QOS_MAP_TYPE_TC_TO_PRIORITY_GROUP};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  SaiQosMapTraits::CreateAttributes c = k;
  return store.setObject(k, c);
}

std::shared_ptr<SaiQosMap> SaiQosMapManager::setPfcPriorityToQueueQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  const auto& newPfcPriorityToQueueMap = qosPolicy->getPfcPriorityToQueueId();
  std::vector<sai_qos_map_t> mapToValueList;
  mapToValueList.reserve(newPfcPriorityToQueueMap->size());
  for (const auto& [pfcPriority, queue] :
       std::as_const(*newPfcPriorityToQueueMap)) {
    sai_qos_map_t mapping{};
    mapping.key.prio = pfcPriority;
    mapping.value.queue_index = queue->cref();
    mapToValueList.push_back(mapping);
  }
  // set the pfcPriority->queue mapping in SAI
  SaiQosMapTraits::Attributes::Type typeAttribute{
      SAI_QOS_MAP_TYPE_PFC_PRIORITY_TO_QUEUE};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  SaiQosMapTraits::CreateAttributes c = k;
  return store.setObject(k, c);
}

std::shared_ptr<SaiQosMap> SaiQosMapManager::setPfcPriorityToPgQosMap(
    const std::shared_ptr<QosPolicy>& qosPolicy) {
  const auto& newPfcPriorityToPgMap = qosPolicy->getPfcPriorityToPgId();
  std::vector<sai_qos_map_t> mapToValueList;
  mapToValueList.reserve(newPfcPriorityToPgMap->size());
  for (const auto& [pfcPriority, pg] : std::as_const(*newPfcPriorityToPgMap)) {
    sai_qos_map_t mapping{};
    mapping.key.prio = pfcPriority;
    mapping.value.pg = pg->cref();
    mapToValueList.push_back(mapping);
  }
  // set the pfcPriority->pg mapping in SAI
  SaiQosMapTraits::Attributes::Type typeAttribute{
      SAI_QOS_MAP_TYPE_PFC_PRIORITY_TO_PRIORITY_GROUP};
  SaiQosMapTraits::Attributes::MapToValueList mapToValueListAttribute{
      mapToValueList};
  auto& store = saiStore_->get<SaiQosMapTraits>();
  SaiQosMapTraits::AdapterHostKey k{typeAttribute, mapToValueListAttribute};
  const SaiQosMapTraits::CreateAttributes& c = k;
  return store.setObject(k, c);
}

void SaiQosMapManager::setQosMaps(
    const std::shared_ptr<QosPolicy>& newQosPolicy,
    bool isDefault) {
  std::string qosPolicyName = newQosPolicy->getName();
  XLOG(DBG2) << "Setting QoS map: " << qosPolicyName;
  std::unique_ptr<SaiQosMapHandle> handle = std::make_unique<SaiQosMapHandle>();
  handle->dscpToTcMap = setDscpToTcQosMap(newQosPolicy);
  handle->tcToQueueMap = setTcToQueueQosMap(newQosPolicy);
  handle->expToTcMap = setExpToTcQosMap(newQosPolicy);
  handle->tcToExpMap = setTcToExpQosMap(newQosPolicy);
  if (platform_->getAsic()->isSupported(HwAsic::Feature::PFC)) {
    if (newQosPolicy->getTrafficClassToPgId()) {
      handle->tcToPgMap = setTcToPgQosMap(newQosPolicy);
    }
    if (newQosPolicy->getPfcPriorityToQueueId()) {
      handle->pfcPriorityToQueueMap = setPfcPriorityToQueueQosMap(newQosPolicy);
      // Cache the state-side map for getPfcPriorityToQueueId().
      for (const auto& [pfcPriority, queue] :
           std::as_const(*newQosPolicy->getPfcPriorityToQueueId())) {
        handle->pfcPriorityToQueueId[pfcPriority] = queue->cref();
      }
    }
    // Programming this map type in SAI is new and unsupported on some
    // platforms, so it is gated behind a feature flag.
    if (FLAGS_enable_pfc_priority_to_pg_map &&
        newQosPolicy->getPfcPriorityToPgId()) {
      handle->pfcPriorityToPgMap = setPfcPriorityToPgQosMap(newQosPolicy);
    }
    if (newQosPolicy->getPcpMap()) {
      handle->pcpToTcMap = setPcpToTcQosMap(newQosPolicy);
      handle->tcToPcpMap = setTcToPcpQosMap(newQosPolicy);
    }
  }
#if defined(SAI_CBFC_SUPPORTED)
  if (platform_->getAsic()->isSupported(HwAsic::Feature::CBFC) &&
      newQosPolicy->getTrafficClassToVcId()) {
    handle->tcToVcMap = setTcToVcQosMap(newQosPolicy);
  }
  if (platform_->getAsic()->isSupported(HwAsic::Feature::CBFC) &&
      newQosPolicy->getQueueToVcId()) {
    handle->queueToVcMap = setQueueToVcQosMap(newQosPolicy);
  }
#endif
  if (newQosPolicy->getTrafficClassToVoqId() &&
      !newQosPolicy->getTrafficClassToVoqId()->empty()) {
    handle->tcToVoqMap = setTcToQueueQosMap(newQosPolicy, true);
  }
  handle->isDefault = isDefault;
  handle->name = qosPolicyName;
  handles_[qosPolicyName] = std::move(handle);
}

void SaiQosMapManager::addQosMap(
    const std::shared_ptr<QosPolicy>& newQosPolicy,
    bool isDefault) {
  std::string qosPolicyName = newQosPolicy->getName();
  XLOG(DBG2) << "add QoS policy " << qosPolicyName;
  if (handles_.find(qosPolicyName) != handles_.end()) {
    XLOG(DBG2) << "QoS policy " << qosPolicyName
               << " already programmed, update it";
  }
  setQosMaps(newQosPolicy, isDefault);
  managerTable_->portManager().setQosPolicy(newQosPolicy);
  managerTable_->systemPortManager().setQosPolicy(newQosPolicy);
}

void SaiQosMapManager::removeQosMap(
    const std::shared_ptr<QosPolicy>& oldQosPolicy,
    bool /*isDefault*/) {
  std::string qosPolicyName = oldQosPolicy->getName();
  if (handles_.find(qosPolicyName) == handles_.end()) {
    throw FbossError(
        "Failed to remove QoS map: none programmed ", qosPolicyName);
  }
  XLOG(DBG2) << "remove QoS policy " << qosPolicyName;
  managerTable_->portManager().clearQosPolicy(oldQosPolicy);
  managerTable_->systemPortManager().clearQosPolicy(oldQosPolicy);
  handles_.erase(qosPolicyName);
}

void SaiQosMapManager::changeQosMap(
    const std::shared_ptr<QosPolicy>& oldQosPolicy,
    const std::shared_ptr<QosPolicy>& newQosPolicy,
    bool newPolicyIsDefault) {
  std::string oldName = oldQosPolicy->getName();
  std::string newName = newQosPolicy->getName();
  XLOG(DBG2) << "change QoS policy old " << oldName << " new " << newName;
  if (handles_.find(oldName) == handles_.end()) {
    throw FbossError("Failed to change QoS map: none programmed");
  }
  // When old name and new name are the same, only call addQosMap() to
  // update QoS map object and the corresponding port QoS mapping attributes
  if (oldName != newName) {
    removeQosMap(oldQosPolicy, newPolicyIsDefault);
  }
  addQosMap(newQosPolicy, newPolicyIsDefault);
  managerTable_->hostifManager().qosPolicyUpdated(newName);
}

const SaiQosMapHandle* FOLLY_NULLABLE SaiQosMapManager::getQosMap(
    const std::optional<std::string>& qosPolicyName) const {
  return getQosMapImpl(qosPolicyName);
}
SaiQosMapHandle* FOLLY_NULLABLE
SaiQosMapManager::getQosMap(const std::optional<std::string>& qosPolicyName) {
  return getQosMapImpl(qosPolicyName);
}
SaiQosMapHandle* FOLLY_NULLABLE SaiQosMapManager::getQosMapImpl(
    const std::optional<std::string>& qosPolicyName) const {
  if (qosPolicyName && handles_.find(qosPolicyName.value()) != handles_.end()) {
    return handles_.at(qosPolicyName.value()).get();
  }
  if (qosPolicyName) {
    XLOG(DBG2) << "unable to find QoS policy " << qosPolicyName.value();
    return nullptr;
  }
  // if name is null, return default QoS policy
  for (const auto& handle : handles_) {
    if (handle.second->isDefault) {
      return handle.second.get();
    }
  }
  XLOG(DBG2) << "unable to find default QoS policy";
  return nullptr;
}

std::map<int16_t, int16_t> SaiQosMapManager::getPfcPriorityToQueueId(
    const std::optional<std::string>& qosPolicyName) const {
  auto handle = getQosMapImpl(qosPolicyName);
  if (!handle) {
    return {};
  }
  return handle->pfcPriorityToQueueId;
}
} // namespace facebook::fboss
