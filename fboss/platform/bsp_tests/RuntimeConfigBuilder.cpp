// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/platform/bsp_tests/RuntimeConfigBuilder.h"
#include "fboss/platform/platform_manager/Utils.h"

#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

using namespace facebook::fboss::platform::platform_manager;
using namespace facebook::fboss::platform::bsp_tests;

namespace facebook::fboss::platform::bsp_tests {

fbiob::AuxData RuntimeConfigBuilder::createBaseAuxData(
    const FpgaIpBlockConfig& auxDev,
    fbiob::AuxDeviceType deviceType) {
  fbiob::AuxData auxData;
  auxData.name() = *auxDev.pmUnitScopedName();
  auxData.type() = deviceType;

  fbiob::AuxID auxId;
  auxId.deviceName() = *auxDev.deviceName();
  auxData.id() = std::move(auxId);

  auxData.csrOffset() = *auxDev.csrOffset();
  auxData.iobufOffset() = *auxDev.iobufOffset();

  return auxData;
}

fbiob::AuxData RuntimeConfigBuilder::createLedAuxData(
    const LedCtrlConfig& ledCtrl) {
  auto auxData = createBaseAuxData(
      *ledCtrl.fpgaIpBlockConfig(), fbiob::AuxDeviceType::LED);

  fbiob::LedData ledInfo;
  ledInfo.portNumber() = *ledCtrl.portNumber();
  ledInfo.ledId() = *ledCtrl.ledId();
  auxData.ledData() = ledInfo;

  return auxData;
}

fbiob::AuxData RuntimeConfigBuilder::createSysLedAuxData(
    const FpgaIpBlockConfig& sysLedCtrlConf) {
  auto auxData =
      createBaseAuxData(sysLedCtrlConf, fbiob::AuxDeviceType::SYSLED);

  return auxData;
}

fbiob::AuxData RuntimeConfigBuilder::createGpioAuxData(
    const FpgaIpBlockConfig& gpioChipConf) {
  auto auxData = createBaseAuxData(gpioChipConf, fbiob::AuxDeviceType::GPIO);

  return auxData;
}

fbiob::AuxData RuntimeConfigBuilder::createXcvrAuxData(
    const XcvrCtrlConfig& xcvrCtrl) {
  auto auxData = createBaseAuxData(
      *xcvrCtrl.fpgaIpBlockConfig(), fbiob::AuxDeviceType::XCVR);

  fbiob::XcvrData xcvrInfo;
  xcvrInfo.portNumber() = *xcvrCtrl.portNumber();
  auxData.xcvrData() = xcvrInfo;

  return auxData;
}

fbiob::AuxData RuntimeConfigBuilder::createFanAuxData(
    const FanPwmCtrlConfig& fanCtrl) {
  auto auxData = createBaseAuxData(
      *fanCtrl.fpgaIpBlockConfig(), fbiob::AuxDeviceType::FAN);

  fbiob::FanData fanInfo;
  fanInfo.numFans() = *fanCtrl.numFans();
  auxData.fanData() = fanInfo;

  return auxData;
}

fbiob::AuxData RuntimeConfigBuilder::createSpiAuxData(
    const SpiMasterConfig& spiMaster) {
  auto auxData = createBaseAuxData(
      *spiMaster.fpgaIpBlockConfig(), fbiob::AuxDeviceType::SPI);

  fbiob::SpiData spiInfo;
  spiInfo.numSpidevs() = spiMaster.spiDeviceConfigs()->size();

  std::vector<fbiob::SpiDevInfo> spiDevices;
  for (const auto& spiDev : *spiMaster.spiDeviceConfigs()) {
    fbiob::SpiDevInfo devInfo;
    devInfo.modalias() = *spiDev.modalias();
    devInfo.chipSelect() = *spiDev.chipSelect();
    devInfo.maxSpeedHz() = *spiDev.maxSpeedHz();
    spiDevices.push_back(devInfo);
  }
  spiInfo.spiDevices() = spiDevices;
  auxData.spiData() = spiInfo;

  return auxData;
}

fbiob::AuxData RuntimeConfigBuilder::createMdioAuxData(
    const FpgaIpBlockConfig& mdioBus) {
  auto auxData = createBaseAuxData(mdioBus, fbiob::AuxDeviceType::MDIO_BUS);
  return auxData;
}

facebook::fboss::platform::bsp_tests::I2CAdapter*
RuntimeConfigBuilder::findAdapter(
    std::map<std::string, facebook::fboss::platform::bsp_tests::I2CAdapter>&
        adapters,
    const std::string& unitName,
    const std::string& scopedName) {
  std::string pmName = fmt::format("{}.{}", unitName, scopedName);
  if (!adapters.contains(pmName)) {
    throw std::runtime_error(
        fmt::format("Could not find adapter for {}.{}", unitName, scopedName));
  }
  return &adapters.at(pmName);
}

void RuntimeConfigBuilder::processIdpromDevices(
    const PlatformConfig& pmConfig,
    const PmUnitConfigMap& pmUnits,
    std::map<std::string, facebook::fboss::platform::bsp_tests::I2CAdapter>&
        adapters) {
  // Process idprom devices from the slotTypeConfigs section
  for (const auto& [slotType, slotTypeConfig] : *pmConfig.slotTypeConfigs()) {
    // Skip if this slotType doesn't have an idpromConfig
    if (!slotTypeConfig.idpromConfig().has_value()) {
      continue;
    }

    const IdpromConfig& idpromConfig = *slotTypeConfig.idpromConfig();

    // Find the pmUnit for this slotType
    for (const auto& [pmUnitName, pmUnit] : pmUnits) {
      // Skip if this pmUnit is not of the current slotType
      if (*pmUnit.pluggedInSlotType() != slotType) {
        continue;
      }

      try {
        // Find the actual adapter for the idprom device
        auto [adapterPmUnit, adapterName, channel] = getActualAdapter(
            pmConfig,
            pmUnits,
            pmUnitName,
            *idpromConfig.busName(),
            *pmUnit.pluggedInSlotType());

        if (adapterPmUnit.empty()) {
          XLOG(WARNING) << "Could not find adapter for IDPROM device in "
                        << pmUnitName;
          continue;
        }

        // Create I2C device for the idprom
        I2CDevice i2cDevice;
        i2cDevice.pmName() =
            pmUnitName + ".IDPROM"; // The PmUnitScopedName of the IDPROM device
                                    // is always just "IDPROM"
        i2cDevice.channel() = channel;
        i2cDevice.deviceName() = *idpromConfig.kernelDeviceName();
        i2cDevice.address() = *idpromConfig.address();

        // Find the adapter and add the idprom device to it
        try {
          auto adapter = findAdapter(adapters, adapterPmUnit, adapterName);
          adapter->i2cDevices()->push_back(i2cDevice);
        } catch (const std::exception& ex) {
          XLOG(WARNING) << "Could not find adapter " << adapterPmUnit << "."
                        << adapterName << " for IDPROM device: " << ex.what();
        }
      } catch (const std::exception& ex) {
        XLOG(WARNING) << "Error processing IDPROM device for " << pmUnitName
                      << ": " << ex.what();
      }
    }
  }
}

RuntimeConfig RuntimeConfigBuilder::buildRuntimeConfig(
    const BspTestsConfig& testConfig,
    const PlatformConfig& pmConfig,
    const BspKmodsFile& kmods,
    const std::string& platformName,
    const PmUnitVersionMap& pmUnitVersions) {
  RuntimeConfig config;

  const PmUnitConfigMap pmUnits =
      Utils::resolvePmUnitConfigs(pmConfig, pmUnitVersions);

  auto kmodsToUse = kmods;
  config.platform() = platformName;
  auto& kmodsList = kmodsToUse.bspKmods().value();
  for (auto& kmod : kmodsList) {
    std::replace(kmod.begin(), kmod.end(), '-', '_');
  }
  if (*config.platform() == "MERU800BFA" ||
      *config.platform() == "MERU800BIA") {
    auto it = std::find(kmodsList.begin(), kmodsList.end(), "bp4a_lm90");
    if (it != kmodsList.end()) {
      kmodsList.erase(it);
    }
  }
  // Blackwolf is the only arista platform that uses aadm1266
  if (*config.platform() != "BLACKWOLF800BANW") {
    auto it = std::find(kmodsList.begin(), kmodsList.end(), "aadm1266");
    if (it != kmodsList.end()) {
      kmodsList.erase(it);
    }
  }
  config.kmods() = kmodsToUse;

  std::vector<PciDevice> devices;
  std::map<std::string, I2CAdapter> i2cAdapters;

  config.testData() = *testConfig.testData();

  // Process all PCI devices from the platform manager config
  // Adds i2cAdapters, i2cDevices, and all auxDevices to the actual
  // pmUnit those devices are attached to
  for (const auto& [unitName, pmUnit] : pmUnits) {
    for (const auto& dev : *pmUnit.pciDeviceConfigs()) {
      PciDevice pciDevice;
      pciDevice.pciInfo()->vendorId() = *dev.vendorId();
      pciDevice.pciInfo()->deviceId() = *dev.deviceId();
      pciDevice.pciInfo()->subSystemVendorId() = *dev.subSystemVendorId();
      pciDevice.pciInfo()->subSystemDeviceId() = *dev.subSystemDeviceId();
      pciDevice.pmName() = unitName + "." + *dev.pmUnitScopedName();

      // Initialize the auxDevices field to ensure it's marked as having a value
      pciDevice.auxDevices() = std::vector<fbiob::AuxData>();

      // Add both i2cAdapterConfigs and i2cAdapterBlockConfigs to i2cAdapters
      auto allI2cAdapters = *dev.i2cAdapterConfigs();
      auto i2cAdapterConfigs = Utils::createI2cAdapterConfigs(dev);
      allI2cAdapters.insert(
          allI2cAdapters.end(),
          i2cAdapterConfigs.begin(),
          i2cAdapterConfigs.end());

      for (const auto& adapter : allI2cAdapters) {
        auto auxDev = adapter.fpgaIpBlockConfig();

        fbiob::AuxData auxData;
        auxData.name() = *auxDev->pmUnitScopedName();
        auxData.type() = fbiob::AuxDeviceType::I2C;

        fbiob::AuxID auxId;
        auxId.deviceName() = *auxDev->deviceName();
        auxData.id() = auxId;
        auxData.csrOffset() = *auxDev->csrOffset();
        auxData.iobufOffset() = *auxDev->iobufOffset();

        fbiob::I2cData i2cInfo;
        i2cInfo.numChannels() = (*adapter.numberOfAdapters() == 0)
            ? 1
            : *adapter.numberOfAdapters();
        auxData.i2cData() = i2cInfo;

        I2CAdapter i2cAdapter;
        PciAdapterInfo pciAdapterInfo;
        i2cAdapter.pmName() = unitName + "." + *auxDev->pmUnitScopedName();
        i2cAdapter.busName() = *auxDev->pmUnitScopedName();
        pciAdapterInfo.pciInfo()->vendorId() = *dev.vendorId();
        pciAdapterInfo.pciInfo()->deviceId() = *dev.deviceId();
        pciAdapterInfo.pciInfo()->subSystemVendorId() =
            *dev.subSystemVendorId();
        pciAdapterInfo.pciInfo()->subSystemDeviceId() =
            *dev.subSystemDeviceId();
        pciAdapterInfo.auxData() = auxData;
        i2cAdapter.pciAdapterInfo() = pciAdapterInfo;

        i2cAdapters[*i2cAdapter.pmName()] = i2cAdapter;
      }

      for (const auto& ledCtrl : Utils::createLedCtrlConfigs(dev)) {
        pciDevice.auxDevices()->push_back(createLedAuxData(ledCtrl));
      }
      for (const auto& sysLedCtrl : *dev.sysLedCtrlConfigs()) {
        pciDevice.auxDevices()->push_back(createSysLedAuxData(sysLedCtrl));
      }
      for (const auto& xcvrCtrl : Utils::createXcvrCtrlConfigs(dev)) {
        pciDevice.auxDevices()->push_back(createXcvrAuxData(xcvrCtrl));
      }
      for (const auto& gpioChipConf : *dev.gpioChipConfigs()) {
        pciDevice.auxDevices()->push_back(createGpioAuxData(gpioChipConf));
      }
      for (const auto& fanCtrl : *dev.fanTachoPwmConfigs()) {
        pciDevice.auxDevices()->push_back(createFanAuxData(fanCtrl));
      }
      for (const auto& spiMaster : *dev.spiMasterConfigs()) {
        pciDevice.auxDevices()->push_back(createSpiAuxData(spiMaster));
      }
      for (const auto& mdioBusConfig : Utils::createMdioBusConfigs(dev)) {
        pciDevice.auxDevices()->push_back(createMdioAuxData(mdioBusConfig));
      }
      devices.push_back(pciDevice);
    }
  }

  // Add cpu adapters to adapter list
  for (const auto& adapterName : *pmConfig.i2cAdaptersFromCpu()) {
    I2CAdapter i2cAdapter;
    i2cAdapter.busName() = adapterName;
    i2cAdapter.pmName() =
        fmt::format("{}.{}", *pmConfig.rootPmUnitName(), adapterName);
    i2cAdapter.isCpuAdapter() = true;
    i2cAdapters.insert({*i2cAdapter.pmName(), i2cAdapter});
  }

  // Add any mux adapters - without their parentAdapter. Once All Muxes have
  // been added, populate the parentAdapter field since the parent may be a mux
  // adapter
  for (const auto& [pmUnitName, pmUnit] : pmUnits) {
    for (const auto& i2cDevice : *pmUnit.i2cDeviceConfigs()) {
      if (i2cDevice.numOutgoingChannels().has_value()) {
        I2CAdapter i2cAdapter;
        I2CMuxAdapterInfo muxInfo;
        i2cAdapter.pmName() =
            fmt::format("{}.{}", pmUnitName, *i2cDevice.pmUnitScopedName());

        auto [adapterPmUnit, adapterName, channel] = getActualAdapter(
            pmConfig,
            pmUnits,
            pmUnitName,
            *i2cDevice.busName(),
            *pmUnit.pluggedInSlotType());
        i2cAdapter.busName() = *i2cDevice.pmUnitScopedName();
        muxInfo.numOutgoingChannels() = *i2cDevice.numOutgoingChannels();
        muxInfo.deviceName() = *i2cDevice.kernelDeviceName();
        muxInfo.address() = *i2cDevice.address();
        i2cAdapter.muxAdapterInfo() = muxInfo;
        i2cAdapters.insert({*i2cAdapter.pmName(), i2cAdapter});
      }
    }
  }

  for (const auto& [pmUnitName, pmUnit] : pmUnits) {
    for (const auto& i2cDevice : *pmUnit.i2cDeviceConfigs()) {
      if (i2cDevice.numOutgoingChannels().has_value()) {
        I2CAdapter* i2cAdapter =
            findAdapter(i2cAdapters, pmUnitName, *i2cDevice.pmUnitScopedName());
        auto [adapterPmUnit, adapterName, channel] = getActualAdapter(
            pmConfig,
            pmUnits,
            pmUnitName,
            *i2cDevice.busName(),
            *pmUnit.pluggedInSlotType());
        i2cAdapter->muxAdapterInfo()->parentAdapter() =
            *findAdapter(i2cAdapters, adapterPmUnit, adapterName);
        i2cAdapter->muxAdapterInfo()->parentAdapterChannel() = channel;
      }
    }
  }

  // Add each i2cDevice to the actual adapter that is is attached to.
  // Traverses platform_manager config to find the adapter if "INCOMING"
  // bus is used.
  for (const auto& [pmUnitName, pmUnit] : pmUnits) {
    for (const auto& pmDev : *pmUnit.i2cDeviceConfigs()) {
      auto [adapterPmUnit, adapterName, channel] = getActualAdapter(
          pmConfig,
          pmUnits,
          pmUnitName,
          *pmDev.busName(),
          *pmUnit.pluggedInSlotType());

      if (adapterPmUnit.empty()) {
        throw std::runtime_error(
            fmt::format(
                "Could not find adapter for {}", *pmDev.pmUnitScopedName()));
      }

      // If this is a MUX, it was already added to list of adapters
      if (pmDev.numOutgoingChannels().has_value()) {
        continue;
      }

      // Create I2C device to be added
      I2CDevice i2cDevice;
      i2cDevice.pmName() = pmUnitName + "." + *pmDev.pmUnitScopedName();
      i2cDevice.channel() = channel;
      i2cDevice.deviceName() = *pmDev.kernelDeviceName();
      i2cDevice.address() = *pmDev.address();
      i2cDevice.isGpioChip() = *pmDev.isGpioChip();
      if (pmDev.fanCpldConfig().has_value()) {
        i2cDevice.fanCpldConfig() = *pmDev.fanCpldConfig();
      }

      auto adapter = findAdapter(i2cAdapters, adapterPmUnit, adapterName);
      adapter->i2cDevices()->push_back(i2cDevice);
    }
  }

  // Process idprom devices from the slotTypeConfigs section
  processIdpromDevices(pmConfig, pmUnits, i2cAdapters);

  config.devices() = devices;
  config.i2cAdapters() = i2cAdapters;
  config.expectedErrors() = *testConfig.expectedErrors();

  XLOG(DBG2) << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
      config);
  return config;
}

// Traverses platform_manager config to find the actual adapter for a given
// pmUnit and busName. E.g. "INCOMING@0" will be resolved to the actual
// adapter from the PmUnit which has an outgoingSlotConfig to the
// sourceUnitName
std::tuple<std::string, std::string, int>
RuntimeConfigBuilder::getActualAdapter(
    const PlatformConfig& pmConfig,
    const PmUnitConfigMap& pmUnits,
    const std::string& sourceUnitName,
    const std::string& sourceBusName,
    const std::string& slotType) {
  std::string busName = sourceBusName;
  int busIdx = 0;

  size_t atPos = sourceBusName.find('@');
  if (atPos != std::string::npos) {
    busName = sourceBusName.substr(0, atPos);
    busIdx = std::stoi(sourceBusName.substr(atPos + 1));
  }

  if (busName != "INCOMING") {
    // Check if the full bus name matches a CPU adapter. CPU adapter names
    // include @N as part of their identity (e.g. "CPU_BUS@0"), unlike
    // MUX/FPGA adapters where @N denotes a channel number.
    const auto& cpuAdapters = *pmConfig.i2cAdaptersFromCpu();
    if (std::find(cpuAdapters.begin(), cpuAdapters.end(), sourceBusName) !=
        cpuAdapters.end()) {
      return std::make_tuple(sourceUnitName, sourceBusName, 0);
    }
    // Adapter is in the same PMUnit
    return std::make_tuple(sourceUnitName, busName, busIdx);
  }

  // Look for the adapter in other PM units
  for (const auto& [pmUnitName, pmUnit] : pmUnits) {
    auto outgoingIt = pmUnit.outgoingSlotConfigs()->find(slotType + "@0");
    if (outgoingIt == pmUnit.outgoingSlotConfigs()->end()) {
      continue;
    }

    const platform_manager::SlotConfig& outgoing = outgoingIt->second;
    if (busIdx >= outgoing.outgoingI2cBusNames()->size()) {
      continue;
    }

    std::string actualBusName = (*outgoing.outgoingI2cBusNames())[busIdx];
    if (actualBusName.find("INCOMING") == 0) {
      // If bus in incoming again, we have to repeat the process
      return getActualAdapter(
          pmConfig,
          pmUnits,
          pmUnitName,
          actualBusName,
          *pmUnit.pluggedInSlotType());
    }

    // Check if the resolved bus name matches a CPU adapter (where @N is
    // part of the identity, not a channel number)
    const auto& cpuAdapters = *pmConfig.i2cAdaptersFromCpu();
    if (std::find(cpuAdapters.begin(), cpuAdapters.end(), actualBusName) !=
        cpuAdapters.end()) {
      return std::make_tuple(pmUnitName, actualBusName, 0);
    }

    busIdx = 0;
    atPos = actualBusName.find('@');
    if (atPos != std::string::npos) {
      busName = actualBusName.substr(0, atPos);
      busIdx = std::stoi(actualBusName.substr(atPos + 1));
    }
    return std::make_tuple(pmUnitName, actualBusName.substr(0, atPos), busIdx);
  }
  throw std::runtime_error(
      fmt::format(
          "Unable to find adapter for incoming bus: {}, {}",
          sourceUnitName,
          busName));
}

} // namespace facebook::fboss::platform::bsp_tests
