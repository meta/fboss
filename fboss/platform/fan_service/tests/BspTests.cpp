/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/FbossError.h"
#include "fboss/platform/fan_service/Bsp.h"
#include "fboss/platform/fan_service/FanServiceConfigTypes.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_constants.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_types.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

using namespace facebook::fboss::platform::fan_service;
using facebook::fboss::FbossError;

class BspTest : public ::testing::Test {};

TEST_F(BspTest, emergencyShutdownIdempotent) {
  auto config = FanServiceConfig{};
  auto bsp = Bsp(config);
  // Call with enable=false should be a no-op (no throw)
  EXPECT_NO_THROW(bsp.emergencyShutdown(false));
}

TEST_F(BspTest, OpticTypeFromMediaCode) {
  using facebook::fboss::MediaInterfaceCode;
  namespace constants = fan_service_config_constants;

  // Given: transceiver media codes and the optic type each should select. An
  // UNKNOWN or missing code takes the fallback; LR_10G has no optic type.
  const std::string kFallback = "FALLBACK";
  const std::vector<
      std::pair<std::optional<MediaInterfaceCode>, std::optional<std::string>>>
      cases = {
          {MediaInterfaceCode::DR4_2x800G,
           constants::OPTIC_TYPE_1600_GENERIC()},
          {MediaInterfaceCode::FR4_2x800G,
           constants::OPTIC_TYPE_1600_GENERIC()},
          {MediaInterfaceCode::DR8_1600G, constants::OPTIC_TYPE_1600_GENERIC()},
          {MediaInterfaceCode::CR8_1600G, constants::OPTIC_TYPE_1600_GENERIC()},
          {MediaInterfaceCode::FR8_800G, constants::OPTIC_TYPE_800_GENERIC()},
          {MediaInterfaceCode::UNKNOWN, kFallback},
          {std::nullopt, kFallback},
          {MediaInterfaceCode::LR_10G, std::nullopt},
      };

  for (const auto& [code, expectedOpticType] : cases) {
    // When: fan_service maps the media code to an optic type.
    auto opticType = getOpticTypeFromMediaCode(code, kFallback);

    // Then: the optic type matches the expected one, none for an ignored code.
    EXPECT_EQ(opticType, expectedOpticType)
        << (code ? apache::thrift::util::enumNameSafe(*code) : "no media code");
  }
}

// Every optic type a media code maps to must be one ConfigValidator accepts,
// or no platform config could give that type a thermal table.
TEST_F(BspTest, EveryMappedOpticTypeIsKnown) {
  using facebook::fboss::MediaInterfaceCode;

  // Given: the optic types ConfigValidator accepts, and every media code except
  // UNKNOWN, which takes the fallback instead of a mapped optic type.
  const auto& known = allOpticTypes();
  for (auto code : apache::thrift::TEnumTraits<MediaInterfaceCode>::values) {
    if (code == MediaInterfaceCode::UNKNOWN) {
      continue;
    }

    // When: fan_service maps the media code to an optic type.
    auto opticType = getOpticTypeFromMediaCode(code, "FALLBACK");

    // Then: any mapped optic type is one ConfigValidator accepts.
    if (opticType) {
      EXPECT_NE(std::find(known.begin(), known.end(), *opticType), known.end())
          << apache::thrift::util::enumNameSafe(code) << " -> " << *opticType;
    }
  }
}
