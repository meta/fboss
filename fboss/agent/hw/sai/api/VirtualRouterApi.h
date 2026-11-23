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
#include <variant>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class VirtualRouterApi;

struct SaiVirtualRouterTraits {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_VIRTUAL_ROUTER;
  using SaiApiT = VirtualRouterApi;
  using AdapterKey = VirtualRouterSaiId;
  // Keyed by RouterID, which the adapter does not know, so SaiStore saves it
  // in warm boot state (AdapterHostKeyWarmbootRecoverable is false).
  using AdapterHostKey = RouterID;
  struct Attributes {
    using EnumType = sai_virtual_router_attr_t;
    using SrcMac = SaiAttribute<
        EnumType,
        SAI_VIRTUAL_ROUTER_ATTR_SRC_MAC_ADDRESS,
        folly::MacAddress>;
  };
  using CreateAttributes = std::tuple<>;
};

SAI_ATTRIBUTE_NAME(VirtualRouter, SrcMac)

class VirtualRouterApi : public SaiApi<VirtualRouterApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_VIRTUAL_ROUTER;
  VirtualRouterApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for virtual router api");
  }
  VirtualRouterApi(const VirtualRouterApi& other) = delete;

 private:
  sai_status_t _create(
      VirtualRouterSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_virtual_router(
        rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(VirtualRouterSaiId virtual_router_id) const {
    return api_->remove_virtual_router(virtual_router_id);
  }
  sai_status_t _getAttribute(VirtualRouterSaiId handle, sai_attribute_t* attr)
      const {
    return api_->get_virtual_router_attribute(handle, 1, attr);
  }
  sai_status_t _setAttribute(
      VirtualRouterSaiId handle,
      const sai_attribute_t* attr) const {
    return api_->set_virtual_router_attribute(handle, attr);
  }

  sai_virtual_router_api_t* api_;
  friend class SaiApi<VirtualRouterApi>;
};

} // namespace facebook::fboss
