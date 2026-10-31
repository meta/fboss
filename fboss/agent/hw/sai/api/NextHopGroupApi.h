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

#include "fboss/agent/hw/sai/api/NextHopApi.h"
#include "fboss/agent/hw/sai/api/SaiApi.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiAttributeDataTypes.h"
#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Types.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include <set>
#include <tuple>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class NextHopGroupApi;

namespace detail {
using NextHopMemberKey =
    std::pair<SaiNextHopTraits::AdapterHostKey, sai_uint32_t>; // weight

/* For NSF, due to the limited availability of ARS supported ECMP groups, the
 * ecmp resource manager does a reclaim of ARS groups when they become
 * available during a state/config update. Since the SAI SDK does not support
 * directly converting a non ARS ECMP group, the route update will set the new
 * ARS mode within SaiNextHopGroupKey.
 * There is no change to the nhop set though.
 *
 * Because the key changed, refOrEmplace operation would trigger a
 * new NHG group reference with the same nhop set as the existing ECMP.
 *
 * Having the ARS mode as part of the adapter host key allows SAI store to
 * create a new ECMP group within the ARS range while a non ARS ECMP still
 * exists with the same nhop set.
 */
struct NextHopGroupAdapterHostKey {
  std::set<NextHopMemberKey> nhopMemberSet = {};
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  sai_uint32_t mode = SAI_ARS_MODE_FIXED; // switching mode
#else
  sai_uint32_t mode = 0;
#endif
  // SAI group type (e.g. SAI_NEXT_HOP_GROUP_TYPE_ECMP vs _HW_PROTECTION). Part
  // of the identity so a protection group is never confused with an ECMP (or
  // hierarchical ECMP) group that has the same members. Defaults to ECMP so
  // plain ECMP groups need not set it.
  sai_int32_t groupType = SAI_NEXT_HOP_GROUP_TYPE_ECMP;
  // Hierarchical ECMP / protection: value-based identities of the child next
  // hop groups that are members of this group (for a protection group this is
  // the backup/standby group). Each child is represented by its OWN adapter
  // host key (recursively), NOT its OID, so the parent's identity is
  // warmboot-stable. Empty for a flat/leaf group. A std::set keeps it sorted
  // and duplicate-free, so equality and hashing are canonical regardless of
  // child insertion order. No per-child weight: weights live on next hops, not
  // next hop groups.
  std::set<NextHopGroupAdapterHostKey> childNextHopGroups = {};
  // Hierarchy level of this group; 0 = leaf/flat. Part of the identity so a
  // nested group cannot alias a flat group with the same members. Not populated
  // yet -- reserved for upcoming hierarchical ECMP support.
  sai_uint32_t level = 0;
  bool operator==(const NextHopGroupAdapterHostKey& other) const {
    return nhopMemberSet == other.nhopMemberSet && mode == other.mode &&
        groupType == other.groupType &&
        childNextHopGroups == other.childNextHopGroups && level == other.level;
  }
  bool operator!=(const NextHopGroupAdapterHostKey& other) const {
    return !(*this == other);
  }
  bool operator<(const NextHopGroupAdapterHostKey& other) const {
    return std::tie(nhopMemberSet, mode, groupType, childNextHopGroups, level) <
        std::tie(
               other.nhopMemberSet,
               other.mode,
               other.groupType,
               other.childNextHopGroups,
               other.level);
  }
  friend struct fmt::formatter<NextHopGroupAdapterHostKey>;
};

} // namespace detail

struct SaiNextHopGroupTraits {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP;
  using SaiApiT = NextHopGroupApi;
  struct Attributes {
    using EnumType = sai_next_hop_group_attr_t;
    using NextHopMemberList = SaiAttribute<
        EnumType,
        SAI_NEXT_HOP_GROUP_ATTR_NEXT_HOP_MEMBER_LIST,
        std::vector<sai_object_id_t>>;
    using Type =
        SaiAttribute<EnumType, SAI_NEXT_HOP_GROUP_ATTR_TYPE, sai_int32_t>;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    using ArsObjectId = SaiAttribute<
        EnumType,
        SAI_NEXT_HOP_GROUP_ATTR_ARS_OBJECT_ID,
        sai_object_id_t,
        SaiObjectIdDefault>;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    using HashAlgorithm = SaiAttribute<
        EnumType,
        SAI_NEXT_HOP_GROUP_ATTR_HASH_ALGORITHM,
        sai_int32_t,
        StdNullOptDefault<sai_int32_t>>;
    using HierarchicalNextHop = SaiAttribute<
        EnumType,
        SAI_NEXT_HOP_GROUP_ATTR_HIERARCHICAL_NEXTHOP,
        bool,
        StdNullOptDefault<bool>>;
#endif
    struct AttributeArsNextHopGroupMetaData {
      std::optional<sai_attr_id_t> operator()();
    };
    using ArsNextHopGroupMetaData = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeArsNextHopGroupMetaData,
        SaiIntDefault<sai_uint32_t>>;
    struct AttributeSplitHorizonEnable {
      std::optional<sai_attr_id_t> operator()();
    };
    using SplitHorizonEnable = SaiExtensionAttribute<
        bool,
        AttributeSplitHorizonEnable,
        StdNullOptDefault<bool>>;

    struct AttributeArsFailPktCount {
      std::optional<sai_attr_id_t> operator()();
    };
    using ArsFailPktCount = SaiExtensionAttribute<
        sai_uint64_t,
        AttributeArsFailPktCount,
        SaiIntDefault<sai_uint64_t>>;

    struct AttributeArsPortReassignCount {
      std::optional<sai_attr_id_t> operator()();
    };
    using ArsPortReassignCount = SaiExtensionAttribute<
        sai_uint64_t,
        AttributeArsPortReassignCount,
        SaiIntDefault<sai_uint64_t>>;
  };

  using AdapterKey = NextHopGroupSaiId;
  using AdapterHostKey = typename detail::NextHopGroupAdapterHostKey;
  using CreateAttributes = std::tuple<
      Attributes::Type
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      ,
      std::optional<Attributes::ArsObjectId>
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      std::optional<Attributes::HashAlgorithm>,
      std::optional<Attributes::HierarchicalNextHop>
#endif
      ,
      std::optional<Attributes::SplitHorizonEnable>>;
};

SAI_ATTRIBUTE_NAME(NextHopGroup, NextHopMemberList)
SAI_ATTRIBUTE_NAME(NextHopGroup, Type)
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
SAI_ATTRIBUTE_NAME(NextHopGroup, ArsObjectId)
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
SAI_ATTRIBUTE_NAME(NextHopGroup, HashAlgorithm)
SAI_ATTRIBUTE_NAME(NextHopGroup, HierarchicalNextHop)
#endif
SAI_ATTRIBUTE_NAME(NextHopGroup, ArsNextHopGroupMetaData)
SAI_ATTRIBUTE_NAME(NextHopGroup, SplitHorizonEnable)
SAI_ATTRIBUTE_NAME(NextHopGroup, ArsFailPktCount)
SAI_ATTRIBUTE_NAME(NextHopGroup, ArsPortReassignCount)

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
// HierarchicalNextHop and HashAlgorithm are CREATE_ONLY attributes, so
// they should not be set after create.
template <>
struct IsCreateOnlyAttribute<SaiNextHopGroupTraits::Attributes::HashAlgorithm>
    : public std::true_type {};
template <>
struct IsCreateOnlyAttribute<
    SaiNextHopGroupTraits::Attributes::HierarchicalNextHop>
    : public std::true_type {};
#endif

struct SaiNextHopGroupMemberTraits {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER;
  using SaiApiT = NextHopGroupApi;
  struct Attributes {
    using EnumType = sai_next_hop_group_member_attr_t;
    using NextHopId = SaiAttribute<
        EnumType,
        SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID,
        SaiObjectIdT>;
    using NextHopGroupId = SaiAttribute<
        EnumType,
        SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID,
        SaiObjectIdT>;
    using Weight = SaiAttribute<
        EnumType,
        SAI_NEXT_HOP_GROUP_MEMBER_ATTR_WEIGHT,
        sai_uint32_t,
        SaiInt1Default>;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    // Protection-group member attributes (only valid when the owning group is
    // SAI_NEXT_HOP_GROUP_TYPE_PROTECTION). ConfiguredRole selects PRIMARY vs
    // STANDBY; MonitoredObject is the PORT/LAG whose state drives switchover.
    using ConfiguredRole = SaiAttribute<
        EnumType,
        SAI_NEXT_HOP_GROUP_MEMBER_ATTR_CONFIGURED_ROLE,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
    using MonitoredObject = SaiAttribute<
        EnumType,
        SAI_NEXT_HOP_GROUP_MEMBER_ATTR_MONITORED_OBJECT,
        SaiObjectIdT,
        SaiObjectIdDefault>;
#endif
  };

  using AdapterKey = NextHopGroupMemberSaiId;
  using AdapterHostKey =
      std::tuple<Attributes::NextHopGroupId, Attributes::NextHopId>;
  using CreateAttributes = std::tuple<
      Attributes::NextHopGroupId,
      Attributes::NextHopId,
      std::optional<Attributes::Weight>
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      std::optional<Attributes::ConfiguredRole>,
      std::optional<Attributes::MonitoredObject>
#endif
      >;
};

SAI_ATTRIBUTE_NAME(NextHopGroupMember, NextHopGroupId)
SAI_ATTRIBUTE_NAME(NextHopGroupMember, NextHopId)
SAI_ATTRIBUTE_NAME(NextHopGroupMember, Weight)
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
SAI_ATTRIBUTE_NAME(NextHopGroupMember, ConfiguredRole)
SAI_ATTRIBUTE_NAME(NextHopGroupMember, MonitoredObject)
#endif

class NextHopGroupApi : public SaiApi<NextHopGroupApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_NEXT_HOP_GROUP;
  NextHopGroupApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for next hop group api");
  }
  NextHopGroupApi(const NextHopGroupApi& other) = delete;
  NextHopGroupApi& operator=(const NextHopGroupApi& other) = delete;

 private:
  sai_status_t _create(
      NextHopGroupSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_next_hop_group(
        rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _create(
      NextHopGroupMemberSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_next_hop_group_member(
        rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(NextHopGroupSaiId next_hop_group_id) const {
    return api_->remove_next_hop_group(next_hop_group_id);
  }
  sai_status_t _remove(NextHopGroupMemberSaiId next_hop_group_id) const {
    return api_->remove_next_hop_group_member(next_hop_group_id);
  }
  sai_status_t _getAttribute(NextHopGroupSaiId id, sai_attribute_t* attr)
      const {
    return api_->get_next_hop_group_attribute(id, 1, attr);
  }
  sai_status_t _getAttribute(NextHopGroupMemberSaiId id, sai_attribute_t* attr)
      const {
    return api_->get_next_hop_group_member_attribute(id, 1, attr);
  }
  sai_status_t _setAttribute(NextHopGroupSaiId id, const sai_attribute_t* attr)
      const {
    return api_->set_next_hop_group_attribute(id, attr);
  }
  sai_status_t _setAttribute(
      NextHopGroupMemberSaiId id,
      const sai_attribute_t* attr) const {
    return api_->set_next_hop_group_member_attribute(id, attr);
  }
  sai_status_t _bulkSetAttribute(
      NextHopGroupMemberSaiId* ids,
      const sai_attribute_t* attr,
      sai_status_t* retStatus,
      size_t objectCount) const {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    sai_object_id_t rawIds[objectCount];
    for (auto idx = 0; idx < objectCount; idx++) {
      rawIds[idx] = *rawSaiId(&ids[idx]);
    }
    return api_->set_next_hop_group_members_attribute(
        objectCount,
        rawIds,
        attr,
        SAI_BULK_OP_ERROR_MODE_STOP_ON_ERROR,
        retStatus);
#else
    return SAI_STATUS_NOT_SUPPORTED;
#endif
  }

  sai_status_t _bulkCreate(
      sai_object_id_t* ids,
      sai_status_t* retStatus,
      sai_object_id_t switch_id,
      uint32_t* attrCount,
      size_t objectCount,
      const sai_attribute_t** attr) const {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    return api_->create_next_hop_group_members(
        switch_id,
        objectCount,
        attrCount,
        attr,
        SAI_BULK_OP_ERROR_MODE_STOP_ON_ERROR,
        ids,
        retStatus);
#else
    return SAI_STATUS_NOT_SUPPORTED;
#endif
  }

  sai_status_t _bulkRemove(
      size_t objectCount,
      const NextHopGroupMemberSaiId* object_id,
      sai_status_t* retStatus) const {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    const unsigned long* objects =
        reinterpret_cast<const unsigned long*>(object_id);
    return api_->remove_next_hop_group_members(
        objectCount, objects, SAI_BULK_OP_ERROR_MODE_STOP_ON_ERROR, retStatus);
#else
    return SAI_STATUS_NOT_SUPPORTED;
#endif
  }

  sai_next_hop_group_api_t* api_;
  friend class SaiApi<NextHopGroupApi>;
};

} // namespace facebook::fboss

namespace fmt {
template <>
struct formatter<facebook::fboss::detail::NextHopGroupAdapterHostKey> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) const {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(
      const facebook::fboss::detail::NextHopGroupAdapterHostKey& ahk,
      FormatContext& ctx) const {
    // Self-recursive lambda so child groups (and grandchildren) are rendered
    // without calling fmt::format on this key type -- which would trip fmt's
    // formattable_const check while this very formatter is being instantiated.
    auto render =
        [](const facebook::fboss::detail::NextHopGroupAdapterHostKey& key,
           auto&& self) -> std::string {
      std::string childGroups = "[";
      bool first = true;
      for (const auto& child : key.childNextHopGroups) {
        if (!first) {
          childGroups += ", ";
        }
        first = false;
        childGroups += fmt::format("{{{}}}", self(child, self));
      }
      childGroups += "]";
      return fmt::format(
          "{}, mode={}, groupType={}, level={}, childGroups={}",
          key.nhopMemberSet,
          key.mode,
          key.groupType,
          key.level,
          childGroups);
    };
    return format_to(ctx.out(), "{}", render(ahk, render));
  }
};
} // namespace fmt

namespace std {
template <>
struct hash<facebook::fboss::SaiNextHopGroupTraits::AdapterHostKey> {
  size_t operator()(
      const facebook::fboss::SaiNextHopGroupTraits::AdapterHostKey& k) const;
};
} // namespace std
