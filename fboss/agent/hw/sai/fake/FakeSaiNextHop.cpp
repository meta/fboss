/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/fake/FakeSaiPort.h"

#include <optional>

using facebook::fboss::FakePort;
using facebook::fboss::FakeSai;

sai_status_t create_next_hop_fn(
    sai_object_id_t* next_hop_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<sai_next_hop_type_t> type;
  std::optional<folly::IPAddress> ip;
  std::optional<sai_object_id_t> routerInterfaceId;
  std::vector<sai_uint32_t> labelStack;
  sai_int32_t outsegType{SAI_OUTSEG_TYPE_SWAP};
  bool disableTtlDecrement{false};
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  sai_object_id_t tunnelId{SAI_NULL_OBJECT_ID};
  sai_object_id_t srv6SidlistId{SAI_NULL_OBJECT_ID};
  std::optional<folly::MacAddress> overlayDstMac;
#endif
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_NEXT_HOP_ATTR_TYPE:
        type = static_cast<sai_next_hop_type_t>(attr_list[i].value.s32);
        break;
      case SAI_NEXT_HOP_ATTR_IP:
        ip = facebook::fboss::fromSaiIpAddress(attr_list[i].value.ipaddr);
        break;
      case SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID:
        routerInterfaceId = attr_list[i].value.oid;
        break;
      case SAI_NEXT_HOP_ATTR_LABELSTACK:
        if (type != SAI_NEXT_HOP_TYPE_MPLS) {
          return SAI_STATUS_INVALID_PARAMETER;
        }
        labelStack.resize(attr_list[i].value.u32list.count);
        for (auto j = 0; j < attr_list[i].value.u32list.count; j++) {
          labelStack[j] = attr_list[i].value.u32list.list[j];
        }
        break;
      case SAI_NEXT_HOP_ATTR_OUTSEG_TYPE:
        if (type != SAI_NEXT_HOP_TYPE_MPLS) {
          return SAI_STATUS_INVALID_PARAMETER;
        }
        outsegType = attr_list[i].value.s32;
        break;
      case SAI_NEXT_HOP_ATTR_DISABLE_DECREMENT_TTL:
        disableTtlDecrement = attr_list[i].value.booldata;
        break;
      case SAI_NEXT_HOP_ATTR_TUNNEL_ID:
        tunnelId = attr_list[i].value.oid;
        break;
      case SAI_NEXT_HOP_ATTR_TUNNEL_MAC:
        overlayDstMac =
            facebook::fboss::fromSaiMacAddress(attr_list[i].value.mac);
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      case SAI_NEXT_HOP_ATTR_SRV6_SIDLIST_ID:
        srv6SidlistId = attr_list[i].value.oid;
        break;
#endif
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  if (!type) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  if (type.value() == SAI_NEXT_HOP_TYPE_TUNNEL_ENCAP) {
    *next_hop_id = fs->nextHopManager.create(
        type.value(),
        ip.value_or(folly::IPAddress("0.0.0.0")),
        routerInterfaceId.value_or(SAI_NULL_OBJECT_ID),
        labelStack,
        disableTtlDecrement);
    auto& nextHop = fs->nextHopManager.get(*next_hop_id);
    nextHop.tunnelId = tunnelId;
    nextHop.overlayDstMac = overlayDstMac;
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  else if (type.value() == SAI_NEXT_HOP_TYPE_SRV6_SIDLIST) {
    *next_hop_id = fs->nextHopManager.create(
        type.value(),
        ip.value_or(folly::IPAddress("0.0.0.0")),
        routerInterfaceId.value_or(SAI_NULL_OBJECT_ID),
        labelStack,
        disableTtlDecrement);
    auto& nextHop = fs->nextHopManager.get(*next_hop_id);
    nextHop.tunnelId = tunnelId;
    nextHop.srv6SidlistId = srv6SidlistId;
  }
#endif
  else {
    if (!ip || !routerInterfaceId) {
      return SAI_STATUS_INVALID_PARAMETER;
    }
    *next_hop_id = fs->nextHopManager.create(
        type.value(),
        ip.value(),
        routerInterfaceId.value(),
        labelStack,
        disableTtlDecrement);
    fs->nextHopManager.get(*next_hop_id).outsegType = outsegType;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
    auto& nextHop = fs->nextHopManager.get(*next_hop_id);
    nextHop.tunnelId = tunnelId;
    nextHop.srv6SidlistId = srv6SidlistId;
#endif
  }

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_next_hop_fn(sai_object_id_t next_hop_id) {
  auto fs = FakeSai::getInstance();
  fs->nextHopManager.remove(next_hop_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_next_hop_attribute_fn(
    sai_object_id_t next_hop_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& nextHop = fs->nextHopManager.get(next_hop_id);
  switch (attr->id) {
    case SAI_NEXT_HOP_ATTR_TUNNEL_MAC:
      nextHop.overlayDstMac =
          facebook::fboss::fromSaiMacAddress(attr->value.mac);
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_next_hop_attribute_fn(
    sai_object_id_t next_hop_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  const auto& nextHop = fs->nextHopManager.get(next_hop_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_NEXT_HOP_ATTR_TYPE:
        attr[i].value.s32 = static_cast<int32_t>(nextHop.type);
        break;
      case SAI_NEXT_HOP_ATTR_IP:
        attr[i].value.ipaddr = facebook::fboss::toSaiIpAddress(nextHop.ip);
        break;
      case SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID:
        attr[i].value.oid = nextHop.routerInterfaceId;
        break;
      case SAI_NEXT_HOP_ATTR_LABELSTACK:
        if (static_cast<int32_t>(nextHop.type) != SAI_NEXT_HOP_TYPE_MPLS) {
          return SAI_STATUS_INVALID_PARAMETER;
        }
        if (attr[i].value.u32list.count < nextHop.labelStack.size()) {
          attr[i].value.u32list.count = nextHop.labelStack.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (auto j = 0; j < attr[i].value.u32list.count; j++) {
          attr[i].value.u32list.list[j] = nextHop.labelStack[j];
        }
        break;
      case SAI_NEXT_HOP_ATTR_OUTSEG_TYPE:
        if (static_cast<int32_t>(nextHop.type) != SAI_NEXT_HOP_TYPE_MPLS) {
          return SAI_STATUS_INVALID_PARAMETER;
        }
        attr[i].value.s32 = nextHop.outsegType;
        break;
      case SAI_NEXT_HOP_ATTR_DISABLE_DECREMENT_TTL:
        attr[i].value.booldata = nextHop.disableTtlDecrement;
        break;
      case SAI_NEXT_HOP_ATTR_TUNNEL_ID:
        attr[i].value.oid = nextHop.tunnelId;
        break;
      case SAI_NEXT_HOP_ATTR_TUNNEL_MAC:
        if (nextHop.overlayDstMac.has_value()) {
          facebook::fboss::toSaiMacAddress(
              nextHop.overlayDstMac.value(), attr[i].value.mac);
        } else {
          facebook::fboss::toSaiMacAddress(
              folly::MacAddress("00:00:00:00:00:00"), attr[i].value.mac);
        }
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      case SAI_NEXT_HOP_ATTR_SRV6_SIDLIST_ID:
        attr[i].value.oid = nextHop.srv6SidlistId;
        break;
#endif
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_next_hop_api_t _next_hop_api;

void populate_next_hop_api(sai_next_hop_api_t** next_hop_api) {
  _next_hop_api.create_next_hop = &create_next_hop_fn;
  _next_hop_api.remove_next_hop = &remove_next_hop_fn;
  _next_hop_api.set_next_hop_attribute = &set_next_hop_attribute_fn;
  _next_hop_api.get_next_hop_attribute = &get_next_hop_attribute_fn;
  *next_hop_api = &_next_hop_api;
}

} // namespace facebook::fboss
