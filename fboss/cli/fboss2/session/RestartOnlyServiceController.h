// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/session/SystemdInterface.h"
#include "fboss/cli/fboss2/session/SystemdSequencer.h"

namespace facebook::fboss {

// Controls a service that is one systemd unit and has neither a warm boot nor
// a config reload RPC: every action is a plain restart.
class RestartOnlyServiceController {
 public:
  // netosUnit, if given, is used when the classic unit is not installed.
  explicit RestartOnlyServiceController(
      std::string unit,
      std::optional<std::string> netosUnit = std::nullopt);
  RestartOnlyServiceController(
      std::string unit,
      std::optional<std::string> netosUnit,
      std::unique_ptr<SystemdInterface> systemd);
  virtual ~RestartOnlyServiceController() = default;

  // Returns the unit that was restarted.
  virtual std::vector<std::string> restart();

 private:
  std::string unit_;
  std::optional<std::string> netosUnit_;
  SystemdSequencer sequencer_;
};

} // namespace facebook::fboss
