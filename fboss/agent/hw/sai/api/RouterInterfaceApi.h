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
#include "fboss/agent/hw/sai/api/Types.h"

#include <folly/logging/xlog.h>

#include <optional>
#include <tuple>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class RouterInterfaceApi;

namespace detail {
template <sai_router_interface_type_t type>
struct RouterInterfaceAttributesTypes;

template <>
struct RouterInterfaceAttributesTypes<SAI_ROUTER_INTERFACE_TYPE_VLAN> {
  using VlanId = SaiAttribute<
      sai_router_interface_attr_t,
      SAI_ROUTER_INTERFACE_ATTR_VLAN_ID,
      SaiObjectIdT>;
  using PortId = void;
};

template <>
struct RouterInterfaceAttributesTypes<SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER> {
  using VlanId = void;
  using PortId = void;
};

template <>
struct RouterInterfaceAttributesTypes<SAI_ROUTER_INTERFACE_TYPE_PORT> {
  using VlanId = void;
  using PortId = SaiAttribute<
      sai_router_interface_attr_t,
      SAI_ROUTER_INTERFACE_ATTR_PORT_ID,
      SaiObjectIdT>;
};

template <typename Attributes, sai_router_interface_type_t type>
struct RouterInterfaceTraitsAttributes;

template <typename Attributes>
struct RouterInterfaceTraitsAttributes<
    Attributes,
    SAI_ROUTER_INTERFACE_TYPE_VLAN> {
  using CreateAttributes = std::tuple<
      typename Attributes::VirtualRouterId,
      typename Attributes::Type,
      typename Attributes::VlanId,
      std::optional<typename Attributes::SrcMac>,
      std::optional<typename Attributes::Mtu>
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      ,
      std::optional<typename Attributes::AdminMplsState>
#endif
      >;
  using AdapterHostKey = std::
      tuple<typename Attributes::VirtualRouterId, typename Attributes::VlanId>;
};

template <typename Attributes>
struct RouterInterfaceTraitsAttributes<
    Attributes,
    SAI_ROUTER_INTERFACE_TYPE_PORT> {
  using CreateAttributes = std::tuple<
      typename Attributes::VirtualRouterId,
      typename Attributes::Type,
      typename Attributes::PortId,
      std::optional<typename Attributes::SrcMac>,
      std::optional<typename Attributes::Mtu>
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      ,
      std::optional<typename Attributes::AdminMplsState>
#endif
      >;
  using AdapterHostKey = std::
      tuple<typename Attributes::VirtualRouterId, typename Attributes::PortId>;
};

template <typename Attributes>
struct RouterInterfaceTraitsAttributes<
    Attributes,
    SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER> {
  using CreateAttributes = std::
      tuple<typename Attributes::VirtualRouterId, typename Attributes::Type>;
  using AdapterHostKey = std::tuple<typename Attributes::VirtualRouterId>;
};
} // namespace detail

template <sai_router_interface_type_t type>
struct SaiRouterInterfaceTraitsT {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_ROUTER_INTERFACE;
  using SaiApiT = RouterInterfaceApi;
  struct Attributes {
    using EnumType = sai_router_interface_attr_t;
    using SrcMac = SaiAttribute<
        EnumType,
        SAI_ROUTER_INTERFACE_ATTR_SRC_MAC_ADDRESS,
        folly::MacAddress>;
    using Type =
        SaiAttribute<EnumType, SAI_ROUTER_INTERFACE_ATTR_TYPE, sai_int32_t>;
    using VirtualRouterId = SaiAttribute<
        EnumType,
        SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID,
        SaiObjectIdT>;
    using VlanId =
        typename detail::RouterInterfaceAttributesTypes<type>::VlanId;
    using PortId =
        typename detail::RouterInterfaceAttributesTypes<type>::PortId;
    using Mtu = SaiAttribute<
        EnumType,
        SAI_ROUTER_INTERFACE_ATTR_MTU,
        sai_uint32_t,
        SaiIntDefault<sai_int32_t>>;
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
    using AdminMplsState = SaiAttribute<
        EnumType,
        SAI_ROUTER_INTERFACE_ATTR_ADMIN_MPLS_STATE,
        bool,
        SaiBoolDefaultFalse>;
#endif
  };
  using AdapterKey = RouterInterfaceSaiId;
  using AdapterHostKey = typename detail::
      RouterInterfaceTraitsAttributes<Attributes, type>::AdapterHostKey;
  using CreateAttributes = typename detail::
      RouterInterfaceTraitsAttributes<Attributes, type>::CreateAttributes;
  using ConditionAttributes = std::tuple<typename Attributes::Type>;
  inline const static ConditionAttributes kConditionAttributes{type};

  static constexpr std::array<sai_stat_id_t, 16> CounterIdsToRead = {
      SAI_ROUTER_INTERFACE_STAT_IN_OCTETS,
      SAI_ROUTER_INTERFACE_STAT_IN_PACKETS,
      SAI_ROUTER_INTERFACE_STAT_OUT_OCTETS,
      SAI_ROUTER_INTERFACE_STAT_OUT_PACKETS,
      SAI_ROUTER_INTERFACE_STAT_IN_ERROR_OCTETS,
      SAI_ROUTER_INTERFACE_STAT_IN_ERROR_PACKETS,
      SAI_ROUTER_INTERFACE_STAT_OUT_ERROR_OCTETS,
      SAI_ROUTER_INTERFACE_STAT_OUT_ERROR_PACKETS,
  };
  static constexpr std::array<sai_stat_id_t, 16> CounterIdsToReadAndClear = {};
};

using SaiVlanRouterInterfaceTraits =
    SaiRouterInterfaceTraitsT<SAI_ROUTER_INTERFACE_TYPE_VLAN>;
using SaiMplsRouterInterfaceTraits =
    SaiRouterInterfaceTraitsT<SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER>;
using SaiPortRouterInterfaceTraits =
    SaiRouterInterfaceTraitsT<SAI_ROUTER_INTERFACE_TYPE_PORT>;

template <>
struct SaiObjectHasStats<SaiVlanRouterInterfaceTraits> : public std::true_type {
};

template <>
struct SaiObjectHasStats<SaiMplsRouterInterfaceTraits> : public std::true_type {
};

template <>
struct SaiObjectHasStats<SaiPortRouterInterfaceTraits> : public std::true_type {
};

template <>
struct SaiObjectHasConditionalAttributes<SaiVlanRouterInterfaceTraits>
    : public std::true_type {};
template <>
struct SaiObjectHasConditionalAttributes<SaiPortRouterInterfaceTraits>
    : public std::true_type {};
template <>
struct SaiObjectHasConditionalAttributes<SaiMplsRouterInterfaceTraits>
    : public std::true_type {};

using SaiRouterInterfaceTraits = ConditionObjectTraits<
    SaiVlanRouterInterfaceTraits,
    SaiPortRouterInterfaceTraits,
    SaiMplsRouterInterfaceTraits>;
using SaiRouterInterfaceAdaptertKey =
    typename SaiRouterInterfaceTraits::AdapterKey<RouterInterfaceSaiId>;

SAI_ATTRIBUTE_NAME(VlanRouterInterface, SrcMac)
SAI_ATTRIBUTE_NAME(VlanRouterInterface, Type)
SAI_ATTRIBUTE_NAME(VlanRouterInterface, VirtualRouterId)
SAI_ATTRIBUTE_NAME(VlanRouterInterface, VlanId)
SAI_ATTRIBUTE_NAME(PortRouterInterface, PortId)
SAI_ATTRIBUTE_NAME(VlanRouterInterface, Mtu)
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
// AdminMplsState resolves to the same SaiAttribute type for all router
// interface trait types, so it only needs a single AttributeName definition.
SAI_ATTRIBUTE_NAME(VlanRouterInterface, AdminMplsState)
#endif

class RouterInterfaceApi : public SaiApi<RouterInterfaceApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_ROUTER_INTERFACE;
  RouterInterfaceApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(
        status, ApiType, "Failed to query for router interface api");
  }
  RouterInterfaceApi(const RouterInterfaceApi& other) = delete;

 private:
  sai_status_t _create(
      RouterInterfaceSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_router_interface(
        rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(RouterInterfaceSaiId router_interface_id) const {
    return api_->remove_router_interface(router_interface_id);
  }
  sai_status_t _getAttribute(RouterInterfaceSaiId id, sai_attribute_t* attr)
      const {
    return api_->get_router_interface_attribute(id, 1, attr);
  }
  sai_status_t _setAttribute(
      RouterInterfaceSaiId key,
      const sai_attribute_t* attr) const {
    return api_->set_router_interface_attribute(key, attr);
  }

  sai_status_t _getStats(
      RouterInterfaceSaiId key,
      uint32_t num_of_counters,
      const sai_stat_id_t* counter_ids,
      sai_stats_mode_t mode,
      uint64_t* counters) const {
    /*
     * Unfortunately not all vendors implement the ext stats api.
     * ext stats api matter only for modes other than the (default)
     * SAI_STATS_MODE_READ. So play defensive and call ext mode only
     * when called with something other than default
     */
    return mode == SAI_STATS_MODE_READ
        ? api_->get_router_interface_stats(
              key, num_of_counters, counter_ids, counters)
        : api_->get_router_interface_stats_ext(
              key, num_of_counters, counter_ids, mode, counters);
  }

  sai_status_t _clearStats(
      RouterInterfaceSaiId key,
      uint32_t num_of_counters,
      const sai_stat_id_t* counter_ids) const {
    return api_->clear_router_interface_stats(
        key, num_of_counters, counter_ids);
  }

  sai_router_interface_api_t* api_;
  friend class SaiApi<RouterInterfaceApi>;
};

} // namespace facebook::fboss
