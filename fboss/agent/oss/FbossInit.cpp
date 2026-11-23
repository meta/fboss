/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/FbossInit.h"

#include <folly/init/Init.h>

namespace facebook::fboss {

void fbossInit(int argc, char** argv) {
  folly::unsafe_unscoped_init(&argc, &argv, true);
}
} // namespace facebook::fboss
