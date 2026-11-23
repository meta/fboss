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

#include "fboss/agent/hw/sai/fake/FakeManager.h"

#include <folly/MacAddress.h>

#include <tuple>

extern "C" {
#include <sai.h>
}
namespace facebook::fboss {

struct FakeFdb {
  FakeFdb(
      const sai_object_id_t bridgePortId,
      sai_int32_t type,
      sai_uint32_t metadata)
      : bridgePortId(bridgePortId), type(type), metadata(metadata) {}
  sai_object_id_t bridgePortId;
  sai_int32_t type{SAI_FDB_ENTRY_TYPE_STATIC};
  sai_uint32_t metadata{0};
};

using FakeFdbEntry =
    std::tuple<sai_object_id_t, sai_object_id_t, folly::MacAddress>;
using FakeFdbManager = FakeManager<FakeFdbEntry, FakeFdb>;

void populate_fdb_api(sai_fdb_api_t** fdb_api);

} // namespace facebook::fboss
