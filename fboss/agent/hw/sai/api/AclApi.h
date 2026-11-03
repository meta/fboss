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

#include "fboss/agent/hw/sai/api/SaiApi.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiAttributeDataTypes.h"
#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Types.h"

#include <folly/logging/xlog.h>

#include <tuple>

extern "C" {
#include <sai.h>
}

inline bool operator==(const sai_u32_range_t& a, const sai_u32_range_t& b) {
  return a.min == b.min && a.max == b.max;
}

inline std::size_t hash_value(const sai_u32_range_t& r) {
  std::size_t seed = 0;
  boost::hash_combine(seed, r.min);
  boost::hash_combine(seed, r.max);
  return seed;
}

namespace facebook::fboss {

class AclApi;

struct SaiAclTableGroupTraits {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_ACL_TABLE_GROUP;
  using SaiApiT = AclApi;
  struct Attributes {
    using EnumType = sai_acl_table_group_attr_t;

    using Stage =
        SaiAttribute<EnumType, SAI_ACL_TABLE_GROUP_ATTR_ACL_STAGE, sai_int32_t>;
    using BindPointTypeList = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_GROUP_ATTR_ACL_BIND_POINT_TYPE_LIST,
        std::vector<sai_int32_t>>;
    using Type =
        SaiAttribute<EnumType, SAI_ACL_TABLE_GROUP_ATTR_TYPE, sai_int32_t>;
    using MemberList = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_GROUP_ATTR_MEMBER_LIST,
        std::vector<sai_object_id_t>>;
  };

  using AdapterKey = AclTableGroupSaiId;
  using AdapterHostKey = std::tuple<
      Attributes::Stage,
      std::optional<Attributes::BindPointTypeList>,
      std::optional<Attributes::Type>>;
  using CreateAttributes = std::tuple<
      Attributes::Stage,
      std::optional<Attributes::BindPointTypeList>,
      std::optional<Attributes::Type>>;
};

SAI_ATTRIBUTE_NAME(AclTableGroup, Stage);
SAI_ATTRIBUTE_NAME(AclTableGroup, BindPointTypeList);
SAI_ATTRIBUTE_NAME(AclTableGroup, Type);
SAI_ATTRIBUTE_NAME(AclTableGroup, MemberList);

struct SaiAclTableGroupMemberTraits {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER;
  using SaiApiT = AclApi;
  struct Attributes {
    using EnumType = sai_acl_table_group_member_attr_t;

    using TableGroupId = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_GROUP_ID,
        SaiObjectIdT>;
    using TableId = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_ID,
        SaiObjectIdT>;
    using Priority = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_GROUP_MEMBER_ATTR_PRIORITY,
        sai_uint32_t>;
  };

  using AdapterKey = AclTableGroupMemberSaiId;
  using AdapterHostKey = std::tuple<
      Attributes::TableGroupId,
      Attributes::TableId,
      Attributes::Priority>;
  using CreateAttributes = std::tuple<
      Attributes::TableGroupId,
      Attributes::TableId,
      Attributes::Priority>;
};

SAI_ATTRIBUTE_NAME(AclTableGroupMember, TableGroupId);
SAI_ATTRIBUTE_NAME(AclTableGroupMember, TableId);
SAI_ATTRIBUTE_NAME(AclTableGroupMember, Priority);

struct SaiAclTableTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_ACL_TABLE;
  using SaiApiT = AclApi;
  struct Attributes {
    using EnumType = sai_acl_table_attr_t;

    using Stage =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_ACL_STAGE, sai_int32_t>;
    using BindPointTypeList = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_ACL_BIND_POINT_TYPE_LIST,
        std::vector<sai_int32_t>>;
    using ActionTypeList = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_ACL_ACTION_TYPE_LIST,
        std::vector<sai_int32_t>>;
    using EntryList = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_ENTRY_LIST,
        std::vector<sai_object_id_t>>;
    using AvailableEntry = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_ENTRY,
        sai_uint32_t>;
    using AvailableCounter = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_COUNTER,
        sai_uint32_t>;
    /*
     * At least one Field* must be set.
     */
    using FieldSrcIpV6 =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_SRC_IPV6, bool>;
    using FieldDstIpV6 =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6, bool>;
    using FieldDstIpV6Word3 = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6_WORD3,
        bool,
        SaiBoolDefaultFalse>;
    using FieldDstIpV6Word2 = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6_WORD2,
        bool,
        SaiBoolDefaultFalse>;
    using FieldSrcIpV4 =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_SRC_IP, bool>;
    using FieldDstIpV4 =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_DST_IP, bool>;
    using FieldL4SrcPort =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_L4_SRC_PORT, bool>;
    using FieldL4DstPort =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_L4_DST_PORT, bool>;
    using FieldIpProtocol =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_IP_PROTOCOL, bool>;
    using FieldTcpFlags =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_TCP_FLAGS, bool>;
#if defined(TAJO_SDK) || defined(CHENAB_SAI_SDK)
    using FieldSrcPort =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_IN_PORT, bool>;
#else
    using FieldSrcPort =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_SRC_PORT, bool>;
#endif
    using FieldOutPort =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_OUT_PORT, bool>;
    using FieldIpFrag =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_FRAG, bool>;
    using FieldIcmpV4Type =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_ICMP_TYPE, bool>;
    using FieldIcmpV4Code =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_ICMP_CODE, bool>;
    using FieldIcmpV6Type =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_ICMPV6_TYPE, bool>;
    using FieldIcmpV6Code =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_ICMPV6_CODE, bool>;
    using FieldDscp =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_DSCP, bool>;
    using FieldTc = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_FIELD_TC,
        bool,
        SaiBoolDefaultFalse>;
    using FieldDstMac =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_DST_MAC, bool>;
    using FieldIpType =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_TYPE, bool>;
    using FieldTtl = SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_TTL, bool>;
    using FieldMplsLabel0Ttl = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_FIELD_MPLS_LABEL0_TTL,
        bool,
        StdNullOptDefault<bool>>;
    using FieldFdbDstUserMeta = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_FIELD_FDB_DST_USER_META,
        bool>;
    using FieldRouteDstUserMeta = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_FIELD_ROUTE_DST_USER_META,
        bool>;
    using FieldNeighborDstUserMeta = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_FIELD_NEIGHBOR_DST_USER_META,
        bool>;
    using FieldPortUserMeta = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_FIELD_PORT_USER_META,
        bool,
        StdNullOptDefault<bool>>;
    using FieldEthertype =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_ETHER_TYPE, bool>;
    using FieldOuterVlanId =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_OUTER_VLAN_ID, bool>;
    using FieldAclRangeType = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_FIELD_ACL_RANGE_TYPE,
        std::vector<sai_int32_t>>;
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
    using FieldBthOpcode =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_BTH_OPCODE, bool>;
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
    using FieldIpv6NextHeader =
        SaiAttribute<EnumType, SAI_ACL_TABLE_ATTR_FIELD_IPV6_NEXT_HEADER, bool>;
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
    using UserDefinedFieldGroupMin0 = SaiAttribute<
        EnumType,
        SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN,
        sai_object_id_t,
        SaiObjectIdDefault>;
    using UserDefinedFieldGroupMin1 = SaiAttribute<
        EnumType,
        static_cast<sai_acl_table_attr_t>(
            SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 1),
        sai_object_id_t,
        SaiObjectIdDefault>;
    using UserDefinedFieldGroupMin2 = SaiAttribute<
        EnumType,
        static_cast<sai_acl_table_attr_t>(
            SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 2),
        sai_object_id_t,
        SaiObjectIdDefault>;
    using UserDefinedFieldGroupMin3 = SaiAttribute<
        EnumType,
        static_cast<sai_acl_table_attr_t>(
            SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 3),
        sai_object_id_t,
        SaiObjectIdDefault>;
    using UserDefinedFieldGroupMin4 = SaiAttribute<
        EnumType,
        static_cast<sai_acl_table_attr_t>(
            SAI_ACL_TABLE_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 4),
        sai_object_id_t,
        SaiObjectIdDefault>;
#endif
    struct AttributeFieldRouteDestination {
      std::optional<sai_attr_id_t> operator()();
    };
    using FieldRouteDestination = SaiExtensionAttribute<
        bool,
        AttributeFieldRouteDestination,
        SaiBoolDefaultFalse>;
  };

  using AdapterKey = AclTableSaiId;
  using CreateAttributes = std::tuple<
      Attributes::Stage,
      std::optional<Attributes::BindPointTypeList>,
      std::optional<Attributes::ActionTypeList>,
      std::optional<Attributes::FieldSrcIpV6>,
      std::optional<Attributes::FieldDstIpV6>,
      std::optional<Attributes::FieldDstIpV6Word3>,
      std::optional<Attributes::FieldDstIpV6Word2>,
      std::optional<Attributes::FieldSrcIpV4>,
      std::optional<Attributes::FieldDstIpV4>,
      std::optional<Attributes::FieldL4SrcPort>,
      std::optional<Attributes::FieldL4DstPort>,
      std::optional<Attributes::FieldIpProtocol>,
      std::optional<Attributes::FieldTcpFlags>,
      std::optional<Attributes::FieldSrcPort>,
      std::optional<Attributes::FieldOutPort>,
      std::optional<Attributes::FieldIpFrag>,
      std::optional<Attributes::FieldIcmpV4Type>,
      std::optional<Attributes::FieldIcmpV4Code>,
      std::optional<Attributes::FieldIcmpV6Type>,
      std::optional<Attributes::FieldIcmpV6Code>,
      std::optional<Attributes::FieldDscp>,
      std::optional<Attributes::FieldTc>,
      std::optional<Attributes::FieldDstMac>,
      std::optional<Attributes::FieldIpType>,
      std::optional<Attributes::FieldTtl>,
      std::optional<Attributes::FieldMplsLabel0Ttl>,
      std::optional<Attributes::FieldFdbDstUserMeta>,
      std::optional<Attributes::FieldRouteDstUserMeta>,
      std::optional<Attributes::FieldNeighborDstUserMeta>,
      std::optional<Attributes::FieldEthertype>,
      std::optional<Attributes::FieldOuterVlanId>,
      std::optional<Attributes::FieldAclRangeType>
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
      ,
      std::optional<Attributes::FieldBthOpcode>
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
      ,
      std::optional<Attributes::FieldIpv6NextHeader>
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
      ,
      std::optional<Attributes::UserDefinedFieldGroupMin0>,
      std::optional<Attributes::UserDefinedFieldGroupMin1>,
      std::optional<Attributes::UserDefinedFieldGroupMin2>,
      std::optional<Attributes::UserDefinedFieldGroupMin3>,
      std::optional<Attributes::UserDefinedFieldGroupMin4>
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      std::optional<Attributes::FieldRouteDestination>
#endif
      ,
      std::optional<Attributes::FieldPortUserMeta>>;

  using AdapterHostKey = std::string;
};

SAI_ATTRIBUTE_NAME(AclTable, Stage);
SAI_ATTRIBUTE_NAME(AclTable, BindPointTypeList);
SAI_ATTRIBUTE_NAME(AclTable, ActionTypeList);
SAI_ATTRIBUTE_NAME(AclTable, EntryList);
SAI_ATTRIBUTE_NAME(AclTable, FieldSrcIpV6);
SAI_ATTRIBUTE_NAME(AclTable, FieldDstIpV6);
SAI_ATTRIBUTE_NAME(AclTable, FieldDstIpV6Word3);
SAI_ATTRIBUTE_NAME(AclTable, FieldDstIpV6Word2);
SAI_ATTRIBUTE_NAME(AclTable, FieldSrcIpV4);
SAI_ATTRIBUTE_NAME(AclTable, FieldDstIpV4);
SAI_ATTRIBUTE_NAME(AclTable, FieldL4SrcPort);
SAI_ATTRIBUTE_NAME(AclTable, FieldL4DstPort);
SAI_ATTRIBUTE_NAME(AclTable, FieldIpProtocol);
SAI_ATTRIBUTE_NAME(AclTable, FieldTcpFlags);
SAI_ATTRIBUTE_NAME(AclTable, FieldSrcPort);
SAI_ATTRIBUTE_NAME(AclTable, FieldOutPort);
SAI_ATTRIBUTE_NAME(AclTable, FieldIpFrag);
SAI_ATTRIBUTE_NAME(AclTable, FieldIcmpV4Type);
SAI_ATTRIBUTE_NAME(AclTable, FieldIcmpV4Code);
SAI_ATTRIBUTE_NAME(AclTable, FieldIcmpV6Type);
SAI_ATTRIBUTE_NAME(AclTable, FieldIcmpV6Code);
SAI_ATTRIBUTE_NAME(AclTable, FieldDscp);
SAI_ATTRIBUTE_NAME(AclTable, FieldTc);
SAI_ATTRIBUTE_NAME(AclTable, FieldDstMac);
SAI_ATTRIBUTE_NAME(AclTable, FieldIpType);
SAI_ATTRIBUTE_NAME(AclTable, FieldTtl);
SAI_ATTRIBUTE_NAME(AclTable, FieldMplsLabel0Ttl);
SAI_ATTRIBUTE_NAME(AclTable, FieldFdbDstUserMeta);
SAI_ATTRIBUTE_NAME(AclTable, FieldRouteDstUserMeta);
SAI_ATTRIBUTE_NAME(AclTable, FieldNeighborDstUserMeta);
SAI_ATTRIBUTE_NAME(AclTable, FieldPortUserMeta);
SAI_ATTRIBUTE_NAME(AclTable, AvailableEntry);
SAI_ATTRIBUTE_NAME(AclTable, AvailableCounter);
SAI_ATTRIBUTE_NAME(AclTable, FieldEthertype);
SAI_ATTRIBUTE_NAME(AclTable, FieldOuterVlanId);
SAI_ATTRIBUTE_NAME(AclTable, FieldAclRangeType);
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
SAI_ATTRIBUTE_NAME(AclTable, FieldBthOpcode);
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
SAI_ATTRIBUTE_NAME(AclTable, FieldIpv6NextHeader);
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
SAI_ATTRIBUTE_NAME(AclTable, UserDefinedFieldGroupMin0);
SAI_ATTRIBUTE_NAME(AclTable, UserDefinedFieldGroupMin1);
SAI_ATTRIBUTE_NAME(AclTable, UserDefinedFieldGroupMin2);
SAI_ATTRIBUTE_NAME(AclTable, UserDefinedFieldGroupMin3);
SAI_ATTRIBUTE_NAME(AclTable, UserDefinedFieldGroupMin4);
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
SAI_ATTRIBUTE_NAME(AclTable, FieldRouteDestination);
#endif

struct SaiAclEntryTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_ACL_ENTRY;
  using SaiApiT = AclApi;
  struct Attributes {
    using EnumType = sai_acl_entry_attr_t;

    using TableId =
        SaiAttribute<EnumType, SAI_ACL_ENTRY_ATTR_TABLE_ID, sai_object_id_t>;
    using Priority =
        SaiAttribute<EnumType, SAI_ACL_ENTRY_ATTR_PRIORITY, sai_uint32_t>;
    using Enabled = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ADMIN_STATE,
        bool,
        SaiBoolDefaultTrue>;
    using FieldSrcIpV6 = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_SRC_IPV6,
        AclEntryFieldIpV6,
        StdNullOptDefault<AclEntryFieldIpV6>>;
    using FieldDstIpV6 = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6,
        AclEntryFieldIpV6,
        StdNullOptDefault<AclEntryFieldIpV6>>;
    using FieldDstIpV6Word3 = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6_WORD3,
        AclEntryFieldIpV6,
        StdNullOptDefault<AclEntryFieldIpV6>>;
    using FieldDstIpV6Word2 = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6_WORD2,
        AclEntryFieldIpV6,
        StdNullOptDefault<AclEntryFieldIpV6>>;
    using FieldSrcIpV4 = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP,
        AclEntryFieldIpV4,
        StdNullOptDefault<AclEntryFieldIpV4>>;
    using FieldDstIpV4 = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_DST_IP,
        AclEntryFieldIpV4,
        StdNullOptDefault<AclEntryFieldIpV4>>;
#if defined(TAJO_SDK) || defined(CHENAB_SAI_SDK)
    using FieldSrcPort = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_IN_PORT,
        AclEntryFieldSaiObjectIdT,
        StdNullOptDefault<AclEntryFieldSaiObjectIdT>>;
#else
    using FieldSrcPort = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_SRC_PORT,
        AclEntryFieldSaiObjectIdT>;
#endif
    using FieldOutPort = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_OUT_PORT,
        AclEntryFieldSaiObjectIdT,
        StdNullOptDefault<AclEntryFieldSaiObjectIdT>>;
    using FieldL4SrcPort = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_L4_SRC_PORT,
        AclEntryFieldU16,
        StdNullOptDefault<AclEntryFieldU16>>;
    using FieldL4DstPort = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_L4_DST_PORT,
        AclEntryFieldU16,
        StdNullOptDefault<AclEntryFieldU16>>;
    using FieldIpProtocol = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_IP_PROTOCOL,
        AclEntryFieldU8,
        StdNullOptDefault<AclEntryFieldU8>>;
    using FieldTcpFlags = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_TCP_FLAGS,
        AclEntryFieldU8>;
    using FieldIpFrag = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_FRAG,
        AclEntryFieldU32>;
    using FieldIcmpV4Type = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_ICMP_TYPE,
        AclEntryFieldU8>;
    using FieldIcmpV4Code = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_ICMP_CODE,
        AclEntryFieldU8>;
    using FieldIcmpV6Type = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_ICMPV6_TYPE,
        AclEntryFieldU8>;
    using FieldIcmpV6Code = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_ICMPV6_CODE,
        AclEntryFieldU8>;
    using FieldDscp =
        SaiAttribute<EnumType, SAI_ACL_ENTRY_ATTR_FIELD_DSCP, AclEntryFieldU8>;
    using FieldTc = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_TC,
        AclEntryFieldU8,
        StdNullOptDefault<AclEntryFieldU8>>;
    using FieldDstMac = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_DST_MAC,
        AclEntryFieldMac>;
    using FieldIpType = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_TYPE,
        AclEntryFieldU32>;
    using FieldTtl =
        SaiAttribute<EnumType, SAI_ACL_ENTRY_ATTR_FIELD_TTL, AclEntryFieldU8>;
    using FieldMplsLabel0Ttl = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_MPLS_LABEL0_TTL,
        AclEntryFieldU8,
        StdNullOptDefault<AclEntryFieldU8>>;
    using FieldFdbDstUserMeta = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_FDB_DST_USER_META,
        AclEntryFieldU32>;
    using FieldRouteDstUserMeta = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_ROUTE_DST_USER_META,
        AclEntryFieldU32>;
    using FieldNeighborDstUserMeta = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_NEIGHBOR_DST_USER_META,
        AclEntryFieldU32>;
    using FieldPortUserMeta = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_PORT_USER_META,
        AclEntryFieldU32,
        StdNullOptDefault<AclEntryFieldU32>>;
    using FieldEthertype = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_ETHER_TYPE,
        AclEntryFieldU16>;
    using FieldOuterVlanId = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_OUTER_VLAN_ID,
        AclEntryFieldU16>;
    using FieldAclRangeType = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_ACL_RANGE_TYPE,
        AclEntryFieldSaiObjectIdList,
        StdNullOptDefault<AclEntryFieldSaiObjectIdList>>;
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
    using FieldBthOpcode = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_BTH_OPCODE,
        AclEntryFieldU8>;
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
    using FieldIpv6NextHeader = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_FIELD_IPV6_NEXT_HEADER,
        AclEntryFieldU8>;
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
    using UserDefinedFieldGroupMin0 = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN,
        AclEntryFieldU8List>;
    using UserDefinedFieldGroupMin1 = SaiAttribute<
        EnumType,
        static_cast<sai_acl_entry_attr_t>(
            SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 1),
        AclEntryFieldU8List>;
    using UserDefinedFieldGroupMin2 = SaiAttribute<
        EnumType,
        static_cast<sai_acl_entry_attr_t>(
            SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 2),
        AclEntryFieldU8List>;
    using UserDefinedFieldGroupMin3 = SaiAttribute<
        EnumType,
        static_cast<sai_acl_entry_attr_t>(
            SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 3),
        AclEntryFieldU8List>;
    using UserDefinedFieldGroupMin4 = SaiAttribute<
        EnumType,
        static_cast<sai_acl_entry_attr_t>(
            SAI_ACL_ENTRY_ATTR_USER_DEFINED_FIELD_GROUP_MIN + 4),
        AclEntryFieldU8List>;
#endif
    using ActionPacketAction = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION,
        AclEntryActionU32>;
    using ActionRedirect = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_REDIRECT,
        AclEntryActionSaiObjectIdT,
        SaiAclEntryActionSaiObjectDefault>;
    using ActionCounter = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_COUNTER,
        AclEntryActionSaiObjectIdT>;
    using ActionSetTC = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_SET_TC,
        AclEntryActionU8>;
    using ActionSetDSCP = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_SET_DSCP,
        AclEntryActionU8>;
    using ActionMirrorIngress = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_INGRESS,
        AclEntryActionSaiObjectIdList>;
    using ActionMirrorEgress = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_EGRESS,
        AclEntryActionSaiObjectIdList>;
    using ActionMacsecFlow = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_MACSEC_FLOW,
        AclEntryActionSaiObjectIdT,
        SaiAclEntryActionSaiObjectDefault>;
#if !defined(TAJO_SDK)
    using ActionSetUserTrap = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_SET_USER_TRAP_ID,
        AclEntryActionSaiObjectIdT>;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    using ActionSetArsObject = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_SET_ARS_OBJECT,
        AclEntryActionSaiObjectIdT,
        SaiAclEntryActionSaiObjectDefault>;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    using ActionDisableArsForwarding = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_DISABLE_ARS_FORWARDING,
        AclEntryActionBool,
        SaiAclEntryActionBoolFalse>;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    using ActionSetEcmpHashAlgorithm = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_ACTION_SET_ECMP_HASH_ALGORITHM,
        AclEntryActionU32,
        StdNullOptDefault<AclEntryActionU32>>;
#endif
    struct AttributeActionL3SwitchCancel {
      std::optional<sai_attr_id_t> operator()();
    };
    using ActionL3SwitchCancel = SaiExtensionAttribute<
        AclEntryActionBool,
        AttributeActionL3SwitchCancel,
        SaiAclEntryActionBoolFalse>;
    struct AttributeFieldRouteDestination {
      std::optional<sai_attr_id_t> operator()();
    };
    using FieldRouteDestination = SaiExtensionAttribute<
        AclEntryFieldSaiObjectIdT,
        AttributeFieldRouteDestination,
        SaiAclEntryFieldSaiObjectIdTDefault>;
#if (defined(TAJO_SDK_GTE_26_5) && !defined(TAJO_SDK_P200)) || \
    defined(BRCM_SAI_SDK_XGS_GTE_15_4_1)
    using Label = SaiAttribute<
        EnumType,
        SAI_ACL_ENTRY_ATTR_LABEL,
        std::vector<sai_int8_t>,
        SaiS8ListDefault>;
#endif
  };

  using AdapterKey = AclEntrySaiId;
  using AdapterHostKey = std::tuple<
      Attributes::TableId,
      std::optional<Attributes::Priority>
#if (defined(TAJO_SDK_GTE_26_5) && !defined(TAJO_SDK_P200)) || \
    defined(BRCM_SAI_SDK_XGS_GTE_15_4_1)
      ,
      std::optional<Attributes::Label>
#endif
      >;
  using CreateAttributes = std::tuple<
      Attributes::TableId,
      std::optional<Attributes::Priority>,
      Attributes::Enabled,
      std::optional<Attributes::FieldSrcIpV6>,
      std::optional<Attributes::FieldDstIpV6>,
      std::optional<Attributes::FieldDstIpV6Word3>,
      std::optional<Attributes::FieldDstIpV6Word2>,
      std::optional<Attributes::FieldSrcIpV4>,
      std::optional<Attributes::FieldDstIpV4>,
      std::optional<Attributes::FieldSrcPort>,
      std::optional<Attributes::FieldOutPort>,
      std::optional<Attributes::FieldL4SrcPort>,
      std::optional<Attributes::FieldL4DstPort>,
      std::optional<Attributes::FieldIpProtocol>,
      std::optional<Attributes::FieldTcpFlags>,
      std::optional<Attributes::FieldIpFrag>,
      std::optional<Attributes::FieldIcmpV4Type>,
      std::optional<Attributes::FieldIcmpV4Code>,
      std::optional<Attributes::FieldIcmpV6Type>,
      std::optional<Attributes::FieldIcmpV6Code>,
      std::optional<Attributes::FieldDscp>,
      std::optional<Attributes::FieldTc>,
      std::optional<Attributes::FieldDstMac>,
      std::optional<Attributes::FieldIpType>,
      std::optional<Attributes::FieldTtl>,
      std::optional<Attributes::FieldMplsLabel0Ttl>,
      std::optional<Attributes::FieldFdbDstUserMeta>,
      std::optional<Attributes::FieldRouteDstUserMeta>,
      std::optional<Attributes::FieldNeighborDstUserMeta>,
      std::optional<Attributes::FieldEthertype>,
      std::optional<Attributes::FieldOuterVlanId>,
      std::optional<Attributes::FieldAclRangeType>,
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
      std::optional<Attributes::FieldBthOpcode>,
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
      std::optional<Attributes::FieldIpv6NextHeader>,
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
      std::optional<Attributes::UserDefinedFieldGroupMin0>,
      std::optional<Attributes::UserDefinedFieldGroupMin1>,
      std::optional<Attributes::UserDefinedFieldGroupMin2>,
      std::optional<Attributes::UserDefinedFieldGroupMin3>,
      std::optional<Attributes::UserDefinedFieldGroupMin4>,
#endif
      std::optional<Attributes::ActionPacketAction>,
      std::optional<Attributes::ActionRedirect>,
      std::optional<Attributes::ActionCounter>,
      std::optional<Attributes::ActionSetTC>,
      std::optional<Attributes::ActionSetDSCP>,
      std::optional<Attributes::ActionMirrorIngress>,
      std::optional<Attributes::ActionMirrorEgress>,
      std::optional<Attributes::ActionMacsecFlow>
#if !defined(TAJO_SDK)
      ,
      std::optional<Attributes::ActionSetUserTrap>
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      std::optional<Attributes::ActionSetArsObject>
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      ,
      std::optional<Attributes::ActionDisableArsForwarding>
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      std::optional<Attributes::ActionSetEcmpHashAlgorithm>,
      std::optional<Attributes::ActionL3SwitchCancel>,
      std::optional<Attributes::FieldRouteDestination>
#endif
#if (defined(TAJO_SDK_GTE_26_5) && !defined(TAJO_SDK_P200)) || \
    defined(BRCM_SAI_SDK_XGS_GTE_15_4_1)
      ,
      std::optional<Attributes::Label>
#endif
      ,
      std::optional<Attributes::FieldPortUserMeta>>;
};

SAI_ATTRIBUTE_NAME(AclEntry, TableId);
SAI_ATTRIBUTE_NAME(AclEntry, Priority);
SAI_ATTRIBUTE_NAME(AclEntry, Enabled);
SAI_ATTRIBUTE_NAME(AclEntry, FieldSrcIpV6);
SAI_ATTRIBUTE_NAME(AclEntry, FieldDstIpV6);
SAI_ATTRIBUTE_NAME(AclEntry, FieldDstIpV6Word3);
SAI_ATTRIBUTE_NAME(AclEntry, FieldDstIpV6Word2);
SAI_ATTRIBUTE_NAME(AclEntry, FieldSrcIpV4);
SAI_ATTRIBUTE_NAME(AclEntry, FieldDstIpV4);
SAI_ATTRIBUTE_NAME(AclEntry, FieldSrcPort);
SAI_ATTRIBUTE_NAME(AclEntry, FieldOutPort);
SAI_ATTRIBUTE_NAME(AclEntry, FieldL4SrcPort);
SAI_ATTRIBUTE_NAME(AclEntry, FieldL4DstPort);
SAI_ATTRIBUTE_NAME(AclEntry, FieldIpProtocol);
SAI_ATTRIBUTE_NAME(AclEntry, FieldTcpFlags);
SAI_ATTRIBUTE_NAME(AclEntry, FieldIpFrag);
SAI_ATTRIBUTE_NAME(AclEntry, FieldIcmpV4Type);
SAI_ATTRIBUTE_NAME(AclEntry, FieldIcmpV4Code);
SAI_ATTRIBUTE_NAME(AclEntry, FieldIcmpV6Type);
SAI_ATTRIBUTE_NAME(AclEntry, FieldIcmpV6Code);
SAI_ATTRIBUTE_NAME(AclEntry, FieldDscp);
SAI_ATTRIBUTE_NAME(AclEntry, FieldTc);
SAI_ATTRIBUTE_NAME(AclEntry, FieldDstMac);
SAI_ATTRIBUTE_NAME(AclEntry, FieldIpType);
SAI_ATTRIBUTE_NAME(AclEntry, FieldTtl);
SAI_ATTRIBUTE_NAME(AclEntry, FieldMplsLabel0Ttl);
SAI_ATTRIBUTE_NAME(AclEntry, FieldFdbDstUserMeta);
SAI_ATTRIBUTE_NAME(AclEntry, FieldRouteDstUserMeta);
SAI_ATTRIBUTE_NAME(AclEntry, FieldNeighborDstUserMeta);
SAI_ATTRIBUTE_NAME(AclEntry, FieldPortUserMeta);
SAI_ATTRIBUTE_NAME(AclEntry, FieldEthertype);
SAI_ATTRIBUTE_NAME(AclEntry, FieldOuterVlanId);
SAI_ATTRIBUTE_NAME(AclEntry, FieldAclRangeType);
#if !defined(TAJO_SDK) || defined(TAJO_SDK_GTE_24_8_3001)
SAI_ATTRIBUTE_NAME(AclEntry, FieldBthOpcode);
#endif
#if !defined(TAJO_SDK) && !defined(BRCM_SAI_SDK_XGS)
SAI_ATTRIBUTE_NAME(AclEntry, FieldIpv6NextHeader);
#endif
#if (                                                                  \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) ||                       \
     (defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_XGS))) && \
    !defined(TAJO_SDK))
SAI_ATTRIBUTE_NAME(AclEntry, UserDefinedFieldGroupMin0);
SAI_ATTRIBUTE_NAME(AclEntry, UserDefinedFieldGroupMin1);
SAI_ATTRIBUTE_NAME(AclEntry, UserDefinedFieldGroupMin2);
SAI_ATTRIBUTE_NAME(AclEntry, UserDefinedFieldGroupMin3);
SAI_ATTRIBUTE_NAME(AclEntry, UserDefinedFieldGroupMin4);
#endif
SAI_ATTRIBUTE_NAME(AclEntry, ActionPacketAction);
SAI_ATTRIBUTE_NAME(AclEntry, ActionRedirect);
SAI_ATTRIBUTE_NAME(AclEntry, ActionCounter);
SAI_ATTRIBUTE_NAME(AclEntry, ActionSetTC);
SAI_ATTRIBUTE_NAME(AclEntry, ActionSetDSCP);
SAI_ATTRIBUTE_NAME(AclEntry, ActionMirrorIngress);
SAI_ATTRIBUTE_NAME(AclEntry, ActionMirrorEgress);
SAI_ATTRIBUTE_NAME(AclEntry, ActionMacsecFlow);
#if !defined(TAJO_SDK)
SAI_ATTRIBUTE_NAME(AclEntry, ActionSetUserTrap);
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
SAI_ATTRIBUTE_NAME(AclEntry, ActionSetArsObject);
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
SAI_ATTRIBUTE_NAME(AclEntry, ActionDisableArsForwarding);
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
SAI_ATTRIBUTE_NAME(AclEntry, ActionSetEcmpHashAlgorithm);
SAI_ATTRIBUTE_NAME(AclEntry, ActionL3SwitchCancel);
SAI_ATTRIBUTE_NAME(AclEntry, FieldRouteDestination);
#endif
#if (defined(TAJO_SDK_GTE_26_5) && !defined(TAJO_SDK_P200)) || \
    defined(BRCM_SAI_SDK_XGS_GTE_15_4_1)
SAI_ATTRIBUTE_NAME(AclEntry, Label);
#endif

struct SaiAclCounterTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_ACL_COUNTER;
  using SaiApiT = AclApi;
  struct Attributes {
    using EnumType = sai_acl_counter_attr_t;

    using TableId =
        SaiAttribute<EnumType, SAI_ACL_COUNTER_ATTR_TABLE_ID, sai_object_id_t>;

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
    using Label =
        SaiAttribute<EnumType, SAI_ACL_COUNTER_ATTR_LABEL, SaiCharArray32>;
#endif

    using EnablePacketCount =
        SaiAttribute<EnumType, SAI_ACL_COUNTER_ATTR_ENABLE_PACKET_COUNT, bool>;
    using EnableByteCount =
        SaiAttribute<EnumType, SAI_ACL_COUNTER_ATTR_ENABLE_BYTE_COUNT, bool>;

    using CounterPackets =
        SaiAttribute<EnumType, SAI_ACL_COUNTER_ATTR_PACKETS, sai_uint64_t>;
    using CounterBytes = SaiAttribute<
        EnumType,
        SAI_ACL_COUNTER_ATTR_BYTES,
        sai_uint64_t,
        StdNullOptDefault<sai_uint64_t>>;
  };

  using AdapterKey = AclCounterSaiId;
  using AdapterHostKey = std::tuple<
      Attributes::TableId,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      std::optional<Attributes::Label>,
#endif
      std::optional<Attributes::EnablePacketCount>,
      std::optional<Attributes::EnableByteCount>>;

  using CreateAttributes = std::tuple<
      Attributes::TableId,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      std::optional<Attributes::Label>,
#endif
      std::optional<Attributes::EnablePacketCount>,
      std::optional<Attributes::EnableByteCount>,
      std::optional<Attributes::CounterPackets>,
      std::optional<Attributes::CounterBytes>>;
};

SAI_ATTRIBUTE_NAME(AclCounter, TableId);
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
SAI_ATTRIBUTE_NAME(AclCounter, Label);
#endif
SAI_ATTRIBUTE_NAME(AclCounter, EnablePacketCount);
SAI_ATTRIBUTE_NAME(AclCounter, EnableByteCount);
SAI_ATTRIBUTE_NAME(AclCounter, CounterPackets);
SAI_ATTRIBUTE_NAME(AclCounter, CounterBytes);

struct SaiAclRangeTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_ACL_RANGE;
  using SaiApiT = AclApi;
  struct Attributes {
    using EnumType = sai_acl_range_attr_t;

    using Type = SaiAttribute<EnumType, SAI_ACL_RANGE_ATTR_TYPE, sai_int32_t>;
    using Limit =
        SaiAttribute<EnumType, SAI_ACL_RANGE_ATTR_LIMIT, sai_u32_range_t>;
  };

  using AdapterKey = AclRangeSaiId;
  using AdapterHostKey = std::tuple<Attributes::Type, Attributes::Limit>;
  using CreateAttributes = std::tuple<Attributes::Type, Attributes::Limit>;
};

SAI_ATTRIBUTE_NAME(AclRange, Type);
SAI_ATTRIBUTE_NAME(AclRange, Limit);

class AclApi : public SaiApi<AclApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_ACL;
  AclApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for Acl api");
  }
  AclApi(const AclApi& other) = delete;

 private:
  sai_status_t _create(
      AclTableGroupSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_acl_table_group(
        rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _create(
      AclTableGroupMemberSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_acl_table_group_member(
        rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _create(
      AclTableSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_acl_table(rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _create(
      AclEntrySaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_acl_entry(rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _create(
      AclCounterSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_acl_counter(rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _create(
      AclRangeSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_acl_range(rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _remove(AclTableGroupSaiId id) const {
    return api_->remove_acl_table_group(id);
  }

  sai_status_t _remove(AclTableGroupMemberSaiId id) const {
    return api_->remove_acl_table_group_member(id);
  }

  sai_status_t _remove(AclTableSaiId id) const {
    return api_->remove_acl_table(id);
  }

  sai_status_t _remove(AclEntrySaiId id) const {
    return api_->remove_acl_entry(id);
  }

  sai_status_t _remove(AclCounterSaiId id) const {
    return api_->remove_acl_counter(id);
  }

  sai_status_t _remove(AclRangeSaiId id) const {
    return api_->remove_acl_range(id);
  }

  sai_status_t _getAttribute(AclTableGroupSaiId id, sai_attribute_t* attr)
      const {
    return api_->get_acl_table_group_attribute(id, 1, attr);
  }

  sai_status_t _getAttribute(AclTableGroupMemberSaiId id, sai_attribute_t* attr)
      const {
    return api_->get_acl_table_group_member_attribute(id, 1, attr);
  }

  sai_status_t _getAttribute(AclTableSaiId id, sai_attribute_t* attr) const {
    return api_->get_acl_table_attribute(id, 1, attr);
  }

  sai_status_t _getAttribute(AclEntrySaiId id, sai_attribute_t* attr) const {
    return api_->get_acl_entry_attribute(id, 1, attr);
  }

  sai_status_t _getAttribute(AclCounterSaiId id, sai_attribute_t* attr) const {
    return api_->get_acl_counter_attribute(id, 1, attr);
  }

  sai_status_t _getAttribute(AclRangeSaiId id, sai_attribute_t* attr) const {
    return api_->get_acl_range_attribute(id, 1, attr);
  }

  sai_status_t _setAttribute(AclTableGroupSaiId id, const sai_attribute_t* attr)
      const {
    return api_->set_acl_table_group_attribute(id, attr);
  }

  sai_status_t _setAttribute(
      AclTableGroupMemberSaiId id,
      const sai_attribute_t* attr) const {
    return api_->set_acl_table_group_member_attribute(id, attr);
  }

  sai_status_t _setAttribute(AclTableSaiId id, const sai_attribute_t* attr)
      const {
    return api_->set_acl_table_attribute(id, attr);
  }

  sai_status_t _setAttribute(AclEntrySaiId id, const sai_attribute_t* attr)
      const {
    return api_->set_acl_entry_attribute(id, attr);
  }

  sai_status_t _setAttribute(AclCounterSaiId id, const sai_attribute_t* attr)
      const {
    return api_->set_acl_counter_attribute(id, attr);
  }

  sai_status_t _setAttribute(AclRangeSaiId id, const sai_attribute_t* attr)
      const {
    return api_->set_acl_range_attribute(id, attr);
  }

  sai_acl_api_t* api_;
  friend class SaiApi<AclApi>;
};

} // namespace facebook::fboss
