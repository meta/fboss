/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/fake/FakeSaiFdb.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include "fboss/agent/hw/sai/api/AddressUtil.h"

#include <optional>

using facebook::fboss::FakeFdb;
using facebook::fboss::FakeSai;

sai_status_t create_fdb_entry_fn(
    const sai_fdb_entry_t* fdb_entry,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto mac = facebook::fboss::fromSaiMacAddress(fdb_entry->mac_address);
  sai_object_id_t bridgePortId = 0;
  sai_int32_t type{SAI_FDB_ENTRY_TYPE_STATIC};
  sai_uint32_t metadata{0};
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID:
        bridgePortId = attr_list[i].value.oid;
        break;
      case SAI_FDB_ENTRY_ATTR_TYPE:
        type = attr_list[i].value.s32;
        break;
      case SAI_FDB_ENTRY_ATTR_META_DATA:
        metadata = attr_list[i].value.u32;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  fs->fdbManager.create(
      std::make_tuple(fdb_entry->switch_id, fdb_entry->bv_id, mac),
      bridgePortId,
      type,
      metadata);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_fdb_entry_fn(const sai_fdb_entry_t* fdb_entry) {
  auto fs = FakeSai::getInstance();
  auto mac = facebook::fboss::fromSaiMacAddress(fdb_entry->mac_address);
  fs->fdbManager.remove(
      std::make_tuple(fdb_entry->switch_id, fdb_entry->bv_id, mac));
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_fdb_entry_attribute_fn(
    const sai_fdb_entry_t* fdb_entry,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto mac = facebook::fboss::fromSaiMacAddress(fdb_entry->mac_address);
  auto fdbKey = std::make_tuple(fdb_entry->switch_id, fdb_entry->bv_id, mac);
  // The entry may be temporarily orphaned when a VLAN object is recreated with
  // a new SAI ID during reconciliation, leaving this key stale.  Return
  // ITEM_NOT_FOUND rather than throwing so FBOSS can re-create the entry.
  if (!fs->fdbManager.exists(fdbKey)) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  auto& fdbEntry = fs->fdbManager.get(fdbKey);
  switch (attr->id) {
    case SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID:
      fdbEntry.bridgePortId = attr->value.oid;
      break;
    case SAI_FDB_ENTRY_ATTR_TYPE:
      fdbEntry.type = attr->value.s32;
      break;
    case SAI_FDB_ENTRY_ATTR_META_DATA:
      fdbEntry.metadata = attr->value.u32;
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_fdb_entry_attribute_fn(
    const sai_fdb_entry_t* fdb_entry,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto mac = facebook::fboss::fromSaiMacAddress(fdb_entry->mac_address);
  auto fdbKey = std::make_tuple(fdb_entry->switch_id, fdb_entry->bv_id, mac);
  // The entry may be temporarily orphaned when a VLAN object is recreated with
  // a new SAI ID during reconciliation, leaving this key stale.  Return
  // ITEM_NOT_FOUND rather than throwing so FBOSS can re-create the entry.
  if (!fs->fdbManager.exists(fdbKey)) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  auto& fdbEntry = fs->fdbManager.get(fdbKey);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID:
        attr_list[i].value.oid = fdbEntry.bridgePortId;
        break;
      case SAI_FDB_ENTRY_ATTR_TYPE:
        attr_list[i].value.s32 = fdbEntry.type;
        break;
      case SAI_FDB_ENTRY_ATTR_META_DATA:
        attr_list[i].value.u32 = fdbEntry.metadata;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t flush_fdb_entries_fn(
    sai_object_id_t switch_id,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  std::optional<sai_object_id_t> bridgePortId;
  sai_int32_t entryType{SAI_FDB_FLUSH_ENTRY_TYPE_ALL};
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_FDB_FLUSH_ATTR_BRIDGE_PORT_ID:
        bridgePortId = attr_list[i].value.oid;
        break;
      case SAI_FDB_FLUSH_ATTR_ENTRY_TYPE:
        entryType = attr_list[i].value.s32;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  auto matchesType = [entryType](sai_int32_t type) {
    switch (entryType) {
      case SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC:
        return type == SAI_FDB_ENTRY_TYPE_DYNAMIC;
      case SAI_FDB_FLUSH_ENTRY_TYPE_STATIC:
        return type == SAI_FDB_ENTRY_TYPE_STATIC;
      default:
        return true;
    }
  };
  auto& fdbs = FakeSai::getInstance()->fdbManager.map();
  std::erase_if(fdbs, [&](const auto& keyAndFdb) {
    const auto& [key, fdb] = keyAndFdb;
    return std::get<0>(key) == switch_id &&
        (!bridgePortId || fdb.bridgePortId == *bridgePortId) &&
        matchesType(fdb.type);
  });
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_fdb_api_t _fdb_api;

void populate_fdb_api(sai_fdb_api_t** fdb_api) {
  _fdb_api.create_fdb_entry = &create_fdb_entry_fn;
  _fdb_api.remove_fdb_entry = &remove_fdb_entry_fn;
  _fdb_api.set_fdb_entry_attribute = &set_fdb_entry_attribute_fn;
  _fdb_api.get_fdb_entry_attribute = &get_fdb_entry_attribute_fn;
  _fdb_api.flush_fdb_entries = &flush_fdb_entries_fn;
  *fdb_api = &_fdb_api;
}

} // namespace facebook::fboss
