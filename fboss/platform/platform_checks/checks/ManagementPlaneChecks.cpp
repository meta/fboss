/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/ManagementPlaneChecks.h"

#include "fboss/platform/platform_checks/checks/CommandCheck.h"

namespace facebook::fboss::platform::platform_checks {

namespace {

constexpr auto kBmcLinkInterface = "eth0.4088";

std::unique_ptr<PlatformCheck> makeCheck(
    CheckTarget target,
    CommandCheckSpec spec) {
  return std::make_unique<CommandCheck>(std::move(target), std::move(spec));
}

std::unique_ptr<PlatformCheck>
makeLinkCheck(CheckTarget target, CheckType type, const std::string& side) {
  return makeCheck(
      std::move(target),
      {.type = type,
       .name = std::string(kBmcLinkInterface) + " on " + side,
       .description = "Checks that the " + side + " has the " +
           kBmcLinkInterface + " interface used for x86 <-> BMC traffic",
       .command = std::string("ifconfig ") + kBmcLinkInterface,
       .remediation = "Check the USB network link between x86 and BMC"});
}

} // namespace

std::unique_ptr<PlatformCheck> makeX86FwUtilCheck(CheckTarget x86) {
  return makeCheck(
      std::move(x86),
      {.type = CheckType::X86_FW_UTIL_CHECK,
       .name = "fw_util on x86",
       .description = "Checks that fw_util can read all firmware versions",
       .command = "fw_util --fw_action=version --fw_target_name=all",
       .remediation = "Inspect the fw_util output in the details (-v)"});
}

std::unique_ptr<PlatformCheck> makeX86BmcLinkCheck(CheckTarget x86) {
  return makeLinkCheck(std::move(x86), CheckType::X86_BMC_LINK_CHECK, "x86");
}

std::unique_ptr<PlatformCheck> makeIpmiMcInfoCheck(CheckTarget x86) {
  return makeCheck(
      std::move(x86),
      {.type = CheckType::IPMI_MC_INFO_CHECK,
       .name = "IPMI to BMC",
       .description = "Checks that the x86 can reach the BMC over IPMI",
       .command = "ipmitool mc info",
       .remediation = "Check that the BMC is up and its IPMI service runs"});
}

std::unique_ptr<PlatformCheck> makeBmcRestApiFromX86Check(CheckTarget x86) {
  return makeCheck(
      std::move(x86),
      {.type = CheckType::X86_BMC_REST_API_CHECK,
       .name = "BMC REST API from x86",
       .description =
           "Checks that the x86 can reach the BMC REST API over eth0.4088",
       .command =
           "curl -fsS -m 10 'http://[fe80::1%eth0.4088]:8080/api/sys/mb/fruid'",
       .remediation = "Check eth0.4088 and the BMC REST API service"});
}

std::unique_ptr<PlatformCheck> makeBmcFwUtilCheck(CheckTarget bmc) {
  return makeCheck(
      std::move(bmc),
      {.type = CheckType::BMC_FW_UTIL_CHECK,
       .name = "fw-util on BMC",
       .description = "Checks that the BMC can read all firmware versions",
       .command = "fw-util all --version",
       .remediation = "Inspect the fw-util output in the details (-v)"});
}

std::unique_ptr<PlatformCheck> makeBmcX86LinkCheck(CheckTarget bmc) {
  return makeLinkCheck(std::move(bmc), CheckType::BMC_X86_LINK_CHECK, "BMC");
}

std::unique_ptr<PlatformCheck> makeBmcRestApiCheck(CheckTarget bmc) {
  return makeCheck(
      std::move(bmc),
      {.type = CheckType::BMC_REST_API_CHECK,
       .name = "BMC REST API",
       .description = "Checks that the BMC REST API serves requests locally",
       .command = "curl -fsS -m 10 http://localhost:8080/api/sys/mb/fruid",
       .remediation = "Check the REST API service on the BMC"});
}

} // namespace facebook::fboss::platform::platform_checks
