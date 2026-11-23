// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/platform/bsp_tests/utils/IdpromUtils.h"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <folly/ScopeGuard.h>
#include <folly/logging/xlog.h>

#include "fboss/platform/bsp_tests/utils/CdevUtils.h"
#include "fboss/platform/bsp_tests/utils/I2CUtils.h"
#include "fboss/platform/bsp_tests/utils/KmodUtils.h"
#include "fboss/platform/helpers/PlatformUtils.h"
#include "fboss/platform/platform_manager/PmUnitIdentity.h"
#include "fboss/platform/weutil/FbossEepromInterface.h"

namespace facebook::fboss::platform::bsp_tests {

namespace {

constexpr std::string_view kIdpromSuffix = ".IDPROM";

// RuntimeConfigBuilder names each IDPROM device `<PmUnit>.IDPROM`.
std::optional<std::string> idpromOwner(const I2CDevice& device) {
  const auto& pmName = *device.pmName();
  if (!pmName.ends_with(kIdpromSuffix)) {
    return std::nullopt;
  }
  return pmName.substr(0, pmName.size() - kIdpromSuffix.size());
}

void readIdprom(
    const platform_manager::PlatformConfig& pmConfig,
    const I2CDevice& idprom,
    int busNum,
    PmUnitVersionMap& versions) {
  // False when the device already exists, e.g. left behind by
  // platform_manager; it is then read in place and not removed.
  const bool created = I2CUtils::createI2CDevice(idprom, busNum);
  try {
    const auto& pmUnit = pmConfig.pmUnitConfigs()->at(*idpromOwner(idprom));
    const auto& slotTypeConfig =
        pmConfig.slotTypeConfigs()->at(*pmUnit.pluggedInSlotType());
    const auto identity = platform_manager::identifyPmUnit(
        slotTypeConfig,
        FbossEepromInterface(
            I2CUtils::getI2CDir(busNum, *idprom.address()) + "eeprom",
            *slotTypeConfig.idpromConfig()->offset())
            .getEepromContents());
    if (identity.version && pmConfig.pmUnitConfigs()->contains(identity.name)) {
      versions[identity.name] = *identity.version;
    }
  } catch (const std::exception& e) {
    XLOG(WARNING) << fmt::format(
        "Could not read {} on bus {}: {}", *idprom.pmName(), busNum, e.what());
  }
  if (created) {
    PlatformUtils().execCommand(
        fmt::format(
            "echo {} > /sys/bus/i2c/devices/i2c-{}/delete_device",
            *idprom.address(),
            busNum));
  }
}

// An FPGA adapter left in place would be created a second time, and fail, when
// a later adapter is a mux behind it.
void deleteCreatedAdapters(const std::vector<CreatedI2CAdapter>& adapters) {
  for (const auto& created : adapters) {
    if (auto pci = created.adapter.pciAdapterInfo()) {
      try {
        CdevUtils::deleteDevice(*pci->pciInfo(), *pci->auxData(), created.id);
      } catch (const std::exception& e) {
        XLOG(ERR) << fmt::format(
            "Failed to delete adapter {}: {}",
            *created.adapter.pmName(),
            e.what());
      }
    }
  }
}

// Scoped to PmUnits declaring respins: some IDPROMs are unreadable on a
// healthy board, so an unread IDPROM alone is not notable.
void warnOnUndetectedRespins(
    const platform_manager::PlatformConfig& pmConfig,
    const PmUnitVersionMap& versions) {
  std::vector<std::string> undetected;
  for (const auto& [pmUnitName, _] : *pmConfig.versionedPmUnitConfigs()) {
    if (!versions.contains(pmUnitName)) {
      undetected.push_back(pmUnitName);
    }
  }
  if (!undetected.empty()) {
    XLOG(WARNING) << fmt::format(
        "Could not determine a version for {}, which declare "
        "versionedPmUnitConfigs. They will be validated against their default "
        "PmUnitConfig, which is wrong if the unit is a respin.",
        fmt::join(undetected, ", "));
  }
}

} // namespace

PmUnitVersionMap IdpromUtils::detectPmUnitVersions(
    const platform_manager::PlatformConfig& pmConfig,
    const RuntimeConfig& runtimeConfig) {
  KmodUtils::unloadKmods(*runtimeConfig.kmods());
  KmodUtils::loadKmods(*runtimeConfig.kmods());

  PmUnitVersionMap versions;
  int id = 1;
  for (const auto& [_, adapter] : *runtimeConfig.i2cAdapters()) {
    std::vector<I2CDevice> idproms;
    for (const auto& device : *adapter.i2cDevices()) {
      if (idpromOwner(device)) {
        idproms.push_back(device);
      }
    }
    if (idproms.empty()) {
      continue;
    }

    I2CAdapterCreationResult result;
    try {
      result = I2CUtils::createI2CAdapter(adapter, id);
    } catch (const std::exception& e) {
      XLOG(WARNING) << fmt::format(
          "Could not bring up adapter {} to read its IDPROMs: {}",
          *adapter.pmName(),
          e.what());
      continue;
    }
    id += result.createdAdapters.size();
    SCOPE_EXIT {
      deleteCreatedAdapters(result.createdAdapters);
    };
    for (const auto& idprom : idproms) {
      auto bus = result.buses.find(*idprom.channel());
      if (bus == result.buses.end()) {
        XLOG(WARNING) << fmt::format(
            "Adapter {} has no channel {} for {}",
            *adapter.pmName(),
            *idprom.channel(),
            *idprom.pmName());
        continue;
      }
      readIdprom(pmConfig, idprom, bus->second.busNum, versions);
    }
  }

  warnOnUndetectedRespins(pmConfig, versions);
  return versions;
}

} // namespace facebook::fboss::platform::bsp_tests
