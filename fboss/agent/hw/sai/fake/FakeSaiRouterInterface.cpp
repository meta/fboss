/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/fake/FakeSaiRouterInterface.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <optional>

using facebook::fboss::FakeRouterInterface;
using facebook::fboss::FakeSai;

sai_status_t create_router_interface_fn(
    sai_object_id_t* router_interface_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<int32_t> type;
  std::optional<sai_object_id_t> portOrVlanId;
  std::optional<sai_object_id_t> vrId;
  std::optional<folly::MacAddress> mac;
  std::optional<sai_uint32_t> mtu;
  std::optional<bool> adminMplsState;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ROUTER_INTERFACE_ATTR_SRC_MAC_ADDRESS:
        mac = folly::MacAddress::fromBinary(
            folly::ByteRange(
                std::begin(attr_list[i].value.mac),
                std::end(attr_list[i].value.mac)));
        break;
      case SAI_ROUTER_INTERFACE_ATTR_TYPE:
        type = attr_list[i].value.s32;
        break;
      case SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID:
        vrId = attr_list[i].value.oid;
        break;
      case SAI_ROUTER_INTERFACE_ATTR_VLAN_ID:
      case SAI_ROUTER_INTERFACE_ATTR_PORT_ID:
        portOrVlanId = attr_list[i].value.oid;
        break;
      case SAI_ROUTER_INTERFACE_ATTR_MTU:
        mtu = attr_list[i].value.u32;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      case SAI_ROUTER_INTERFACE_ATTR_ADMIN_MPLS_STATE:
        adminMplsState = attr_list[i].value.booldata;
        break;
#endif
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  if (!vrId || !type) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  switch (type.value()) {
    case SAI_ROUTER_INTERFACE_TYPE_VLAN:
    case SAI_ROUTER_INTERFACE_TYPE_PORT:
      *router_interface_id =
          fs->routeInterfaceManager.create(FakeRouterInterface(
              vrId.value(), portOrVlanId.value(), type.value()));
      break;
    case SAI_ROUTER_INTERFACE_TYPE_MPLS_ROUTER:
      *router_interface_id =
          fs->routeInterfaceManager.create(FakeRouterInterface(vrId.value()));
      break;
    default:
      return SAI_STATUS_NOT_SUPPORTED;
  }

  if (mac) {
    auto& ri = fs->routeInterfaceManager.get(*router_interface_id);
    ri.setSrcMac(mac.value());
  }
  if (mtu) {
    auto& ri = fs->routeInterfaceManager.get(*router_interface_id);
    ri.mtu = mtu.value();
  }
  if (adminMplsState) {
    auto& ri = fs->routeInterfaceManager.get(*router_interface_id);
    ri.adminMplsState = adminMplsState.value();
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_router_interface_fn(sai_object_id_t router_interface_id) {
  auto fs = FakeSai::getInstance();
  // Refuse while next hops or neighbors use the router interface
  for (const auto& [nextHopId, nextHop] : fs->nextHopManager.map()) {
    if (nextHop.routerInterfaceId == router_interface_id) {
      return SAI_STATUS_OBJECT_IN_USE;
    }
  }
  for (const auto& [neighborEntry, neighbor] : fs->neighborManager.map()) {
    if (std::get<1>(neighborEntry) == router_interface_id) {
      return SAI_STATUS_OBJECT_IN_USE;
    }
  }
  fs->routeInterfaceManager.remove(router_interface_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_router_interface_attribute_fn(
    sai_object_id_t router_interface_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& ri = fs->routeInterfaceManager.get(router_interface_id);
  switch (attr->id) {
    case SAI_ROUTER_INTERFACE_ATTR_SRC_MAC_ADDRESS:
      ri.setSrcMac(attr->value.mac);
      break;
    case SAI_ROUTER_INTERFACE_ATTR_MTU:
      ri.mtu = attr->value.u32;
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
    case SAI_ROUTER_INTERFACE_ATTR_ADMIN_MPLS_STATE:
      ri.adminMplsState = attr->value.booldata;
      break;
#endif
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_router_interface_attribute_fn(
    sai_object_id_t router_interface_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  const auto& ri = fs->routeInterfaceManager.get(router_interface_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_ROUTER_INTERFACE_ATTR_SRC_MAC_ADDRESS:
        std::copy_n(ri.srcMac().bytes(), 6, std::begin(attr[i].value.mac));
        break;
      case SAI_ROUTER_INTERFACE_ATTR_TYPE:
        attr[i].value.s32 = ri.type;
        break;
      case SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID:
        attr[i].value.oid = ri.virtualRouterId;
        break;
      case SAI_ROUTER_INTERFACE_ATTR_VLAN_ID:
        if (ri.type != SAI_ROUTER_INTERFACE_TYPE_VLAN) {
          return SAI_STATUS_IS_ATTR_NOT_SUPPORTED(i);
        }
        attr[i].value.oid = ri.portOrVlanId;
        break;
      case SAI_ROUTER_INTERFACE_ATTR_PORT_ID:
        if (ri.type != SAI_ROUTER_INTERFACE_TYPE_PORT) {
          return SAI_STATUS_IS_ATTR_NOT_SUPPORTED(i);
        }
        attr[i].value.oid = ri.portOrVlanId;
        break;
      case SAI_ROUTER_INTERFACE_ATTR_MTU:
        attr[i].value.u32 = ri.mtu;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      case SAI_ROUTER_INTERFACE_ATTR_ADMIN_MPLS_STATE:
        attr[i].value.booldata = ri.adminMplsState;
        break;
#endif
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

/*
 * No dataplane in fake sai, so stats stay 0. Must still be populated:
 * SaiRouterInterfaceManager::updateStats() calls getStats() every tick, and an
 * unset pointer in the static api table is nullptr -> SIGSEGV at 0x0.
 */
sai_status_t get_router_interface_stats_fn(
    sai_object_id_t /*router_interface*/,
    uint32_t num_of_counters,
    const sai_stat_id_t* /*counter_ids*/,
    uint64_t* counters) {
  for (auto i = 0; i < num_of_counters; ++i) {
    counters[i] = 0;
  }
  return SAI_STATUS_SUCCESS;
}

// Stats are always 0, so mode (READ, READ_AND_CLEAR) doesn't matter.
sai_status_t get_router_interface_stats_ext_fn(
    sai_object_id_t router_interface,
    uint32_t num_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_stats_mode_t /*mode*/,
    uint64_t* counters) {
  return get_router_interface_stats_fn(
      router_interface, num_of_counters, counter_ids, counters);
}

// noop: stats are always 0, nothing to clear.
sai_status_t clear_router_interface_stats_fn(
    sai_object_id_t /*router_interface*/,
    uint32_t /*number_of_counters*/,
    const sai_stat_id_t* /*counter_ids*/) {
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_router_interface_api_t _router_interface_api;

void populate_router_interface_api(
    sai_router_interface_api_t** router_interface_api) {
  _router_interface_api.create_router_interface = &create_router_interface_fn;
  _router_interface_api.remove_router_interface = &remove_router_interface_fn;
  _router_interface_api.set_router_interface_attribute =
      &set_router_interface_attribute_fn;
  _router_interface_api.get_router_interface_attribute =
      &get_router_interface_attribute_fn;

  _router_interface_api.get_router_interface_stats =
      &get_router_interface_stats_fn;
  _router_interface_api.get_router_interface_stats_ext =
      &get_router_interface_stats_ext_fn;
  _router_interface_api.clear_router_interface_stats =
      &clear_router_interface_stats_fn;

  *router_interface_api = &_router_interface_api;
}

} // namespace facebook::fboss
