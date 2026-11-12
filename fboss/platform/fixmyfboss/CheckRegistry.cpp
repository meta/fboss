/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/fixmyfboss/CheckRegistry.h"

#include "fboss/platform/platform_checks/LocalHost.h"
#include "fboss/platform/platform_checks/checks/BmcChecks.h"
#include "fboss/platform/platform_checks/checks/BmcReachableCheck.h"
#include "fboss/platform/platform_checks/checks/HwTestChecks.h"
#include "fboss/platform/platform_checks/checks/MacAddressCheck.h"
#include "fboss/platform/platform_checks/checks/ManagementPlaneChecks.h"
#include "fboss/platform/platform_checks/checks/PciDeviceCheck.h"
#include "fboss/platform/platform_checks/checks/PowerCycleChecks.h"
#include "fboss/platform/platform_checks/checks/PowerResetCheck.h"
#include "fboss/platform/platform_checks/checks/SolConsoleCheck.h"
#include "fboss/platform/platform_checks/checks/i801SmbusTimeoutCheck.h"

namespace facebook::fboss::platform::fixmyfboss {

using namespace platform_checks;

std::vector<std::unique_ptr<PlatformCheck>> createAllChecks(
    const CheckEnvironment& env) {
  const CheckTarget x86{.host = env.x86, .platformName = env.platformName};
  const CheckTarget bmc{
      .host = env.bmc, .unavailableReason = env.bmcUnavailableReason};
  // The x86 side of the management plane only exists on switches with a BMC.
  CheckTarget x86WithBmc = x86;
  if (env.noBmcReason) {
    x86WithBmc = {.host = nullptr, .unavailableReason = *env.noBmcReason};
  }

  std::vector<std::unique_ptr<PlatformCheck>> checks;
  checks.push_back(
      std::make_unique<BmcReachableCheck>(
          CheckTarget{
              .host = env.bmc, .unavailableReason = env.bmcUnavailableReason},
          env.bmcConnectError));
  checks.push_back(std::make_unique<MacAddressCheck>(x86));
  checks.push_back(std::make_unique<PciDeviceCheck>(x86));
  checks.push_back(std::make_unique<RecentManualRebootCheck>(x86));
  checks.push_back(std::make_unique<RecentKernelPanicCheck>(x86));
  checks.push_back(std::make_unique<WatchdogDidNotStopCheck>(x86));
  checks.push_back(std::make_unique<i801SmbusTimeoutCheck>(x86));

  checks.push_back(makeX86FwUtilCheck(x86WithBmc));
  checks.push_back(makeX86BmcLinkCheck(x86WithBmc));
  checks.push_back(makeIpmiMcInfoCheck(x86WithBmc));
  checks.push_back(makeBmcRestApiFromX86Check(x86WithBmc));

  checks.push_back(makeBmcFwUtilCheck(bmc));
  checks.push_back(makeBmcX86LinkCheck(bmc));
  checks.push_back(makeBmcRestApiCheck(bmc));
  checks.push_back(std::make_unique<BmcMacAddressCheck>(bmc));
  checks.push_back(std::make_unique<BmcEepromCheck>(bmc));
  checks.push_back(std::make_unique<SolConsoleCheck>(bmc));
  checks.push_back(std::make_unique<X86MacConsistencyCheck>(x86, bmc));

  checks.push_back(std::make_unique<SecondaryBmcBootCheck>(bmc));
  checks.push_back(
      std::make_unique<WedgePowerCheck>(
          x86, bmc, std::make_shared<LocalHost>()));
  for (auto& spec : platformHwTestSpecs()) {
    checks.push_back(
        std::make_unique<HwTestCheck>(x86, std::move(spec), env.hwTestsDir));
  }
  checks.push_back(
      std::make_unique<BmcCitTestCheck>(
          bmc, x86, env.hwTestsDir, env.bmcPlatform));
  return checks;
}

} // namespace facebook::fboss::platform::fixmyfboss
