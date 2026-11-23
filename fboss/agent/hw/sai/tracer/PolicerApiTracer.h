// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include "fboss/agent/hw/sai/tracer/SaiTracer.h"

namespace facebook::fboss {

sai_policer_api_t* wrappedPolicerApi();
SET_ATTRIBUTE_FUNC_DECLARATION(Policer);

} // namespace facebook::fboss
