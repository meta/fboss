// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <string>
#include <vector>

#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_constants.h"

namespace facebook::fboss::platform::fan_service {

// Every optic type fan_service understands. Bsp::getOpticTypeFromMediaCode()
// can only produce one of these, and ConfigValidator rejects any other key in
// a platform config. Kept as a single definition so that config validation and
// the per-optic-type counter lifecycle cannot drift apart.
inline const std::vector<std::string>& allOpticTypes() {
  namespace constants = fan_service_config_constants;
  static const std::vector<std::string> kOpticTypes = {
      constants::OPTIC_TYPE_100_GENERIC(),
      constants::OPTIC_TYPE_200_GENERIC(),
      constants::OPTIC_TYPE_400_GENERIC(),
      constants::OPTIC_TYPE_800_GENERIC(),
      constants::OPTIC_TYPE_800_ZR(),
      constants::OPTIC_TYPE_1600_GENERIC()};
  return kOpticTypes;
}

} // namespace facebook::fboss::platform::fan_service
