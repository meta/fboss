// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/utils/LoopbackUtils.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/cli/fboss2/utils/PrbsUtils.h"
#include "fboss/qsfp_service/if/gen-cpp2/QsfpService.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

#include <boost/algorithm/string.hpp>
#include <fmt/format.h>
#include <folly/MapUtil.h>

namespace facebook::fboss::loopback_utils {

std::optional<phy::LoopbackMode> parseLoopbackDirection(
    const std::string& token) {
  const auto lower = boost::to_lower_copy(token);
  if (lower == kDirectionInput) {
    return phy::LoopbackMode::INPUT;
  }
  if (lower == kDirectionOutput) {
    return phy::LoopbackMode::OUTPUT;
  }
  return std::nullopt;
}

std::string_view loopbackDirectionName(phy::LoopbackMode mode) {
  return mode == phy::LoopbackMode::OUTPUT ? kDirectionOutput : kDirectionInput;
}

LoopbackComponentAction::LoopbackComponentAction(std::vector<std::string> v)
    : BaseObjectArgType(v) {
  if (v.size() == 3) {
    auto direction = parseLoopbackDirection(v[1]);
    if (!direction) {
      throw FbossError(
          "Unknown direction '", v[1], "', expecting 'input' or 'output'");
    }
    direction_ = *direction;
    v.erase(v.begin() + 1);
  }
  if (v.size() != 2) {
    throw FbossError(
        "Incomplete command, expecting 'loopback <asic|xphy_system|xphy_line|"
        "transceiver_system|transceiver_line> [input|output] <enable|disable>'");
  }
  componentName_ = boost::to_lower_copy(v[0]);
  // Throws "Unsupported component: <x>" for anything outside the shared list.
  auto components =
      prbsComponents({componentName_}, false /* returnAllIfEmpty */);
  if (components.size() != 1) {
    throw FbossError("Unknown component '", v[0], "'");
  }
  component_ = components[0];

  const auto action = boost::to_lower_copy(v[1]);
  if (action == kActionEnable) {
    enable_ = true;
  } else if (action == kActionDisable) {
    enable_ = false;
  } else {
    throw FbossError(
        "Expected '",
        kActionEnable,
        "' or '",
        kActionDisable,
        "', got '",
        v[1],
        "'");
  }
}

std::map<int32_t, PortInfoThrift> fetchAllPortInfo(
    apache::thrift::Client<FbossCtrl>* agent) {
  std::map<int32_t, PortInfoThrift> portEntries;
  agent->sync_getAllPortInfo(portEntries);
  return portEntries;
}

int32_t resolveTransceiverId(
    apache::thrift::Client<FbossCtrl>* agent,
    const std::string& portName,
    const std::map<int32_t, PortInfoThrift>& portEntries) {
  int32_t portId = -1;
  for (const auto& [id, info] : portEntries) {
    if (*info.name() == portName) {
      portId = id;
      break;
    }
  }
  if (portId < 0) {
    throw FbossError("Port '", portName, "' not found");
  }

  std::map<int32_t, PortStatus> portStatusEntries;
  std::vector<int32_t> portIds = {portId};
  agent->sync_getPortStatus(portStatusEntries, portIds);

  auto* statusPtr = folly::get_ptr(portStatusEntries, portId);
  if (!statusPtr || !statusPtr->transceiverIdx().has_value() ||
      !apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          statusPtr->transceiverIdx()->transceiverId())) {
    throw FbossError("No transceiver found for port '", portName, "'");
  }
  return *statusPtr->transceiverIdx()->transceiverId();
}

LoopbackCapability fetchLoopbackCapability(
    apache::thrift::Client<QsfpService>* qsfpService,
    int32_t transceiverId) {
  std::map<int32_t, TransceiverInfo> transceiverEntries;
  qsfpService->sync_getTransceiverInfo(
      transceiverEntries, std::vector<int32_t>{transceiverId});

  auto* tcvrInfo = folly::get_ptr(transceiverEntries, transceiverId);
  if (!tcvrInfo) {
    throw FbossError("No transceiver info for transceiver ID ", transceiverId);
  }

  const auto& diags = *tcvrInfo->tcvrState()->diagCapability();
  if (auto lbCap = diags.loopbackCapability().to_optional()) {
    return LoopbackCapability{
        .systemInput = *lbCap->hostSideInput(),
        .systemOutput = *lbCap->hostSideOutput(),
        .lineInput = *lbCap->mediaSideInput(),
        .lineOutput = *lbCap->mediaSideOutput(),
    };
  }
  // Modules (e.g. SFF) without the per-mode capability only support INPUT.
  return LoopbackCapability{
      .systemInput = *diags.loopbackSystem(),
      .lineInput = *diags.loopbackLine(),
  };
}

uint8_t readOneByte(
    apache::thrift::Client<QsfpService>* qsfpService,
    int32_t transceiverId,
    int page,
    int offset) {
  ReadRequest req;
  TransceiverIOParameters param;
  req.ids() = {transceiverId};
  param.offset() = offset;
  param.length() = 1;
  if (page >= 0) {
    param.page() = page;
  }
  req.parameter() = param;

  std::map<int32_t, ReadResponse> resp;
  qsfpService->sync_readTransceiverRegister(resp, req);
  auto* respPtr = folly::get_ptr(resp, transceiverId);
  if (!respPtr || respPtr->data()->length() < 1) {
    throw FbossError(
        "Failed to read byte at page 0x",
        fmt::format("{:02X}", page),
        " offset ",
        offset);
  }
  return respPtr->data()->data()[0];
}

LoopbackState readLoopbackState(
    apache::thrift::Client<QsfpService>* qsfpService,
    int32_t transceiverId) {
  return LoopbackState{
      .systemInput = readOneByte(
          qsfpService, transceiverId, kLoopbackPage, kHostInputLbEnOffset),
      .systemOutput = readOneByte(
          qsfpService, transceiverId, kLoopbackPage, kHostOutputLbEnOffset),
      .lineInput = readOneByte(
          qsfpService, transceiverId, kLoopbackPage, kMediaInputLbEnOffset),
      .lineOutput = readOneByte(
          qsfpService, transceiverId, kLoopbackPage, kMediaOutputLbEnOffset),
  };
}

std::string formatState(const LoopbackState& state) {
  std::string out;
  auto line = [&out](std::string_view name, uint8_t value) {
    out += fmt::format(
        "  {:<34}0x{:02X}  {}\n", name, value, value ? "enabled" : "disabled");
  };
  line("system input (host input):", state.systemInput);
  line("system output (host output):", state.systemOutput);
  line("line input (media input):", state.lineInput);
  line("line output (media output):", state.lineOutput);
  return out;
}

std::string setTransceiverLoopbackForPort(
    apache::thrift::Client<FbossCtrl>* agent,
    apache::thrift::Client<QsfpService>* qsfpService,
    const std::string& portName,
    const LoopbackAction& action,
    const std::map<int32_t, PortInfoThrift>& portEntries) {
  int32_t transceiverId = resolveTransceiverId(agent, portName, portEntries);
  auto cap = fetchLoopbackCapability(qsfpService, transceiverId);

  if (action.isDisableAll()) {
    if (!cap.any()) {
      return fmt::format(
          "Port: {}\nLoopback not supported by this module, nothing to disable.\n",
          portName);
    }
  } else if (!cap.supports(action.mode() == kModeSystem, action.direction())) {
    return fmt::format(
        "Error ({}): {} {} loopback not supported by this module\n",
        portName,
        action.mode(),
        loopbackDirectionName(action.direction()));
  }

  std::string output;
  output += fmt::format("Port: {}\n", portName);
  output += fmt::format("Transceiver ID: {}\n", transceiverId);

  // Read before state
  LoopbackState before;
  try {
    before = readLoopbackState(qsfpService, transceiverId);
  } catch (const std::exception& ex) {
    output += fmt::format("Error reading before-state: {}\n", ex.what());
  }

  if (action.isDisableAll()) {
    output += "Action: disable-all\n";
    struct Target {
      phy::PortComponent component;
      phy::LoopbackMode mode;
      bool supported;
    };
    std::vector<Target> targets = {
        {phy::PortComponent::TRANSCEIVER_SYSTEM,
         phy::LoopbackMode::INPUT,
         cap.systemInput},
        {phy::PortComponent::TRANSCEIVER_SYSTEM,
         phy::LoopbackMode::OUTPUT,
         cap.systemOutput},
        {phy::PortComponent::TRANSCEIVER_LINE,
         phy::LoopbackMode::INPUT,
         cap.lineInput},
        {phy::PortComponent::TRANSCEIVER_LINE,
         phy::LoopbackMode::OUTPUT,
         cap.lineOutput},
    };
    int attempted = 0;
    int failCount = 0;
    for (const auto& target : targets) {
      if (!target.supported) {
        continue;
      }
      ++attempted;
      try {
        qsfpService->sync_setPortLoopbackState(
            portName, target.component, false, target.mode);
      } catch (const std::exception& ex) {
        output += fmt::format(
            "Error disabling {} {}: {}\n",
            target.component == phy::PortComponent::TRANSCEIVER_SYSTEM
                ? kModeSystem
                : kModeLine,
            loopbackDirectionName(target.mode),
            ex.what());
        ++failCount;
      }
    }
    auto resultStr = failCount == 0 ? "success"
        : failCount < attempted     ? "partial-failure"
                                    : "failure";
    output += fmt::format("Result: {}\n", resultStr);
  } else {
    phy::PortComponent component = (action.mode() == kModeSystem)
        ? phy::PortComponent::TRANSCEIVER_SYSTEM
        : phy::PortComponent::TRANSCEIVER_LINE;

    output += fmt::format(
        "Mode: {} {}\n",
        action.mode(),
        loopbackDirectionName(action.direction()));
    output += fmt::format(
        "Action: {}\n", action.enable() ? kActionEnable : kActionDisable);

    try {
      qsfpService->sync_setPortLoopbackState(
          portName, component, action.enable(), action.direction());
      output += "Result: success\n";
    } catch (const std::exception& ex) {
      output += "Result: error\n";
      output += fmt::format("Reason: {}\n", ex.what());
      return output;
    }
  }

  output += "\nBefore:\n";
  output += formatState(before);

  // Read after state
  try {
    auto after = readLoopbackState(qsfpService, transceiverId);
    output += "\nAfter:\n";
    output += formatState(after);
  } catch (const std::exception& ex) {
    output += fmt::format("\nError reading after-state: {}\n", ex.what());
  }

  return output;
}

} // namespace facebook::fboss::loopback_utils
