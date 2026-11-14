/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

#include <pwd.h>
#include <unistd.h>

#include <string>

namespace facebook::fboss::utils {

std::string getUserInfo() {
  // Effective uid mapped through passwd: the environment ($USER) is
  // caller-controlled and must not attest identity. Numeric fallback
  // keeps a failed lookup truthful instead of empty.
  uid_t uid = geteuid();
  if (auto* entry = getpwuid(uid)) {
    if (entry->pw_name) {
      return entry->pw_name;
    }
  }
  return "uid" + std::to_string(uid);
}

} // namespace facebook::fboss::utils
