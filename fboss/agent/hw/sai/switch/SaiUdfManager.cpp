/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiUdfManager.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/IPProto.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

namespace facebook::fboss {

SaiUdfTraits::CreateAttributes SaiUdfManager::udfAttr(
    const std::shared_ptr<UdfGroup> swUdfGroup,
    const sai_object_id_t saiUdfGroupId,
    const sai_object_id_t saiUdfMatchId) const {
  auto udfMatchIdAttr = SaiUdfTraits::Attributes::UdfMatchId{saiUdfMatchId};
  auto udfGroupIdAttr = SaiUdfTraits::Attributes::UdfGroupId{saiUdfGroupId};
  std::optional<SaiUdfTraits::Attributes::Base> baseAttr;
  baseAttr = cfgBaseToSai(swUdfGroup->getUdfBaseHeader());
  auto offsetAttr = SaiUdfTraits::Attributes::Offset{
      static_cast<sai_uint16_t>(swUdfGroup->getStartOffsetInBytes())};
  return SaiUdfTraits::CreateAttributes{
      udfMatchIdAttr, udfGroupIdAttr, baseAttr, offsetAttr};
}

SaiUdfGroupTraits::CreateAttributes SaiUdfManager::udfGroupAttr(
    const std::shared_ptr<UdfGroup> swUdfGroup) const {
  // Type
  auto typeAttr = SaiUdfGroupTraits::Attributes::Type{SAI_UDF_GROUP_TYPE_HASH};
  // TODO(ravi) ensure config gen include type for hash also for SAI
  CHECK(swUdfGroup->getUdfGroupType().has_value());
  cfg::UdfGroupType udfGroupType = swUdfGroup->getUdfGroupType().value();
  if (udfGroupType == cfg::UdfGroupType::ACL) {
    typeAttr = SaiUdfGroupTraits::Attributes::Type{SAI_UDF_GROUP_TYPE_GENERIC};
  }
  // Length
  auto lengthAttr = SaiUdfGroupTraits::Attributes::Length{
      static_cast<sai_uint16_t>(swUdfGroup->getFieldSizeInBytes())};
  return SaiUdfGroupTraits::CreateAttributes{typeAttr, lengthAttr};
}

SaiUdfMatchTraits::CreateAttributes SaiUdfManager::udfMatchAttr(
    const std::shared_ptr<UdfPacketMatcher> swUdfMatch) const {
  // L2 Match Type - match l3 protocol
  auto l2MatchType = cfgL3MatchTypeToSai(swUdfMatch->getUdfl3PktType());
  auto l2MatchAttr =
      SaiUdfMatchTraits::Attributes::L2Type{AclEntryFieldU16(l2MatchType)};
  // L3 Match Type - match l4 protocol
  auto l3MatchType = cfgL4MatchTypeToSai(swUdfMatch->getUdfl4PktType());
  auto l3MatchAttr =
      SaiUdfMatchTraits::Attributes::L3Type{AclEntryFieldU8(l3MatchType)};
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  // L4 Dst Port
  auto l4DstPortAttr = SaiUdfMatchTraits::Attributes::L4DstPortType{
      AclEntryFieldU16(std::make_pair(0, kMaskDontCare))};
  if (auto l4DstPort = swUdfMatch->getUdfL4DstPort()) {
    l4DstPortAttr = SaiUdfMatchTraits::Attributes::L4DstPortType{
        AclEntryFieldU16(std::make_pair(*l4DstPort, kL4PortMask))};
  }
#endif
  return SaiUdfMatchTraits::CreateAttributes{
      l2MatchAttr,
      l3MatchAttr
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      ,
      l4DstPortAttr
#endif
  };
}

UdfGroupSaiId SaiUdfManager::addUdfGroup(
    const std::shared_ptr<UdfGroup>& swUdfGroup) {
  XLOG(DBG2) << "Adding Udf group " << swUdfGroup->getName();
  // Create Sai UDF group
  auto udfGroupCreateAttr = udfGroupAttr(swUdfGroup);
  auto& udfGroupStore = saiStore_->get<SaiUdfGroupTraits>();
  SaiUdfGroupTraits::AdapterHostKey adapterHostKey{swUdfGroup->getName()};
  auto saiUdfGroup =
      udfGroupStore.setObject(adapterHostKey, udfGroupCreateAttr);
  auto udfGroupHandle = std::make_unique<SaiUdfGroupHandle>();
  udfGroupHandle->udfGroup = saiUdfGroup;

  // Create Sai UDF that connects SaiUdfGroup and SaiUdfMatch
  auto& udfStore = saiStore_->get<SaiUdfTraits>();
  for (const auto& udfMatchName : swUdfGroup->getUdfPacketMatcherIds()) {
    XLOG(DBG2) << "Associating Udf group " << swUdfGroup->getName()
               << " with UdfPackerMatcher " << udfMatchName;
    auto saiUdfMatchId = udfMatchHandles_[udfMatchName]->udfMatch->adapterKey();
    auto udfCreateAttr =
        udfAttr(swUdfGroup, saiUdfGroup->adapterKey(), saiUdfMatchId);
    auto udfHandle = std::make_unique<SaiUdfHandle>();
    udfHandle->udf = udfStore.setObject(udfCreateAttr, udfCreateAttr);
    udfHandle->udfGroup = udfGroupHandle.get();
    udfHandle->udfMatch = udfMatchHandles_[udfMatchName].get();

    // Update pointers in UdfMatchHandle
    udfMatchHandles_[udfMatchName]->udfs.push_back(udfHandle.get());

    // Give ownership to UdfGroupHandle
    udfGroupHandle->udfs[udfMatchName] = std::move(udfHandle);
  }

  udfGroupHandles_[swUdfGroup->getName()] = std::move(udfGroupHandle);
  return saiUdfGroup->adapterKey();
}

void SaiUdfManager::removeUdfGroup(
    const std::shared_ptr<UdfGroup>& swUdfGroup) {
  XLOG(DBG2) << "Removing UdfGroup " << swUdfGroup->getName();
  auto udfGroupHandle = udfGroupHandles_[swUdfGroup->getName()].get();
  // Remove SaiUdfs that are still owned by UdfGroup. This means UdfMatch is
  // referenced by multiple UdfGroups.
  for (const auto& [udfMatchName, udfHandle] : udfGroupHandle->udfs) {
    XLOG(DBG2) << "Removing association between UdfGroup "
               << swUdfGroup->getName() << " and UdfPacketMatcher "
               << udfMatchName;
    // Cleanup UdfMatch pointer to Udf
    auto erased = std::erase(udfHandle->udfMatch->udfs, udfHandle.get());
    if (erased != 1) {
      throw FbossError(
          "Cannot find UdfMatch " + udfMatchName +
          " association with UdfGroup " + swUdfGroup->getName());
    }
  }
  udfGroupHandle->udfs.clear();
  udfGroupHandles_.erase(swUdfGroup->getName());
}

UdfMatchSaiId SaiUdfManager::addUdfMatch(
    const std::shared_ptr<UdfPacketMatcher>& swUdfMatch) {
  XLOG(DBG2) << "Adding UdfPackerMatcher " << swUdfMatch->getName();
  auto createAttributes = udfMatchAttr(swUdfMatch);
  auto& udfMatchStore = saiStore_->get<SaiUdfMatchTraits>();
  auto saiUdfMatch =
      udfMatchStore.setObject(createAttributes, createAttributes);

  auto handle = std::make_unique<SaiUdfMatchHandle>();
  handle->udfMatch = saiUdfMatch;
  udfMatchHandles_[swUdfMatch->getName()] = std::move(handle);

  return saiUdfMatch->adapterKey();
}

void SaiUdfManager::removeUdfMatch(
    const std::shared_ptr<UdfPacketMatcher>& swUdfMatch) {
  XLOG(DBG2) << "Removing UdfPackerMatcher " << swUdfMatch->getName();
  auto udfMatchHandle = udfMatchHandles_[swUdfMatch->getName()].get();
  // Remove SaiUdfs that connect udfMatch to udfGroup
  for (const auto udfHandle : udfMatchHandle->udfs) {
    // Cleanup udfHandle owned by UdfGroup
    auto udfGroupHandle = udfHandle->udfGroup;
    udfGroupHandle->udfs.erase(swUdfMatch->getName());
  }
  udfMatchHandles_.erase(swUdfMatch->getName());
}

std::pair<uint8_t, uint8_t> SaiUdfManager::cfgL4MatchTypeToSai(
    cfg::UdfMatchL4Type cfgType) const {
  switch (cfgType) {
    case cfg::UdfMatchL4Type::UDF_L4_PKT_TYPE_ANY:
      return std::make_pair(0, kMaskDontCare);
    case cfg::UdfMatchL4Type::UDF_L4_PKT_TYPE_UDP:
      return std::make_pair(
          static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP),
          static_cast<uint8_t>(kMaskAny));
    case cfg::UdfMatchL4Type::UDF_L4_PKT_TYPE_TCP:
      return std::make_pair(
          static_cast<uint8_t>(IP_PROTO::IP_PROTO_TCP),
          static_cast<uint8_t>(kMaskAny));
  }
  throw FbossError("Invalid udf l2 match type: ", cfgType);
}

std::pair<uint16_t, uint16_t> SaiUdfManager::cfgL3MatchTypeToSai(
    cfg::UdfMatchL3Type cfgType) const {
  switch (cfgType) {
    case cfg::UdfMatchL3Type::UDF_L3_PKT_TYPE_ANY:
      if (platform_->getAsic()->useAllOnesForUdfL2TypeAny()) {
        return std::make_pair(
            static_cast<uint16_t>(kMaskAny), static_cast<uint16_t>(kMaskAny));
      }
      return std::make_pair(0, kMaskDontCare);
    case cfg::UdfMatchL3Type::UDF_L3_PKT_TYPE_IPV4:
      return std::make_pair(
          static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4),
          static_cast<uint16_t>(kMaskAny));
    case cfg::UdfMatchL3Type::UDF_L3_PKT_TYPE_IPV6:
      return std::make_pair(
          static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6),
          static_cast<uint16_t>(kMaskAny));
  }
  throw FbossError("Invalid udf l3 match type: ", cfgType);
}

sai_udf_base_t SaiUdfManager::cfgBaseToSai(
    cfg::UdfBaseHeaderType cfgType) const {
  switch (cfgType) {
    case cfg::UdfBaseHeaderType::UDF_L2_HEADER:
      return SAI_UDF_BASE_L2;
    case cfg::UdfBaseHeaderType::UDF_L3_HEADER:
      return SAI_UDF_BASE_L3;
    case cfg::UdfBaseHeaderType::UDF_L4_HEADER:
      return SAI_UDF_BASE_L4;
  }
  throw FbossError("Invalid udf base header type: ", cfgType);
}

std::vector<sai_object_id_t> SaiUdfManager::getUdfGroupIds(
    std::vector<std::string> udfGroupIds) const {
  std::vector<sai_object_id_t> udfGroupSaiIds;
  for (const auto& udfGroupName : udfGroupIds) {
    if (udfGroupHandles_.find(udfGroupName) != udfGroupHandles_.end()) {
      udfGroupSaiIds.push_back(
          udfGroupHandles_.at(udfGroupName)->udfGroup->adapterKey());
    } else {
      throw FbossError(
          "Unable to find SaiUdfGroupId for UdfGroup " + udfGroupName);
    }
  }
  return udfGroupSaiIds;
}
} // namespace facebook::fboss
