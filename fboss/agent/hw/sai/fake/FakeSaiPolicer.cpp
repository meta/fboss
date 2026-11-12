// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include "fboss/agent/hw/sai/fake/FakeSai.h"

using facebook::fboss::FakePolicer;
using facebook::fboss::FakeSai;

namespace {
sai_status_t setPolicerAttribute(
    FakePolicer& policer,
    const sai_attribute_t& attr) {
  switch (attr.id) {
    case SAI_POLICER_ATTR_METER_TYPE:
      policer.type = attr.value.s32;
      break;
    case SAI_POLICER_ATTR_MODE:
      policer.mode = attr.value.s32;
      break;
    case SAI_POLICER_ATTR_COLOR_SOURCE:
      policer.colorSource = attr.value.s32;
      break;
    case SAI_POLICER_ATTR_CIR:
      policer.cir = attr.value.u64;
      break;
    case SAI_POLICER_ATTR_CBS:
      policer.cbs = attr.value.u64;
      break;
    case SAI_POLICER_ATTR_PBS:
      policer.pbs = attr.value.u64;
      break;
    case SAI_POLICER_ATTR_GREEN_PACKET_ACTION:
      policer.greenPacketAction = attr.value.s32;
      break;
    case SAI_POLICER_ATTR_YELLOW_PACKET_ACTION:
      policer.yellowPacketAction = attr.value.s32;
      break;
    case SAI_POLICER_ATTR_RED_PACKET_ACTION:
      policer.redPacketAction = attr.value.s32;
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}
} // namespace

sai_status_t create_policer_fn(
    sai_object_id_t* policer_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  FakePolicer policer;
  bool hasType = false;
  bool hasMode = false;
  for (uint32_t i = 0; i < attr_count; ++i) {
    hasType |= (attr_list[i].id == SAI_POLICER_ATTR_METER_TYPE);
    hasMode |= (attr_list[i].id == SAI_POLICER_ATTR_MODE);
    auto status = setPolicerAttribute(policer, attr_list[i]);
    if (status != SAI_STATUS_SUCCESS) {
      return status;
    }
  }
  if (!hasType || !hasMode) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  *policer_id = FakeSai::getInstance()->policerManager.create(policer);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_policer_fn(sai_object_id_t policer_id) {
  auto fs = FakeSai::getInstance();
  if (!fs->policerManager.exists(policer_id)) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  fs->policerManager.remove(policer_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_policer_attribute_fn(
    sai_object_id_t policer_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  if (!fs->policerManager.exists(policer_id)) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  if (attr->id == SAI_POLICER_ATTR_METER_TYPE ||
      attr->id == SAI_POLICER_ATTR_MODE) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  return setPolicerAttribute(fs->policerManager.get(policer_id), *attr);
}

sai_status_t get_policer_attribute_fn(
    sai_object_id_t policer_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  if (!fs->policerManager.exists(policer_id)) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  const auto& policer = fs->policerManager.get(policer_id);
  for (uint32_t i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_POLICER_ATTR_METER_TYPE:
        attr_list[i].value.s32 = policer.type;
        break;
      case SAI_POLICER_ATTR_MODE:
        attr_list[i].value.s32 = policer.mode;
        break;
      case SAI_POLICER_ATTR_COLOR_SOURCE:
        attr_list[i].value.s32 = policer.colorSource;
        break;
      case SAI_POLICER_ATTR_CIR:
        attr_list[i].value.u64 = policer.cir;
        break;
      case SAI_POLICER_ATTR_CBS:
        attr_list[i].value.u64 = policer.cbs;
        break;
      case SAI_POLICER_ATTR_PBS:
        attr_list[i].value.u64 = policer.pbs;
        break;
      case SAI_POLICER_ATTR_GREEN_PACKET_ACTION:
        attr_list[i].value.s32 = policer.greenPacketAction;
        break;
      case SAI_POLICER_ATTR_YELLOW_PACKET_ACTION:
        attr_list[i].value.s32 = policer.yellowPacketAction;
        break;
      case SAI_POLICER_ATTR_RED_PACKET_ACTION:
        attr_list[i].value.s32 = policer.redPacketAction;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

void populate_policer_api(sai_policer_api_t** policer_api) {
  static sai_policer_api_t api{};
  api.create_policer = &create_policer_fn;
  api.remove_policer = &remove_policer_fn;
  api.set_policer_attribute = &set_policer_attribute_fn;
  api.get_policer_attribute = &get_policer_attribute_fn;
  *policer_api = &api;
}

} // namespace facebook::fboss
