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

#include <folly/MacAddress.h>

#include <tuple>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
class MyMacApi;

struct SaiMyMacTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_MY_MAC;
  using SaiApiT = MyMacApi;
  struct Attributes {
    using EnumType = sai_my_mac_attr_t;
    using MacAddress =
        SaiAttribute<EnumType, SAI_MY_MAC_ATTR_MAC_ADDRESS, folly::MacAddress>;
    using MacAddressMask = SaiAttribute<
        EnumType,
        SAI_MY_MAC_ATTR_MAC_ADDRESS_MASK,
        folly::MacAddress,
        SaiMacAddressExactMatchMaskDefault>;
    using VlanId = SaiAttribute<
        EnumType,
        SAI_MY_MAC_ATTR_VLAN_ID,
        sai_uint16_t,
        SaiIntDefault<sai_uint16_t>>;
  };
  using AdapterKey = MyMacSaiId;
  // All attributes are CREATE_ONLY
  using AdapterHostKey = std::tuple<
      Attributes::MacAddress,
      Attributes::MacAddressMask,
      Attributes::VlanId>;
  using CreateAttributes = AdapterHostKey;
};

SAI_ATTRIBUTE_NAME(MyMac, MacAddress)
SAI_ATTRIBUTE_NAME(MyMac, MacAddressMask)
SAI_ATTRIBUTE_NAME(MyMac, VlanId)

class MyMacApi : public SaiApi<MyMacApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_MY_MAC;
  MyMacApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for my mac api");
  }

 private:
  sai_status_t _create(
      MyMacSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_my_mac(rawSaiId(id), switch_id, count, attr_list);
  }
  sai_status_t _remove(MyMacSaiId id) const {
    return api_->remove_my_mac(id);
  }
  sai_status_t _getAttribute(MyMacSaiId id, sai_attribute_t* attr) const {
    return api_->get_my_mac_attribute(id, 1, attr);
  }
  sai_status_t _setAttribute(MyMacSaiId id, const sai_attribute_t* attr) const {
    return api_->set_my_mac_attribute(id, attr);
  }

  sai_my_mac_api_t* api_;
  friend class SaiApi<MyMacApi>;
};
#endif

} // namespace facebook::fboss
