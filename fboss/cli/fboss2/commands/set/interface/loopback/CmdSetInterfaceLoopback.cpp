// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/commands/set/interface/loopback/CmdSetInterfaceLoopback.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/LoopbackUtils.h"
#include "fboss/cli/fboss2/utils/SafetyPromptUtils.h"

#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"

#include <fmt/format.h>
#include <folly/String.h>

namespace facebook::fboss {

using namespace loopback_utils;

namespace {
constexpr auto kSetInterfaceLoopbackWarning = R"WARN(
================================================================================
WARNING: `fboss2 set interface loopback` puts the selected component into
loopback. This BREAKS THE DATA PATH and causes IMMEDIATE TRAFFIC IMPACT.

Run this only on a device/circuit that has been drained. Every invocation of
`fboss2 set interface loopback` is logged and reviewed.
================================================================================
)WARN";

constexpr auto kClearInterfaceLoopbackWarning = R"WARN(
================================================================================
NOTE: `fboss2 set interface loopback <component> disable` clears loopback and
restores the normal data path. Every invocation is logged and reviewed.
================================================================================
)WARN";

bool isXphyComponent(phy::PortComponent component) {
  return component == phy::PortComponent::GB_LINE ||
      component == phy::PortComponent::GB_SYSTEM;
}

bool isTransceiverComponent(phy::PortComponent component) {
  return component == phy::PortComponent::TRANSCEIVER_LINE ||
      component == phy::PortComponent::TRANSCEIVER_SYSTEM;
}
} // namespace

CmdSetInterfaceLoopback::RetType CmdSetInterfaceLoopback::queryClient(
    const HostInfo& hostInfo,
    const utils::PortList& queriedIfs,
    const ObjectArgType& action) {
  if (queriedIfs.data().empty()) {
    return "No interface specified. Usage: set interface <intf> loopback "
           "<asic|xphy_system|xphy_line|transceiver_system|transceiver_line> "
           "[input|output] <enable|disable>\n";
  }

  const auto component = action.component();
  const bool enable = action.enable();

  // The qsfp_service loopback API covers only the xphy and transceiver
  // components; it silently ignores ASIC. Return before doing anything (and
  // before prompting) rather than issuing a call that would report success
  // while changing nothing.
  if (!isXphyComponent(component) && !isTransceiverComponent(component)) {
    return fmt::format(
        "Component '{}' is not supported by this command; nothing was changed.\n",
        action.componentName());
  }
  if (!isTransceiverComponent(component) &&
      action.direction() != phy::LoopbackMode::INPUT) {
    return fmt::format(
        "Direction '{}' is only supported on transceiver components; nothing was changed.\n",
        loopbackDirectionName(action.direction()));
  }

  const auto target = fmt::format(
      "{} {} loopback on component '{}' for interface(s) [{}] on {}",
      enable ? "enable" : "disable",
      loopbackDirectionName(action.direction()),
      action.componentName(),
      folly::join(", ", queriedIfs.data()),
      hostInfo.getName());
  utils::requireConfirmation(
      kSetInterfaceLoopbackCommandName,
      kSetInterfaceLoopbackYesFlag,
      enable ? kSetInterfaceLoopbackWarning : kClearInterfaceLoopbackWarning,
      target);

  auto qsfpClient =
      utils::createClient<apache::thrift::Client<QsfpService>>(hostInfo);

  std::string output;

  if (isTransceiverComponent(component)) {
    // Reuse the transceiver path shared with `set transceiver <port> loopback`
    // so capability checks and before/after register dumps stay identical.
    auto agent =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    auto portEntries = fetchAllPortInfo(agent.get());
    const std::string mode = component == phy::PortComponent::TRANSCEIVER_SYSTEM
        ? std::string(kModeSystem)
        : std::string(kModeLine);
    const std::string act =
        enable ? std::string(kActionEnable) : std::string(kActionDisable);
    const std::string direction(loopbackDirectionName(action.direction()));

    for (const auto& intf : queriedIfs.data()) {
      try {
        output += setTransceiverLoopbackForPort(
            agent.get(),
            qsfpClient.get(),
            intf,
            LoopbackAction({mode, direction, act}),
            portEntries);
        if (queriedIfs.data().size() > 1) {
          output += "\n";
        }
      } catch (const std::exception& ex) {
        output += fmt::format("Error ({}): {}\n", intf, ex.what());
      }
    }
    return output;
  }

  // Each component is independent: only the selected side is written, matching
  // the transceiver components above.
  for (const auto& intf : queriedIfs.data()) {
    try {
      qsfpClient->sync_setPortLoopbackState(
          intf, component, enable, action.direction());
      output += fmt::format(
          "Set loopback {}={} on {}\n",
          action.componentName(),
          enable ? kActionEnable : kActionDisable,
          intf);
    } catch (const std::exception& ex) {
      output +=
          fmt::format("Failed to set loopback on {}: {}\n", intf, ex.what());
    }
  }
  return output;
}

void CmdSetInterfaceLoopback::printOutput(
    const RetType& model,
    std::ostream& out) {
  out << model;
}

// Explicit template instantiation
template void
CmdHandler<CmdSetInterfaceLoopback, CmdSetInterfaceLoopbackTraits>::run();
template const ValidFilterMapType CmdHandler<
    CmdSetInterfaceLoopback,
    CmdSetInterfaceLoopbackTraits>::getValidFilters();

} // namespace facebook::fboss
