/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/fake/FakeSaiVirtualRouter.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include "fboss/agent/hw/sai/api/AddressUtil.h"

using facebook::fboss::FakeSai;
using facebook::fboss::FakeVirtualRouter;

sai_status_t create_virtual_router_fn(
    sai_object_id_t* virtual_router_id,
    sai_object_id_t /* switch_id */,
    uint32_t /* attr_count */,
    const sai_attribute_t* /* attr_list */) {
  auto fs = FakeSai::getInstance();
  *virtual_router_id = fs->virtualRouteManager.create(FakeVirtualRouter());
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_virtual_router_fn(sai_object_id_t virtual_router_id) {
  auto fs = FakeSai::getInstance();
  // Like Broadcom and Cisco SAI, refuse while router interfaces use it
  for (const auto& [rifId, rif] : fs->routeInterfaceManager.map()) {
    if (rif.virtualRouterId == virtual_router_id) {
      return SAI_STATUS_OBJECT_IN_USE;
    }
  }
  fs->virtualRouteManager.remove(virtual_router_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_virtual_router_attribute_fn(
    sai_object_id_t virtual_router_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& virtualRouter = fs->virtualRouteManager.get(virtual_router_id);
  sai_status_t res = SAI_STATUS_SUCCESS;
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  switch (attr->id) {
    case SAI_VIRTUAL_ROUTER_ATTR_SRC_MAC_ADDRESS:
      virtualRouter.srcMac =
          facebook::fboss::fromSaiMacAddress(attr->value.mac);
      break;
    default:
      res = SAI_STATUS_INVALID_PARAMETER;
      break;
  }
  return res;
}

sai_status_t get_virtual_router_attribute_fn(
    sai_object_id_t virtual_router_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& virtualRouter = fs->virtualRouteManager.get(virtual_router_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_VIRTUAL_ROUTER_ATTR_SRC_MAC_ADDRESS:
        facebook::fboss::toSaiMacAddress(
            virtualRouter.srcMac, attr[i].value.mac);
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }

  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_virtual_router_api_t _virtual_router_api;

void populate_virtual_router_api(
    sai_virtual_router_api_t** virtual_router_api) {
  _virtual_router_api.create_virtual_router = &create_virtual_router_fn;
  _virtual_router_api.remove_virtual_router = &remove_virtual_router_fn;
  _virtual_router_api.set_virtual_router_attribute =
      &set_virtual_router_attribute_fn;
  _virtual_router_api.get_virtual_router_attribute =
      &get_virtual_router_attribute_fn;
  *virtual_router_api = &_virtual_router_api;
}

} // namespace facebook::fboss
