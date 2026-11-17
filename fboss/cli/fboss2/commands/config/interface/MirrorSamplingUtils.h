// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <fmt/format.h>
#include <folly/String.h>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss {

// A port with sampleDest MIRROR sends its sFlow samples through its ingress
// mirror. The agent allows only one such mirror across the whole switch: it
// collects the ingress mirror of every port that has sampleDest MIRROR, and
// if that set has more than one name it rejects the config with "Only one
// mirror can be configured across all ports, to sample traffic"
// (ThriftConfigApplier::updateMirrors). A sampling port with no ingress
// mirror adds nothing to that set, and neither does any egress mirror.
//
// Two CLI commands can break this rule:
//   - `config interface <port> mirror ingress <name>` changes which mirror a
//     port is bound to.
//   - `config interface <port> sflow sample-dest mirror` turns sampling on.
// Each one calls this function before staging its change, so the CLI rejects
// the config instead of the agent rejecting it at commit time.
//
// The check runs on the config as it would be after the change, not as it is
// now:
//   - `targets`: the ports the command is changing.
//   - `targetsSample`: true if the change turns on sampleDest MIRROR for
//     `targets` (the sflow command).
//   - `targetIngressMirror`: set if the change binds `targets` to this ingress
//     mirror (the mirror command); otherwise `targets` keep their current one.
// Every other port is taken as it is in `swConfig`.
//
// Throws std::invalid_argument naming the mirrors if the sampling ports would
// end up on more than one ingress mirror.
inline void checkSingleSamplingMirror(
    const cfg::SwitchConfig& swConfig,
    const std::unordered_set<const cfg::Port*>& targets,
    bool targetsSample,
    const std::optional<std::string>& targetIngressMirror) {
  std::set<std::string> samplingMirrors;
  for (const auto& port : *swConfig.ports()) {
    bool isTarget = targets.contains(&port);
    bool sampling = (isTarget && targetsSample) ||
        (port.sampleDest().has_value() &&
         *port.sampleDest() == cfg::SampleDestination::MIRROR);
    if (!sampling) {
      continue;
    }
    if (isTarget && targetIngressMirror.has_value()) {
      samplingMirrors.insert(*targetIngressMirror);
    } else if (port.ingressMirror().has_value()) {
      samplingMirrors.insert(*port.ingressMirror());
    }
  }
  if (samplingMirrors.size() > 1) {
    throw std::invalid_argument(
        fmt::format(
            "Ports with sample-dest mirror must all use the same ingress "
            "mirror; this would leave them on: {}",
            folly::join(", ", samplingMirrors)));
  }
}

} // namespace facebook::fboss
