/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/FdbApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/FdbApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _FdbEntryMap{
    SAI_ATTR_MAP(Fdb, Type),
    SAI_ATTR_MAP(Fdb, BridgePortId),
    SAI_ATTR_MAP(Fdb, Metadata),
};

// NOLINTNEXTLINE(facebook-avoid-non-const-global-variables)
std::map<int32_t, std::pair<std::string, std::size_t>> _FdbFlushMap{
    SAI_ATTR_MAP(FdbFlush, BridgePortId),
    SAI_ATTR_MAP(FdbFlush, EntryType),
};
} // namespace

namespace facebook::fboss {

sai_status_t wrap_create_fdb_entry(
    const sai_fdb_entry_t* fdb_entry,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto rv = SaiTracer::getInstance()->fdbApi_->create_fdb_entry(
      fdb_entry, attr_count, attr_list);

  SaiTracer::getInstance()->logFdbEntryCreateFn(
      fdb_entry, attr_count, attr_list, rv);
  return rv;
}

sai_status_t wrap_remove_fdb_entry(const sai_fdb_entry_t* fdb_entry) {
  auto rv = SaiTracer::getInstance()->fdbApi_->remove_fdb_entry(fdb_entry);

  SaiTracer::getInstance()->logFdbEntryRemoveFn(fdb_entry, rv);
  return rv;
}

sai_status_t wrap_set_fdb_entry_attribute(
    const sai_fdb_entry_t* fdb_entry,
    const sai_attribute_t* attr) {
  auto rv = SaiTracer::getInstance()->fdbApi_->set_fdb_entry_attribute(
      fdb_entry, attr);

  SaiTracer::getInstance()->logFdbEntrySetAttrFn(fdb_entry, attr, rv);
  return rv;
}

sai_status_t wrap_get_fdb_entry_attribute(
    const sai_fdb_entry_t* fdb_entry,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  // TODO(zecheng): Log get functions as well
  return SaiTracer::getInstance()->fdbApi_->get_fdb_entry_attribute(
      fdb_entry, attr_count, attr_list);
}

sai_status_t wrap_flush_fdb_entries(
    sai_object_id_t switch_id,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto rv = SaiTracer::getInstance()->fdbApi_->flush_fdb_entries(
      switch_id, attr_count, attr_list);

  SaiTracer::getInstance()->logFdbFlushFn(switch_id, attr_count, attr_list, rv);
  return rv;
}

sai_fdb_api_t* wrappedFdbApi() {
  static sai_fdb_api_t fdbWrappers;

  fdbWrappers.create_fdb_entry = &wrap_create_fdb_entry;
  fdbWrappers.remove_fdb_entry = &wrap_remove_fdb_entry;
  fdbWrappers.set_fdb_entry_attribute = &wrap_set_fdb_entry_attribute;
  fdbWrappers.get_fdb_entry_attribute = &wrap_get_fdb_entry_attribute;
  fdbWrappers.flush_fdb_entries = &wrap_flush_fdb_entries;

  return &fdbWrappers;
}

SET_SAI_ATTRIBUTES(FdbEntry)
SET_SAI_ATTRIBUTES(FdbFlush)

} // namespace facebook::fboss
