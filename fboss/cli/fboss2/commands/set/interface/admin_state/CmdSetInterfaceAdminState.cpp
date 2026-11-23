// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/set/interface/admin_state/CmdSetInterfaceAdminState.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/PrbsUtils.h"
#include "fboss/cli/fboss2/utils/SafetyPromptUtils.h"

#include <boost/algorithm/string.hpp>
#include <fmt/format.h>
#include <folly/String.h>

namespace facebook::fboss {

namespace {
constexpr auto kSetInterfaceAdminStateWarning = R"WARN(
================================================================================
WARNING: `fboss2 set interface admin-state` administratively enables/disables
the selected retimer (XPHY) side. Disabling squelches that side and causes
IMMEDIATE TRAFFIC IMPACT.

Run this only on a device/circuit that has been drained. Every invocation of
`fboss2 set interface admin-state` is logged and reviewed.
================================================================================
)WARN";
} // namespace

AdminStateArg::AdminStateArg(std::vector<std::string> v)
    : BaseObjectArgType(v) {
  if (v.size() != 2) {
    throw FbossError(
        "Incomplete command, expecting 'admin-state <xphy_system|xphy_line> "
        "<enable|disable>'");
  }
  componentName_ = boost::to_lower_copy(v[0]);
  const auto components =
      prbsComponents({componentName_}, false /* returnAllIfEmpty */);
  if (components.size() != 1 ||
      (components[0] != phy::PortComponent::GB_SYSTEM &&
       components[0] != phy::PortComponent::GB_LINE)) {
    throw FbossError(
        "Unsupported component '", v[0], "', expecting xphy_system|xphy_line");
  }
  component_ = components[0];

  const auto state = boost::to_lower_copy(v[1]);
  if (state == "enable") {
    adminUp_ = true;
  } else if (state == "disable") {
    adminUp_ = false;
  } else {
    throw FbossError(
        "Invalid admin state '", v[1], "', expecting enable|disable");
  }
}

CmdSetInterfaceAdminState::RetType CmdSetInterfaceAdminState::queryClient(
    const HostInfo& hostInfo,
    const utils::PortList& queriedIfs,
    const ObjectArgType& adminState) {
  if (queriedIfs.data().empty()) {
    return "No interface specified. Usage: set interface <intf> admin-state "
           "<xphy_system|xphy_line> <enable|disable>\n";
  }

  const bool up = adminState.adminUp();
  const auto target = fmt::format(
      "{} '{}' admin state for interface(s) [{}] on {}",
      up ? "enable" : "disable",
      adminState.componentName(),
      folly::join(", ", queriedIfs.data()),
      hostInfo.getName());
  utils::requireConfirmation(
      kSetInterfaceAdminStateCommandName,
      kSetInterfaceAdminStateYesFlag,
      kSetInterfaceAdminStateWarning,
      target);

  auto qsfpClient =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);

  std::string output;
  bool anyFailed = false;
  for (const auto& intf : queriedIfs.data()) {
    try {
      qsfpClient->sync_setPortAdminState(intf, adminState.component(), up);
      output += fmt::format(
          "Set admin state {}={} on {}\n",
          adminState.componentName(),
          up ? "enable" : "disable",
          intf);
    } catch (const std::exception& ex) {
      anyFailed = true;
      output +=
          fmt::format("Failed to set admin state on {}: {}\n", intf, ex.what());
    }
  }
  if (anyFailed) {
    throw FbossError(output);
  }
  return output;
}

void CmdSetInterfaceAdminState::printOutput(
    const RetType& model,
    std::ostream& out) {
  out << model;
}

// Explicit template instantiation
template void
CmdHandler<CmdSetInterfaceAdminState, CmdSetInterfaceAdminStateTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdSetInterfaceAdminState,
    CmdSetInterfaceAdminStateTraits>::getValidFilters();

} // namespace facebook::fboss
