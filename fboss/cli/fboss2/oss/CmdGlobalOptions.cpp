/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/CmdGlobalOptions.h"

namespace facebook::fboss {

void CmdGlobalOptions::initAdditional(CLI::App& app) {
  app.add_flag("--detail", detail_, "Print detailed output");
  // OSS default is local syslog; "none" disables. (The shared header
  // default stays "scuba" for the internal build.)
  logUsage_ = "syslog";
  app.add_option(
         "--log-usage", logUsage_, "Log usage (syslog, none to disable)")
      ->check(CLI::IsMember({"syslog", "none"}));
}

} // namespace facebook::fboss
