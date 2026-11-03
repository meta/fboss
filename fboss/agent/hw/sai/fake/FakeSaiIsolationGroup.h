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

extern "C" {
#include <sai.h>
}
namespace facebook::fboss {

class FakeIsolationGroupMember {
 public:
  explicit FakeIsolationGroupMember(sai_object_id_t isolationGroupId)
      : isolationGroupId(isolationGroupId) {}
  sai_object_id_t isolationGroupId;
  sai_object_id_t isolationObject;
  sai_object_id_t id;
};

class FakeIsolationGroup {
 public:
  explicit FakeIsolationGroup(sai_int32_t type) : type(type) {}
  sai_int32_t type;
  sai_object_id_t id;
  FakeManager<sai_object_id_t, FakeIsolationGroupMember>& fm() {
    return fm_;
  }
  const FakeManager<sai_object_id_t, FakeIsolationGroupMember>& fm() const {
    return fm_;
  }

 private:
  FakeManager<sai_object_id_t, FakeIsolationGroupMember> fm_;
};

using FakeIsolationGroupManager =
    FakeManagerWithMembers<FakeIsolationGroup, FakeIsolationGroupMember>;

void populate_isolation_group_api(
    sai_isolation_group_api_t** isolation_group_api);

} // namespace facebook::fboss
