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

#include "fboss/agent/hw/sai/tracer/SaiTracer.h"

namespace facebook::fboss {

sai_isolation_group_api_t* wrappedIsolationGroupApi();

SET_ATTRIBUTE_FUNC_DECLARATION(IsolationGroup);
SET_ATTRIBUTE_FUNC_DECLARATION(IsolationGroupMember);

} // namespace facebook::fboss
