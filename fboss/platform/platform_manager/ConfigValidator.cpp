// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/platform_manager/ConfigValidator.h"

#include <set>

#include <folly/logging/xlog.h>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/drop.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/split.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/unique.hpp>
#include <re2/re2.h>
#include <thrift/lib/cpp2/op/Get.h>

#include "fboss/platform/platform_manager/CpldManager.h"
#include "fboss/platform/platform_manager/I2cAddr.h"
#include "fboss/platform/platform_manager/Utils.h"
#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_validators_constants.h"
#include "fboss/platform/platform_manager/uapi/fbcpld-ioctl.h"

namespace facebook::fboss::platform::platform_manager {
namespace {
const re2::RE2 kRpmVersionRegex{"^[0-9]+\\.[0-9]+\\.[0-9]+\\-[0-9]+$"};
const re2::RE2 kPciIdRegex{"0x[0-9a-f]{4}"};
const re2::RE2 kPciDevOffsetRegex{"0x[0-9a-f]+"};
const re2::RE2 kSymlinkRegex{"^/run/devmap/(?P<SymlinkDirs>[a-z0-9-]+)/.+"};
const re2::RE2 kDevPathRegex{"(?P<SlotPath>.*)\\[(?P<DeviceName>.+)\\]"};
const re2::RE2 kSlotNameRegex{"(?P<SlotType>.([A-Z]+_)+SLOT)@\\d+"};
const re2::RE2 kSlotPathRegex{"/|(/([A-Z]+_)+SLOT@\\d+)+"};
const re2::RE2 kInfoRomDevicePrefixRegex{"^fpga_info_(dom|iob|scm|mcb)$"};
const re2::RE2 kI2cAdapterNameRegex{"(?P<PmUnitScopedName>.+)@(?P<Num>\\d+)"};
const re2::RE2 kIncomingBusRegex{"INCOMING@(?P<Index>\\d+)"};
const re2::RE2 kRpmNameRegex{"(?P<KEYWORD>[a-z]+)_bsp_kmods"};
constexpr auto kSymlinkDirs = {
    "eeproms",
    "sensors",
    "cplds",
    "fpgas",
    "inforoms",
    "i2c-busses",
    "gpiochips",
    "xcvrs",
    "flashes",
    "watchdogs",
    "mdio-busses",
    "rtms"};
// Supported modalias - spidev +
// https://github.com/torvalds/linux/blob/master/drivers/spi/spidev.c#L702
constexpr auto kSpiDevModaliases = {
    "spidev",
    "dh2228fv",
    "ltc2488",
    "sx1301",
    "bk4",
    "dhcom-board",
    "m53cpld",
    "spi-petra",
    "spi-authenta",
    "em3581",
    "si3210"};
constexpr auto kXcvrDeviceName = "xcvr_ctrl";

bool containsLower(const std::string& s) {
  return std::any_of(s.begin(), s.end(), ::islower);
}

// Tokenize the SlotPath by delimiter '/'
std::vector<std::string> split(const std::string& slotPath) {
  return slotPath | ranges::views::split('/') | ranges::views::drop(1) |
      ranges::to<std::vector<std::string>>;
}

// Returns all PmUnitConfigs that has the given slotType.
std::vector<PmUnitConfig> getPmUnitConfigsBySlotType(
    const PlatformConfig& platformConfig,
    const SlotType& slotType) {
  return *platformConfig.pmUnitConfigs() | ranges::views::values |
      ranges::views::filter([&](const auto& pmUnitConfig) {
        return *pmUnitConfig.pluggedInSlotType() == slotType;
      }) |
      ranges::to_vector;
}

std::optional<SlotType> extractSlotType(const std::string& slotName) {
  SlotType slotType;
  if (!re2::RE2::FullMatch(slotName, kSlotNameRegex, &slotType)) {
    return std::nullopt;
  }
  return slotType;
}

std::optional<SlotType> resolveSlotType(
    const PlatformConfig& platformConfig,
    const std::string& slotPath) {
  std::optional<SlotType> slotType;
  if (slotPath == "/") {
    return *platformConfig.rootSlotType();
  }
  const auto lastSlotName = std::move(split(slotPath).back());
  // Find the SlotType of the lastSlotName.
  return extractSlotType(lastSlotName);
}

} // namespace

bool ConfigValidator::isValidSlotTypeConfig(
    const SlotTypeConfig& slotTypeConfig) {
  if (!slotTypeConfig.idpromConfig() && !slotTypeConfig.pmUnitName()) {
    XLOG(ERR) << "SlotTypeConfig must have either IDPROM or PmUnit name";
    return false;
  }
  if (slotTypeConfig.idpromConfig()) {
    try {
      I2cAddr(*slotTypeConfig.idpromConfig()->address());
    } catch (std::invalid_argument& e) {
      XLOG(ERR) << "IDPROM has invalid address " << e.what();
      return false;
    }
  }
  return true;
}

bool ConfigValidator::isValidSlotConfig(
    const SlotConfig& slotConfig,
    const std::string& slotName,
    const std::map<std::string, SlotTypeConfig>& slotTypeConfigs) {
  if (slotConfig.slotType()->empty()) {
    XLOG(ERR) << "SlotType in SlotConfig must be a non-empty string";
    return false;
  }
  auto slotType = extractSlotType(slotName);
  if (!slotType) {
    XLOG(ERR) << fmt::format(
        "Invalid SlotName format {}. Must follow <SlotType>@<Num>", slotName);
    return false;
  }
  if (*slotType != *slotConfig.slotType()) {
    XLOG(ERR) << fmt::format(
        "SlotName must contain the SlotType {} instead contains {}",
        *slotConfig.slotType(),
        *slotType);
    return false;
  }
  if (slotConfig.presenceDetection()) {
    return isValidPresenceDetection(*slotConfig.presenceDetection());
  }
  // Validate outgoingI2cBusNames size matches numOutgoingI2cBuses in
  // the corresponding SlotTypeConfig
  if (!slotTypeConfigs.contains(*slotConfig.slotType())) {
    XLOG(ERR) << fmt::format(
        "SlotConfig '{}' references SlotType '{}' which has no "
        "SlotTypeConfig definition",
        slotName,
        *slotConfig.slotType());
    return false;
  }
  const auto& slotTypeConfig = slotTypeConfigs.at(*slotConfig.slotType());
  auto actualBuses =
      static_cast<int32_t>(slotConfig.outgoingI2cBusNames()->size());
  auto expectedBuses = *slotTypeConfig.numOutgoingI2cBuses();
  if (actualBuses != expectedBuses) {
    XLOG(ERR) << fmt::format(
        "SlotConfig '{}' has {} outgoingI2cBusNames but SlotTypeConfig "
        "'{}' expects {} (numOutgoingI2cBuses)",
        slotName,
        actualBuses,
        *slotConfig.slotType(),
        expectedBuses);
    return false;
  }
  return true;
}

bool ConfigValidator::isValidFpgaIpBlockConfig(
    const FpgaIpBlockConfig& fpgaIpBlockConfig) {
  if (fpgaIpBlockConfig.pmUnitScopedName()->empty()) {
    XLOG(ERR) << "PmUnitScopedName must be a non-empty string";
    return false;
  }
  if (containsLower(*fpgaIpBlockConfig.pmUnitScopedName())) {
    XLOGF(
        ERR,
        "PmUnitScopedName must be in uppercase; {} contains lowercase characters",
        *fpgaIpBlockConfig.pmUnitScopedName());
    return false;
  }
  if (fpgaIpBlockConfig.pmUnitScopedName()->ends_with('_')) {
    XLOG(ERR) << "PmUnitScopedName must not end with an underscore";
    return false;
  }
  if (!fpgaIpBlockConfig.csrOffset()->empty() &&
      !re2::RE2::FullMatch(
          *fpgaIpBlockConfig.csrOffset(), kPciDevOffsetRegex)) {
    XLOG(ERR) << "Invalid CSR Offset : " << *fpgaIpBlockConfig.csrOffset();
    return false;
  }
  if (!fpgaIpBlockConfig.iobufOffset()->empty() &&
      !re2::RE2::FullMatch(
          *fpgaIpBlockConfig.iobufOffset(), kPciDevOffsetRegex)) {
    XLOG(ERR) << "Invalid IOBuf Offset : " << *fpgaIpBlockConfig.iobufOffset();
    return false;
  }
  return true;
}

bool ConfigValidator::isValidLedCtrlBlockConfig(
    const LedCtrlBlockConfig& ledCtrlBlockConfig) {
  if (ledCtrlBlockConfig.pmUnitScopedNamePrefix()->empty()) {
    XLOG(ERR) << "PmUnitScopedNamePrefix must be a non-empty string";
    return false;
  }
  if (ledCtrlBlockConfig.pmUnitScopedNamePrefix()->ends_with('_')) {
    XLOG(ERR) << "PmUnitScopedNamePrefix must not end with an underscore";
    return false;
  }
  if (ledCtrlBlockConfig.deviceName()->empty()) {
    XLOG(ERR) << "deviceName must be a non-empty string";
    return false;
  }
  if (ledCtrlBlockConfig.csrOffsetCalc()->empty()) {
    XLOG(ERR) << "csrOffsetCalc must be a non-empty string";
    return false;
  }
  if (*ledCtrlBlockConfig.numPorts() <= 0) {
    XLOG(ERR) << "numPorts must be a value greater than 0";
    return false;
  }
  if (*ledCtrlBlockConfig.ledPerPort() <= 0) {
    XLOG(ERR) << "ledPerPort must be a value greater than 0";
    return false;
  }
  if (*ledCtrlBlockConfig.ledPerPort() > 4) {
    XLOG(ERR) << "ledPerPort must be a value less than or equal to 4";
    return false;
  }
  if (*ledCtrlBlockConfig.lanesPerPort() <= 0) {
    XLOG(ERR) << "lanesPerPort must be a value greater than 0";
    return false;
  }
  if (*ledCtrlBlockConfig.lanesPerPort() > 8) {
    XLOG(ERR) << "lanesPerPort must be a value less than or equal to 8";
    return false;
  }
  if (*ledCtrlBlockConfig.startPort() <= 0) {
    XLOG(ERR) << "startPort must be a value greater than 0";
    return false;
  }
  if (*ledCtrlBlockConfig.numPorts() > numXcvrs_) {
    XLOG(ERR) << fmt::format(
        "numPorts must be less than or equal to {}", numXcvrs_);
    return false;
  }
  if (*ledCtrlBlockConfig.startPort() > numXcvrs_) {
    XLOG(ERR) << fmt::format(
        "startPort must be less than or equal to {}", numXcvrs_);
    return false;
  }
  if (*ledCtrlBlockConfig.startPort() + *ledCtrlBlockConfig.numPorts() - 1 >
      numXcvrs_) {
    XLOG(ERR) << fmt::format(
        "startPort + numPorts - 1 must be must be less than or equal to {}",
        numXcvrs_);
    return false;
  }

  for (int16_t port = *ledCtrlBlockConfig.startPort();
       port < *ledCtrlBlockConfig.startPort() + *ledCtrlBlockConfig.numPorts();
       port++) {
    for (int16_t led = 1; led <= *ledCtrlBlockConfig.ledPerPort(); led++) {
      if (!isValidCsrOffsetCalc(
              *ledCtrlBlockConfig.csrOffsetCalc(),
              port,
              *ledCtrlBlockConfig.startPort(),
              led)) {
        return false;
      }

      if (!ledCtrlBlockConfig.iobufOffsetCalc()->empty()) {
        if (!isValidIobufOffsetCalc(
                *ledCtrlBlockConfig.iobufOffsetCalc(),
                port,
                *ledCtrlBlockConfig.startPort(),
                led)) {
          return false;
        }
      }
    }
  }

  return true;
}

bool ConfigValidator::isValidXcvrCtrlBlockConfig(
    const XcvrCtrlBlockConfig& xcvrCtrlBlockConfig) {
  if (xcvrCtrlBlockConfig.pmUnitScopedNamePrefix()->empty()) {
    XLOG(ERR) << "PmUnitScopedNamePrefix must be a non-empty string";
    return false;
  }
  if (xcvrCtrlBlockConfig.pmUnitScopedNamePrefix()->ends_with('_')) {
    XLOG(ERR) << "PmUnitScopedNamePrefix must not end with an underscore";
    return false;
  }
  if (xcvrCtrlBlockConfig.deviceName()->empty()) {
    XLOG(ERR) << "deviceName must be a non-empty string";
    return false;
  }
  if (xcvrCtrlBlockConfig.csrOffsetCalc()->empty()) {
    XLOG(ERR) << "csrOffsetCalc must be a non-empty string";
    return false;
  }
  if (*xcvrCtrlBlockConfig.numPorts() <= 0) {
    XLOG(ERR) << "numPorts must be a value greater than 0";
    return false;
  }
  if (*xcvrCtrlBlockConfig.startPort() <= 0) {
    XLOG(ERR) << "startPort must be a value greater than 0";
    return false;
  }
  if (*xcvrCtrlBlockConfig.numPorts() > numXcvrs_) {
    XLOG(ERR) << fmt::format(
        "numPorts must be less than or equal to {}", numXcvrs_);
    return false;
  }
  if (*xcvrCtrlBlockConfig.startPort() > numXcvrs_) {
    XLOG(ERR) << fmt::format(
        "startPort must be less than or equal to {}", numXcvrs_);
    return false;
  }
  if (*xcvrCtrlBlockConfig.startPort() + *xcvrCtrlBlockConfig.numPorts() - 1 >
      numXcvrs_) {
    XLOG(ERR) << fmt::format(
        "startPort + numPorts - 1 must be must be less than or equal to {}",
        numXcvrs_);
    return false;
  }

  for (int16_t port = *xcvrCtrlBlockConfig.startPort(); port <
       *xcvrCtrlBlockConfig.startPort() + *xcvrCtrlBlockConfig.numPorts();
       port++) {
    if (!isValidCsrOffsetCalc(
            *xcvrCtrlBlockConfig.csrOffsetCalc(),
            port,
            *xcvrCtrlBlockConfig.startPort())) {
      return false;
    }

    if (!xcvrCtrlBlockConfig.iobufOffsetCalc()->empty()) {
      if (!isValidIobufOffsetCalc(
              *xcvrCtrlBlockConfig.iobufOffsetCalc(),
              port,
              *xcvrCtrlBlockConfig.startPort())) {
        return false;
      }
    }
  }

  return true;
}

bool ConfigValidator::isValidI2cAdaptersFromCpu(
    const std::vector<std::string>& i2cAdaptersFromCpu) {
  static const re2::RE2 kCpuBusNameRegex{"CPU_BUS@\\d+"};
  bool hasVirtual = false;
  bool hasExact = false;
  std::set<std::string> seen;
  for (const auto& name : i2cAdaptersFromCpu) {
    if (re2::RE2::FullMatch(name, kCpuBusNameRegex)) {
      static const re2::RE2 kSupportedCpuBusNameRegex{"CPU_BUS@[0-3]"};
      if (!re2::RE2::FullMatch(name, kSupportedCpuBusNameRegex)) {
        XLOG(ERR) << fmt::format(
            "Invalid virtual bus name '{}'. "
            "Only CPU_BUS@0 through CPU_BUS@3 are supported",
            name);
        return false;
      }
      if (!seen.insert(name).second) {
        XLOG(ERR) << fmt::format("Duplicate virtual bus name '{}'", name);
        return false;
      }
      hasVirtual = true;
    } else {
      hasExact = true;
    }
  }
  if (hasVirtual && hasExact) {
    XLOG(ERR)
        << "i2cAdaptersFromCpu must not mix CPU_BUS@N virtual names with exact adapter names";
    return false;
  }
  return true;
}

bool ConfigValidator::isValidI2cAdapterBlockConfig(
    const I2cAdapterBlockConfig& i2cAdapterBlockConfig) {
  if (i2cAdapterBlockConfig.pmUnitScopedNamePrefix()->empty()) {
    XLOG(ERR) << "PmUnitScopedNamePrefix must be a non-empty string";
    return false;
  }
  if (i2cAdapterBlockConfig.pmUnitScopedNamePrefix()->ends_with('_')) {
    XLOG(ERR) << "PmUnitScopedNamePrefix must not end with '_'";
    return false;
  }
  if (i2cAdapterBlockConfig.deviceName()->empty()) {
    XLOG(ERR) << "deviceName must be a non-empty string";
    return false;
  }
  if (i2cAdapterBlockConfig.csrOffsetCalc()->empty()) {
    XLOG(ERR) << "csrOffsetCalc must be a non-empty string";
    return false;
  }
  if (*i2cAdapterBlockConfig.startAdapterIndex() < 0) {
    XLOG(ERR) << "startAdapterIndex must be a value greater than or equal to 0";
    return false;
  }
  if (*i2cAdapterBlockConfig.numAdapters() <= 0) {
    XLOG(ERR) << "numAdapters must be a value greater than 0";
    return false;
  }
  if (*i2cAdapterBlockConfig.numBusesPerAdapter() <= 0) {
    XLOG(ERR) << "numBusesPerAdapter must be a value greater than 0";
    return false;
  }

  for (int32_t adapterIndex = *i2cAdapterBlockConfig.startAdapterIndex();
       adapterIndex < *i2cAdapterBlockConfig.startAdapterIndex() +
           *i2cAdapterBlockConfig.numAdapters();
       adapterIndex++) {
    std::string csrExpression = fmt::format(
        fmt::runtime(*i2cAdapterBlockConfig.csrOffsetCalc()),
        fmt::arg("adapterIndex", adapterIndex),
        fmt::arg(
            "startAdapterIndex", *i2cAdapterBlockConfig.startAdapterIndex()));
    try {
      Utils().evaluateExpression(csrExpression);
    } catch (const std::exception& e) {
      XLOG(ERR) << fmt::format(
          "Invalid csrOffsetCalc expression: {} with adapterIndex={}: {}",
          *i2cAdapterBlockConfig.csrOffsetCalc(),
          adapterIndex,
          e.what());
      return false;
    }

    if (!i2cAdapterBlockConfig.iobufOffsetCalc()->empty()) {
      std::string iobufExpression = fmt::format(
          fmt::runtime(*i2cAdapterBlockConfig.iobufOffsetCalc()),
          fmt::arg("adapterIndex", adapterIndex),
          fmt::arg(
              "startAdapterIndex", *i2cAdapterBlockConfig.startAdapterIndex()));
      try {
        Utils().evaluateExpression(iobufExpression);
      } catch (const std::exception& e) {
        XLOG(ERR) << fmt::format(
            "Invalid iobufOffsetCalc expression: {} with adapterIndex={}: {}",
            *i2cAdapterBlockConfig.iobufOffsetCalc(),
            adapterIndex,
            e.what());
        return false;
      }
    }
  }

  return true;
}

bool ConfigValidator::isValidPciDeviceConfig(
    const PciDeviceConfig& pciDeviceConfig) {
  if (pciDeviceConfig.pmUnitScopedName()->empty()) {
    XLOG(ERR) << "PmUnitScopedName must be a non-empty string";
    return false;
  }
  if (containsLower(*pciDeviceConfig.pmUnitScopedName())) {
    XLOGF(
        ERR,
        "PmUnitScopedName must be in uppercase; {} contains lowercase characters",
        *pciDeviceConfig.pmUnitScopedName());
    return false;
  }
  if (pciDeviceConfig.pmUnitScopedName()->ends_with('_')) {
    XLOG(ERR) << "PmUnitScopedName must not end with an underscore";
    return false;
  }
  if (!re2::RE2::FullMatch(*pciDeviceConfig.vendorId(), kPciIdRegex)) {
    XLOG(ERR) << "Invalid PCI vendor id : " << *pciDeviceConfig.vendorId();
    return false;
  }
  if (!re2::RE2::FullMatch(*pciDeviceConfig.deviceId(), kPciIdRegex)) {
    XLOG(ERR) << "Invalid PCI device id : " << *pciDeviceConfig.deviceId();
    return false;
  }
  if (!pciDeviceConfig.subSystemDeviceId()->empty() &&
      !re2::RE2::FullMatch(*pciDeviceConfig.subSystemVendorId(), kPciIdRegex)) {
    XLOG(ERR) << "Invalid PCI subsystem vendor id : "
              << *pciDeviceConfig.subSystemVendorId();
    return false;
  }
  if (!pciDeviceConfig.subSystemVendorId()->empty() &&
      !re2::RE2::FullMatch(*pciDeviceConfig.subSystemDeviceId(), kPciIdRegex)) {
    XLOG(ERR) << "Invalid PCI subsystem device id : "
              << *pciDeviceConfig.subSystemDeviceId();
    return false;
  }

  std::vector<FpgaIpBlockConfig> fpgaIpBlockConfigs{};
  for (const auto& config : *pciDeviceConfig.i2cAdapterConfigs()) {
    fpgaIpBlockConfigs.push_back(*config.fpgaIpBlockConfig());
  }
  for (const auto& config : *pciDeviceConfig.spiMasterConfigs()) {
    if (!isValidSpiDeviceConfigs(*config.spiDeviceConfigs())) {
      return false;
    }
    fpgaIpBlockConfigs.push_back(*config.fpgaIpBlockConfig());
  }
  for (const auto& config : *pciDeviceConfig.gpioChipConfigs()) {
    fpgaIpBlockConfigs.push_back(config);
  }
  for (const auto& config : *pciDeviceConfig.watchdogConfigs()) {
    fpgaIpBlockConfigs.push_back(config);
  }
  for (const auto& config : *pciDeviceConfig.fanTachoPwmConfigs()) {
    fpgaIpBlockConfigs.push_back(*config.fpgaIpBlockConfig());
  }
  for (const auto& config : *pciDeviceConfig.ledCtrlConfigs()) {
    fpgaIpBlockConfigs.push_back(*config.fpgaIpBlockConfig());
  }
  for (const auto& config : *pciDeviceConfig.sysLedCtrlConfigs()) {
    fpgaIpBlockConfigs.push_back(config);
  }
  for (const auto& config : *pciDeviceConfig.xcvrCtrlConfigs()) {
    if (*config.fpgaIpBlockConfig()->deviceName() != kXcvrDeviceName) {
      XLOG(ERR) << fmt::format(
          "Invalid DeviceName : {} in XcvrCtrlConfig. It must be {}",
          *config.fpgaIpBlockConfig()->deviceName(),
          kXcvrDeviceName);
      return false;
    }
    fpgaIpBlockConfigs.push_back(*config.fpgaIpBlockConfig());
  }
  for (const auto& config : *pciDeviceConfig.infoRomConfigs()) {
    if (!re2::RE2::FullMatch(*config.deviceName(), kInfoRomDevicePrefixRegex)) {
      XLOG(ERR) << fmt::format(
          "Invalid DeviceName : {} in InfoRomConfig. It must follow naming style : fpga_info_[dom|iob]",
          *config.deviceName());
      return false;
    }
    fpgaIpBlockConfigs.push_back(config);
  }
  for (const auto& config : *pciDeviceConfig.miscCtrlConfigs()) {
    fpgaIpBlockConfigs.push_back(config);
  }
  for (const auto& config : *pciDeviceConfig.mdioBusConfigs()) {
    fpgaIpBlockConfigs.push_back(config);
  }

  std::set<std::string> uniqueNames{};
  for (const auto& config : fpgaIpBlockConfigs) {
    auto [it, inserted] = uniqueNames.insert(*config.pmUnitScopedName());
    if (!inserted) {
      XLOG(ERR) << "Duplicate pmUnitScopedName: " << *config.pmUnitScopedName();
      return false;
    }
    if (!isValidFpgaIpBlockConfig(config)) {
      return false;
    }
  }

  for (const auto& config : *pciDeviceConfig.ledCtrlBlockConfigs()) {
    if (!isValidLedCtrlBlockConfig(config)) {
      return false;
    }
  }

  std::vector<std::pair<int16_t, int16_t>> ledPortRanges;
  for (const auto& config : *pciDeviceConfig.ledCtrlBlockConfigs()) {
    ledPortRanges.emplace_back(*config.startPort(), *config.numPorts());
  }
  if (!isValidPortRanges(ledPortRanges)) {
    return false;
  }

  for (const auto& config : *pciDeviceConfig.xcvrCtrlBlockConfigs()) {
    if (!isValidXcvrCtrlBlockConfig(config)) {
      return false;
    }
  }

  std::vector<std::pair<int16_t, int16_t>> xcvrPortRanges;
  for (const auto& config : *pciDeviceConfig.xcvrCtrlBlockConfigs()) {
    xcvrPortRanges.emplace_back(*config.startPort(), *config.numPorts());
  }
  if (!isValidPortRanges(xcvrPortRanges)) {
    return false;
  }

  for (const auto& config : *pciDeviceConfig.i2cAdapterBlockConfigs()) {
    if (!isValidI2cAdapterBlockConfig(config)) {
      return false;
    }
  }

  for (const auto& config : *pciDeviceConfig.rtmCtrlBlockConfigs()) {
    if (!isValidRtmCtrlBlockConfig(config)) {
      return false;
    }
  }

  for (const auto& config : *pciDeviceConfig.mdioBusBlockConfigs()) {
    if (!isValidMdioBusBlockConfig(config)) {
      return false;
    }
  }

  std::vector<std::pair<int16_t, int16_t>> rtmPortRanges;
  for (const auto& config : *pciDeviceConfig.rtmCtrlBlockConfigs()) {
    rtmPortRanges.emplace_back(*config.startPort(), *config.numPorts());
  }
  if (!isValidPortRanges(rtmPortRanges)) {
    return false;
  }

  return true;
}

bool ConfigValidator::isValidI2cDeviceConfig(
    const I2cDeviceConfig& i2cDeviceConfig) {
  try {
    I2cAddr(*i2cDeviceConfig.address());
  } catch (std::invalid_argument& e) {
    XLOG(ERR) << "IDPROM has invalid address " << e.what();
    return false;
  }
  if (i2cDeviceConfig.pmUnitScopedName()->empty()) {
    XLOG(ERR) << "PmUnitScopedName must be a non-empty string";
    return false;
  }
  if (containsLower(*i2cDeviceConfig.pmUnitScopedName())) {
    XLOGF(
        ERR,
        "PmUnitScopedName must be in uppercase; {} contains lowercase characters",
        *i2cDeviceConfig.pmUnitScopedName());
    return false;
  }
  if (i2cDeviceConfig.pmUnitScopedName()->ends_with('_')) {
    XLOG(ERR) << "PmUnitScopedName must not end with an underscore";
    return false;
  }
  if (*i2cDeviceConfig.isEeprom() &&
      !i2cDeviceConfig.pmUnitScopedName()->ends_with("_EEPROM")) {
    XLOGF(
        ERR,
        "isEeprom is true but pmUnitScopedName '{}' does not end with '_EEPROM'",
        *i2cDeviceConfig.pmUnitScopedName());
    return false;
  }
  if (i2cDeviceConfig.eepromOffset() && !*i2cDeviceConfig.isEeprom()) {
    XLOGF(
        ERR,
        "eepromOffset defined while isEeprom is not true for {}",
        *i2cDeviceConfig.pmUnitScopedName());
    return false;
  }
  if (i2cDeviceConfig.cpldSysfsAttrs() &&
      !isValidCpldSysfsAttrs(*i2cDeviceConfig.cpldSysfsAttrs())) {
    return false;
  }
  if (i2cDeviceConfig.fanCpldConfig() &&
      !isValidFanCpldConfig(*i2cDeviceConfig.fanCpldConfig())) {
    return false;
  }
  return true;
}

bool ConfigValidator::isValidFanCpldConfig(const FanCpldConfig& fanCpldConfig) {
  if (*fanCpldConfig.numFans() < 1 || *fanCpldConfig.numFans() > 8) {
    XLOG(ERR) << fmt::format(
        "FanCpldConfig numFans {} out of range (1-8)",
        *fanCpldConfig.numFans());
    return false;
  }
  if (*fanCpldConfig.pwmMax() < 1 || *fanCpldConfig.pwmMax() > 255) {
    XLOG(ERR) << fmt::format(
        "FanCpldConfig pwmMax {} out of range (1-255)",
        *fanCpldConfig.pwmMax());
    return false;
  }
  if (*fanCpldConfig.speedMultiplier() < 1) {
    XLOG(ERR) << fmt::format(
        "FanCpldConfig speedMultiplier {} must be positive",
        *fanCpldConfig.speedMultiplier());
    return false;
  }
  return true;
}

bool ConfigValidator::isValidCpldSysfsAttrs(
    const std::vector<CpldSysfsAttr>& cpldSysfsAttrs) {
  static const re2::RE2 kHexRegex{"0x[0-9a-fA-F]+"};

  if (cpldSysfsAttrs.empty()) {
    XLOG(ERR) << "cpldSysfsAttrs must not be empty";
    return false;
  }
  if (cpldSysfsAttrs.size() > FBCPLD_MAX_ATTRS) {
    XLOG(ERR) << fmt::format(
        "cpldSysfsAttrs has {} entries, exceeds max {}",
        cpldSysfsAttrs.size(),
        FBCPLD_MAX_ATTRS);
    return false;
  }

  std::set<std::string> seenNames;
  for (const auto& attr : cpldSysfsAttrs) {
    if (attr.name()->empty()) {
      XLOG(ERR) << "CpldSysfsAttr name must not be empty";
      return false;
    }
    auto [it, inserted] = seenNames.insert(*attr.name());
    if (!inserted) {
      XLOG(ERR) << fmt::format(
          "Duplicate CpldSysfsAttr name: {}", *attr.name());
      return false;
    }
    if (*attr.mode() != "ro" && *attr.mode() != "rw" && *attr.mode() != "wo") {
      XLOG(ERR) << fmt::format(
          "CpldSysfsAttr '{}' has invalid mode '{}'. Must be 'ro', 'rw', or 'wo'",
          *attr.name(),
          *attr.mode());
      return false;
    }
    if (attr.regAddr()->empty() ||
        !re2::RE2::FullMatch(*attr.regAddr(), kHexRegex)) {
      XLOG(ERR) << fmt::format(
          "CpldSysfsAttr '{}' has invalid regAddr '{}'. Must be hex (e.g. 0x10)",
          *attr.name(),
          *attr.regAddr());
      return false;
    }
    auto regValue = std::stoul(*attr.regAddr(), nullptr, 16);
    if (regValue > 0xFF) {
      XLOG(ERR) << fmt::format(
          "CpldSysfsAttr '{}' has regAddr '{}' out of range. Must be 0x0-0xFF",
          *attr.name(),
          *attr.regAddr());
      return false;
    }
    if (attr.description()->empty()) {
      XLOG(ERR) << fmt::format(
          "CpldSysfsAttr '{}' has empty description", *attr.name());
      return false;
    }
    if (*attr.bitOffset() < 0 || *attr.bitOffset() > 7) {
      XLOG(ERR) << fmt::format(
          "CpldSysfsAttr '{}' has invalid bitOffset {}. Must be 0-7",
          *attr.name(),
          *attr.bitOffset());
      return false;
    }
    if (*attr.numBits() < 1 || *attr.numBits() > 8) {
      XLOG(ERR) << fmt::format(
          "CpldSysfsAttr '{}' has invalid numBits {}. Must be 1-8",
          *attr.name(),
          *attr.numBits());
      return false;
    }
    if (*attr.bitOffset() + *attr.numBits() > 8) {
      XLOG(ERR) << fmt::format(
          "CpldSysfsAttr '{}' bitOffset ({}) + numBits ({}) exceeds 8",
          *attr.name(),
          *attr.bitOffset(),
          *attr.numBits());
      return false;
    }
    for (const auto& flag : *attr.flags()) {
      if (!getCpldFlagMap().contains(flag)) {
        XLOG(ERR) << fmt::format(
            "CpldSysfsAttr '{}' has unrecognized flag '{}'",
            *attr.name(),
            flag);
        return false;
      }
    }
  }
  return true;
}

bool ConfigValidator::isValidSymlink(const std::string& symlink) {
  std::string dir;
  if (!re2::RE2::FullMatch(symlink, kSymlinkRegex, &dir)) {
    XLOG(ERR) << fmt::format("\"{}\" is invalid symlink", symlink);
    return false;
  }
  if (std::find(kSymlinkDirs.begin(), kSymlinkDirs.end(), dir) ==
      kSymlinkDirs.end()) {
    XLOG(ERR) << fmt::format(
        "{} in {} is not predefined symlink directory", dir, symlink);
    return false;
  }
  return true;
}

bool ConfigValidator::isValidDevicePath(
    const PlatformConfig& platformConfig,
    const std::string& devicePath) {
  std::string slotPath, deviceName;
  if (!re2::RE2::FullMatch(devicePath, kDevPathRegex, &slotPath, &deviceName)) {
    XLOG(ERR) << fmt::format("Invalid device path {}", devicePath);
    return false;
  }
  CHECK_EQ(slotPath.back(), '/');
  if (slotPath.length() > 1) {
    slotPath.pop_back();
  }
  if (!isValidSlotPath(platformConfig, slotPath) ||
      !isValidDeviceName(platformConfig, slotPath, deviceName)) {
    return false;
  }
  return true;
}

bool ConfigValidator::isValidSlotPath(
    const PlatformConfig& platformConfig,
    const std::string& slotPath) {
  // Syntactic validation.
  if (!re2::RE2::FullMatch(slotPath, kSlotPathRegex)) {
    XLOG(ERR) << fmt::format("Invalid SlotPath format {}", slotPath);
    return false;
  }

  // Slot topological validation.
  // Starting from the root, check for a PmUnit from CurrSlot to NextSlot.
  auto slotNames = split(slotPath);
  auto currSlotType = *platformConfig.rootSlotType();
  for (const auto& nextSlotName : slotNames) {
    // Find all pmUnits that can be plug into currSlotType.
    auto pmUnitConfigs =
        getPmUnitConfigsBySlotType(platformConfig, currSlotType);
    if (pmUnitConfigs.empty()) {
      XLOG(ERR) << fmt::format(
          "Couldn't find PmUnitConfigs that can be plug-into {} in SlotPath {}",
          currSlotType,
          slotPath);
      return false;
    }
    // Find next SlotType from the found PmUnits' outgoingSlotConfig of
    // nextSlotName to verify that PmUnit sits between CurrSlot and
    // NextSlot.
    auto nextSlotType =
        pmUnitConfigs | ranges::views::filter([&](const auto& pmUnitConfig) {
          return pmUnitConfig.outgoingSlotConfigs()->contains(nextSlotName);
        }) |
        ranges::views::transform([&](const auto& pmUnitConfig) -> SlotType {
          return *pmUnitConfig.outgoingSlotConfigs()
                      ->at(nextSlotName)
                      .slotType();
        }) |
        ranges::views::unique | ranges::to_vector;
    if (nextSlotType.size() != 1) {
      XLOG(ERR) << fmt::format(
          "Invalid SlotName {}. It maps to {} SlotConfig(s)",
          nextSlotName,
          nextSlotType.size());
      return false;
    }
    currSlotType = nextSlotType.front();
  }
  return true;
}

bool ConfigValidator::isValidDeviceName(
    const PlatformConfig& platformConfig,
    const std::string& slotPath,
    const std::string& deviceName) {
  auto slotType = resolveSlotType(platformConfig, slotPath);
  CHECK(slotType) << "SlotType must be nonnull";

  // If the device is IDPROM, search from the SlotTypeConfigs.
  if (deviceName == "IDPROM") {
    if (!platformConfig.slotTypeConfigs()->contains(*slotType)) {
      XLOG(ERR) << fmt::format(
          "Found no SlotTypeConfig for SlotType {}", *slotType);
      return false;
    }
    const auto& slotTypeConfig =
        platformConfig.slotTypeConfigs()->at(*slotType);
    if (!slotTypeConfig.idpromConfig()) {
      XLOG(ERR) << fmt::format(
          "Unexpected IDPROM at SlotPath {}. IdpromConfig is not defined at {}",
          slotPath,
          *slotType);
      return false;
    }
    return true;
  }

  // Lazy-load the cache on first use
  buildDeviceNameCache(platformConfig);
  auto it = deviceNamesBySlotType_->find(*slotType);
  if (it != deviceNamesBySlotType_->end() && it->second.contains(deviceName)) {
    return true;
  }

  XLOG(ERR) << fmt::format(
      "Invalid DeviceName {} at SlotPath {}", deviceName, slotPath);
  return false;
}

bool ConfigValidator::isValidPlatformWithoutPmOptics(
    const PlatformConfig& config) {
  const auto& platforms =
      platform_manager_validators_constants::PLATFORMS_WITHOUT_PM_OPTICS();
  if (std::find(platforms.begin(), platforms.end(), *config.platformName()) ==
      platforms.end()) {
    return true;
  }
  auto fail = [&](std::string_view reason) {
    XLOGF(ERR, "Platform {}: {}", *config.platformName(), reason);
    return false;
  };
  if (*config.numXcvrs() != 0) {
    return fail("must not have numXcvrs set");
  }
  for (const auto& [_, pmUnitCfg] : *config.pmUnitConfigs()) {
    for (const auto& pciDev : *pmUnitCfg.pciDeviceConfigs()) {
      if (!pciDev.xcvrCtrlBlockConfigs()->empty() ||
          !pciDev.ledCtrlBlockConfigs()->empty()) {
        return fail("must not have xcvr/led block configs");
      }
    }
  }
  for (const auto& [symlink, _] : *config.symbolicLinkToDevicePath()) {
    if (symlink.starts_with("/run/devmap/xcvrs/")) {
      return fail("must not have xcvr symlinks");
    }
  }
  return true;
}

bool ConfigValidator::isValid(const PlatformConfig& config) {
  XLOG(INFO) << "Validating platform_manager config";

  // Store numXcvrs for use by other validation methods
  numXcvrs_ = *config.numXcvrs();
  numRtms_ = *config.numRtms();

  // Verify presence of platform name
  if (config.platformName()->empty()) {
    XLOG(ERR) << "Platform name cannot be empty";
    return false;
  }

  // Verify platformName is in uppercase
  if (containsLower(*config.platformName())) {
    XLOGF(
        ERR,
        "Platform name must be in uppercase; {} contains lowercase characters",
        *config.platformName());
    return false;
  }

  if (!isValidPlatformWithoutPmOptics(config)) {
    return false;
  }

  if (config.rootSlotType()->empty()) {
    XLOG(ERR) << "Platform rootSlotType cannot be empty";
    return false;
  }

  if (config.slotTypeConfigs()->find(*config.rootSlotType()) ==
      config.slotTypeConfigs()->end()) {
    XLOG(ERR) << fmt::format(
        "Invalid rootSlotType {}. Not found in slotTypeConfigs",
        *config.rootSlotType());
    return false;
  }

  // Validate i2cAdaptersFromCpu entries: must all be either CPU_BUS@N
  // virtual names or exact adapter names, not a mix.
  if (!isValidI2cAdaptersFromCpu(*config.i2cAdaptersFromCpu())) {
    return false;
  }

  // Validate chassisEepromDevicePath
  if (!isValidChassisEepromDevicePath(
          config, *config.chassisEepromDevicePath())) {
    return false;
  }

  // Validate SlotTypeConfigs.
  for (const auto& [slotName, slotTypeConfig] : *config.slotTypeConfigs()) {
    XLOG(INFO) << fmt::format(
        "Validating SlotTypeConfig for Slot {}...", slotName);
    if (!isValidSlotTypeConfig(slotTypeConfig)) {
      return false;
    }
    // Validate IDPROM busName is directly connected (no MUX/FPGA in between)
    if (slotTypeConfig.idpromConfig()) {
      const auto& busName = *slotTypeConfig.idpromConfig()->busName();
      int index;
      bool isIncomingBus =
          re2::RE2::FullMatch(busName, kIncomingBusRegex, &index);
      bool isCpuBus = std::find(
                          config.i2cAdaptersFromCpu()->begin(),
                          config.i2cAdaptersFromCpu()->end(),
                          busName) != config.i2cAdaptersFromCpu()->end();
      if (!isIncomingBus && !isCpuBus) {
        XLOG(ERR) << fmt::format(
            "IDPROM busName '{}' in SlotTypeConfig '{}' must be either an "
            "INCOMING@N bus or a CPU I2C adapter from i2cAdaptersFromCpu. "
            "IDPROM must be directly connected without MUX or FPGA in between.",
            busName,
            slotName);
        return false;
      }
      if (isIncomingBus && index >= *slotTypeConfig.numOutgoingI2cBuses()) {
        XLOG(ERR) << fmt::format(
            "IDPROM busName '{}' in SlotTypeConfig '{}' references bus index "
            "{} but numOutgoingI2cBuses is {}",
            busName,
            slotName,
            index,
            *slotTypeConfig.numOutgoingI2cBuses());
        return false;
      }
    }
    // Validate that pmUnitName in slotTypeConfig exists in pmUnitConfigs
    if (slotTypeConfig.pmUnitName() &&
        !config.pmUnitConfigs()->contains(*slotTypeConfig.pmUnitName())) {
      XLOG(ERR) << fmt::format(
          "PMUnit name '{}' in SlotTypeConfig '{}' does not exist in pmUnitConfigs",
          *slotTypeConfig.pmUnitName(),
          slotName);
      return false;
    }
  }

  for (const auto& [pmUnitName, pmUnitConfig] : *config.pmUnitConfigs()) {
    XLOG(INFO) << fmt::format(
        "Validating PmUnitConfig for PmUnit {} in Slot {}...",
        pmUnitName,
        *pmUnitConfig.pluggedInSlotType());

    // Validate that pmUnitName is in the allowed list
    if (std::find(
            platform_manager_validators_constants::ALLOWED_PMUNIT_NAMES()
                .begin(),
            platform_manager_validators_constants::ALLOWED_PMUNIT_NAMES().end(),
            pmUnitName) ==
        platform_manager_validators_constants::ALLOWED_PMUNIT_NAMES().end()) {
      XLOG(ERR) << fmt::format(
          "PMUnit name '{}' is not in the allowed list of PMUnit names",
          pmUnitName);
      return false;
    }

    if (!isValidPmUnitConfig(*config.slotTypeConfigs(), pmUnitConfig)) {
      return false;
    }

    if (!isValidLogicalEeprom(config, pmUnitName, pmUnitConfig)) {
      return false;
    }
  }

  for (const auto& [pmUnitName, versionedPmUnitConfigs] :
       *config.versionedPmUnitConfigs()) {
    XLOG(INFO) << fmt::format(
        "Validating VersionedPmUnitConfigs for PmUnit {}...", pmUnitName);

    auto defaultConfigIt = config.pmUnitConfigs()->find(pmUnitName);

    // Validate that PMUnit name exists in pmUnitConfigs
    if (defaultConfigIt == config.pmUnitConfigs()->end()) {
      XLOG(ERR) << fmt::format(
          "PMUnit name '{}' in versionedPmUnitConfigs does not exist in pmUnitConfigs",
          pmUnitName);
      return false;
    }

    if (!isValidVersionedPmUnitConfig(
            pmUnitName,
            versionedPmUnitConfigs,
            defaultConfigIt->second,
            *config.slotTypeConfigs())) {
      return false;
    }
  }

  XLOG(INFO) << "Validating Symbolic links...";
  for (const auto& [symlink, devicePath] : *config.symbolicLinkToDevicePath()) {
    if (!isValidSymlink(symlink)) {
      return false;
    }
    if (!isValidDevicePath(config, devicePath)) {
      return false;
    }
  }

  XLOG(INFO) << "Validating Transceiver symbolic links...";
  auto symlinks = *config.symbolicLinkToDevicePath() | ranges::views::keys |
      ranges::to<std::vector<std::string>>();
  if (!isValidXcvrSymlinks(*config.numXcvrs(), symlinks)) {
    return false;
  }

  if (!isValidBspKmodsRpmVersion(*config.bspKmodsRpmVersion())) {
    return false;
  }

  if (!isValidBspKmodsRpmName(*config.bspKmodsRpmName())) {
    return false;
  }

  XLOG(INFO) << "Validating xcvr LED coverage...";
  if (!isValidLedCtrlBlockXcvrCoverage(config)) {
    return false;
  }

  XLOG(INFO) << "Validating xcvr ctrl coverage...";
  if (!isValidXcvrCtrlBlockXcvrCoverage(config)) {
    return false;
  }

  return true;
}

bool ConfigValidator::isValidPmUnitConfig(
    const std::map<std::string, SlotTypeConfig>& slotTypeConfigs,
    const PmUnitConfig& pmUnitConfig) {
  if (!slotTypeConfigs.contains(*pmUnitConfig.pluggedInSlotType())) {
    XLOG(ERR) << fmt::format(
        "Plugged-into Slot {} which has a missing SlotTypeConfig definition",
        *pmUnitConfig.pluggedInSlotType());
    return false;
  }

  // Validate PciDeviceConfigs
  for (const auto& pciDeviceConfig : *pmUnitConfig.pciDeviceConfigs()) {
    if (!isValidPciDeviceConfig(pciDeviceConfig)) {
      return false;
    }
  }

  // Validate I2cDeviceConfigs
  for (const auto& i2cDeviceConfig : *pmUnitConfig.i2cDeviceConfigs()) {
    if (!isValidI2cDeviceConfig(i2cDeviceConfig)) {
      return false;
    }
  }

  // Validate SlotConfigs
  for (const auto& [slotName, slotConfig] :
       *pmUnitConfig.outgoingSlotConfigs()) {
    if (!isValidSlotConfig(slotConfig, slotName, slotTypeConfigs)) {
      return false;
    }
  }
  return true;
}

bool ConfigValidator::isValidPresenceDetection(
    const PresenceDetection& presenceDetection) {
  if (presenceDetection.gpioLineHandle() &&
      presenceDetection.sysfsFileHandle()) {
    XLOG(ERR)
        << "Only one of GpioLineHandle or SysfsFileHandle must be set for PresenceDetection";
    return false;
  }
  if (!presenceDetection.gpioLineHandle() &&
      !presenceDetection.sysfsFileHandle()) {
    XLOG(ERR)
        << "GpioLineHandle or SysfsFileHandle must be set for PresenceDetection";
    return false;
  }
  if (presenceDetection.gpioLineHandle()) {
    if (presenceDetection.gpioLineHandle()->devicePath()->empty()) {
      XLOG(ERR) << "devicePath for GpioLineHandle cannot be empty";
      return false;
    }
    if (presenceDetection.gpioLineHandle()->desiredValue() < 0) {
      XLOG(ERR)
          << "desiredValue for GpioLineHandle cannot be < 0. Typically 0 or 1";
      return false;
    }
  }
  if (presenceDetection.sysfsFileHandle()) {
    if (presenceDetection.sysfsFileHandle()->devicePath()->empty()) {
      XLOG(ERR) << "devicePath for SysfsFileHandle cannot be empty";
      return false;
    }
    if (*presenceDetection.sysfsFileHandle()->desiredValue() == 0) {
      XLOG(ERR) << "desiredValue for SysfsFileHandle cannot be 0. Typically 1";
      return false;
    }
    if (presenceDetection.sysfsFileHandle()->presenceFileName()->empty()) {
      XLOG(ERR) << "presenceFileName for SysfsFileHandle cannot be empty";
      return false;
    }
  }
  return true;
}

bool ConfigValidator::isValidSpiDeviceConfigs(
    const std::vector<SpiDeviceConfig>& spiDeviceConfigs) {
  if (spiDeviceConfigs.empty()) {
    XLOG(ERR) << "Invalid empty SpiDeviceConfigs";
    return false;
  }
  std::vector<bool> seenChipSelects(spiDeviceConfigs.size(), false);
  for (const auto& spiDeviceConfig : spiDeviceConfigs) {
    if (spiDeviceConfig.pmUnitScopedName()->empty()) {
      XLOG(ERR) << fmt::format(
          "PmUnitScopedName must be a non-empty string in SpiDeviceConfig");
      return false;
    }
    if (containsLower(*spiDeviceConfig.pmUnitScopedName())) {
      XLOGF(
          ERR,
          "PmUnitScopedName must be in uppercase; {} contains lowercase characters",
          *spiDeviceConfig.pmUnitScopedName());
      return false;
    }
    if (spiDeviceConfig.pmUnitScopedName()->ends_with('_')) {
      XLOG(ERR) << "PmUnitScopedName must not end with an underscore";
      return false;
    }
    if (spiDeviceConfig.modalias()->length() >= NAME_MAX) {
      XLOG(ERR) << fmt::format(
          "Modalias exceeded the {} characters limit for SpiDevice {}",
          NAME_MAX,
          *spiDeviceConfig.pmUnitScopedName());
      return false;
    }
    if (std::find(
            kSpiDevModaliases.begin(),
            kSpiDevModaliases.end(),
            *spiDeviceConfig.modalias()) == kSpiDevModaliases.end()) {
      XLOG(ERR) << fmt::format(
          "Unsupported modalias {} for SpiDevice {}",
          *spiDeviceConfig.modalias(),
          *spiDeviceConfig.pmUnitScopedName());
      return false;
    }
    if (*spiDeviceConfig.chipSelect() < 0 ||
        *spiDeviceConfig.chipSelect() >= spiDeviceConfigs.size()) {
      XLOG(ERR) << fmt::format(
          "Out of range chipselect value {} for SpiDevice {}",
          *spiDeviceConfig.chipSelect(),
          *spiDeviceConfig.pmUnitScopedName());
      return false;
    }
    CHECK_EQ(seenChipSelects.size(), spiDeviceConfigs.size());
    if (seenChipSelects[*spiDeviceConfig.chipSelect()]) {
      XLOG(ERR) << fmt::format(
          "Duplicate chipselect value {} for SpiDevice {}",
          *spiDeviceConfig.chipSelect(),
          *spiDeviceConfig.pmUnitScopedName());
      return false;
    }
    seenChipSelects[*spiDeviceConfig.chipSelect()] = true;
    if (*spiDeviceConfig.maxSpeedHz() <= 0) {
      XLOG(ERR) << fmt::format(
          "Invalid maxSpeedHz {} for SpiDevice {}",
          *spiDeviceConfig.maxSpeedHz(),
          *spiDeviceConfig.pmUnitScopedName());
      return false;
    }
  }
  return true;
}

bool ConfigValidator::isValidBspKmodsRpmVersion(
    const std::string& bspKmodsRpmVersion) {
  if (bspKmodsRpmVersion.empty()) {
    XLOG(ERR) << "BspKmodsRpmVersion cannot be empty";
    return false;
  }
  if (!re2::RE2::FullMatch(bspKmodsRpmVersion, kRpmVersionRegex)) {
    XLOG(ERR) << fmt::format(
        "Invalid BspKmodsRpmVersion : {}", bspKmodsRpmVersion);
    return false;
  }
  return true;
}

bool ConfigValidator::isValidPmUnitName(
    const PlatformConfig& platformConfig,
    const std::string& slotPath,
    const std::string& pmUnitName) {
  if (!platformConfig.pmUnitConfigs()->contains(pmUnitName)) {
    XLOG(ERR) << fmt::format("Undefined PmUnitConfig for {}", pmUnitName);
    return false;
  }
  const auto& pmUnitConfig = platformConfig.pmUnitConfigs()->at(pmUnitName);
  const auto slotType = resolveSlotType(platformConfig, slotPath);
  if (!slotType || *slotType != *pmUnitConfig.pluggedInSlotType()) {
    XLOG(ERR) << fmt::format(
        "Unexpected SlotType {} for PmUnit {}. Expected SlotType {} ",
        *slotType,
        pmUnitName,
        *pmUnitConfig.pluggedInSlotType());
    return false;
  }
  return true;
}

bool ConfigValidator::isValidBspKmodsRpmName(
    const std::string& bspKmodsRpmName) {
  if (bspKmodsRpmName.empty()) {
    XLOG(ERR) << "BspKmodsRpmName cannot be empty";
    return false;
  }
  std::string keyword{};
  if (!re2::RE2::FullMatch(bspKmodsRpmName, kRpmNameRegex, &keyword)) {
    XLOG(ERR) << fmt::format("Invalid BspKmodsRpmName : {}", bspKmodsRpmName);
    return false;
  }
  return true;
}

bool ConfigValidator::isValidXcvrSymlinks(
    int16_t numXcvrs,
    const std::vector<std::string>& symlinks) {
  std::set<std::string> xcvrCtrlSymlinks, xcvrIoSymlinks;
  for (const auto& symlink : symlinks) {
    if (symlink.starts_with("/run/devmap/xcvrs/xcvr_ctrl_")) {
      xcvrCtrlSymlinks.insert(symlink);
    }
    if (symlink.starts_with("/run/devmap/xcvrs/xcvr_io_")) {
      xcvrIoSymlinks.insert(symlink);
    }
  }
  if (xcvrCtrlSymlinks.size() != numXcvrs) {
    XLOG(ERR) << fmt::format(
        "Expected {} xcvr control symlinks, but found {}",
        numXcvrs,
        xcvrCtrlSymlinks.size());
    return false;
  }
  if (xcvrIoSymlinks.size() != numXcvrs) {
    XLOG(ERR) << fmt::format(
        "Expected {} xcvr IO symlinks, but found {}",
        numXcvrs,
        xcvrIoSymlinks.size());
    return false;
  }

  for (int16_t xcvrId = 1; xcvrId <= numXcvrs; xcvrId++) {
    auto xcvrCtrlSymlink =
        fmt::format("/run/devmap/xcvrs/xcvr_ctrl_{}", xcvrId);
    if (!xcvrCtrlSymlinks.contains(xcvrCtrlSymlink)) {
      XLOG(ERR) << fmt::format(
          "Missing xcvr control symlink for xcvr {}", xcvrId);
      return false;
    }
    auto xcvrIoSymlink = fmt::format("/run/devmap/xcvrs/xcvr_io_{}", xcvrId);
    if (!xcvrIoSymlinks.contains(xcvrIoSymlink)) {
      XLOG(ERR) << fmt::format("Missing xcvr IO symlink for xcvr {}", xcvrId);
      return false;
    }
  }
  return true;
}

bool ConfigValidator::isValidCsrOffsetCalc(
    const std::string& csrOffsetCalc,
    const int16_t& portNum,
    const int16_t& startPort,
    std::optional<int16_t> ledNum) {
  // Test the expression with sample values to see if it's computable
  try {
    // Use Utils to test if the expression can be compiled and evaluated
    auto result =
        Utils().computeHexExpression(csrOffsetCalc, portNum, startPort, ledNum);

    // Validate the resulting hex value
    if (result.empty()) {
      XLOG(ERR) << "csrOffsetCalc expression resulted in empty value";
      return false;
    }
    if (!result.starts_with("0x")) {
      XLOG(ERR)
          << "csrOffsetCalc expression result is not in valid hex format: "
          << result;
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    XLOG(ERR) << "csrOffsetCalc expression validation failed: " << e.what();
    return false;
  }
}

bool ConfigValidator::isValidIobufOffsetCalc(
    const std::string& iobufOffsetCalc,
    const int16_t& portNum,
    const int16_t& startPort,
    std::optional<int16_t> ledNum) {
  // Test the expression with sample values to see if it's computable
  try {
    // Use Utils to test if the expression can be compiled and evaluated
    auto result = Utils().computeHexExpression(
        iobufOffsetCalc, portNum, startPort, ledNum);

    // Validate the resulting hex value
    if (result.empty()) {
      XLOG(ERR) << "iobufOffsetCalc expression resulted in empty value";
      return false;
    }
    if (!result.starts_with("0x")) {
      XLOG(ERR)
          << "iobufOffsetCalc expression result is not in valid hex format: "
          << result;
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    XLOG(ERR) << "iobufOffsetCalc expression validation failed: " << e.what();
    return false;
  }
}

bool ConfigValidator::isValidVersionedPmUnitConfig(
    const std::string& pmUnitName,
    const std::vector<VersionedPmUnitConfig>& versionedPmUnitConfigs,
    const PmUnitConfig& defaultPmUnitConfig,
    const std::map<std::string, SlotTypeConfig>& slotTypeConfigs) {
  using apache::thrift::op::get;
  using apache::thrift::op::get_name_v;
  using apache::thrift::op::get_value_or_null;
  namespace ident = apache::thrift::ident;

  if (versionedPmUnitConfigs.empty()) {
    XLOG(ERR) << fmt::format(
        "VersionedPmUnitConfigs for {} must not be empty", pmUnitName);
    return false;
  }

  for (const auto& versionedPmUnitConfig : versionedPmUnitConfigs) {
    if (auto pmUvs = versionedPmUnitConfig.pmUnitVersions();
        pmUvs && !pmUvs->empty()) {
      for (const auto& pmUv : *pmUvs) {
        if (*pmUv.productionState() < 0 || *pmUv.productionSubState() < 0 ||
            *pmUv.respinVariantIndicator() < 0) {
          XLOG(ERR) << fmt::format(
              "PmUnit {}'s VersionedPmUnitConfig has invalid pmUnitVersion "
              "{}.{}.{}: all fields must be >= 0",
              pmUnitName,
              *pmUv.productionState(),
              *pmUv.productionSubState(),
              *pmUv.respinVariantIndicator());
          return false;
        }
      }
    } else if (versionedPmUnitConfig.productSubVersion()) {
      if (*versionedPmUnitConfig.productSubVersion() < 0) {
        XLOG(ERR) << fmt::format(
            "One of PmUnit {}'s VersionedPmUnitConfig has a negative ProductSubVersion",
            pmUnitName);
        return false;
      }
    } else {
      XLOG(ERR) << fmt::format(
          "PmUnit {}'s VersionedPmUnitConfig must set at least one of "
          "productSubVersion or pmUnitVersions",
          pmUnitName);
      return false;
    }

    bool fieldMismatch = false;
    apache::thrift::op::for_each_field_id<PmUnitConfig>([&]<class Id>(Id) {
      // i2cDeviceConfigs, embeddedSensorConfigs and pciDeviceConfigs are
      // allowed to differ between versioned and default configs
      if constexpr (
          std::is_same_v<
              apache::thrift::op::get_ident<PmUnitConfig, Id>,
              ident::i2cDeviceConfigs> ||
          std::is_same_v<
              apache::thrift::op::get_ident<PmUnitConfig, Id>,
              ident::embeddedSensorConfigs> ||
          std::is_same_v<
              apache::thrift::op::get_ident<PmUnitConfig, Id>,
              ident::pciDeviceConfigs>) {
        return;
      }

      const auto& defaultFieldRef = get<Id>(defaultPmUnitConfig);
      const auto& versionedFieldRef =
          get<Id>(*versionedPmUnitConfig.pmUnitConfig());

      const auto* defaultValue = get_value_or_null(defaultFieldRef);
      const auto* versionedValue = get_value_or_null(versionedFieldRef);

      // Check if fields mismatch:
      if ((defaultValue && versionedValue &&
           *defaultValue != *versionedValue) ||
          (defaultValue && !versionedValue) ||
          (!defaultValue && versionedValue)) {
        XLOG(ERR) << fmt::format(
            "The {} of VersionedPmUnitConfig {}, does not match default config",
            get_name_v<PmUnitConfig, Id>,
            pmUnitName);
        fieldMismatch = true;
      }
    });

    if (fieldMismatch) {
      return false;
    }

    if (!isValidPmUnitConfig(
            slotTypeConfigs, *versionedPmUnitConfig.pmUnitConfig())) {
      return false;
    }

    if (!isValidVersionedPciDeviceCoverage(
            defaultPmUnitConfig, versionedPmUnitConfig)) {
      return false;
    }
  }
  return true;
}

bool ConfigValidator::isValidLedCtrlBlockXcvrCoverage(
    const PlatformConfig& config) {
  if (*config.numXcvrs() == 0) {
    return true;
  }

  const auto& platformName = *config.platformName();

  // TODO: Remove once ladakh/leh ledCtrlBlockConfigs cover all xcvrs
  if (platformName == "LADAKH800BCLS" || platformName == "LEH800BCLS" ||
      platformName == "LADAKH800BCLSM") {
    return true;
  }

  // Collect all ledCtrlBlockConfigs across all PmUnits/PciDevices
  std::vector<const LedCtrlBlockConfig*> allLedBlocks;
  for (const auto& [_, pmUnitConfig] : *config.pmUnitConfigs()) {
    for (const auto& pciDev : *pmUnitConfig.pciDeviceConfigs()) {
      for (const auto& block : *pciDev.ledCtrlBlockConfigs()) {
        allLedBlocks.push_back(&block);
      }
    }
  }

  // Check that every xcvr port [1, numXcvrs] is covered by ledCtrlBlockConfigs
  std::set<int16_t> coveredPorts;
  for (const auto* block : allLedBlocks) {
    for (int16_t port = *block->startPort();
         port < *block->startPort() + *block->numPorts();
         ++port) {
      coveredPorts.insert(port);
    }
  }

  bool valid = true;
  for (int16_t xcvrId = 1; xcvrId <= *config.numXcvrs(); ++xcvrId) {
    if (coveredPorts.find(xcvrId) == coveredPorts.end()) {
      XLOG(ERR) << fmt::format(
          "Platform {}: xcvr {} is not covered by any ledCtrlBlockConfig",
          platformName,
          xcvrId);
      valid = false;
    }
  }

  return valid;
}

bool ConfigValidator::isValidXcvrCtrlBlockXcvrCoverage(
    const PlatformConfig& config) {
  if (*config.numXcvrs() == 0) {
    return true;
  }

  const auto& platformName = *config.platformName();

  std::set<int16_t> coveredPorts;
  for (const auto& [_, pmUnitConfig] : *config.pmUnitConfigs()) {
    for (const auto& pciDev : *pmUnitConfig.pciDeviceConfigs()) {
      for (const auto& block : *pciDev.xcvrCtrlBlockConfigs()) {
        for (int16_t port = *block.startPort();
             port < *block.startPort() + *block.numPorts();
             ++port) {
          coveredPorts.insert(port);
        }
      }
    }
  }

  bool valid = true;
  for (int16_t xcvrId = 1; xcvrId <= *config.numXcvrs(); ++xcvrId) {
    if (coveredPorts.find(xcvrId) == coveredPorts.end()) {
      XLOG(ERR) << fmt::format(
          "Platform {}: xcvr {} is not covered by any xcvrCtrlBlockConfig",
          platformName,
          xcvrId);
      valid = false;
    }
  }

  return valid;
}

bool ConfigValidator::isValidVersionedPciDeviceCoverage(
    const PmUnitConfig& defaultPmUnitConfig,
    const VersionedPmUnitConfig& versionedPmUnitConfig) {
  // The default is already validated to cover all xcvrs, so require each
  // version to cover the same LED/xcvr port set. Compares port sets, not raw
  // blocks, so re-layouts covering the same ports are allowed.
  auto coveredPorts = [](const PmUnitConfig& pmUnitConfig,
                         const auto& getBlocks) {
    std::set<int16_t> ports;
    for (const auto& pciDev : *pmUnitConfig.pciDeviceConfigs()) {
      for (const auto& block : getBlocks(pciDev)) {
        for (int16_t port = *block.startPort();
             port < *block.startPort() + *block.numPorts();
             ++port) {
          ports.insert(port);
        }
      }
    }
    return ports;
  };
  auto ledCtrlBlocks = [](const PciDeviceConfig& pciDev) -> const auto& {
    return *pciDev.ledCtrlBlockConfigs();
  };
  auto xcvrCtrlBlocks = [](const PciDeviceConfig& pciDev) -> const auto& {
    return *pciDev.xcvrCtrlBlockConfigs();
  };
  const auto& versionedConfig = *versionedPmUnitConfig.pmUnitConfig();
  if (coveredPorts(defaultPmUnitConfig, ledCtrlBlocks) !=
      coveredPorts(versionedConfig, ledCtrlBlocks)) {
    XLOG(ERR)
        << "Versioned PmUnit LED ctrl coverage differs from default config";
    return false;
  }
  if (coveredPorts(defaultPmUnitConfig, xcvrCtrlBlocks) !=
      coveredPorts(versionedConfig, xcvrCtrlBlocks)) {
    XLOG(ERR)
        << "Versioned PmUnit xcvr ctrl coverage differs from default config";
    return false;
  }
  return true;
}

bool ConfigValidator::isValidPortRanges(
    const std::vector<std::pair<int16_t, int16_t>>& startPortAndNumPorts) {
  if (startPortAndNumPorts.empty()) {
    return true; // Empty list is valid
  }

  // Create a vector of port ranges (startPort, endPort) for sorting
  std::vector<std::pair<int, int>> portRanges;
  for (const auto& [startPort, numPorts] : startPortAndNumPorts) {
    int endPort = startPort + numPorts - 1;
    portRanges.emplace_back(startPort, endPort);
  }

  // Check for sorting, overlaps and gaps
  for (size_t i = 1; i < portRanges.size(); i++) {
    int prevStart = portRanges[i - 1].first;
    int prevEnd = portRanges[i - 1].second;
    int currStart = portRanges[i].first;

    // Check if port ranges are sorted by start port in ascending order
    if (currStart < prevStart) {
      XLOG(ERR) << fmt::format(
          "Port ranges are not sorted by start port: found port {} after port {}",
          currStart,
          prevStart);
      return false;
    }

    // Check for overlap
    if (currStart <= prevEnd) {
      XLOG(ERR) << fmt::format(
          "Overlapping port ranges detected: previous range ends at port {}, current range starts at port {}",
          prevEnd,
          currStart);
      return false;
    }
  }

  return true;
}

bool ConfigValidator::isValidChassisEepromDevicePath(
    const PlatformConfig& platformConfig,
    const std::string& chassisEepromDevicePath) {
  if (platformConfig.platformName() == "DARWIN") {
    // Darwin has a special case where the chassis EEPROM is not a real device
    return true;
  }

  // First check if the device path is valid
  if (!isValidDevicePath(platformConfig, chassisEepromDevicePath)) {
    return false;
  }

  auto [_, deviceName] = Utils().parseDevicePath(chassisEepromDevicePath);
  if (deviceName == "IDPROM") {
    // IDPROM is only allowed for certain platforms
    const auto& exceptionPlatforms = platform_manager_validators_constants::
        PLATFORMS_WITH_IDPROM_CHASSIS_EEPROM();
    if (std::find(
            exceptionPlatforms.begin(),
            exceptionPlatforms.end(),
            *platformConfig.platformName()) == exceptionPlatforms.end()) {
      XLOG(ERR) << fmt::format(
          "Platform {} has chassisEepromDevicePath pointing to IDPROM device '{}'. "
          "New platforms must NOT use IDPROM for chassisEepromDevicePath. "
          "Please use a dedicated chassis EEPROM device instead.",
          *platformConfig.platformName(),
          chassisEepromDevicePath);
      return false;
    }
  } else if (deviceName != "CHASSIS_EEPROM") {
    // Device name must be CHASSIS_EEPROM
    XLOG(ERR) << fmt::format(
        "Platform {} has chassisEepromDevicePath pointing to device '{}'. "
        "Device name must be 'CHASSIS_EEPROM'.",
        *platformConfig.platformName(),
        deviceName);
    return false;
  }

  return true;
}

void ConfigValidator::buildDeviceNameCache(
    const PlatformConfig& platformConfig) {
  if (deviceNamesBySlotType_.has_value()) {
    return;
  }

  std::unordered_map<std::string, std::unordered_set<std::string>> cache;

  // Helper lambda to add device name from fpgaIpBlockConfig
  auto addFromFpgaIpBlock = [&](const std::string& slotType,
                                const FpgaIpBlockConfig& fpgaConfig) {
    cache[slotType].insert(*fpgaConfig.pmUnitScopedName());
  };

  // Helper lambda to add I2C adapter names with @N suffix
  auto addI2cAdapterNames = [&](const std::string& slotType,
                                const I2cAdapterConfig& adapterConfig) {
    const auto& baseName =
        *adapterConfig.fpgaIpBlockConfig()->pmUnitScopedName();
    cache[slotType].insert(baseName);
    for (int i = 0; i < *adapterConfig.numberOfAdapters(); ++i) {
      cache[slotType].insert(fmt::format("{}@{}", baseName, i));
    }
  };

  for (const auto& [pmUnitName, pmUnitConfig] :
       *platformConfig.pmUnitConfigs()) {
    const auto& slotType = *pmUnitConfig.pluggedInSlotType();

    // Add device names from i2cDeviceConfigs, plus one bus name per mux
    // channel ("<name>@<channel>"), as registered by PlatformExplorer.
    for (const auto& i2cConfig : *pmUnitConfig.i2cDeviceConfigs()) {
      cache[slotType].insert(*i2cConfig.pmUnitScopedName());
      if (i2cConfig.numOutgoingChannels()) {
        for (int i = 0; i < *i2cConfig.numOutgoingChannels(); ++i) {
          cache[slotType].insert(
              fmt::format("{}@{}", *i2cConfig.pmUnitScopedName(), i));
        }
      }
    }

    // Add device names from embeddedSensorConfigs
    for (const auto& sensorConfig : *pmUnitConfig.embeddedSensorConfigs()) {
      cache[slotType].insert(*sensorConfig.pmUnitScopedName());
    }

    // Add device names from pciDeviceConfigs
    for (const auto& pciConfig : *pmUnitConfig.pciDeviceConfigs()) {
      cache[slotType].insert(*pciConfig.pmUnitScopedName());

      // Add names from nested configs within PCI device
      for (const auto& adapterConfig : *pciConfig.i2cAdapterConfigs()) {
        addI2cAdapterNames(slotType, adapterConfig);
      }

      for (const auto& spiMaster : *pciConfig.spiMasterConfigs()) {
        addFromFpgaIpBlock(slotType, *spiMaster.fpgaIpBlockConfig());
        for (const auto& spiDevice : *spiMaster.spiDeviceConfigs()) {
          cache[slotType].insert(*spiDevice.pmUnitScopedName());
        }
      }

      for (const auto& fanConfig : *pciConfig.fanTachoPwmConfigs()) {
        addFromFpgaIpBlock(slotType, *fanConfig.fpgaIpBlockConfig());
      }

      for (const auto& ledConfig : *pciConfig.ledCtrlConfigs()) {
        addFromFpgaIpBlock(slotType, *ledConfig.fpgaIpBlockConfig());
      }

      for (const auto& xcvrConfig : *pciConfig.xcvrCtrlConfigs()) {
        addFromFpgaIpBlock(slotType, *xcvrConfig.fpgaIpBlockConfig());
      }

      // gpioChipConfigs, watchdogConfigs, infoRomConfigs, miscCtrlConfigs,
      // sysLedCtrlConfigs are directly FpgaIpBlockConfig (no wrapper)
      for (const auto& gpioConfig : *pciConfig.gpioChipConfigs()) {
        cache[slotType].insert(*gpioConfig.pmUnitScopedName());
      }

      for (const auto& watchdogConfig : *pciConfig.watchdogConfigs()) {
        cache[slotType].insert(*watchdogConfig.pmUnitScopedName());
      }

      for (const auto& infoRomConfig : *pciConfig.infoRomConfigs()) {
        cache[slotType].insert(*infoRomConfig.pmUnitScopedName());
      }

      for (const auto& miscConfig : *pciConfig.miscCtrlConfigs()) {
        cache[slotType].insert(*miscConfig.pmUnitScopedName());
      }

      for (const auto& sysLedConfig : *pciConfig.sysLedCtrlConfigs()) {
        cache[slotType].insert(*sysLedConfig.pmUnitScopedName());
      }

      // Block configs
      for (const auto& ledCtrlConfig : Utils::createLedCtrlConfigs(pciConfig)) {
        addFromFpgaIpBlock(slotType, *ledCtrlConfig.fpgaIpBlockConfig());
      }

      for (const auto& xcvrCtrlConfig :
           Utils::createXcvrCtrlConfigs(pciConfig)) {
        addFromFpgaIpBlock(slotType, *xcvrCtrlConfig.fpgaIpBlockConfig());
      }

      for (const auto& i2cAdapterConfig :
           Utils::createI2cAdapterConfigs(pciConfig)) {
        addI2cAdapterNames(slotType, i2cAdapterConfig);
      }

      for (const auto& mdioBusConfig : Utils::createMdioBusConfigs(pciConfig)) {
        cache[slotType].insert(*mdioBusConfig.pmUnitScopedName());
      }

      for (const auto& rtmCtrlConfig : Utils::createRtmCtrlConfigs(pciConfig)) {
        addFromFpgaIpBlock(slotType, *rtmCtrlConfig.fpgaIpBlockConfig());
      }
    }
  }

  deviceNamesBySlotType_ = std::move(cache);
}

bool ConfigValidator::isValidLogicalEeprom(
    const PlatformConfig& config,
    const std::string& pmUnitName,
    const PmUnitConfig& pmUnitConfig) {
  auto logicalEeproms = getLogicalEeproms(
      *config.slotTypeConfigs(),
      *pmUnitConfig.pluggedInSlotType(),
      pmUnitConfig);
  if (!logicalEeproms.empty()) {
    const auto& allowedPlatforms =
        platform_manager_validators_constants::PLATFORMS_WITH_LOGICAL_EEPROMS();
    if (std::find(
            allowedPlatforms.begin(),
            allowedPlatforms.end(),
            *config.platformName()) == allowedPlatforms.end()) {
      XLOG(ERR) << fmt::format(
          "Platform has logical EEPROMs in PmUnit {}.  This is not allowed. ",
          pmUnitName);
      return false;
    }
  }

  constexpr int16_t kEepromSize = 512;

  for (const auto& [location, eeproms] : logicalEeproms) {
    const auto& [bus, addr] = location;

    // Validate same kernelDeviceName for all EEPROMs at this location
    for (size_t i = 1; i < eeproms.size(); ++i) {
      if (eeproms[i].kernelDeviceName != eeproms[0].kernelDeviceName) {
        XLOG(ERR) << fmt::format(
            "Logical eeproms {} and {} at (bus: {}, addr: {}) have different "
            "kernelDeviceNames: {} vs {}",
            eeproms[0].pmUnitScopedName,
            eeproms[i].pmUnitScopedName,
            bus,
            addr,
            eeproms[0].kernelDeviceName,
            eeproms[i].kernelDeviceName);
        return false;
      }
    }

    // Validate no overlapping regions (sort by offset, check adjacent pairs)
    auto sortedEeproms = eeproms;
    std::sort(
        sortedEeproms.begin(),
        sortedEeproms.end(),
        [](const auto& a, const auto& b) { return a.offset < b.offset; });

    for (size_t i = 0; i + 1 < sortedEeproms.size(); ++i) {
      const auto& curr = sortedEeproms[i];
      const auto& next = sortedEeproms[i + 1];
      if (curr.offset + kEepromSize > next.offset) {
        XLOG(ERR) << fmt::format(
            "Logical eeproms {} and {} at (bus: {}, addr: {}) have "
            "overlapping regions: [{}, {}) and [{}, {})",
            curr.pmUnitScopedName,
            next.pmUnitScopedName,
            bus,
            addr,
            curr.offset,
            curr.offset + kEepromSize,
            next.offset,
            next.offset + kEepromSize);
        return false;
      }
    }
  }
  return true;
}

std::map<std::pair<std::string, std::string>, std::vector<LogicalEeprom>>
ConfigValidator::getLogicalEeproms(
    const std::map<std::string, SlotTypeConfig>& slotTypeConfigs,
    const std::string& slotType,
    const PmUnitConfig& pmUnitConfig) {
  using EepromLoc = std::pair<std::string, std::string>;
  std::map<EepromLoc, std::vector<LogicalEeprom>> eepromsByLocation;

  // Add IDPROM if present
  if (slotTypeConfigs.contains(slotType)) {
    const auto& slotTypeConfig = slotTypeConfigs.at(slotType);
    if (slotTypeConfig.idpromConfig()) {
      const auto& idprom = *slotTypeConfig.idpromConfig();
      eepromsByLocation[{*idprom.busName(), *idprom.address()}].push_back(
          LogicalEeprom{
              "IDPROM", *idprom.offset(), *idprom.kernelDeviceName()});
    }
  }

  // Add I2C EEPROM devices
  for (const auto& i2cDevice : *pmUnitConfig.i2cDeviceConfigs()) {
    if (!*i2cDevice.isEeprom()) {
      continue;
    }
    int16_t offset = i2cDevice.eepromOffset() ? *i2cDevice.eepromOffset() : 0;
    eepromsByLocation[{*i2cDevice.busName(), *i2cDevice.address()}].push_back(
        LogicalEeprom{
            *i2cDevice.pmUnitScopedName(),
            offset,
            *i2cDevice.kernelDeviceName()});
  }

  // Return only locations with more than one EEPROM (logical EEPROMs)
  std::map<EepromLoc, std::vector<LogicalEeprom>> result;
  for (auto& [location, eeproms] : eepromsByLocation) {
    if (eeproms.size() > 1) {
      result[location] = std::move(eeproms);
    }
  }
  return result;
}

bool ConfigValidator::isValidRtmCtrlBlockConfig(
    const RtmCtrlBlockConfig& rtmCtrlBlockConfig) {
  if (rtmCtrlBlockConfig.pmUnitScopedNamePrefix()->empty()) {
    XLOG(ERR) << "PmUnitScopedNamePrefix must be a non-empty string";
    return false;
  }
  if (rtmCtrlBlockConfig.pmUnitScopedNamePrefix()->ends_with('_')) {
    XLOG(ERR) << "PmUnitScopedNamePrefix must not end with an underscore";
    return false;
  }
  if (rtmCtrlBlockConfig.deviceName()->empty()) {
    XLOG(ERR) << "deviceName must be a non-empty string";
    return false;
  }
  if (rtmCtrlBlockConfig.csrOffsetCalc()->empty()) {
    XLOG(ERR) << "csrOffsetCalc must be a non-empty string";
    return false;
  }
  if (*rtmCtrlBlockConfig.numPorts() <= 0) {
    XLOG(ERR) << "numPorts must be a value greater than 0";
    return false;
  }
  if (*rtmCtrlBlockConfig.startPort() <= 0) {
    XLOG(ERR) << "startPort must be a value greater than 0";
    return false;
  }
  if (*rtmCtrlBlockConfig.numPorts() > numRtms_) {
    XLOG(ERR) << fmt::format(
        "numPorts must be less than or equal to {}", numRtms_);
    return false;
  }
  if (*rtmCtrlBlockConfig.startPort() > numRtms_) {
    XLOG(ERR) << fmt::format(
        "startPort must be less than or equal to {}", numRtms_);
    return false;
  }
  if (*rtmCtrlBlockConfig.startPort() + *rtmCtrlBlockConfig.numPorts() - 1 >
      numRtms_) {
    XLOG(ERR) << fmt::format(
        "startPort + numPorts - 1 must be must be less than or equal to {}",
        numRtms_);
    return false;
  }

  for (int16_t port = *rtmCtrlBlockConfig.startPort();
       port < *rtmCtrlBlockConfig.startPort() + *rtmCtrlBlockConfig.numPorts();
       port++) {
    if (!isValidCsrOffsetCalc(
            *rtmCtrlBlockConfig.csrOffsetCalc(),
            port,
            *rtmCtrlBlockConfig.startPort())) {
      return false;
    }

    if (!rtmCtrlBlockConfig.iobufOffsetCalc()->empty()) {
      if (!isValidIobufOffsetCalc(
              *rtmCtrlBlockConfig.iobufOffsetCalc(),
              port,
              *rtmCtrlBlockConfig.startPort())) {
        return false;
      }
    }
  }

  return true;
}

bool ConfigValidator::isValidMdioBusBlockConfig(
    const MdioBusBlockConfig& mdioBusBlockConfig) {
  if (mdioBusBlockConfig.pmUnitScopedNamePrefix()->empty()) {
    XLOG(ERR) << "PmUnitScopedNamePrefix must be a non-empty string";
    return false;
  }
  if (mdioBusBlockConfig.pmUnitScopedNamePrefix()->ends_with('_')) {
    XLOG(ERR) << "PmUnitScopedNamePrefix must not end with an underscore";
    return false;
  }
  if (mdioBusBlockConfig.deviceName()->empty()) {
    XLOG(ERR) << "deviceName must be a non-empty string";
    return false;
  }
  if (mdioBusBlockConfig.csrOffsetCalc()->empty()) {
    XLOG(ERR) << "csrOffsetCalc must be a non-empty string";
    return false;
  }
  if (*mdioBusBlockConfig.numBuses() <= 0) {
    XLOG(ERR) << "numBuses must be a value greater than 0";
    return false;
  }
  for (int busIndex = 0; busIndex < *mdioBusBlockConfig.numBuses();
       ++busIndex) {
    try {
      Utils().evaluateExpression(
          fmt::format(
              fmt::runtime(*mdioBusBlockConfig.csrOffsetCalc()),
              fmt::arg("busIndex", busIndex)));
    } catch (const std::exception& e) {
      XLOG(ERR) << fmt::format(
          "Invalid csrOffsetCalc expression: {} with busIndex={}: {}",
          *mdioBusBlockConfig.csrOffsetCalc(),
          busIndex,
          e.what());
      return false;
    }
    if (!mdioBusBlockConfig.iobufOffsetCalc()->empty()) {
      try {
        Utils().evaluateExpression(
            fmt::format(
                fmt::runtime(*mdioBusBlockConfig.iobufOffsetCalc()),
                fmt::arg("busIndex", busIndex)));
      } catch (const std::exception& e) {
        XLOG(ERR) << fmt::format(
            "Invalid iobufOffsetCalc expression: {} with busIndex={}: {}",
            *mdioBusBlockConfig.iobufOffsetCalc(),
            busIndex,
            e.what());
        return false;
      }
    }
  }
  return true;
}

} // namespace facebook::fboss::platform::platform_manager
