// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <optional>
#include <string>

#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"
#include "fboss/platform/weutil/if/gen-cpp2/eeprom_contents_types.h"

namespace facebook::fboss::platform::platform_manager {

// The PmUnit an IDPROM identifies, decided the way platform_manager decides it
// while exploring a slot. Shared so that tools which read IDPROMs themselves,
// such as bsp_tests, select the same versionedPmUnitConfigs entry
// platform_manager would.
struct PmUnitIdentity {
  std::string name;
  // Absent unless all three version fields parse.
  std::optional<PmUnitVersion> version;

  bool operator==(const PmUnitIdentity&) const = default;
};

// `slotTypeConfig` is the SlotTypeConfig of the slot the IDPROM belongs to, and
// `idprom` its parsed contents (FbossEepromInterface::getEepromContents()). The
// slot's pmUnitName, when set, wins over the IDPROM's product name; the two are
// known to disagree in the field.
PmUnitIdentity identifyPmUnit(
    const SlotTypeConfig& slotTypeConfig,
    const EepromContents& idprom);

} // namespace facebook::fboss::platform::platform_manager
