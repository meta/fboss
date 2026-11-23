// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/hw/sai/api/SaiApi.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiAttributeDataTypes.h"
#include "fboss/agent/hw/sai/api/TunnelApi.h"
#include "fboss/agent/hw/sai/api/Types.h"

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

// SAI tunnel maps (SAI_OBJECT_TYPE_TUNNEL_MAP) and tunnel map entries
// (SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY) are part of the SAI tunnel API
// (SAI_API_TUNNEL), so these traits plug into TunnelApi. Only the
// VNI <-> virtual router ID map types used by VXLAN tunnels are modeled.

struct SaiTunnelMapTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_TUNNEL_MAP;
  using SaiApiT = TunnelApi;
  struct Attributes {
    using EnumType = sai_tunnel_map_attr_t;
    using Type = SaiAttribute<EnumType, SAI_TUNNEL_MAP_ATTR_TYPE, sai_int32_t>;
  };
  using AdapterKey = TunnelMapSaiId;
  // A map's only create-time attribute is its type, so the store holds at
  // most one map per type. VXLAN uses one map per direction.
  using AdapterHostKey = std::tuple<Attributes::Type>;
  using CreateAttributes = AdapterHostKey;
};

SAI_ATTRIBUTE_NAME(TunnelMap, Type);

namespace detail {

template <sai_tunnel_map_type_t type>
struct SaiTunnelMapEntryTraits;

template <>
struct SaiTunnelMapEntryTraits<SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID> {
  using EnumType = sai_tunnel_map_entry_attr_t;
  struct Attributes {
    using TunnelMapType = SaiAttribute<
        EnumType,
        SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE,
        sai_int32_t>;
    using TunnelMap = SaiAttribute<
        EnumType,
        SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP,
        SaiObjectIdT>;
    using VniIdKey = SaiAttribute<
        EnumType,
        SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_KEY,
        sai_uint32_t>;
    using VniIdValue = void;
    using VirtualRouterIdKey = void;
    using VirtualRouterIdValue = SaiAttribute<
        EnumType,
        SAI_TUNNEL_MAP_ENTRY_ATTR_VIRTUAL_ROUTER_ID_VALUE,
        SaiObjectIdT>;
  };
  using AdapterKey = TunnelMapEntrySaiId;
  using CreateAttributes = std::tuple<
      Attributes::TunnelMapType,
      Attributes::TunnelMap,
      Attributes::VniIdKey,
      Attributes::VirtualRouterIdValue>;
  using AdapterHostKey = CreateAttributes;
};

template <>
struct SaiTunnelMapEntryTraits<SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI> {
  using EnumType = sai_tunnel_map_entry_attr_t;
  struct Attributes {
    using TunnelMapType = SaiAttribute<
        EnumType,
        SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE,
        sai_int32_t>;
    using TunnelMap = SaiAttribute<
        EnumType,
        SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP,
        SaiObjectIdT>;
    using VniIdKey = void;
    using VniIdValue = SaiAttribute<
        EnumType,
        SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_VALUE,
        sai_uint32_t>;
    using VirtualRouterIdKey = SaiAttribute<
        EnumType,
        SAI_TUNNEL_MAP_ENTRY_ATTR_VIRTUAL_ROUTER_ID_KEY,
        SaiObjectIdT>;
    using VirtualRouterIdValue = void;
  };
  using AdapterKey = TunnelMapEntrySaiId;
  using CreateAttributes = std::tuple<
      Attributes::TunnelMapType,
      Attributes::TunnelMap,
      Attributes::VirtualRouterIdKey,
      Attributes::VniIdValue>;
  using AdapterHostKey = CreateAttributes;
};

} // namespace detail

template <sai_tunnel_map_type_t type>
struct SaiTunnelMapEntryTraitsT {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY;
  using SaiApiT = TunnelApi;
  using AdapterKey = TunnelMapEntrySaiId;
  using Attributes = typename detail::SaiTunnelMapEntryTraits<type>::Attributes;
  using CreateAttributes =
      typename detail::SaiTunnelMapEntryTraits<type>::CreateAttributes;
  using AdapterHostKey =
      typename detail::SaiTunnelMapEntryTraits<type>::AdapterHostKey;
  using ConditionAttributes =
      std::tuple<typename detail::SaiTunnelMapEntryTraits<
          type>::Attributes::TunnelMapType>;
  inline const static ConditionAttributes kConditionAttributes{type};
};

using SaiVniToVrTunnelMapEntryTraits =
    SaiTunnelMapEntryTraitsT<SAI_TUNNEL_MAP_TYPE_VNI_TO_VIRTUAL_ROUTER_ID>;
using SaiVrToVniTunnelMapEntryTraits =
    SaiTunnelMapEntryTraitsT<SAI_TUNNEL_MAP_TYPE_VIRTUAL_ROUTER_ID_TO_VNI>;

template <>
struct SaiObjectHasConditionalAttributes<SaiVniToVrTunnelMapEntryTraits>
    : public std::true_type {};

template <>
struct SaiObjectHasConditionalAttributes<SaiVrToVniTunnelMapEntryTraits>
    : public std::true_type {};

using SaiTunnelMapEntryTraits = ConditionObjectTraits<
    SaiVniToVrTunnelMapEntryTraits,
    SaiVrToVniTunnelMapEntryTraits>;
using SaiTunnelMapEntryAdapterHostKey =
    typename SaiTunnelMapEntryTraits::AdapterHostKey;
using SaiTunnelMapEntryAdapterKey =
    typename SaiTunnelMapEntryTraits::AdapterKey<TunnelMapEntrySaiId>;

SAI_ATTRIBUTE_NAME(VniToVrTunnelMapEntry, TunnelMapType);
SAI_ATTRIBUTE_NAME(VniToVrTunnelMapEntry, TunnelMap);
SAI_ATTRIBUTE_NAME(VniToVrTunnelMapEntry, VniIdKey);
SAI_ATTRIBUTE_NAME(VniToVrTunnelMapEntry, VirtualRouterIdValue);
// TunnelMapType and TunnelMap are identical types in both entry traits, so
// only the direction-specific attributes need names here.
SAI_ATTRIBUTE_NAME(VrToVniTunnelMapEntry, VirtualRouterIdKey);
SAI_ATTRIBUTE_NAME(VrToVniTunnelMapEntry, VniIdValue);

} // namespace facebook::fboss
