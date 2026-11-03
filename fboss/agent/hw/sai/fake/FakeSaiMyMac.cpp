/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/fake/FakeSaiMyMac.h"
#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <optional>

using facebook::fboss::FakeSai;

sai_status_t create_my_mac_fn(
    sai_object_id_t* my_mac_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<folly::MacAddress> macAddress;
  std::optional<folly::MacAddress> macAddressMask;
  sai_uint16_t vlanId{0};
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_MY_MAC_ATTR_MAC_ADDRESS:
        macAddress = facebook::fboss::fromSaiMacAddress(attr_list[i].value.mac);
        break;
      case SAI_MY_MAC_ATTR_MAC_ADDRESS_MASK:
        macAddressMask =
            facebook::fboss::fromSaiMacAddress(attr_list[i].value.mac);
        break;
      case SAI_MY_MAC_ATTR_VLAN_ID:
        vlanId = attr_list[i].value.u16;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  if (!macAddress || !macAddressMask) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  *my_mac_id = fs->myMacManager.create(
      macAddress.value(), macAddressMask.value(), vlanId);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_my_mac_fn(sai_object_id_t my_mac_id) {
  auto fs = FakeSai::getInstance();
  fs->myMacManager.remove(my_mac_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_my_mac_attribute_fn(
    sai_object_id_t /* my_mac_id */,
    const sai_attribute_t* /* attr */) {
  return SAI_STATUS_INVALID_PARAMETER;
}

sai_status_t get_my_mac_attribute_fn(
    sai_object_id_t my_mac_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  const auto& myMac = fs->myMacManager.get(my_mac_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_MY_MAC_ATTR_MAC_ADDRESS:
        facebook::fboss::toSaiMacAddress(
            myMac.macAddress, attr_list[i].value.mac);
        break;
      case SAI_MY_MAC_ATTR_MAC_ADDRESS_MASK:
        facebook::fboss::toSaiMacAddress(
            myMac.macAddressMask, attr_list[i].value.mac);
        break;
      case SAI_MY_MAC_ATTR_VLAN_ID:
        attr_list[i].value.u16 = myMac.vlanId;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

void populate_my_mac_api(sai_my_mac_api_t** my_mac_api) {
  static sai_my_mac_api_t _my_mac_api;
  _my_mac_api.create_my_mac = &create_my_mac_fn;
  _my_mac_api.remove_my_mac = &remove_my_mac_fn;
  _my_mac_api.set_my_mac_attribute = &set_my_mac_attribute_fn;
  _my_mac_api.get_my_mac_attribute = &get_my_mac_attribute_fn;
  *my_mac_api = &_my_mac_api;
}

} // namespace facebook::fboss
