/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiAclTableManager.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

namespace facebook::fboss {

SaiAclTableManager::SaiAclTableManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform)
    : saiStore_(saiStore),
      managerTable_(managerTable),
      platform_(platform),
      aclStats_(HwFb303Stats(std::nullopt)),
      aclEntryMinimumPriority_(
          (platform->getAsic()->getAsicType() ==
               cfg::AsicType::ASIC_TYPE_SANDIA_PHY ||
           platform->getAsic()->getAsicType() ==
               cfg::AsicType::ASIC_TYPE_AGERA3)
              ? 0
              : SaiApiTable::getInstance()->switchApi().getAttribute(
                    managerTable_->switchManager().getSwitchSaiId(),
                    SaiSwitchTraits::Attributes::AclEntryMinimumPriority())),
      aclEntryMaximumPriority_(
          (platform->getAsic()->getAsicType() ==
               cfg::AsicType::ASIC_TYPE_SANDIA_PHY ||
           platform->getAsic()->getAsicType() ==
               cfg::AsicType::ASIC_TYPE_AGERA3)
              ? 0
              : SaiApiTable::getInstance()->switchApi().getAttribute(
                    managerTable_->switchManager().getSwitchSaiId(),
                    SaiSwitchTraits::Attributes::AclEntryMaximumPriority())),
      fdbDstUserMetaDataRangeMin_(0),
      fdbDstUserMetaDataRangeMax_(0),
      fdbDstUserMetaDataMask_(0),
      routeDstUserMetaDataRangeMin_(0),
      routeDstUserMetaDataRangeMax_(0),
      routeDstUserMetaDataMask_(0),
      neighborDstUserMetaDataRangeMin_(0),
      neighborDstUserMetaDataRangeMax_(0),
      neighborDstUserMetaDataMask_(0),
      hasTableGroups_(
          platform->getAsic()->isSupported(HwAsic::Feature::ACL_TABLE_GROUP)) {}

std::vector<sai_int32_t> SaiAclTableManager::getActionTypeList(
    const std::shared_ptr<AclTable>& /* addedAclTable */) {
  std::vector<sai_int32_t> actionTypeList{SAI_ACL_ACTION_TYPE_PACKET_ACTION};
  return actionTypeList;
}

std::set<cfg::AclTableQualifier> SaiAclTableManager::getQualifierSet(
    sai_acl_stage_t /*aclStage*/,
    const std::shared_ptr<AclTable>& /* addedAclTable */) {
  return {cfg::AclTableQualifier::DST_MAC, cfg::AclTableQualifier::ETHER_TYPE};
}

/*
 * aclTableCreateAttributes
 *
 * This function the ACL table crete attributes for the Phy. The Phy needs only
 * Stage, bindPoint, actionType, DestMac and EthType
 */
std::
    pair<SaiAclTableTraits::AdapterHostKey, SaiAclTableTraits::CreateAttributes>
    SaiAclTableManager::aclTableCreateAttributes(
        sai_acl_stage_t aclStage,
        const std::shared_ptr<AclTable>& addedAclTable,
        cfg::AclTableGroupBindPoint /*bindPoint*/) {
  SaiAclTableTraits::Attributes::Stage tableStage = aclStage;
  std::vector<sai_int32_t> bindPointList{SAI_ACL_BIND_POINT_TYPE_PORT};

  SaiAclTableTraits::CreateAttributes attributes{
      tableStage, // stage
      bindPointList,
      std::nullopt, // actionTypeList
      std::nullopt, // srcIpv6
      std::nullopt, // dstIpv6
      std::nullopt, // dstIpv6Word3
      std::nullopt, // dstIpv6Word2
      std::nullopt, // srcIpV4
      std::nullopt, // dstIpV4
      std::nullopt, // fieldL4SrcPort
      std::nullopt, // fieldL4DstPort
      std::nullopt, // ipProtocol
      std::nullopt, // fieldTcpFlags
      std::nullopt, // fieldSrcPort
      std::nullopt, // fieldOutPort
      std::nullopt, // fieldIpFrag
      std::nullopt, // fieldIcmpV4Type
      std::nullopt, // fieldIcmpV4Code
      std::nullopt, // fieldIcmpV6Type
      std::nullopt, // fieldIcmpV6Code
      std::nullopt, // dscp
      std::nullopt, // fieldTc
      true, // fieldDstMac
      std::nullopt, // ipType
      std::nullopt, // ttl
      std::nullopt, // mplsLabel0Ttl
      std::nullopt, // fieldFdbDstUserMeta
      std::nullopt, // route meta
      std::nullopt, // neighbor meta
      true, // ether type
      std::nullopt, // fieldOuterVlanId
      std::nullopt, // fieldAclRangeType
      std::nullopt, // fieldBthOpcode
      std::nullopt, // fieldIpv6NextHeader
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
      std::nullopt, // UserDefinedFieldGroupMin0
      std::nullopt, // UserDefinedFieldGroupMin1
      std::nullopt, // UserDefinedFieldGroupMin2
      std::nullopt, // UserDefinedFieldGroupMin3
      std::nullopt, // UserDefinedFieldGroupMin4
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      std::nullopt, // FieldRouteDestination
#endif
      std::nullopt, // port meta
  };

  SaiAclTableTraits::AdapterHostKey adapterHostKey{addedAclTable->getID()};

  return std::make_pair(adapterHostKey, attributes);
}

} // namespace facebook::fboss
