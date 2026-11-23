/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/fixmyfboss/CliOptions.h"

#include <folly/String.h>

namespace facebook::fboss::platform::fixmyfboss {

void addOptions(CLI::App& app, CliOptions& options) {
  app.add_flag(
         "--list-checks",
         options.listChecks,
         "Show list of available checks and exit")
      ->group("Information");

  app.add_option(
         "--hostname",
         options.connect.hostname,
         "Diagnose this switch over SSH instead of the local machine")
      ->group("Remote");
  app.add_option(
         "--transport",
         options.transport,
         "How to reach --hostname: ssh, sush2, or auto (sush2 if installed)")
      ->check(CLI::IsMember({"auto", "ssh", "sush2"}))
      ->group("Remote");
  app.add_option(
         "--bmc-hostname",
         options.connect.bmcHostname,
         "BMC to check over SSH (default with --hostname: <hostname>-oob)")
      ->group("Remote");
  app.add_flag(
         "--no-bmc", options.connect.noBmc, "Skip all checks that need the BMC")
      ->group("Remote")
      ->excludes("--bmc-hostname");

  app.add_option(
         "--checks",
         options.run.nameFilter,
         "Only run checks whose name matches this regex (case-insensitive)")
      ->group("Checks");
  app.add_flag(
         "--include-disruptive",
         options.run.includeDisruptive,
         "Also run checks that reboot or power cycle the switch")
      ->group("Checks");
  app.add_option(
         "--hw-tests-dir",
         options.hwTestsDir,
         "Directory with prebuilt platform hw tests and OpenBMC tests2/, "
         "copied to the switch to run (disruptive)")
      ->check(CLI::ExistingDirectory)
      ->group("Checks");
  app.add_option(
         "--bmc-platform",
         options.bmcPlatform,
         "Platform for the BMC CIT runner (default: lowercase platform name)")
      ->group("Checks");

  app.add_flag(
         "--json",
         options.json,
         "Print results as a JSON CheckReport instead of a summary")
      ->group("Output");
  app.add_flag(
         "-v,--verbose", options.verbose, "Enable verbose logging (INFO level)")
      ->group("Logging");
  app.add_flag("-d,--debug", options.debug, "Enable debug logging (DBG level)")
      ->group("Logging");
}

void resolveTransport(CliOptions& options) {
  using Transport = platform_checks::RemoteHost::Transport;
  if (options.transport == "ssh") {
    options.connect.transport = Transport::SSH;
  } else if (options.transport == "sush2") {
    options.connect.transport = Transport::SUSH2;
  } else {
    options.connect.transport = platform_checks::RemoteHost::detectTransport();
  }
}

void applyToEnvironment(const CliOptions& options, CheckEnvironment& env) {
  if (options.hwTestsDir && !options.hwTestsDir->empty()) {
    env.hwTestsDir = *options.hwTestsDir;
  }
  if (options.bmcPlatform && !options.bmcPlatform->empty()) {
    env.bmcPlatform = *options.bmcPlatform;
  } else {
    env.bmcPlatform = env.platformName;
    folly::toLowerAscii(env.bmcPlatform);
  }
}

} // namespace facebook::fboss::platform::fixmyfboss
