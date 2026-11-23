/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/interface/sflow/CmdConfigInterfaceSflow.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {
constexpr std::string_view kAttrSampleDest = "sample-dest";
constexpr std::string_view kAttrIngressRate = "ingress-rate";
constexpr std::string_view kAttrEgressRate = "egress-rate";
constexpr std::string_view kSampleDestCpu = "cpu";
constexpr std::string_view kSampleDestMirror = "mirror";
constexpr auto kValidSflowAttrs = "sample-dest, ingress-rate, egress-rate";

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return std::tolower(c);
  });
  return s;
}

cfg::SampleDestination parseSampleDest(const std::string& token) {
  if (token == kSampleDestCpu) {
    return cfg::SampleDestination::CPU;
  }
  if (token == kSampleDestMirror) {
    return cfg::SampleDestination::MIRROR;
  }
  throw std::invalid_argument(
      fmt::format(
          "Invalid sample destination '{}': must be {} or {}",
          token,
          kSampleDestCpu,
          kSampleDestMirror));
}

// sFlowIngressRate/sFlowEgressRate are "every 1/rate packets sampled"; 0
// disables sampling. Negative values are meaningless and would wrap when
// handed to the SAI SDK's unsigned rate attribute.
int64_t parseSampleRate(const std::string& attr, const std::string& value) {
  int64_t rate = 0;
  try {
    rate = folly::to<int64_t>(value);
  } catch (const std::exception&) {
    throw std::invalid_argument(
        fmt::format("Invalid {} value '{}': must be an integer", attr, value));
  }
  if (rate < 0) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid {} value '{}': must be a non-negative integer",
            attr,
            value));
  }
  return rate;
}
} // namespace

SflowAttrArgs::SflowAttrArgs(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "No sflow attribute provided. Valid attributes are: {}",
            kValidSflowAttrs));
  }
  if (v.size() % 2 != 0) {
    throw std::invalid_argument(
        "Expected <attr> <value> pairs; got an odd number of tokens");
  }
  for (size_t i = 0; i < v.size(); i += 2) {
    std::string attr = toLower(v[i]);
    if (attr != kAttrSampleDest && attr != kAttrIngressRate &&
        attr != kAttrEgressRate) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown sflow attribute '{}'. Valid attributes are: {}",
              attr,
              kValidSflowAttrs));
    }
    attributes_.emplace_back(std::move(attr), v[i + 1]);
  }
  data_ = std::move(v);
}

CmdConfigInterfaceSflowTraits::RetType CmdConfigInterfaceSflow::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::InterfaceList& interfaces,
    const ObjectArgType& sflowAttrs) {
  if (interfaces.empty()) {
    throw std::invalid_argument("No interface name provided");
  }
  if (sflowAttrs.getAttributes().empty()) {
    throw std::runtime_error(
        fmt::format(
            "Incomplete command. Provide one or more attributes ({})",
            kValidSflowAttrs));
  }

  // Parse every <attr> <value> pair up front (last occurrence of a repeated
  // attribute wins) so all attributes present in this call can be applied
  // together, rather than one attribute excluding the others.
  std::optional<cfg::SampleDestination> newDest;
  std::optional<int64_t> newIngressRate;
  std::optional<int64_t> newEgressRate;
  std::string destToken;
  for (const auto& [attr, value] : sflowAttrs.getAttributes()) {
    if (attr == kAttrSampleDest) {
      destToken = toLower(value);
      newDest = parseSampleDest(destToken);
    } else if (attr == kAttrIngressRate) {
      newIngressRate = parseSampleRate(attr, value);
    } else {
      newEgressRate = parseSampleRate(attr, value);
    }
  }

  auto& session = ConfigSession::getInstance();

  std::vector<std::string> updatedNames;
  std::vector<std::string> skippedNames;
  for (const utils::Intf& intf : interfaces) {
    cfg::Port* port = intf.getPort();
    if (!port) {
      // Resolved as an L3 interface only (e.g. an SVI): these sflow
      // attributes are all Port attributes, so there is nothing to set --
      // report it rather than silently succeeding.
      skippedNames.push_back(intf.name());
      continue;
    }

    // The agent rejects egress sampling to a mirror destination
    // (ApplyThriftConfig throws for MIRROR + sFlowEgressRate > 0). Evaluate
    // the constraint against the state this port will actually end up in --
    // either value may come from this call or, if not given here, from the
    // port's current config -- so a combined "sample-dest mirror egress-rate
    // 0" succeeds and a combined "sample-dest mirror egress-rate 50" fails,
    // regardless of the order the attributes were given in.
    bool destIsMirror = newDest.has_value()
        ? (*newDest == cfg::SampleDestination::MIRROR)
        : (port->sampleDest().has_value() &&
           *port->sampleDest() == cfg::SampleDestination::MIRROR);
    int64_t effectiveEgressRate =
        newEgressRate.value_or(*port->sFlowEgressRate());
    if (destIsMirror && effectiveEgressRate > 0) {
      throw std::invalid_argument(
          fmt::format(
              "Port {}: sample-dest {} requires sFlowEgressRate 0 — egress "
              "sampling to a mirror destination is unsupported",
              intf.name(),
              kSampleDestMirror));
    }

    if (newDest.has_value()) {
      port->sampleDest() = *newDest;
    }
    if (newIngressRate.has_value()) {
      port->sFlowIngressRate() = *newIngressRate;
    }
    if (newEgressRate.has_value()) {
      port->sFlowEgressRate() = *newEgressRate;
    }
    updatedNames.push_back(intf.name());
  }
  if (updatedNames.empty()) {
    throw std::invalid_argument("No port found for the specified interface(s)");
  }

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  std::vector<std::string> setParts;
  if (newDest.has_value()) {
    setParts.push_back(fmt::format("sample-dest={}", destToken));
  }
  if (newIngressRate.has_value()) {
    setParts.push_back(fmt::format("ingress-rate={}", *newIngressRate));
  }
  if (newEgressRate.has_value()) {
    setParts.push_back(fmt::format("egress-rate={}", *newEgressRate));
  }
  std::string message = fmt::format(
      "Successfully set sFlow {} for interface(s) {}",
      folly::join(", ", setParts),
      folly::join(", ", updatedNames));
  if (!skippedNames.empty()) {
    message +=
        fmt::format("; skipped (no port): {}", folly::join(", ", skippedNames));
  }
  return message;
}

void CmdConfigInterfaceSflow::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigInterfaceSflow, CmdConfigInterfaceSflowTraits>::run();

std::vector<std::string> CmdConfigInterfaceSflowTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completeAttrGrammar(
      typed,
      {.attrs =
           {std::string(kAttrSampleDest),
            std::string(kAttrIngressRate),
            std::string(kAttrEgressRate)},
       .values = {
           {std::string(kAttrSampleDest),
            {std::string(kSampleDestCpu), std::string(kSampleDestMirror)}}}});
}

} // namespace facebook::fboss
