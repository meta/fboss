/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/AclApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/AclApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _AclTableMap{
    SAI_ATTR_MAP(AclTable, Stage),
    SAI_ATTR_MAP(AclTable, Stage),
    SAI_ATTR_MAP(AclTable, BindPointTypeList),
    SAI_ATTR_MAP(AclTable, ActionTypeList),
    SAI_ATTR_MAP(AclTable, EntryList),
    SAI_ATTR_MAP(AclTable, FieldSrcIpV6),
    SAI_ATTR_MAP(AclTable, FieldDstIpV6),
    SAI_ATTR_MAP(AclTable, FieldDstIpV6Word3),
    SAI_ATTR_MAP(AclTable, FieldDstIpV6Word2),
    SAI_ATTR_MAP(AclTable, FieldSrcIpV4),
    SAI_ATTR_MAP(AclTable, FieldDstIpV4),
    SAI_ATTR_MAP(AclTable, FieldL4SrcPort),
    SAI_ATTR_MAP(AclTable, FieldL4DstPort),
    SAI_ATTR_MAP(AclTable, FieldIpProtocol),
    SAI_ATTR_MAP(AclTable, FieldTcpFlags),
    SAI_ATTR_MAP(AclTable, FieldSrcPort),
    SAI_ATTR_MAP(AclTable, FieldOutPort),
    SAI_ATTR_MAP(AclTable, FieldIpFrag),
    SAI_ATTR_MAP(AclTable, FieldIcmpV4Type),
    SAI_ATTR_MAP(AclTable, FieldIcmpV4Code),
    SAI_ATTR_MAP(AclTable, FieldIcmpV6Type),
    SAI_ATTR_MAP(AclTable, FieldIcmpV6Code),
    SAI_ATTR_MAP(AclTable, FieldDscp),
    SAI_ATTR_MAP(AclTable, FieldTc),
    SAI_ATTR_MAP(AclTable, FieldDstMac),
    SAI_ATTR_MAP(AclTable, FieldIpType),
    SAI_ATTR_MAP(AclTable, FieldTtl),
    SAI_ATTR_MAP(AclTable, FieldMplsLabel0Ttl),
    SAI_ATTR_MAP(AclTable, FieldFdbDstUserMeta),
    SAI_ATTR_MAP(AclTable, FieldRouteDstUserMeta),
    SAI_ATTR_MAP(AclTable, FieldNeighborDstUserMeta),
    SAI_ATTR_MAP(AclTable, FieldPortUserMeta),
    SAI_ATTR_MAP(AclTable, AvailableEntry),
    SAI_ATTR_MAP(AclTable, AvailableCounter),
    SAI_ATTR_MAP(AclTable, FieldEthertype),
    SAI_ATTR_MAP(AclTable, FieldOuterVlanId),
    SAI_ATTR_MAP(AclTable, FieldAclRangeType),
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
    SAI_ATTR_MAP(AclTable, FieldBthOpcode),
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
    SAI_ATTR_MAP(AclTable, FieldIpv6NextHeader),
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
    SAI_ATTR_MAP(AclTable, UserDefinedFieldGroupMin0),
    SAI_ATTR_MAP(AclTable, UserDefinedFieldGroupMin1),
    SAI_ATTR_MAP(AclTable, UserDefinedFieldGroupMin2),
    SAI_ATTR_MAP(AclTable, UserDefinedFieldGroupMin3),
    SAI_ATTR_MAP(AclTable, UserDefinedFieldGroupMin4),
#endif
};

std::map<int32_t, std::pair<std::string, std::size_t>> _AclCounterMap{
    SAI_ATTR_MAP(AclCounter, TableId),
    SAI_ATTR_MAP(AclCounter, EnablePacketCount),
    SAI_ATTR_MAP(AclCounter, EnableByteCount),
    SAI_ATTR_MAP(AclCounter, CounterPackets),
    SAI_ATTR_MAP(AclCounter, CounterBytes),
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
    SAI_ATTR_MAP(AclCounter, Label),
#endif
};

std::map<int32_t, std::pair<std::string, std::size_t>> _AclTableGroupMap{
    SAI_ATTR_MAP(AclTableGroup, Stage),
    SAI_ATTR_MAP(AclTableGroup, BindPointTypeList),
    SAI_ATTR_MAP(AclTableGroup, Type),
    SAI_ATTR_MAP(AclTableGroup, MemberList),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _AclTableGroupMemberMap{
    SAI_ATTR_MAP(AclTableGroupMember, TableGroupId),
    SAI_ATTR_MAP(AclTableGroupMember, TableId),
    SAI_ATTR_MAP(AclTableGroupMember, Priority),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _AclRangeMap{
    SAI_ATTR_MAP(AclRange, Type),
    SAI_ATTR_MAP(AclRange, Limit),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _AclEntryMap{
    SAI_ATTR_MAP(AclEntry, TableId),
    SAI_ATTR_MAP(AclEntry, Priority),
    SAI_ATTR_MAP(AclEntry, Enabled),
    SAI_ATTR_MAP(AclEntry, FieldSrcIpV6),
    SAI_ATTR_MAP(AclEntry, FieldDstIpV6),
    SAI_ATTR_MAP(AclEntry, FieldDstIpV6Word3),
    SAI_ATTR_MAP(AclEntry, FieldDstIpV6Word2),
    SAI_ATTR_MAP(AclEntry, FieldSrcIpV4),
    SAI_ATTR_MAP(AclEntry, FieldDstIpV4),
    SAI_ATTR_MAP(AclEntry, FieldSrcPort),
    SAI_ATTR_MAP(AclEntry, FieldOutPort),
    SAI_ATTR_MAP(AclEntry, FieldL4SrcPort),
    SAI_ATTR_MAP(AclEntry, FieldL4DstPort),
    SAI_ATTR_MAP(AclEntry, FieldIpProtocol),
    SAI_ATTR_MAP(AclEntry, FieldTcpFlags),
    SAI_ATTR_MAP(AclEntry, FieldIpFrag),
    SAI_ATTR_MAP(AclEntry, FieldIcmpV4Type),
    SAI_ATTR_MAP(AclEntry, FieldIcmpV4Code),
    SAI_ATTR_MAP(AclEntry, FieldIcmpV6Type),
    SAI_ATTR_MAP(AclEntry, FieldIcmpV6Code),
    SAI_ATTR_MAP(AclEntry, FieldDscp),
    SAI_ATTR_MAP(AclEntry, FieldTc),
    SAI_ATTR_MAP(AclEntry, FieldDstMac),
    SAI_ATTR_MAP(AclEntry, FieldIpType),
    SAI_ATTR_MAP(AclEntry, FieldTtl),
    SAI_ATTR_MAP(AclEntry, FieldMplsLabel0Ttl),
    SAI_ATTR_MAP(AclEntry, FieldFdbDstUserMeta),
    SAI_ATTR_MAP(AclEntry, FieldRouteDstUserMeta),
    SAI_ATTR_MAP(AclEntry, FieldNeighborDstUserMeta),
    SAI_ATTR_MAP(AclEntry, FieldPortUserMeta),
    SAI_ATTR_MAP(AclEntry, FieldEthertype),
    SAI_ATTR_MAP(AclEntry, FieldOuterVlanId),
    SAI_ATTR_MAP(AclEntry, FieldAclRangeType),
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
    SAI_ATTR_MAP(AclEntry, FieldBthOpcode),
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
    SAI_ATTR_MAP(AclEntry, FieldIpv6NextHeader),
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
    SAI_ATTR_MAP(AclEntry, UserDefinedFieldGroupMin0),
    SAI_ATTR_MAP(AclEntry, UserDefinedFieldGroupMin1),
    SAI_ATTR_MAP(AclEntry, UserDefinedFieldGroupMin2),
    SAI_ATTR_MAP(AclEntry, UserDefinedFieldGroupMin3),
    SAI_ATTR_MAP(AclEntry, UserDefinedFieldGroupMin4),
#endif
    SAI_ATTR_MAP(AclEntry, ActionPacketAction),
    SAI_ATTR_MAP(AclEntry, ActionRedirect),
    SAI_ATTR_MAP(AclEntry, ActionCounter),
    SAI_ATTR_MAP(AclEntry, ActionSetTC),
    SAI_ATTR_MAP(AclEntry, ActionSetDSCP),
    SAI_ATTR_MAP(AclEntry, ActionMirrorIngress),
    SAI_ATTR_MAP(AclEntry, ActionMirrorEgress),
    SAI_ATTR_MAP(AclEntry, ActionMacsecFlow),
#if !defined(TAJO_SDK)
    SAI_ATTR_MAP(AclEntry, ActionSetUserTrap),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    SAI_ATTR_MAP(AclEntry, ActionSetArsObject),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    SAI_ATTR_MAP(AclEntry, ActionDisableArsForwarding),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    SAI_ATTR_MAP(AclEntry, ActionSetEcmpHashAlgorithm),
#endif
#if (defined(TAJO_SDK_GTE_26_5) && !defined(TAJO_SDK_P200)) || \
    defined(BRCM_SAI_SDK_XGS_GTE_15_4_1)
    SAI_ATTR_MAP(AclEntry, Label),
#endif
};

void handleExtensionAttributes() {
#if defined(BRCM_SAI_SDK_GTE_13_0) && defined(BRCM_SAI_SDK_XGS)
  SAI_EXT_ATTR_MAP(AclEntry, ActionL3SwitchCancel);
#endif
  SAI_EXT_ATTR_MAP(AclEntry, FieldRouteDestination);
  SAI_EXT_ATTR_MAP(AclTable, FieldRouteDestination);
}

} // namespace

namespace facebook::fboss {

WRAP_CREATE_FUNC(acl_counter, SAI_OBJECT_TYPE_ACL_COUNTER, acl);
WRAP_REMOVE_FUNC(acl_counter, SAI_OBJECT_TYPE_ACL_COUNTER, acl);
WRAP_SET_ATTR_FUNC(acl_counter, SAI_OBJECT_TYPE_ACL_COUNTER, acl);
WRAP_GET_ATTR_FUNC(acl_counter, SAI_OBJECT_TYPE_ACL_COUNTER, acl);

WRAP_CREATE_FUNC(acl_entry, SAI_OBJECT_TYPE_ACL_ENTRY, acl);
WRAP_REMOVE_FUNC(acl_entry, SAI_OBJECT_TYPE_ACL_ENTRY, acl);
WRAP_SET_ATTR_FUNC(acl_entry, SAI_OBJECT_TYPE_ACL_ENTRY, acl);
WRAP_GET_ATTR_FUNC(acl_entry, SAI_OBJECT_TYPE_ACL_ENTRY, acl);

WRAP_CREATE_FUNC(acl_table, SAI_OBJECT_TYPE_ACL_TABLE, acl);
WRAP_REMOVE_FUNC(acl_table, SAI_OBJECT_TYPE_ACL_TABLE, acl);
WRAP_SET_ATTR_FUNC(acl_table, SAI_OBJECT_TYPE_ACL_TABLE, acl);
WRAP_GET_ATTR_FUNC(acl_table, SAI_OBJECT_TYPE_ACL_TABLE, acl);

WRAP_CREATE_FUNC(acl_table_group, SAI_OBJECT_TYPE_ACL_TABLE_GROUP, acl);
WRAP_REMOVE_FUNC(acl_table_group, SAI_OBJECT_TYPE_ACL_TABLE_GROUP, acl);
WRAP_SET_ATTR_FUNC(acl_table_group, SAI_OBJECT_TYPE_ACL_TABLE_GROUP, acl);
WRAP_GET_ATTR_FUNC(acl_table_group, SAI_OBJECT_TYPE_ACL_TABLE_GROUP, acl);

WRAP_CREATE_FUNC(
    acl_table_group_member,
    SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER,
    acl);
WRAP_REMOVE_FUNC(
    acl_table_group_member,
    SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER,
    acl);
WRAP_SET_ATTR_FUNC(
    acl_table_group_member,
    SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER,
    acl);
WRAP_GET_ATTR_FUNC(
    acl_table_group_member,
    SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER,
    acl);

WRAP_CREATE_FUNC(acl_range, SAI_OBJECT_TYPE_ACL_RANGE, acl);
WRAP_REMOVE_FUNC(acl_range, SAI_OBJECT_TYPE_ACL_RANGE, acl);
WRAP_SET_ATTR_FUNC(acl_range, SAI_OBJECT_TYPE_ACL_RANGE, acl);
WRAP_GET_ATTR_FUNC(acl_range, SAI_OBJECT_TYPE_ACL_RANGE, acl);

sai_acl_api_t* wrappedAclApi() {
  static sai_acl_api_t aclWrappers;

  handleExtensionAttributes();
  aclWrappers.create_acl_table = &wrap_create_acl_table;
  aclWrappers.remove_acl_table = &wrap_remove_acl_table;
  aclWrappers.set_acl_table_attribute = &wrap_set_acl_table_attribute;
  aclWrappers.get_acl_table_attribute = &wrap_get_acl_table_attribute;
  aclWrappers.create_acl_entry = &wrap_create_acl_entry;
  aclWrappers.remove_acl_entry = &wrap_remove_acl_entry;
  aclWrappers.set_acl_entry_attribute = &wrap_set_acl_entry_attribute;
  aclWrappers.get_acl_entry_attribute = &wrap_get_acl_entry_attribute;
  aclWrappers.create_acl_counter = &wrap_create_acl_counter;
  aclWrappers.remove_acl_counter = &wrap_remove_acl_counter;
  aclWrappers.set_acl_counter_attribute = &wrap_set_acl_counter_attribute;
  aclWrappers.get_acl_counter_attribute = &wrap_get_acl_counter_attribute;
  aclWrappers.create_acl_range = &wrap_create_acl_range;
  aclWrappers.remove_acl_range = &wrap_remove_acl_range;
  aclWrappers.set_acl_range_attribute = &wrap_set_acl_range_attribute;
  aclWrappers.get_acl_range_attribute = &wrap_get_acl_range_attribute;
  aclWrappers.create_acl_table_group = &wrap_create_acl_table_group;
  aclWrappers.remove_acl_table_group = &wrap_remove_acl_table_group;
  aclWrappers.set_acl_table_group_attribute =
      &wrap_set_acl_table_group_attribute;
  aclWrappers.get_acl_table_group_attribute =
      &wrap_get_acl_table_group_attribute;
  aclWrappers.create_acl_table_group_member =
      &wrap_create_acl_table_group_member;
  aclWrappers.remove_acl_table_group_member =
      &wrap_remove_acl_table_group_member;
  aclWrappers.set_acl_table_group_member_attribute =
      &wrap_set_acl_table_group_member_attribute;
  aclWrappers.get_acl_table_group_member_attribute =
      &wrap_get_acl_table_group_member_attribute;

  return &aclWrappers;
}

SET_SAI_ATTRIBUTES_ACL_COUNTER(AclCounter)
SET_SAI_ATTRIBUTES(AclTable)
SET_SAI_ATTRIBUTES(AclEntry)
SET_SAI_ATTRIBUTES(AclTableGroup)
SET_SAI_ATTRIBUTES(AclTableGroupMember)
SET_SAI_ATTRIBUTES(AclRange)

} // namespace facebook::fboss
