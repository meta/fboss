/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "fboss/platform/platform_checks/Host.h"
#include "fboss/platform/platform_checks/RemoteHost.h"

namespace facebook::fboss::platform::fixmyfboss {

/**
 * The switch being diagnosed: either the machine fixmyfboss runs on, or a
 * remote switch reached over SSH.
 */
struct CheckEnvironment {
  // Platform of the x86, used to select configs.
  std::string platformName;
  std::shared_ptr<const platform_checks::Host> x86;
  // Null if the BMC cannot be reached; see bmcUnavailableReason.
  std::shared_ptr<const platform_checks::Host> bmc;
  std::string bmcUnavailableReason;
  // Set if the switch should have a BMC but it could not be reached.
  std::optional<std::string> bmcConnectError;
  // Set if the switch is treated as having no BMC, so checks of the x86 side
  // of the management plane are skipped too.
  std::optional<std::string> noBmcReason;
  // Local directory with prebuilt hw test binaries and OpenBMC tests2/.
  std::optional<std::filesystem::path> hwTestsDir;
  // Platform name for the OpenBMC CIT runner, e.g. "montblanc".
  std::string bmcPlatform;
};

struct ConnectOptions {
  // Unset means the machine fixmyfboss runs on.
  std::optional<std::string> hostname;
  // Unset means <hostname>-oob when diagnosing a remote switch, and no BMC
  // when diagnosing the local machine.
  std::optional<std::string> bmcHostname;
  bool noBmc{false};
  // Called with a prompt when a host rejects key login but accepts a
  // password; returns the password, or nullopt if none can be entered. Unset
  // means key login only.
  std::function<std::optional<std::string>(const std::string& prompt)>
      askPassword;
  platform_checks::RemoteHost::Transport transport{
      platform_checks::RemoteHost::Transport::SSH};
};

// "rsw1.foo" -> "rsw1-oob.foo", the BMC naming convention.
std::string deriveOobHostname(const std::string& hostname);

// Connects to the switch and resolves its platform. Throws std::runtime_error
// with a user-facing message if the switch is unreachable or its platform is
// unknown.
CheckEnvironment createEnvironment(const ConnectOptions& options);

} // namespace facebook::fboss::platform::fixmyfboss
