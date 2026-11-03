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
#include "fboss/agent/hw/sai/switch/SaiAclTableGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiUdfManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

extern "C" {
#if defined(BRCM_SAI_SDK_GTE_13_0) && defined(BRCM_SAI_SDK_XGS)
#include <experimental/saiaclextensions.h>
#endif
}

DECLARE_bool(enable_acl_table_group);

namespace facebook::fboss {

SaiAclTableManager::SaiAclTableManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform)
    : saiStore_(saiStore),
      managerTable_(managerTable),
      platform_(platform),
      aclStats_(HwFb303Stats(platform->getMultiSwitchStatsPrefix())),
      aclEntryMinimumPriority_(
          SaiApiTable::getInstance()->switchApi().getAttribute(
              managerTable_->switchManager().getSwitchSaiId(),
              SaiSwitchTraits::Attributes::AclEntryMinimumPriority())),
      aclEntryMaximumPriority_(
          SaiApiTable::getInstance()->switchApi().getAttribute(
              managerTable_->switchManager().getSwitchSaiId(),
              SaiSwitchTraits::Attributes::AclEntryMaximumPriority())),
      fdbDstUserMetaDataRangeMin_(getFdbDstUserMetaDataRange().min),
      fdbDstUserMetaDataRangeMax_(getFdbDstUserMetaDataRange().max),
      fdbDstUserMetaDataMask_(getMetaDataMask(fdbDstUserMetaDataRangeMax_)),
      routeDstUserMetaDataRangeMin_(getRouteDstUserMetaDataRange().min),
      routeDstUserMetaDataRangeMax_(getRouteDstUserMetaDataRange().max),
      routeDstUserMetaDataMask_(getMetaDataMask(routeDstUserMetaDataRangeMax_)),
      neighborDstUserMetaDataRangeMin_(getNeighborDstUserMetaDataRange().min),
      neighborDstUserMetaDataRangeMax_(getNeighborDstUserMetaDataRange().max),
      neighborDstUserMetaDataMask_(
          getMetaDataMask(neighborDstUserMetaDataRangeMax_)),
      hasTableGroups_(
          platform->getAsic()->isSupported(HwAsic::Feature::ACL_TABLE_GROUP)) {}

std::vector<sai_int32_t> SaiAclTableManager::getActionTypeList(
    const std::shared_ptr<AclTable>& addedAclTable) {
  /*
   * The current wedge agent code does the following.
   * 1. The sai code creates a default ACL table group and ACL table using
   * default qualifier and actiontype list to accommodate warmboot transition
   * from non multi acls to multi acls.
   * 2. The software switch state populates only the ACL entries and does not
   * populate the fields for ACL table group and ACL tables.
   * 3. As a consequence, the warmboot state does not contain the fields for ACL
   * table qualifiers and action type list.
   *
   * The following steps are done for when enable_acl_table_group flag is set.
   * 1. The Agent code populates default ACL table group and ACL table rather
   * than directly populating the ACLs.
   * 2. However, when the ACL table is being populated, software switch state
   * code is not aware of what action types and qualifiers are supported by the
   * current hardware.
   * 3. So ACL table is created with empty qualifier and action type list.
   * 4. When delta processing is hit for the ACL table, the newly added tables
   * will have empty lists for both qualifiers and actiontype list.
   * 5. To handle that case, we have a special check here where if the multi acl
   * flag is enabled and the qualifiers and actiontype list is empty, instead of
   * creating the new table with empty qualifiers and actiontypes, we populate
   * the default set of values so the ACLs can be created without issues.
   */

  auto aclActionTypes = addedAclTable->getActionTypes();

  if (FLAGS_enable_acl_table_group && aclActionTypes.size() != 0) {
    return cfgActionTypeListToSaiActionTypeList(aclActionTypes);
  } else {
    bool isTajo = platform_->getAsic()->getAsicVendor() ==
        HwAsic::AsicVendor::ASIC_VENDOR_TAJO;
    bool isJericho2 = platform_->getAsic()->getAsicType() ==
        cfg::AsicType::ASIC_TYPE_JERICHO2;
    bool isJericho3 = platform_->getAsic()->getAsicType() ==
        cfg::AsicType::ASIC_TYPE_JERICHO3;
    bool isJericho4 = platform_->getAsic()->getAsicType() ==
        cfg::AsicType::ASIC_TYPE_JERICHO4;
    bool isChenab = platform_->getAsic()->getAsicVendor() ==
        HwAsic::AsicVendor::ASIC_VENDOR_CHENAB;

    bool isQumran4d = platform_->getAsic()->getAsicType() ==
        cfg::AsicType::ASIC_TYPE_QUMRAN4D;

    std::vector<sai_int32_t> actionTypeList{
        SAI_ACL_ACTION_TYPE_PACKET_ACTION,
        SAI_ACL_ACTION_TYPE_COUNTER,
        SAI_ACL_ACTION_TYPE_SET_TC,
        SAI_ACL_ACTION_TYPE_SET_DSCP};

    // TODO (Q4D/J4/R4): Enable once SDK support is available
    if (!isQumran4d && !isJericho4) {
      actionTypeList.push_back(SAI_ACL_ACTION_TYPE_MIRROR_INGRESS);
    }

    if (!(isTajo || isJericho2 || isJericho3 || isJericho4 || isChenab ||
          isQumran4d)) {
      // Chenab supports egress mirror action in egress table
      actionTypeList.push_back(SAI_ACL_ACTION_TYPE_MIRROR_EGRESS);
    }
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::SAI_USER_DEFINED_TRAP) &&
        FLAGS_sai_user_defined_trap) {
      actionTypeList.push_back(SAI_ACL_ACTION_TYPE_SET_USER_TRAP_ID);
    }
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    if (platform_->getAsic()->isSupported(HwAsic::Feature::ARS)) {
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      if (platform_->getAsic()->isSupported(
              HwAsic::Feature::ARS_ALTERNATE_MEMBERS) ||
          isChenab) {
        actionTypeList.push_back(SAI_ACL_ACTION_TYPE_SET_ARS_OBJECT);
      }
#endif
      actionTypeList.push_back(SAI_ACL_ACTION_TYPE_DISABLE_ARS_FORWARDING);
    }
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::ACL_SET_ECMP_HASH_ALGORITHM)) {
      actionTypeList.push_back(SAI_ACL_ACTION_TYPE_SET_ECMP_HASH_ALGORITHM);
    }
#endif

#if defined(BRCM_SAI_SDK_GTE_13_0) && defined(BRCM_SAI_SDK_XGS)
    if (platform_->getAsic()->isSupported(
            HwAsic::Feature::ARS_ALTERNATE_MEMBERS) ||
        platform_->getAsic()->isSupported(HwAsic::Feature::VIRTUAL_ARS_GROUP)) {
      actionTypeList.push_back(SAI_ACL_ACTION_TYPE_L3_SWITCH_CANCEL);
    }
#endif

    if (FLAGS_enable_acl_table_redirect_action) {
      actionTypeList.push_back(SAI_ACL_ACTION_TYPE_REDIRECT);
    }
    return actionTypeList;
  }
}

std::set<cfg::AclTableQualifier> SaiAclTableManager::getQualifierSet(
    sai_acl_stage_t aclStage,
    const std::shared_ptr<AclTable>& addedAclTable) {
  auto aclQualifiers = addedAclTable->getQualifiers();
  /*
   * Please refer to the detailed comment under getActionTypeList() to
   * understand why we have the size check
   */
  if (FLAGS_enable_acl_table_group && aclQualifiers.size() != 0) {
    std::set<cfg::AclTableQualifier> qualifiers;
    for (const auto& qualifier : aclQualifiers) {
      qualifiers.insert(qualifier);
    }

    return qualifiers;
  } else {
    return getSupportedQualifierSet(aclStage);
  }
}

std::
    pair<SaiAclTableTraits::AdapterHostKey, SaiAclTableTraits::CreateAttributes>
    SaiAclTableManager::aclTableCreateAttributes(
        sai_acl_stage_t aclStage,
        const std::shared_ptr<AclTable>& addedAclTable,
        cfg::AclTableGroupBindPoint bindPoint) {
  std::vector<sai_int32_t> bindPointList{
      bindPoint == cfg::AclTableGroupBindPoint::PORT
          ? SAI_ACL_BIND_POINT_TYPE_PORT
          : SAI_ACL_BIND_POINT_TYPE_SWITCH};
  SaiAclTableTraits::Attributes::Stage tableStage = aclStage;

  auto actionTypeList = getActionTypeList(addedAclTable);

  auto qualifierSet = getQualifierSet(aclStage, addedAclTable);
  auto qualifierExistsFn = [=](cfg::AclTableQualifier qualifier) {
    auto exists = qualifierSet.find(qualifier) != qualifierSet.end();
    if (exists) {
      XLOG(DBG2) << "Qualifier "
                 << apache::thrift::util::enumNameSafe(qualifier)
                 << " exists in ACL table " << addedAclTable->getID();
    }
    return exists;
  };

  // Q4D/J4 DNX SDK Workaround: FIELD_IP_PROTOCOL and
  // FIELD_IPV6_NEXT_HEADER are overloaded onto the same underlying HW field on
  // J4/Q4D, so a table created with FIELD_IP_PROTOCOL=false /
  // FIELD_IPV6_NEXT_HEADER=true reads back FIELD_IP_PROTOCOL=true on a
  // get_acl_table_attribute, which breaks warmboot/rollback state
  // reconciliation. As agreed with Broadcom, whenever IPV6_NEXT_HEADER is used
  // we also set FIELD_IP_PROTOCOL=true so the created value matches the value
  // returned on GET.
  // TODO (Q4D/J4/R4): Remove once the SDK decouples the two fields.
  bool isQumran4dOrJericho4 = platform_->getAsic()->getAsicType() ==
          cfg::AsicType::ASIC_TYPE_QUMRAN4D ||
      platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_JERICHO4;
  bool fieldIpProtocol =
      qualifierExistsFn(cfg::AclTableQualifier::IP_PROTOCOL_NUMBER) ||
      (isQumran4dOrJericho4 &&
       qualifierExistsFn(cfg::AclTableQualifier::IPV6_NEXT_HEADER));
  std::optional<SaiAclTableTraits::Attributes::FieldPortUserMeta>
      fieldPortUserMeta;
  if (qualifierExistsFn(cfg::AclTableQualifier::LOOKUP_CLASS_PORT)) {
    fieldPortUserMeta = SaiAclTableTraits::Attributes::FieldPortUserMeta{true};
  }

  std::vector<std::optional<sai_object_id_t>> udfGroupIds(
      SaiAclTableManager::kMaxUdfGroups, std::nullopt);
  int i = 0;
  auto udfGroupSaiIds = managerTable_->udfManager().getUdfGroupIds(
      addedAclTable->getUdfGroups()->toThrift());
  for (const auto udfGroupSaiId : udfGroupSaiIds) {
    udfGroupIds[i++] = udfGroupSaiId;
  }

  SaiAclTableTraits::CreateAttributes attributes{
      tableStage,
      bindPointList,
      actionTypeList,
      qualifierExistsFn(cfg::AclTableQualifier::SRC_IPV6),
      qualifierExistsFn(cfg::AclTableQualifier::DST_IPV6),
      qualifierExistsFn(cfg::AclTableQualifier::DST_IPV6_WORD3),
      qualifierExistsFn(cfg::AclTableQualifier::DST_IPV6_WORD2),
      qualifierExistsFn(cfg::AclTableQualifier::SRC_IPV4),
      qualifierExistsFn(cfg::AclTableQualifier::DST_IPV4),
      qualifierExistsFn(cfg::AclTableQualifier::L4_SRC_PORT),
      qualifierExistsFn(cfg::AclTableQualifier::L4_DST_PORT),
      fieldIpProtocol,
      qualifierExistsFn(cfg::AclTableQualifier::TCP_FLAGS),
      qualifierExistsFn(cfg::AclTableQualifier::SRC_PORT),
      qualifierExistsFn(cfg::AclTableQualifier::OUT_PORT),
      qualifierExistsFn(cfg::AclTableQualifier::IP_FRAG),
      qualifierExistsFn(cfg::AclTableQualifier::ICMPV4_TYPE),
      qualifierExistsFn(cfg::AclTableQualifier::ICMPV4_CODE),
      qualifierExistsFn(cfg::AclTableQualifier::ICMPV6_TYPE),
      qualifierExistsFn(cfg::AclTableQualifier::ICMPV6_CODE),
      qualifierExistsFn(cfg::AclTableQualifier::DSCP),
      qualifierExistsFn(cfg::AclTableQualifier::TC),
      qualifierExistsFn(cfg::AclTableQualifier::DST_MAC),
      qualifierExistsFn(cfg::AclTableQualifier::IP_TYPE),
      qualifierExistsFn(cfg::AclTableQualifier::TTL),
      // Leave unset rather than false when unused: SDKs that do not
      // implement this qualifier reject the whole table create on an
      // unknown attribute, which would break every ACL table, not just
      // MPLS ones.
      (platform_->getAsic()->isSupported(
           HwAsic::Feature::SAI_ACL_MPLS_LABEL0_TTL) &&
       qualifierExistsFn(cfg::AclTableQualifier::MPLS_LABEL0_TTL))
          ? std::optional<
                SaiAclTableTraits::Attributes::FieldMplsLabel0Ttl>{true}
          : std::nullopt, // FieldMplsLabel0Ttl
      qualifierExistsFn(cfg::AclTableQualifier::LOOKUP_CLASS_L2),
      qualifierExistsFn(cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE),
      qualifierExistsFn(cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR),
      qualifierExistsFn(cfg::AclTableQualifier::ETHER_TYPE),
      qualifierExistsFn(cfg::AclTableQualifier::OUTER_VLAN),
      qualifierExistsFn(cfg::AclTableQualifier::L4_DST_PORT_RANGE)
          ? std::optional<
                SaiAclTableTraits::Attributes::FieldAclRangeType>{std::vector<
                sai_int32_t>{SAI_ACL_RANGE_TYPE_L4_DST_PORT_RANGE}}
          : std::nullopt, // FieldAclRangeType
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
      qualifierExistsFn(cfg::AclTableQualifier::BTH_OPCODE),
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
      qualifierExistsFn(cfg::AclTableQualifier::IPV6_NEXT_HEADER),
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
      udfGroupIds[0], // UserDefinedFieldGroupMin0
      udfGroupIds[1], // UserDefinedFieldGroupMin1
      udfGroupIds[2], // UserDefinedFieldGroupMin2
      udfGroupIds[3], // UserDefinedFieldGroupMin3
      udfGroupIds[4], // UserDefinedFieldGroupMin4
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      SaiAclTableTraits::Attributes::FieldRouteDestination::
              optionalExtensionAttributeId()
                  .has_value()
          ? std::optional<SaiAclTableTraits::Attributes::
                              FieldRouteDestination>{qualifierExistsFn(
                cfg::AclTableQualifier::NEXT_HOP_GROUP_ID)}
          : std::nullopt,
#endif
      fieldPortUserMeta,
  };

  SaiAclTableTraits::AdapterHostKey adapterHostKey{addedAclTable->getID()};

  return std::make_pair(adapterHostKey, attributes);
}

} // namespace facebook::fboss
