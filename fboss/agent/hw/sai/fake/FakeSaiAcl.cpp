/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/fake/FakeSaiAcl.h"

#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

using facebook::fboss::FakeSai;

namespace facebook::fboss {
bool FakeAclTable::entryFieldSupported(const sai_attribute_t& attr) const {
  switch (attr.id) {
    case SAI_ACL_ENTRY_ATTR_PRIORITY:
      return true;
    case SAI_ACL_ENTRY_ATTR_ADMIN_STATE:
      return true;
    case SAI_ACL_ENTRY_ATTR_FIELD_SRC_IPV6:
      return fieldSrcIpV6;
    case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6:
      return fieldDstIpV6;
    case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6_WORD3:
      return fieldDstIpV6Word3;
    case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6_WORD2:
      return fieldDstIpV6Word2;
    case SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP:
      return fieldSrcIpV4;
    case SAI_ACL_ENTRY_ATTR_FIELD_DST_IP:
      return fieldDstIpV4;
    case SAI_ACL_ENTRY_ATTR_FIELD_IN_PORT:
    case SAI_ACL_ENTRY_ATTR_FIELD_SRC_PORT:
      return fieldSrcPort;
    case SAI_ACL_ENTRY_ATTR_FIELD_OUT_PORT:
      return fieldOutPort;
    case SAI_ACL_ENTRY_ATTR_FIELD_L4_SRC_PORT:
      return fieldL4SrcPort;
    case SAI_ACL_ENTRY_ATTR_FIELD_L4_DST_PORT:
      return fieldL4DstPort;
    case SAI_ACL_ENTRY_ATTR_FIELD_IP_PROTOCOL:
      return fieldIpProtocol;
    case SAI_ACL_ENTRY_ATTR_FIELD_TCP_FLAGS:
      return fieldTcpFlags;
    case SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_FRAG:
      return fieldIpFrag;
    case SAI_ACL_ENTRY_ATTR_FIELD_ICMP_TYPE:
      return fieldIcmpV4Type;
    case SAI_ACL_ENTRY_ATTR_FIELD_ICMP_CODE:
      return fieldIcmpV4Code;
    case SAI_ACL_ENTRY_ATTR_FIELD_ICMPV6_TYPE:
      return fieldIcmpV6Type;
    case SAI_ACL_ENTRY_ATTR_FIELD_ICMPV6_CODE:
      return fieldIcmpV6Code;
    case SAI_ACL_ENTRY_ATTR_FIELD_DSCP:
      return fieldDscp;
    case SAI_ACL_ENTRY_ATTR_FIELD_TC:
      return fieldTc;
    case SAI_ACL_ENTRY_ATTR_FIELD_DST_MAC:
      return fieldDstMac;
    case SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_TYPE:
      return fieldIpType;
    case SAI_ACL_ENTRY_ATTR_FIELD_TTL:
      return fieldTtl;
    case SAI_ACL_ENTRY_ATTR_FIELD_MPLS_LABEL0_TTL:
      return fieldMplsLabel0Ttl;
    case SAI_ACL_ENTRY_ATTR_FIELD_FDB_DST_USER_META:
      return fieldFdbDstUserMeta;
    case SAI_ACL_ENTRY_ATTR_FIELD_ROUTE_DST_USER_META:
      return fieldRouteDstUserMeta;
    case SAI_ACL_ENTRY_ATTR_FIELD_NEIGHBOR_DST_USER_META:
      return fieldNeighborDstUserMeta;
    case SAI_ACL_ENTRY_ATTR_FIELD_PORT_USER_META:
      return fieldPortUserMeta;
    case SAI_ACL_ENTRY_ATTR_FIELD_ETHER_TYPE:
      return fieldEthertype;
    case SAI_ACL_ENTRY_ATTR_FIELD_OUTER_VLAN_ID:
      return fieldOuterVlanId;
    case SAI_ACL_ENTRY_ATTR_FIELD_ACL_RANGE_TYPE:
      return !fieldAclRangeType.empty();
    case SAI_ACL_ENTRY_ATTR_FIELD_BTH_OPCODE:
      return fieldBthOpcode;
    case SAI_ACL_ENTRY_ATTR_FIELD_IPV6_NEXT_HEADER:
      return fieldIpv6NextHeader;
    case SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN:
    case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 1):
    case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 2):
    case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 3):
    case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 4):
      return true;
    // Actions
    case SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION:
    case SAI_ACL_ENTRY_ATTR_ACTION_REDIRECT:
    case SAI_ACL_ENTRY_ATTR_ACTION_COUNTER:
    case SAI_ACL_ENTRY_ATTR_ACTION_SET_TC:
    case SAI_ACL_ENTRY_ATTR_ACTION_SET_DSCP:
    case SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_INGRESS:
    case SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_EGRESS:
    case SAI_ACL_ENTRY_ATTR_ACTION_MACSEC_FLOW:
    case SAI_ACL_ENTRY_ATTR_ACTION_SET_USER_TRAP_ID:
    case SAI_ACL_ENTRY_ATTR_ACTION_SET_ARS_OBJECT:
    case SAI_ACL_ENTRY_ATTR_ACTION_DISABLE_ARS_FORWARDING:
    case SAI_ACL_ENTRY_ATTR_ACTION_SET_ECMP_HASH_ALGORITHM:
      return true;
    case SAI_ACL_ENTRY_ATTR_ACTION_L3_SWITCH_CANCEL:
      return true;
    case SAI_ACL_ENTRY_ATTR_FIELD_ROUTE_DST:
      return true;
    default:
      return false;
  }
}
} // namespace facebook::fboss

sai_status_t create_acl_table_fn(
    sai_object_id_t* acl_table_id,
    sai_object_id_t /*switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();

  std::optional<sai_int32_t> stage;
  std::vector<int32_t> bindPointTypeList;
  std::vector<int32_t> actionTypeList;
  bool fieldSrcIpV6 = 0;
  bool fieldDstIpV6 = 0;
  bool fieldDstIpV6Word3 = 0;
  bool fieldDstIpV6Word2 = 0;
  bool fieldSrcIpV4 = 0;
  bool fieldDstIpV4 = 0;
  bool fieldL4SrcPort = 0;
  bool fieldL4DstPort = 0;
  bool fieldIpProtocol = 0;
  bool fieldTcpFlags = 0;
  bool fieldSrcPort = 0;
  bool fieldOutPort = 0;
  bool fieldIpFrag = 0;
  bool fieldIcmpV4Type = 0;
  bool fieldIcmpV4Code = 0;
  bool fieldIcmpV6Type = 0;
  bool fieldIcmpV6Code = 0;
  bool fieldDscp = 0;
  bool fieldTc = 0;
  bool fieldDstMac = 0;
  bool fieldIpType = 0;
  bool fieldTtl = 0;
  bool fieldMplsLabel0Ttl = 0;
  bool fieldFdbDstUserMeta = 0;
  bool fieldRouteDstUserMeta = 0;
  bool fieldNeighborDstUserMeta = 0;
  bool fieldPortUserMeta = 0;
  bool fieldEthertype = 0;
  bool fieldOuterVlanId = 0;
  std::vector<sai_int32_t> fieldAclRangeType;
  bool fieldBthOpcode = 0;
  bool fieldIpv6NextHeader = 0;
  sai_object_id_t userDefinedFieldGroupMin = SAI_NULL_OBJECT_ID;
  sai_object_id_t userDefinedFieldGroupMin1 = SAI_NULL_OBJECT_ID;
  sai_object_id_t userDefinedFieldGroupMin2 = SAI_NULL_OBJECT_ID;
  sai_object_id_t userDefinedFieldGroupMin3 = SAI_NULL_OBJECT_ID;
  sai_object_id_t userDefinedFieldGroupMin4 = SAI_NULL_OBJECT_ID;
  bool fieldRouteDestination = false;

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_TABLE_ATTR_ACL_STAGE:
        stage = attr_list[i].value.s32;
        break;
      case SAI_ACL_TABLE_ATTR_ACL_BIND_POINT_TYPE_LIST:
        for (int j = 0; j < attr_list[i].value.s32list.count; ++j) {
          bindPointTypeList.push_back(attr_list[i].value.s32list.list[j]);
        }
        break;
      case SAI_ACL_TABLE_ATTR_ACL_ACTION_TYPE_LIST:
        for (int j = 0; j < attr_list[i].value.s32list.count; ++j) {
          actionTypeList.push_back(attr_list[i].value.s32list.list[j]);
        }
        break;

      case SAI_ACL_TABLE_ATTR_FIELD_SRC_IPV6:
        fieldSrcIpV6 = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6:
        fieldDstIpV6 = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6_WORD3:
        fieldDstIpV6Word3 = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6_WORD2:
        fieldDstIpV6Word2 = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_SRC_IP:
        fieldSrcIpV4 = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_DST_IP:
        fieldDstIpV4 = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_L4_SRC_PORT:
        fieldL4SrcPort = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_L4_DST_PORT:
        fieldL4DstPort = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_IP_PROTOCOL:
        fieldIpProtocol = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_TCP_FLAGS:
        fieldTcpFlags = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_SRC_PORT:
      case SAI_ACL_TABLE_ATTR_FIELD_IN_PORT:
        fieldSrcPort = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_OUT_PORT:
        fieldOutPort = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_FRAG:
        fieldIpFrag = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_ICMP_TYPE:
        fieldIcmpV4Type = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_ICMP_CODE:
        fieldIcmpV4Code = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_ICMPV6_TYPE:
        fieldIcmpV6Type = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_ICMPV6_CODE:
        fieldIcmpV6Code = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_DSCP:
        fieldDscp = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_TC:
        fieldTc = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_ROUTE_DST:
        fieldRouteDestination = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_DST_MAC:
        fieldDstMac = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_TYPE:
        fieldIpType = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_TTL:
        fieldTtl = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_MPLS_LABEL0_TTL:
        fieldMplsLabel0Ttl = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_FDB_DST_USER_META:
        fieldFdbDstUserMeta = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_ROUTE_DST_USER_META:
        fieldRouteDstUserMeta = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_NEIGHBOR_DST_USER_META:
        fieldNeighborDstUserMeta = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_PORT_USER_META:
        fieldPortUserMeta = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_ETHER_TYPE:
        fieldEthertype = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_OUTER_VLAN_ID:
        fieldOuterVlanId = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_ACL_RANGE_TYPE:
        fieldAclRangeType.assign(
            attr_list[i].value.s32list.list,
            attr_list[i].value.s32list.list + attr_list[i].value.s32list.count);
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_BTH_OPCODE:
        fieldBthOpcode = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_FIELD_IPV6_NEXT_HEADER:
        fieldIpv6NextHeader = attr_list[i].value.booldata;
        break;
      case SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN:
        userDefinedFieldGroupMin = attr_list[i].value.oid;
        break;
      case (SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 1):
        userDefinedFieldGroupMin1 = attr_list[i].value.oid;
        break;
      case (SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 2):
        userDefinedFieldGroupMin2 = attr_list[i].value.oid;
        break;
      case (SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 3):
        userDefinedFieldGroupMin3 = attr_list[i].value.oid;
        break;
      case (SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 4):
        userDefinedFieldGroupMin4 = attr_list[i].value.oid;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }

  if (!stage) {
    return SAI_STATUS_INVALID_PARAMETER;
  }

  *acl_table_id = fs->aclTableManager.create(
      stage.value(),
      bindPointTypeList,
      actionTypeList,
      fieldSrcIpV6,
      fieldDstIpV6,
      fieldDstIpV6Word3,
      fieldDstIpV6Word2,
      fieldSrcIpV4,
      fieldDstIpV4,
      fieldL4SrcPort,
      fieldL4DstPort,
      fieldIpProtocol,
      fieldTcpFlags,
      fieldSrcPort,
      fieldOutPort,
      fieldIpFrag,
      fieldIcmpV4Type,
      fieldIcmpV4Code,
      fieldIcmpV6Type,
      fieldIcmpV6Code,
      fieldDscp,
      fieldTc,
      fieldDstMac,
      fieldIpType,
      fieldTtl,
      fieldMplsLabel0Ttl,
      fieldFdbDstUserMeta,
      fieldRouteDstUserMeta,
      fieldNeighborDstUserMeta,
      fieldPortUserMeta,
      fieldEthertype,
      fieldOuterVlanId,
      fieldAclRangeType,
      fieldBthOpcode,
      fieldIpv6NextHeader,
      userDefinedFieldGroupMin,
      userDefinedFieldGroupMin1,
      userDefinedFieldGroupMin2,
      userDefinedFieldGroupMin3,
      userDefinedFieldGroupMin4);

  fs->aclTableManager.get(*acl_table_id).fieldRouteDestination =
      fieldRouteDestination;

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_acl_table_fn(sai_object_id_t acl_table_id) {
  auto fs = FakeSai::getInstance();
  fs->aclTableManager.remove(acl_table_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_acl_table_attribute_fn(
    sai_object_id_t /*acl_table_id*/,
    const sai_attribute_t* attr) {
  switch (attr->id) {
    default:
      // SAI spec does not support setting any attribute for ACL table post
      // creation.
      return SAI_STATUS_NOT_SUPPORTED;
  }

  return SAI_STATUS_SUCCESS;
}

sai_status_t get_acl_table_attribute_fn(
    sai_object_id_t acl_table_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_ACL_TABLE_ATTR_ACL_STAGE: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.s32 = aclTable.stage;
      } break;
      case SAI_ACL_TABLE_ATTR_ACL_BIND_POINT_TYPE_LIST: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        if (aclTable.bindPointTypeList.size() > attr[i].value.s32list.count) {
          attr[i].value.s32list.count = aclTable.bindPointTypeList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }

        attr[i].value.s32list.count = aclTable.bindPointTypeList.size();
        int j = 0;
        for (const auto& bindPointType : aclTable.bindPointTypeList) {
          attr[i].value.s32list.list[j++] = bindPointType;
        }
      } break;
      case SAI_ACL_TABLE_ATTR_ACL_ACTION_TYPE_LIST: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        if (aclTable.actionTypeList.size() > attr[i].value.s32list.count) {
          attr[i].value.s32list.count = aclTable.actionTypeList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        attr[i].value.s32list.count = aclTable.actionTypeList.size();
        int j = 0;
        for (const auto& actionType : aclTable.actionTypeList) {
          attr[i].value.s32list.list[j++] = actionType;
        }
      } break;
      case SAI_ACL_TABLE_ATTR_ENTRY_LIST: {
        int cnt = 0;
        for (const auto& [oid, aclEntry] : fs->aclEntryManager.map()) {
          std::ignore = oid;
          if (aclEntry.tableId == acl_table_id) {
            cnt++;
          }
        }

        if (cnt > attr[i].value.objlist.count) {
          attr[i].value.objlist.count = cnt;
          return SAI_STATUS_BUFFER_OVERFLOW;
        }

        int j = 0;
        for (const auto& [oid, aclEntry] : fs->aclEntryManager.map()) {
          if (aclEntry.tableId == acl_table_id) {
            attr[i].value.objlist.list[j++] = oid;
          }
        }
        attr[i].value.objlist.count = j;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_SRC_IPV6: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldSrcIpV6;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldDstIpV6;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6_WORD3: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldDstIpV6Word3;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6_WORD2: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldDstIpV6Word2;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_SRC_IP: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldSrcIpV4;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_DST_IP: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldDstIpV4;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_L4_SRC_PORT: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldL4SrcPort;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_L4_DST_PORT: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldL4DstPort;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_IP_PROTOCOL: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldIpProtocol;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_TCP_FLAGS: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldTcpFlags;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_IN_PORT:
      case SAI_ACL_TABLE_ATTR_FIELD_SRC_PORT: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldSrcPort;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_OUT_PORT: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldOutPort;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_FRAG: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldIpFrag;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_ICMP_TYPE: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldIcmpV4Type;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_ICMP_CODE: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldIcmpV4Code;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_ICMPV6_TYPE: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldIcmpV6Type;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_ICMPV6_CODE: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldIcmpV6Code;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_DSCP: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldDscp;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_TC: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldTc;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_ROUTE_DST: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldRouteDestination;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_DST_MAC: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldDstMac;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_TYPE: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldIpType;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_TTL: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldTtl;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_MPLS_LABEL0_TTL: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldMplsLabel0Ttl;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_FDB_DST_USER_META: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldFdbDstUserMeta;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_ROUTE_DST_USER_META: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldRouteDstUserMeta;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_NEIGHBOR_DST_USER_META: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldNeighborDstUserMeta;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_PORT_USER_META: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldPortUserMeta;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_ETHER_TYPE: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldEthertype;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_OUTER_VLAN_ID: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldOuterVlanId;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_ACL_RANGE_TYPE: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        if (aclTable.fieldAclRangeType.size() > attr[i].value.s32list.count) {
          attr[i].value.s32list.count = aclTable.fieldAclRangeType.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        attr[i].value.s32list.count = aclTable.fieldAclRangeType.size();
        int j = 0;
        for (const auto& rangeType : aclTable.fieldAclRangeType) {
          attr[i].value.s32list.list[j++] = rangeType;
        }
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_BTH_OPCODE: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldBthOpcode;
      } break;
      case SAI_ACL_TABLE_ATTR_FIELD_IPV6_NEXT_HEADER: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.booldata = aclTable.fieldIpv6NextHeader;
      } break;
      case SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN: {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.oid = aclTable.userDefinedFieldGroupMin;
      } break;
      case (SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 1): {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.oid = aclTable.userDefinedFieldGroupMin1;
      } break;
      case (SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 2): {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.oid = aclTable.userDefinedFieldGroupMin2;
      } break;
      case (SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 3): {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.oid = aclTable.userDefinedFieldGroupMin3;
      } break;
      case (SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 4): {
        const auto& aclTable = fs->aclTableManager.get(acl_table_id);
        attr[i].value.oid = aclTable.userDefinedFieldGroupMin4;
      } break;
      case SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_ENTRY:
      case SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_COUNTER:
        attr[i].value.u32 = 1000;
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }

  return SAI_STATUS_SUCCESS;
}

void acl_entry_copy_u8list_from_attr(
    std::vector<sai_uint8_t>& data,
    std::vector<sai_uint8_t>& mask,
    const sai_attribute_t* attr) {
  data.resize(attr->value.aclfield.data.u8list.count);
  std::copy(
      attr->value.aclfield.data.u8list.list,
      attr->value.aclfield.data.u8list.list +
          attr->value.aclfield.data.u8list.count,
      std::begin(data));
  mask.resize(attr->value.aclfield.mask.u8list.count);
  std::copy(
      attr->value.aclfield.mask.u8list.list,
      attr->value.aclfield.mask.u8list.list +
          attr->value.aclfield.mask.u8list.count,
      std::begin(mask));
  return;
}

sai_status_t set_acl_entry_attribute_fn(
    sai_object_id_t acl_entry_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& aclEntry = fs->aclEntryManager.get(acl_entry_id);
  sai_status_t res;
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }

  const auto& aclTable = fs->aclTableManager.get(aclEntry.tableId);
  if (!aclTable.entryFieldSupported(*attr)) {
    return SAI_STATUS_NOT_SUPPORTED;
  }
  switch (attr->id) {
    case SAI_ACL_ENTRY_ATTR_PRIORITY:
      aclEntry.priority = attr->value.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ADMIN_STATE:
      aclEntry.enabled = attr->value.booldata;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_SRC_IPV6:
      aclEntry.fieldSrcIpV6Enable = attr->value.aclfield.enable;
      aclEntry.fieldSrcIpV6Data =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.data.ip6);
      aclEntry.fieldSrcIpV6Mask =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.mask.ip6);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6:
      aclEntry.fieldDstIpV6Enable = attr->value.aclfield.enable;
      aclEntry.fieldDstIpV6Data =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.data.ip6);
      aclEntry.fieldDstIpV6Mask =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.mask.ip6);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6_WORD3:
      aclEntry.fieldDstIpV6Word3Enable = attr->value.aclfield.enable;
      aclEntry.fieldDstIpV6Word3Data =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.data.ip6);
      aclEntry.fieldDstIpV6Word3Mask =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.mask.ip6);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6_WORD2:
      aclEntry.fieldDstIpV6Word2Enable = attr->value.aclfield.enable;
      aclEntry.fieldDstIpV6Word2Data =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.data.ip6);
      aclEntry.fieldDstIpV6Word2Mask =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.mask.ip6);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP:
      aclEntry.fieldSrcIpV4Enable = attr->value.aclfield.enable;
      aclEntry.fieldSrcIpV4Data =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.data.ip4);
      aclEntry.fieldSrcIpV4Mask =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.mask.ip4);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_DST_IP:
      aclEntry.fieldDstIpV4Enable = attr->value.aclfield.enable;
      aclEntry.fieldDstIpV4Data =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.data.ip4);
      aclEntry.fieldDstIpV4Mask =
          facebook::fboss::fromSaiIpAddress(attr->value.aclfield.mask.ip4);
      res = SAI_STATUS_SUCCESS;
      break;

    /*
     * Mask is not needed for SAI_ACL_ENTRY_ATTR_FIELD_IN_PORT
     * or SAI_ACL_ENTRY_ATTR_FIELD_OUT_PORT.
     * Thus, there is no oid field in sai_acl_field_data_mask_t.
     * oid is u64, but unfortunately, u64 was not added to
     * sai_acl_field_data_mask_t till SAI 1.6, so use u32.
     * This will be ignored by the implementation anyway.
     */
    case SAI_ACL_ENTRY_ATTR_FIELD_IN_PORT:
    case SAI_ACL_ENTRY_ATTR_FIELD_SRC_PORT:
      aclEntry.fieldSrcPortEnable = attr->value.aclfield.enable;
      aclEntry.fieldSrcPortData = attr->value.aclfield.data.oid;
      aclEntry.fieldSrcPortMask = attr->value.aclfield.mask.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_OUT_PORT:
      aclEntry.fieldOutPortEnable = attr->value.aclfield.enable;
      aclEntry.fieldOutPortData = attr->value.aclfield.data.oid;
      aclEntry.fieldOutPortMask = attr->value.aclfield.mask.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_L4_SRC_PORT:
      aclEntry.fieldL4SrcPortEnable = attr->value.aclfield.enable;
      aclEntry.fieldL4SrcPortData = attr->value.aclfield.data.u16;
      aclEntry.fieldL4SrcPortMask = attr->value.aclfield.mask.u16;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_L4_DST_PORT:
      aclEntry.fieldL4DstPortEnable = attr->value.aclfield.enable;
      aclEntry.fieldL4DstPortData = attr->value.aclfield.data.u16;
      aclEntry.fieldL4DstPortMask = attr->value.aclfield.mask.u16;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_IP_PROTOCOL:
      aclEntry.fieldIpProtocolEnable = attr->value.aclfield.enable;
      aclEntry.fieldIpProtocolData = attr->value.aclfield.data.u8;
      aclEntry.fieldIpProtocolMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_TCP_FLAGS:
      aclEntry.fieldTcpFlagsEnable = attr->value.aclfield.enable;
      aclEntry.fieldTcpFlagsData = attr->value.aclfield.data.u8;
      aclEntry.fieldTcpFlagsMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_FRAG:
      aclEntry.fieldIpFragEnable = attr->value.aclfield.enable;
      aclEntry.fieldIpFragData = attr->value.aclfield.data.u32;
      aclEntry.fieldIpFragMask = attr->value.aclfield.mask.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_ICMP_TYPE:
      aclEntry.fieldIcmpV4TypeEnable = attr->value.aclfield.enable;
      aclEntry.fieldIcmpV4TypeData = attr->value.aclfield.data.u8;
      aclEntry.fieldIcmpV4TypeMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_ICMP_CODE:
      aclEntry.fieldIcmpV4CodeEnable = attr->value.aclfield.enable;
      aclEntry.fieldIcmpV4CodeData = attr->value.aclfield.data.u8;
      aclEntry.fieldIcmpV4CodeMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_ICMPV6_TYPE:
      aclEntry.fieldIcmpV6TypeEnable = attr->value.aclfield.enable;
      aclEntry.fieldIcmpV6TypeData = attr->value.aclfield.data.u8;
      aclEntry.fieldIcmpV6TypeMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_ICMPV6_CODE:
      aclEntry.fieldIcmpV6CodeEnable = attr->value.aclfield.enable;
      aclEntry.fieldIcmpV6CodeData = attr->value.aclfield.data.u8;
      aclEntry.fieldIcmpV6CodeMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_DSCP:
      aclEntry.fieldDscpEnable = attr->value.aclfield.enable;
      aclEntry.fieldDscpData = attr->value.aclfield.data.u8;
      aclEntry.fieldDscpMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_TC:
      aclEntry.fieldTcEnable = attr->value.aclfield.enable;
      aclEntry.fieldTcData = attr->value.aclfield.data.u8;
      aclEntry.fieldTcMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_DST_MAC:
      aclEntry.fieldDstMacEnable = attr->value.aclfield.enable;
      aclEntry.fieldDstMacData =
          facebook::fboss::fromSaiMacAddress(attr->value.aclfield.data.mac);
      aclEntry.fieldDstMacMask =
          facebook::fboss::fromSaiMacAddress(attr->value.aclfield.mask.mac);
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_TYPE:
      aclEntry.fieldIpTypeEnable = attr->value.aclfield.enable;
      aclEntry.fieldIpTypeData = attr->value.aclfield.data.u32;
      aclEntry.fieldIpTypeMask = attr->value.aclfield.mask.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_TTL:
      aclEntry.fieldTtlEnable = attr->value.aclfield.enable;
      aclEntry.fieldTtlData = attr->value.aclfield.data.u8;
      aclEntry.fieldTtlMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_MPLS_LABEL0_TTL:
      aclEntry.fieldMplsLabel0TtlEnable = attr->value.aclfield.enable;
      aclEntry.fieldMplsLabel0TtlData = attr->value.aclfield.data.u8;
      aclEntry.fieldMplsLabel0TtlMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_FDB_DST_USER_META:
      aclEntry.fieldFdbDstUserMetaEnable = attr->value.aclfield.enable;
      aclEntry.fieldFdbDstUserMetaData = attr->value.aclfield.data.u32;
      aclEntry.fieldFdbDstUserMetaMask = attr->value.aclfield.mask.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_ROUTE_DST_USER_META:
      aclEntry.fieldRouteDstUserMetaEnable = attr->value.aclfield.enable;
      aclEntry.fieldRouteDstUserMetaData = attr->value.aclfield.data.u32;
      aclEntry.fieldRouteDstUserMetaMask = attr->value.aclfield.mask.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_NEIGHBOR_DST_USER_META:
      aclEntry.fieldNeighborDstUserMetaEnable = attr->value.aclfield.enable;
      aclEntry.fieldNeighborDstUserMetaData = attr->value.aclfield.data.u32;
      aclEntry.fieldNeighborDstUserMetaMask = attr->value.aclfield.mask.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_PORT_USER_META:
      aclEntry.fieldPortUserMetaEnable = attr->value.aclfield.enable;
      aclEntry.fieldPortUserMetaData = attr->value.aclfield.data.u32;
      aclEntry.fieldPortUserMetaMask = attr->value.aclfield.mask.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_ETHER_TYPE:
      aclEntry.fieldEtherTypeEnable = attr->value.aclfield.enable;
      aclEntry.fieldEtherTypeData = attr->value.aclfield.data.u16;
      aclEntry.fieldEtherTypeMask = attr->value.aclfield.mask.u16;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_OUTER_VLAN_ID:
      aclEntry.fieldOuterVlanIdEnable = attr->value.aclfield.enable;
      aclEntry.fieldOuterVlanIdData = attr->value.aclfield.data.u16;
      aclEntry.fieldOuterVlanIdMask = attr->value.aclfield.mask.u16;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_ACL_RANGE_TYPE:
      aclEntry.fieldAclRangeTypeEnable = attr->value.aclfield.enable;
      aclEntry.fieldAclRangeTypeData.resize(
          attr->value.aclfield.data.objlist.count);
      std::copy(
          attr->value.aclfield.data.objlist.list,
          attr->value.aclfield.data.objlist.list +
              attr->value.aclfield.data.objlist.count,
          std::begin(aclEntry.fieldAclRangeTypeData));
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_BTH_OPCODE:
      aclEntry.fieldBthOpcodeEnable = attr->value.aclfield.enable;
      aclEntry.fieldBthOpcodeData = attr->value.aclfield.data.u8;
      aclEntry.fieldBthOpcodeMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_IPV6_NEXT_HEADER:
      aclEntry.fieldIpv6NextHeaderEnable = attr->value.aclfield.enable;
      aclEntry.fieldIpv6NextHeaderData = attr->value.aclfield.data.u8;
      aclEntry.fieldIpv6NextHeaderMask = attr->value.aclfield.mask.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN:
      aclEntry.userDefinedFieldGroupMinEnable = attr->value.aclfield.enable;
      acl_entry_copy_u8list_from_attr(
          aclEntry.userDefinedFieldGroupMinData,
          aclEntry.userDefinedFieldGroupMinMask,
          attr);
      res = SAI_STATUS_SUCCESS;
      break;
    case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 1):
      aclEntry.userDefinedFieldGroupMin1Enable = attr->value.aclfield.enable;
      acl_entry_copy_u8list_from_attr(
          aclEntry.userDefinedFieldGroupMin1Data,
          aclEntry.userDefinedFieldGroupMin1Mask,
          attr);
      res = SAI_STATUS_SUCCESS;
      break;
    case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 2):
      aclEntry.userDefinedFieldGroupMin2Enable = attr->value.aclfield.enable;
      acl_entry_copy_u8list_from_attr(
          aclEntry.userDefinedFieldGroupMin2Data,
          aclEntry.userDefinedFieldGroupMin2Mask,
          attr);
      res = SAI_STATUS_SUCCESS;
      break;
    case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 3):
      aclEntry.userDefinedFieldGroupMin3Enable = attr->value.aclfield.enable;
      acl_entry_copy_u8list_from_attr(
          aclEntry.userDefinedFieldGroupMin3Data,
          aclEntry.userDefinedFieldGroupMin3Mask,
          attr);
      res = SAI_STATUS_SUCCESS;
      break;
    case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 4):
      aclEntry.userDefinedFieldGroupMin4Enable = attr->value.aclfield.enable;
      acl_entry_copy_u8list_from_attr(
          aclEntry.userDefinedFieldGroupMin4Data,
          aclEntry.userDefinedFieldGroupMin4Mask,
          attr);
      res = SAI_STATUS_SUCCESS;
      break;

    case SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION:
      aclEntry.actionPacketActionEnable = attr->value.aclaction.enable;
      aclEntry.actionPacketActionData = attr->value.aclaction.parameter.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_REDIRECT:
      aclEntry.actionRedirectEnable = attr->value.aclaction.enable;
      aclEntry.actionRedirectData = attr->value.aclaction.parameter.oid;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_COUNTER:
      aclEntry.actionCounterEnable = attr->value.aclaction.enable;
      aclEntry.actionCounterData = attr->value.aclaction.parameter.oid;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_SET_TC:
      aclEntry.actionSetTCEnable = attr->value.aclaction.enable;
      aclEntry.actionSetTCData = attr->value.aclaction.parameter.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_SET_DSCP:
      aclEntry.actionSetDSCPEnable = attr->value.aclaction.enable;
      aclEntry.actionSetDSCPData = attr->value.aclaction.parameter.u8;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_INGRESS:
      aclEntry.actionMirrorIngressEnable = attr->value.aclaction.enable;
      aclEntry.actionMirrorIngressData.resize(
          attr->value.aclaction.parameter.objlist.count);
      std::copy(
          attr->value.aclaction.parameter.objlist.list,
          attr->value.aclaction.parameter.objlist.list +
              attr->value.aclaction.parameter.objlist.count,
          std::begin(aclEntry.actionMirrorIngressData));
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_EGRESS:
      aclEntry.actionMirrorEgressEnable = attr->value.aclaction.enable;
      aclEntry.actionMirrorEgressData.resize(
          attr->value.aclaction.parameter.objlist.count);
      std::copy(
          attr->value.aclaction.parameter.objlist.list,
          attr->value.aclaction.parameter.objlist.list +
              attr->value.aclaction.parameter.objlist.count,
          std::begin(aclEntry.actionMirrorEgressData));
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_MACSEC_FLOW:
      aclEntry.actionMacsecFlowEnable = attr->value.aclaction.enable;
      aclEntry.actionMacsecFlowData = attr->value.aclaction.parameter.oid;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_SET_USER_TRAP_ID:
      aclEntry.actionSetUserTrapEnable = attr->value.aclaction.enable;
      aclEntry.actionSetUserTrapData = attr->value.aclaction.parameter.oid;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_SET_ARS_OBJECT:
      aclEntry.actionSetArsObjectEnable = attr->value.aclaction.enable;
      aclEntry.actionSetArsObjectData = attr->value.aclaction.parameter.oid;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_DISABLE_ARS_FORWARDING:
      aclEntry.actionDisableArsForwarding =
          attr->value.aclaction.parameter.booldata;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_SET_ECMP_HASH_ALGORITHM:
      aclEntry.actionSetEcmpHashAlgorithmEnable = attr->value.aclaction.enable;
      aclEntry.actionSetEcmpHashAlgorithmData =
          attr->value.aclaction.parameter.oid;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_ACTION_L3_SWITCH_CANCEL:
      aclEntry.actionL3SwitchCancelEnable = attr->value.aclaction.enable;
      aclEntry.actionL3SwitchCancelData =
          attr->value.aclaction.parameter.booldata;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_ENTRY_ATTR_FIELD_ROUTE_DST:
      aclEntry.fieldRouteDestinationEnable = attr->value.aclfield.enable;
      aclEntry.fieldRouteDestinationData = attr->value.aclfield.data.oid;
      aclEntry.fieldRouteDestinationMask = attr->value.aclfield.mask.u32;
      res = SAI_STATUS_SUCCESS;
      break;
    default:
      res = SAI_STATUS_NOT_SUPPORTED;
      break;
  }
  return res;
}

sai_status_t acl_entry_copy_attr_to_u8list(
    std::vector<sai_uint8_t>& data,
    std::vector<sai_uint8_t>& mask,
    sai_acl_field_data_t* aclfield) {
  if (aclfield->data.u8list.count < data.size() ||
      aclfield->mask.u8list.count < mask.size()) {
    aclfield->data.u8list.count = data.size();
    aclfield->mask.u8list.count = mask.size();
    return SAI_STATUS_BUFFER_OVERFLOW;
  }
  aclfield->enable = true;
  aclfield->data.u8list.count = data.size();
  aclfield->mask.u8list.count = mask.size();
  aclfield->data.u8list.list = data.data();
  aclfield->mask.u8list.list = mask.data();
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_acl_entry_attribute_fn(
    sai_object_id_t acl_entry_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& aclEntry = fs->aclEntryManager.get(acl_entry_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_ENTRY_ATTR_TABLE_ID:
        attr_list[i].value.oid = aclEntry.tableId;
        break;
      case SAI_ACL_ENTRY_ATTR_PRIORITY:
        attr_list[i].value.u32 = aclEntry.priority;
        break;
      case SAI_ACL_ENTRY_ATTR_ADMIN_STATE:
        attr_list[i].value.booldata = aclEntry.enabled;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_SRC_IPV6:
        attr_list[i].value.aclfield.enable = aclEntry.fieldSrcIpV6Enable;
        facebook::fboss::toSaiIpAddressV6(
            aclEntry.fieldSrcIpV6Data, &attr_list[i].value.aclfield.data.ip6);
        facebook::fboss::toSaiIpAddressV6(
            aclEntry.fieldSrcIpV6Mask, &attr_list[i].value.aclfield.mask.ip6);
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6:
        attr_list[i].value.aclfield.enable = aclEntry.fieldDstIpV6Enable;
        facebook::fboss::toSaiIpAddressV6(
            aclEntry.fieldDstIpV6Data, &attr_list[i].value.aclfield.data.ip6);
        facebook::fboss::toSaiIpAddressV6(
            aclEntry.fieldDstIpV6Mask, &attr_list[i].value.aclfield.mask.ip6);
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6_WORD3:
        attr_list[i].value.aclfield.enable = aclEntry.fieldDstIpV6Word3Enable;
        facebook::fboss::toSaiIpAddressV6(
            aclEntry.fieldDstIpV6Word3Data,
            &attr_list[i].value.aclfield.data.ip6);
        facebook::fboss::toSaiIpAddressV6(
            aclEntry.fieldDstIpV6Word3Mask,
            &attr_list[i].value.aclfield.mask.ip6);
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6_WORD2:
        attr_list[i].value.aclfield.enable = aclEntry.fieldDstIpV6Word2Enable;
        facebook::fboss::toSaiIpAddressV6(
            aclEntry.fieldDstIpV6Word2Data,
            &attr_list[i].value.aclfield.data.ip6);
        facebook::fboss::toSaiIpAddressV6(
            aclEntry.fieldDstIpV6Word2Mask,
            &attr_list[i].value.aclfield.mask.ip6);
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP:
        attr_list[i].value.aclfield.enable = aclEntry.fieldSrcIpV4Enable;
        attr_list[i].value.aclfield.data.ip4 =
            facebook::fboss::toSaiIpAddress(aclEntry.fieldSrcIpV4Data).addr.ip4;
        attr_list[i].value.aclfield.mask.ip4 =
            facebook::fboss::toSaiIpAddress(aclEntry.fieldSrcIpV4Mask).addr.ip4;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_DST_IP:
        attr_list[i].value.aclfield.enable = aclEntry.fieldDstIpV4Enable;
        attr_list[i].value.aclfield.data.ip4 =
            facebook::fboss::toSaiIpAddress(aclEntry.fieldDstIpV4Data).addr.ip4;
        attr_list[i].value.aclfield.mask.ip4 =
            facebook::fboss::toSaiIpAddress(aclEntry.fieldDstIpV4Mask).addr.ip4;
        break;

      /*
       * Mask is not needed for SAI_ACL_ENTRY_ATTR_FIELD_IN_PORT
       * or SAI_ACL_ENTRY_ATTR_FIELD_OUT_PORT.
       * Thus, there is no oid field in sai_acl_field_data_mask_t.
       * oid is u64, but unfortunately, u64 was not added to
       * sai_acl_field_data_mask_t till SAI 1.6, so use u32.
       * This will be ignored by the implementation anyway.
       */
      case SAI_ACL_ENTRY_ATTR_FIELD_IN_PORT:
      case SAI_ACL_ENTRY_ATTR_FIELD_SRC_PORT:
        attr_list[i].value.aclfield.enable = aclEntry.fieldSrcPortEnable;
        attr_list[i].value.aclfield.data.oid = aclEntry.fieldSrcPortData;
        attr_list[i].value.aclfield.mask.u32 = aclEntry.fieldSrcPortMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_OUT_PORT:
        attr_list[i].value.aclfield.enable = aclEntry.fieldOutPortEnable;
        attr_list[i].value.aclfield.data.oid = aclEntry.fieldOutPortData;
        attr_list[i].value.aclfield.mask.u32 = aclEntry.fieldOutPortMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_L4_SRC_PORT:
        attr_list[i].value.aclfield.enable = aclEntry.fieldL4SrcPortEnable;
        attr_list[i].value.aclfield.data.u16 = aclEntry.fieldL4SrcPortData;
        attr_list[i].value.aclfield.mask.u16 = aclEntry.fieldL4SrcPortMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_L4_DST_PORT:
        attr_list[i].value.aclfield.enable = aclEntry.fieldL4DstPortEnable;
        attr_list[i].value.aclfield.data.u16 = aclEntry.fieldL4DstPortData;
        attr_list[i].value.aclfield.mask.u16 = aclEntry.fieldL4DstPortMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_IP_PROTOCOL:
        attr_list[i].value.aclfield.enable = aclEntry.fieldIpProtocolEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldIpProtocolData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldIpProtocolMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_TCP_FLAGS:
        attr_list[i].value.aclfield.enable = aclEntry.fieldTcpFlagsEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldTcpFlagsData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldTcpFlagsMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_FRAG:
        attr_list[i].value.aclfield.enable = aclEntry.fieldIpFragEnable;
        attr_list[i].value.aclfield.data.u32 = aclEntry.fieldIpFragData;
        attr_list[i].value.aclfield.mask.u32 = aclEntry.fieldIpFragMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_ICMP_TYPE:
        attr_list[i].value.aclfield.enable = aclEntry.fieldIcmpV4TypeEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldIcmpV4TypeData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldIcmpV4TypeMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_ICMP_CODE:
        attr_list[i].value.aclfield.enable = aclEntry.fieldIcmpV4CodeEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldIcmpV4CodeData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldIcmpV4CodeMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_ICMPV6_TYPE:
        attr_list[i].value.aclfield.enable = aclEntry.fieldIcmpV6TypeEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldIcmpV6TypeData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldIcmpV6TypeMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_ICMPV6_CODE:
        attr_list[i].value.aclfield.enable = aclEntry.fieldIcmpV6CodeEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldIcmpV6CodeData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldIcmpV6CodeMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_DSCP:
        attr_list[i].value.aclfield.enable = aclEntry.fieldDscpEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldDscpData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldDscpMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_TC:
        attr_list[i].value.aclfield.enable = aclEntry.fieldTcEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldTcData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldTcMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_DST_MAC:
        attr_list[i].value.aclfield.enable = aclEntry.fieldDstMacEnable;
        facebook::fboss::toSaiMacAddress(
            aclEntry.fieldDstMacData, attr_list[i].value.aclfield.data.mac);
        facebook::fboss::toSaiMacAddress(
            aclEntry.fieldDstMacMask, attr_list[i].value.aclfield.mask.mac);
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_TYPE:
        attr_list[i].value.aclfield.enable = aclEntry.fieldIpTypeEnable;
        attr_list[i].value.aclfield.data.u32 = aclEntry.fieldIpTypeData;
        attr_list[i].value.aclfield.mask.u32 = aclEntry.fieldIpTypeMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_TTL:
        attr_list[i].value.aclfield.enable = aclEntry.fieldTtlEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldTtlData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldTtlMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_MPLS_LABEL0_TTL:
        attr_list[i].value.aclfield.enable = aclEntry.fieldMplsLabel0TtlEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldMplsLabel0TtlData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldMplsLabel0TtlMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_FDB_DST_USER_META:
        attr_list[i].value.aclfield.enable = aclEntry.fieldFdbDstUserMetaEnable;
        attr_list[i].value.aclfield.data.u32 = aclEntry.fieldFdbDstUserMetaData;
        attr_list[i].value.aclfield.mask.u32 = aclEntry.fieldFdbDstUserMetaMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_ROUTE_DST_USER_META:
        attr_list[i].value.aclfield.enable =
            aclEntry.fieldRouteDstUserMetaEnable;
        attr_list[i].value.aclfield.data.u32 =
            aclEntry.fieldRouteDstUserMetaData;
        attr_list[i].value.aclfield.mask.u32 =
            aclEntry.fieldRouteDstUserMetaMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_NEIGHBOR_DST_USER_META:
        attr_list[i].value.aclfield.enable =
            aclEntry.fieldNeighborDstUserMetaEnable;
        attr_list[i].value.aclfield.data.u32 =
            aclEntry.fieldNeighborDstUserMetaData;
        attr_list[i].value.aclfield.mask.u32 =
            aclEntry.fieldNeighborDstUserMetaMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_PORT_USER_META:
        attr_list[i].value.aclfield.enable = aclEntry.fieldPortUserMetaEnable;
        attr_list[i].value.aclfield.data.u32 = aclEntry.fieldPortUserMetaData;
        attr_list[i].value.aclfield.mask.u32 = aclEntry.fieldPortUserMetaMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_ETHER_TYPE:
        attr_list[i].value.aclfield.enable = aclEntry.fieldEtherTypeEnable;
        attr_list[i].value.aclfield.data.u16 = aclEntry.fieldEtherTypeData;
        attr_list[i].value.aclfield.mask.u16 = aclEntry.fieldEtherTypeMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_OUTER_VLAN_ID:
        attr_list[i].value.aclfield.enable = aclEntry.fieldOuterVlanIdEnable;
        attr_list[i].value.aclfield.data.u16 = aclEntry.fieldOuterVlanIdData;
        attr_list[i].value.aclfield.mask.u16 = aclEntry.fieldOuterVlanIdMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_ACL_RANGE_TYPE:
        attr_list[i].value.aclfield.enable = aclEntry.fieldAclRangeTypeEnable;
        attr_list[i].value.aclfield.data.objlist.count =
            aclEntry.fieldAclRangeTypeData.size();
        attr_list[i].value.aclfield.data.objlist.list =
            aclEntry.fieldAclRangeTypeData.data();
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_BTH_OPCODE:
        attr_list[i].value.aclfield.enable = aclEntry.fieldBthOpcodeEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldBthOpcodeData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldBthOpcodeMask;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_IPV6_NEXT_HEADER:
        attr_list[i].value.aclfield.enable = aclEntry.fieldIpv6NextHeaderEnable;
        attr_list[i].value.aclfield.data.u8 = aclEntry.fieldIpv6NextHeaderData;
        attr_list[i].value.aclfield.mask.u8 = aclEntry.fieldIpv6NextHeaderMask;
        break;
      case SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN:
        return acl_entry_copy_attr_to_u8list(
            aclEntry.userDefinedFieldGroupMinData,
            aclEntry.userDefinedFieldGroupMinMask,
            &attr_list[i].value.aclfield);
      case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 1):
        return acl_entry_copy_attr_to_u8list(
            aclEntry.userDefinedFieldGroupMin1Data,
            aclEntry.userDefinedFieldGroupMin1Mask,
            &attr_list[i].value.aclfield);
      case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 2):
        return acl_entry_copy_attr_to_u8list(
            aclEntry.userDefinedFieldGroupMin2Data,
            aclEntry.userDefinedFieldGroupMin2Mask,
            &attr_list[i].value.aclfield);
      case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 3):
        return acl_entry_copy_attr_to_u8list(
            aclEntry.userDefinedFieldGroupMin3Data,
            aclEntry.userDefinedFieldGroupMin3Mask,
            &attr_list[i].value.aclfield);
      case (SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 4):
        return acl_entry_copy_attr_to_u8list(
            aclEntry.userDefinedFieldGroupMin4Data,
            aclEntry.userDefinedFieldGroupMin4Mask,
            &attr_list[i].value.aclfield);

      case SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION:
        attr_list[i].value.aclaction.enable = aclEntry.actionPacketActionEnable;
        attr_list[i].value.aclaction.parameter.u32 =
            aclEntry.actionPacketActionData;
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_REDIRECT:
        attr_list[i].value.aclaction.enable = aclEntry.actionRedirectEnable;
        attr_list[i].value.aclaction.parameter.oid =
            aclEntry.actionRedirectData;
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_COUNTER:
        attr_list[i].value.aclaction.enable = aclEntry.actionCounterEnable;
        attr_list[i].value.aclaction.parameter.oid = aclEntry.actionCounterData;
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_SET_TC:
        attr_list[i].value.aclaction.enable = aclEntry.actionSetTCEnable;
        attr_list[i].value.aclaction.parameter.u8 = aclEntry.actionSetTCData;
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_SET_DSCP:
        attr_list[i].value.aclaction.enable = aclEntry.actionSetDSCPEnable;
        attr_list[i].value.aclaction.parameter.u8 = aclEntry.actionSetDSCPData;
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_INGRESS:
        attr_list[i].value.aclaction.enable =
            aclEntry.actionMirrorIngressEnable;
        attr_list[i].value.aclaction.parameter.objlist.count =
            aclEntry.actionMirrorIngressData.size();
        attr_list[i].value.aclaction.parameter.objlist.list =
            aclEntry.actionMirrorIngressData.data();
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_EGRESS:
        attr_list[i].value.aclaction.enable = aclEntry.actionMirrorEgressEnable;
        attr_list[i].value.aclaction.parameter.objlist.count =
            aclEntry.actionMirrorEgressData.size();
        attr_list[i].value.aclaction.parameter.objlist.list =
            aclEntry.actionMirrorEgressData.data();
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_MACSEC_FLOW:
        attr_list[i].value.aclaction.enable = aclEntry.actionMacsecFlowEnable;
        attr_list[i].value.aclaction.parameter.oid =
            aclEntry.actionMacsecFlowData;
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_SET_USER_TRAP_ID:
        attr_list[i].value.aclaction.enable = aclEntry.actionSetUserTrapEnable;
        attr_list[i].value.aclaction.parameter.oid =
            aclEntry.actionSetUserTrapData;
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_SET_ARS_OBJECT:
        attr_list[i].value.aclaction.enable = aclEntry.actionSetArsObjectEnable;
        attr_list[i].value.aclaction.parameter.oid =
            aclEntry.actionSetArsObjectData;
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_DISABLE_ARS_FORWARDING:
        attr_list[i].value.aclaction.parameter.booldata =
            aclEntry.actionDisableArsForwarding;
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_SET_ECMP_HASH_ALGORITHM:
        attr_list[i].value.aclaction.enable =
            aclEntry.actionSetEcmpHashAlgorithmEnable;
        attr_list[i].value.aclaction.parameter.oid =
            aclEntry.actionSetEcmpHashAlgorithmData;
        break;
      case SAI_ACL_ENTRY_ATTR_ACTION_L3_SWITCH_CANCEL:
        attr_list[i].value.aclaction.enable =
            aclEntry.actionL3SwitchCancelEnable;
        attr_list[i].value.aclaction.parameter.booldata =
            aclEntry.actionL3SwitchCancelData;
        break;
      case SAI_ACL_ENTRY_ATTR_FIELD_ROUTE_DST:
        attr_list[i].value.aclfield.enable =
            aclEntry.fieldRouteDestinationEnable;
        attr_list[i].value.aclfield.data.oid =
            aclEntry.fieldRouteDestinationData;
        attr_list[i].value.aclfield.mask.u32 =
            aclEntry.fieldRouteDestinationMask;
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_acl_entry_fn(
    sai_object_id_t* acl_entry_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();

  std::optional<sai_object_id_t> tableId;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_ENTRY_ATTR_TABLE_ID:
        tableId = attr_list[i].value.oid;
        break;
    }
  }

  if (!tableId) {
    return SAI_STATUS_INVALID_PARAMETER;
  }

  *acl_entry_id = fs->aclEntryManager.create(tableId.value());
  auto& aclEntry = fs->aclEntryManager.get(*acl_entry_id);

  for (int i = 0; i < attr_count; ++i) {
    if (attr_list[i].id == SAI_ACL_ENTRY_ATTR_TABLE_ID) {
      aclEntry.tableId = attr_list[i].value.oid;
    } else {
      sai_status_t res =
          set_acl_entry_attribute_fn(*acl_entry_id, &attr_list[i]);
      if (res != SAI_STATUS_SUCCESS) {
        fs->aclEntryManager.remove(*acl_entry_id);
        return res;
      }
    }
  }

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_acl_entry_fn(sai_object_id_t acl_entry_id) {
  auto fs = FakeSai::getInstance();
  fs->aclEntryManager.remove(acl_entry_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_acl_counter_attribute_fn(
    sai_object_id_t acl_counter_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& aclCounter = fs->aclCounterManager.get(acl_counter_id);
  sai_status_t res;
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }

  switch (attr->id) {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
    case SAI_ACL_COUNTER_ATTR_LABEL:
      std::copy(
          attr->value.chardata,
          attr->value.chardata + aclCounter.label.size(),
          std::begin(aclCounter.label));
      res = SAI_STATUS_SUCCESS;
      break;
#endif
    case SAI_ACL_COUNTER_ATTR_PACKETS:
      aclCounter.counterPackets = attr->value.u64;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_ACL_COUNTER_ATTR_BYTES:
      aclCounter.counterBytes = attr->value.u64;
      res = SAI_STATUS_SUCCESS;
      break;
    default:
      res = SAI_STATUS_NOT_SUPPORTED;
      break;
  }
  return res;
}

sai_status_t get_acl_counter_attribute_fn(
    sai_object_id_t acl_counter_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& aclCounter = fs->aclCounterManager.get(acl_counter_id);

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_COUNTER_ATTR_TABLE_ID:
        attr_list[i].value.oid = aclCounter.tableId;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      case SAI_ACL_COUNTER_ATTR_LABEL:
        std::copy(
            std::begin(aclCounter.label),
            std::end(aclCounter.label),
            attr_list[i].value.chardata);
        break;
#endif
      case SAI_ACL_COUNTER_ATTR_ENABLE_PACKET_COUNT:
        attr_list[i].value.booldata = aclCounter.enablePacketCount;
        break;
      case SAI_ACL_COUNTER_ATTR_ENABLE_BYTE_COUNT:
        attr_list[i].value.booldata = aclCounter.enableByteCount;
        break;
      case SAI_ACL_COUNTER_ATTR_PACKETS:
        attr_list[i].value.u64 = aclCounter.counterPackets;
        break;
      case SAI_ACL_COUNTER_ATTR_BYTES:
        attr_list[i].value.u64 = aclCounter.counterBytes;
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_acl_counter_fn(
    sai_object_id_t* acl_counter_id,
    sai_object_id_t /*switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();

  std::optional<sai_object_id_t> tableId;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_COUNTER_ATTR_TABLE_ID:
        tableId = attr_list[i].value.oid;
        break;
    }
  }

  if (!tableId) {
    return SAI_STATUS_INVALID_PARAMETER;
  }

  *acl_counter_id = fs->aclCounterManager.create(tableId.value());
  auto& aclCounter = fs->aclCounterManager.get(*acl_counter_id);

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_COUNTER_ATTR_TABLE_ID:
        aclCounter.tableId = attr_list[i].value.oid;
        break;
      case SAI_ACL_COUNTER_ATTR_ENABLE_PACKET_COUNT:
        aclCounter.enablePacketCount = attr_list[i].value.booldata;
        break;
      case SAI_ACL_COUNTER_ATTR_ENABLE_BYTE_COUNT:
        aclCounter.enableByteCount = attr_list[i].value.booldata;
        break;
      default: {
        sai_status_t res =
            set_acl_counter_attribute_fn(*acl_counter_id, &attr_list[i]);
        if (res != SAI_STATUS_SUCCESS) {
          fs->aclCounterManager.remove(*acl_counter_id);
          return res;
        }
      }
    }
  }

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_acl_counter_fn(sai_object_id_t acl_counter_id) {
  auto fs = FakeSai::getInstance();
  fs->aclCounterManager.remove(acl_counter_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_acl_range_fn(
    sai_object_id_t* acl_range_id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();

  std::optional<sai_int32_t> type;
  sai_u32_range_t limit{0, 0};

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_RANGE_ATTR_TYPE:
        type = attr_list[i].value.s32;
        break;
      case SAI_ACL_RANGE_ATTR_LIMIT:
        limit = attr_list[i].value.u32range;
        break;
    }
  }

  if (!type) {
    return SAI_STATUS_INVALID_PARAMETER;
  }

  *acl_range_id = fs->aclRangeManager.create(type.value(), limit);

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_acl_range_fn(sai_object_id_t acl_range_id) {
  auto fs = FakeSai::getInstance();
  fs->aclRangeManager.remove(acl_range_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_acl_range_attribute_fn(
    sai_object_id_t /*acl_range_id*/,
    const sai_attribute_t* /*attr*/) {
  return SAI_STATUS_NOT_SUPPORTED;
}

sai_status_t get_acl_range_attribute_fn(
    sai_object_id_t acl_range_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& aclRange = fs->aclRangeManager.get(acl_range_id);

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_RANGE_ATTR_TYPE:
        attr_list[i].value.s32 = aclRange.type;
        break;
      case SAI_ACL_RANGE_ATTR_LIMIT:
        attr_list[i].value.u32range = aclRange.limit;
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_acl_table_group_fn(
    sai_object_id_t* acl_table_group_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();

  std::optional<sai_int32_t> stage;
  std::vector<sai_int32_t> bindPointTypeList;
  sai_int32_t type;

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_TABLE_GROUP_ATTR_ACL_STAGE:
        stage = attr_list[i].value.s32;
        break;
      case SAI_ACL_TABLE_GROUP_ATTR_ACL_BIND_POINT_TYPE_LIST:
        for (int j = 0; j < attr_list[i].value.s32list.count; ++j) {
          bindPointTypeList.push_back(attr_list[i].value.s32list.list[j]);
        }
        break;
      case SAI_ACL_TABLE_GROUP_ATTR_TYPE:
        type = attr_list[i].value.s32;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }

  if (!stage) {
    return SAI_STATUS_INVALID_PARAMETER;
  }

  *acl_table_group_id =
      fs->aclTableGroupManager.create(stage.value(), bindPointTypeList, type);

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_acl_table_group_fn(sai_object_id_t acl_table_group_id) {
  auto fs = FakeSai::getInstance();
  fs->aclTableGroupManager.remove(acl_table_group_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_acl_table_group_attribute_fn(
    sai_object_id_t /*acl_table_group_id*/,
    const sai_attribute_t* attr) {
  switch (attr->id) {
    default:
      // SAI spec does not support setting any attribute for ACL table group
      // post creation.
      return SAI_STATUS_NOT_SUPPORTED;
  }

  return SAI_STATUS_SUCCESS;
}

sai_status_t get_acl_table_group_attribute_fn(
    sai_object_id_t acl_table_group_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_TABLE_GROUP_ATTR_ACL_STAGE: {
        const auto& aclTableGroup =
            fs->aclTableGroupManager.get(acl_table_group_id);
        attr_list[i].value.s32 = aclTableGroup.stage;
      } break;
      case SAI_ACL_TABLE_GROUP_ATTR_ACL_BIND_POINT_TYPE_LIST: {
        const auto& aclTableGroup =
            fs->aclTableGroupManager.get(acl_table_group_id);
        if (aclTableGroup.bindPointTypeList.size() >
            attr_list[i].value.s32list.count) {
          attr_list[i].value.s32list.count =
              aclTableGroup.bindPointTypeList.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }

        attr_list[i].value.s32list.count =
            aclTableGroup.bindPointTypeList.size();
        int j = 0;
        for (const auto& bindPointType : aclTableGroup.bindPointTypeList) {
          attr_list[i].value.s32list.list[j++] = bindPointType;
        }
      } break;
      case SAI_ACL_TABLE_GROUP_ATTR_TYPE: {
        const auto& aclTableGroup =
            fs->aclTableGroupManager.get(acl_table_group_id);
        attr_list[i].value.s32 = aclTableGroup.type;
      } break;
      case SAI_ACL_TABLE_GROUP_ATTR_MEMBER_LIST: {
        const auto& aclTableGroupMemberMap =
            fs->aclTableGroupManager.get(acl_table_group_id).fm().map();
        if (aclTableGroupMemberMap.size() > attr_list[i].value.objlist.count) {
          attr_list[i].value.objlist.count = aclTableGroupMemberMap.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        attr_list[i].value.objlist.count = aclTableGroupMemberMap.size();
        int j = 0;
        for (const auto& m : aclTableGroupMemberMap) {
          attr_list[i].value.objlist.list[j++] = m.first;
        }
      } break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }

  return SAI_STATUS_SUCCESS;
}

sai_status_t create_acl_table_group_member_fn(
    sai_object_id_t* acl_table_group_member_id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();

  std::optional<sai_object_id_t> tableGroupId;
  std::optional<sai_object_id_t> tableId;
  std::optional<sai_uint32_t> priority;

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_GROUP_ID:
        tableGroupId = attr_list[i].value.oid;
        break;
      case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_ID:
        tableId = attr_list[i].value.oid;
        break;
      case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_PRIORITY:
        priority = attr_list[i].value.u32;
        break;
    }
  }

  if (!tableGroupId || !tableId || !priority) {
    return SAI_STATUS_INVALID_PARAMETER;
  }

  *acl_table_group_member_id = fs->aclTableGroupManager.createMember(
      tableGroupId.value(),
      tableGroupId.value(),
      tableId.value(),
      priority.value());

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_acl_table_group_member_fn(
    sai_object_id_t acl_table_group_member_id) {
  auto fs = FakeSai::getInstance();
  fs->aclTableGroupManager.removeMember(acl_table_group_member_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_acl_table_group_member_attribute_fn(
    sai_object_id_t /*acl_table_group_member_id*/,
    const sai_attribute_t* attr) {
  return SAI_STATUS_NOT_IMPLEMENTED;

  switch (attr->id) {
    default:
      // SAI spec does not support setting any attribute for ACL table group
      // memeber post creation.
      return SAI_STATUS_NOT_SUPPORTED;
  }

  return SAI_STATUS_SUCCESS;
}

sai_status_t get_acl_table_group_member_attribute_fn(
    sai_object_id_t acl_table_group_member_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& aclTableGroupMember =
      fs->aclTableGroupManager.getMember(acl_table_group_member_id);

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_GROUP_ID:
        attr_list[i].value.oid = aclTableGroupMember.tableGroupId;
        break;
      case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_ID:
        attr_list[i].value.oid = aclTableGroupMember.tableId;
        break;
      case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_PRIORITY:
        attr_list[i].value.u32 = aclTableGroupMember.priority;
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

sai_acl_api_t* FakeAclTable::kApi() {
  static sai_acl_api_t kAclApi = {
      &create_acl_table_fn,
      &remove_acl_table_fn,
      &set_acl_table_attribute_fn,
      &get_acl_table_attribute_fn,
      &create_acl_entry_fn,
      &remove_acl_entry_fn,
      &set_acl_entry_attribute_fn,
      &get_acl_entry_attribute_fn,
      &create_acl_counter_fn,
      &remove_acl_counter_fn,
      &set_acl_counter_attribute_fn,
      &get_acl_counter_attribute_fn,
      &create_acl_range_fn,
      &remove_acl_range_fn,
      &set_acl_range_attribute_fn,
      &get_acl_range_attribute_fn,
      &create_acl_table_group_fn,
      &remove_acl_table_group_fn,
      &set_acl_table_group_attribute_fn,
      &get_acl_table_group_attribute_fn,
      &create_acl_table_group_member_fn,
      &remove_acl_table_group_member_fn,
      &set_acl_table_group_member_attribute_fn,
      &get_acl_table_group_member_attribute_fn};

  return &kAclApi;
}

void populate_acl_api(sai_acl_api_t** acl_api) {
  *acl_api = FakeAclTable::kApi();
}

} // namespace facebook::fboss
