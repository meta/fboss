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

#include <gflags/gflags.h>
#include "fboss/lib/if/gen-cpp2/fboss_common_types.h"

DECLARE_bool(use_bsp_helpers);

namespace facebook::fboss {

enum class ExternalPhyVersion : char {
  MILN4_2,
  MILN5_2,
};
} // namespace facebook::fboss
