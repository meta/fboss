/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/fake/FakeSaiVirtualChannel.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

using facebook::fboss::FakeSai;

sai_status_t create_virtual_channel_fn(
    sai_object_id_t* virtual_channel_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();

  std::optional<sai_object_id_t> port;
  std::optional<sai_uint8_t> index;
  std::optional<sai_object_id_t> creditProfile;
  std::optional<bool> receiverEnable;
  std::optional<bool> senderEnable;

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_VIRTUAL_CHANNEL_ATTR_PORT:
        port = attr_list[i].value.oid;
        break;
      case SAI_VIRTUAL_CHANNEL_ATTR_INDEX:
        index = attr_list[i].value.u8;
        break;
      case SAI_VIRTUAL_CHANNEL_ATTR_CBFC_SENDER_CREDIT_PROFILE:
        creditProfile = attr_list[i].value.oid;
        break;
      case SAI_VIRTUAL_CHANNEL_ATTR_CBFC_RECEIVER_ENABLE:
        receiverEnable = attr_list[i].value.booldata;
        break;
      case SAI_VIRTUAL_CHANNEL_ATTR_CBFC_SENDER_ENABLE:
        senderEnable = attr_list[i].value.booldata;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }

  if (!port || !index) {
    return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
  }

  *virtual_channel_id =
      fs->virtualChannelManager.create(port.value(), index.value());
  auto& virtualChannel = fs->virtualChannelManager.get(*virtual_channel_id);
  if (creditProfile) {
    virtualChannel.setCbfcSenderCreditProfile(creditProfile.value());
  }
  if (receiverEnable) {
    virtualChannel.setCbfcReceiverEnable(receiverEnable.value());
  }
  if (senderEnable) {
    virtualChannel.setCbfcSenderEnable(senderEnable.value());
  }

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_virtual_channel_fn(sai_object_id_t virtual_channel_id) {
  auto fs = FakeSai::getInstance();
  fs->virtualChannelManager.remove(virtual_channel_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_virtual_channel_attribute_fn(
    sai_object_id_t virtual_channel_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& virtualChannel = fs->virtualChannelManager.get(virtual_channel_id);
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  switch (attr->id) {
    case SAI_VIRTUAL_CHANNEL_ATTR_CBFC_SENDER_CREDIT_PROFILE:
      virtualChannel.setCbfcSenderCreditProfile(attr->value.oid);
      break;
    case SAI_VIRTUAL_CHANNEL_ATTR_CBFC_RECEIVER_ENABLE:
      virtualChannel.setCbfcReceiverEnable(attr->value.booldata);
      break;
    case SAI_VIRTUAL_CHANNEL_ATTR_CBFC_SENDER_ENABLE:
      virtualChannel.setCbfcSenderEnable(attr->value.booldata);
      break;
    default:
      // PORT and INDEX are CREATE_ONLY.
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_virtual_channel_attribute_fn(
    sai_object_id_t virtual_channel_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& virtualChannel = fs->virtualChannelManager.get(virtual_channel_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_VIRTUAL_CHANNEL_ATTR_PORT:
        attr_list[i].value.oid = virtualChannel.getPort();
        break;
      case SAI_VIRTUAL_CHANNEL_ATTR_INDEX:
        attr_list[i].value.u8 = virtualChannel.getIndex();
        break;
      case SAI_VIRTUAL_CHANNEL_ATTR_CBFC_RECEIVER_NATIVE_CREDIT_LIMIT:
        attr_list[i].value.u32 =
            virtualChannel.getCbfcReceiverNativeCreditLimit();
        break;
      case SAI_VIRTUAL_CHANNEL_ATTR_CBFC_SENDER_CREDIT_PROFILE:
        attr_list[i].value.oid = virtualChannel.getCbfcSenderCreditProfile();
        break;
      case SAI_VIRTUAL_CHANNEL_ATTR_CBFC_RECEIVER_ENABLE:
        attr_list[i].value.booldata = virtualChannel.getCbfcReceiverEnable();
        break;
      case SAI_VIRTUAL_CHANNEL_ATTR_CBFC_SENDER_ENABLE:
        attr_list[i].value.booldata = virtualChannel.getCbfcSenderEnable();
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_virtual_channel_stats_fn(
    sai_object_id_t /* virtual_channel_id */,
    uint32_t number_of_counters,
    const sai_stat_id_t* /* counter_ids */,
    uint64_t* counters) {
  for (auto i = 0; i < number_of_counters; ++i) {
    counters[i] = 0;
  }
  return SAI_STATUS_SUCCESS;
}

/*
 * Fake sai has no dataplane, so credit counters stay at 0 and the stats mode
 * makes no difference.
 */
sai_status_t get_virtual_channel_stats_ext_fn(
    sai_object_id_t virtual_channel_id,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_stats_mode_t /* mode */,
    uint64_t* counters) {
  return get_virtual_channel_stats_fn(
      virtual_channel_id, number_of_counters, counter_ids, counters);
}

sai_status_t clear_virtual_channel_stats_fn(
    sai_object_id_t /* virtual_channel_id */,
    uint32_t /* number_of_counters */,
    const sai_stat_id_t* /* counter_ids */) {
  return SAI_STATUS_SUCCESS;
}

/*
 * POOL_ID is allownull, so credit pools are optional in the spec and FBOSS
 * models none. Unimplemented until something needs them, like any other spec
 * object the agent does not create.
 */
sai_status_t create_cbfc_credit_pool_fn(
    sai_object_id_t* /* cbfc_credit_pool_id */,
    sai_object_id_t /* switch_id */,
    uint32_t /* attr_count */,
    const sai_attribute_t* /* attr_list */) {
  return SAI_STATUS_NOT_SUPPORTED;
}

sai_status_t remove_cbfc_credit_pool_fn(
    sai_object_id_t /* cbfc_credit_pool_id */) {
  return SAI_STATUS_NOT_SUPPORTED;
}

sai_status_t set_cbfc_credit_pool_attribute_fn(
    sai_object_id_t /* cbfc_credit_pool_id */,
    const sai_attribute_t* /* attr */) {
  return SAI_STATUS_NOT_SUPPORTED;
}

sai_status_t get_cbfc_credit_pool_attribute_fn(
    sai_object_id_t /* cbfc_credit_pool_id */,
    uint32_t /* attr_count */,
    sai_attribute_t* /* attr_list */) {
  return SAI_STATUS_NOT_SUPPORTED;
}

sai_status_t create_cbfc_credit_profile_fn(
    sai_object_id_t* cbfc_credit_profile_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();

  std::optional<sai_object_id_t> poolId;
  std::optional<sai_uint64_t> reservedCreditSize;

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_CBFC_CREDIT_PROFILE_ATTR_POOL_ID:
        poolId = attr_list[i].value.oid;
        break;
      case SAI_CBFC_CREDIT_PROFILE_ATTR_RESERVED_CREDIT_SIZE:
        reservedCreditSize = attr_list[i].value.u64;
        break;
      default:
        // THRESHOLD_MODE and the two shared thresholds are unmodelled.
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }

  if (!poolId || !reservedCreditSize) {
    return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
  }
  *cbfc_credit_profile_id = fs->cbfcCreditProfileManager.create(
      poolId.value(), reservedCreditSize.value());

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_cbfc_credit_profile_fn(
    sai_object_id_t cbfc_credit_profile_id) {
  auto fs = FakeSai::getInstance();
  fs->cbfcCreditProfileManager.remove(cbfc_credit_profile_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_cbfc_credit_profile_attribute_fn(
    sai_object_id_t cbfc_credit_profile_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& profile = fs->cbfcCreditProfileManager.get(cbfc_credit_profile_id);
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  switch (attr->id) {
    case SAI_CBFC_CREDIT_PROFILE_ATTR_RESERVED_CREDIT_SIZE:
      profile.setReservedCreditSize(attr->value.u64);
      break;
    default:
      // POOL_ID is CREATE_ONLY.
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_cbfc_credit_profile_attribute_fn(
    sai_object_id_t cbfc_credit_profile_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& profile = fs->cbfcCreditProfileManager.get(cbfc_credit_profile_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_CBFC_CREDIT_PROFILE_ATTR_POOL_ID:
        attr_list[i].value.oid = profile.getPoolId();
        break;
      case SAI_CBFC_CREDIT_PROFILE_ATTR_RESERVED_CREDIT_SIZE:
        attr_list[i].value.u64 = profile.getReservedCreditSize();
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_virtual_channel_api_t _virtual_channel_api;

void populate_virtual_channel_api(
    sai_virtual_channel_api_t** virtual_channel_api) {
  _virtual_channel_api.create_virtual_channel = &create_virtual_channel_fn;
  _virtual_channel_api.remove_virtual_channel = &remove_virtual_channel_fn;
  _virtual_channel_api.set_virtual_channel_attribute =
      &set_virtual_channel_attribute_fn;
  _virtual_channel_api.get_virtual_channel_attribute =
      &get_virtual_channel_attribute_fn;
  _virtual_channel_api.get_virtual_channel_stats =
      &get_virtual_channel_stats_fn;
  _virtual_channel_api.get_virtual_channel_stats_ext =
      &get_virtual_channel_stats_ext_fn;
  _virtual_channel_api.clear_virtual_channel_stats =
      &clear_virtual_channel_stats_fn;
  _virtual_channel_api.create_cbfc_credit_pool = &create_cbfc_credit_pool_fn;
  _virtual_channel_api.remove_cbfc_credit_pool = &remove_cbfc_credit_pool_fn;
  _virtual_channel_api.set_cbfc_credit_pool_attribute =
      &set_cbfc_credit_pool_attribute_fn;
  _virtual_channel_api.get_cbfc_credit_pool_attribute =
      &get_cbfc_credit_pool_attribute_fn;
  _virtual_channel_api.create_cbfc_credit_profile =
      &create_cbfc_credit_profile_fn;
  _virtual_channel_api.remove_cbfc_credit_profile =
      &remove_cbfc_credit_profile_fn;
  _virtual_channel_api.set_cbfc_credit_profile_attribute =
      &set_cbfc_credit_profile_attribute_fn;
  _virtual_channel_api.get_cbfc_credit_profile_attribute =
      &get_cbfc_credit_profile_attribute_fn;
  *virtual_channel_api = &_virtual_channel_api;
}

} // namespace facebook::fboss
