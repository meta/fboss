// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "fboss/cli/fboss2/session/RestartOnlyServiceController.h"
#include "fboss/cli/fboss2/session/SystemdInterface.h"

namespace facebook::fboss {

inline constexpr std::string_view kBgpdUnit = "bgpd";
inline constexpr std::string_view kNetosBgpdUnit = "netos.service.fboss_bgp";

// bgpd: one unit, no warm boot and no config reload RPC.
class BgpServiceController : public RestartOnlyServiceController {
 public:
  BgpServiceController()
      : RestartOnlyServiceController(
            std::string(kBgpdUnit),
            std::string(kNetosBgpdUnit)) {}
  explicit BgpServiceController(std::unique_ptr<SystemdInterface> systemd)
      : RestartOnlyServiceController(
            std::string(kBgpdUnit),
            std::string(kNetosBgpdUnit),
            std::move(systemd)) {}
};

} // namespace facebook::fboss
