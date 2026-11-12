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

#include "fboss/cli/fboss2/CmdGlobalOptions.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

#include <pwd.h>
#include <syslog.h>
#include <unistd.h>

namespace facebook::fboss::utils {

std::vector<std::string> getHostsInSmcTier(
    const std::string& /* parentTierName */) {
  return {};
}

const std::string getOobNameFromHost(const std::string& /* host */) {
  return "";
}

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

void logUsage(const CmdLogInfo& cmdLogInfo) {
  if (CmdGlobalOptions::getInstance()->getLogUsage() == "none") {
    return;
  }
  openlog("fboss2", LOG_PID, LOG_USER);
  syslog(LOG_USER | LOG_INFO, "%s", formatUsageLine(cmdLogInfo).c_str());
}

std::vector<std::string> getBgpDrainedInterafces(const HostInfo& /*hostInfo*/) {
  return {};
}

std::vector<std::string> getBgpSwitchDrainState(const HostInfo& /*hostInfo*/) {
  return {};
}

} // namespace facebook::fboss::utils
