// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <optional>
#include <string>

#include "fboss/platform/helpers/PlatformFsUtils.h"
#include "fboss/platform/helpers/PlatformUtils.h"

namespace facebook::fboss::platform::helpers {

class PlatformNameLib {
 public:
  auto static constexpr dmidecodeCommand = "dmidecode -s system-product-name";
  auto static constexpr kPlatformNameBiosReadFailures =
      "platform_name_bios_read_failures";
  // TODO: If extending beyond platform name, write out in JSON format instead.
  auto static constexpr kCachePath = "/var/facebook/fboss/platform_name";

  PlatformNameLib(
      const std::shared_ptr<PlatformUtils> platformUtils =
          std::make_shared<PlatformUtils>(),
      const std::shared_ptr<PlatformFsUtils> platformFsUtils =
          std::make_shared<PlatformFsUtils>());

  virtual ~PlatformNameLib() = default;

  // Returns platform name from dmidecode. Platform names are UPPERCASE but
  // otherwise should match config names. Known dmidecode values are mapped to
  // config names.
  virtual std::string getPlatformNameFromBios(bool writeToCache = false) const;

  // Returns platform name matching getPlatformNameFromBios, but (1) allows
  // forcing a name via parameter and (2) returns nullopt instead of throwing
  // exceptions.
  virtual std::optional<std::string> getPlatformName() const;

  // Some platforms do not have the standardized platform-names in dmidecode
  // yet. Maps dmidecode output to the standardized platform-name.
  static std::string sanitizePlatformName(
      const std::string& platformNameFromBios);

 private:
  const std::shared_ptr<PlatformUtils> platformUtils_;
  const std::shared_ptr<PlatformFsUtils> platformFsUtils_;
};

} // namespace facebook::fboss::platform::helpers
