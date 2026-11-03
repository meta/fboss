// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/platform_manager/PmUnitIdentity.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>

namespace facebook::fboss::platform::platform_manager {

PmUnitIdentity identifyPmUnit(
    const SlotTypeConfig& slotTypeConfig,
    const EepromContents& idprom) {
  PmUnitIdentity identity;
  const auto& productName = *idprom.productName();
  if (auto configuredName = slotTypeConfig.pmUnitName()) {
    if (productName != *configuredName) {
      XLOG(WARNING) << fmt::format(
          "The PmUnit name in IDPROM `{}` is different from the one in config "
          "`{}`. Going with the config.",
          productName,
          *configuredName);
    }
    identity.name = *configuredName;
  } else {
    identity.name = productName;
  }

  try {
    PmUnitVersion version;
    version.productionState() =
        static_cast<int16_t>(std::stoi(*idprom.productionState()));
    version.productionSubState() =
        static_cast<int16_t>(std::stoi(*idprom.productionSubState()));
    version.respinVariantIndicator() =
        static_cast<int16_t>(std::stoi(*idprom.variantIndicator()));
    identity.version = version;
  } catch (const std::exception& e) {
    XLOG(WARNING) << fmt::format(
        "Could not parse the version of PmUnit {} from its IDPROM: {}",
        identity.name,
        e.what());
  }
  return identity;
}

} // namespace facebook::fboss::platform::platform_manager
