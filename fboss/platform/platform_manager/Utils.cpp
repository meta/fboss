// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/platform_manager/Utils.h"

#include <exprtk.hpp>
#include <gpiod.h>
#include <filesystem>
#include <stdexcept>

#include <folly/logging/xlog.h>
#include <re2/re2.h>

#include "fboss/lib/GpiodLine.h"

namespace fs = std::filesystem;
using namespace facebook::fboss::platform;

namespace {
const re2::RE2 kPmDeviceParseRe{"(?P<SlotPath>.*)\\[(?P<DeviceName>.*)\\]"};
const re2::RE2 kGpioChipNameRe{"gpiochip\\d+"};
const std::string kGpioChip = "gpiochip";

const re2::RE2 kWatchdogNameRe{"watchdog(\\d+)"};
const std::string kWatchdog = "watchdog";
constexpr auto kWatchdogDevCreationWaitSecs = std::chrono::seconds(5);
constexpr auto kMdioBusCharDevCreationWaitSecs = std::chrono::seconds(5);
} // namespace

namespace facebook::fboss::platform::platform_manager {
std::pair<std::string, std::string> Utils::parseDevicePath(
    const std::string& devicePath) {
  std::string slotPath, deviceName;
  CHECK(RE2::FullMatch(devicePath, kPmDeviceParseRe, &slotPath, &deviceName));
  // Remove trailling '/' (e.g /abc/dfg/)
  CHECK_EQ(slotPath.back(), '/');
  if (slotPath.length() > 1) {
    slotPath.pop_back();
  }
  return {slotPath, deviceName};
}

std::string Utils::createDevicePath(
    const std::string& slotPath,
    const std::string& deviceName) {
  return fmt::format("{}/[{}]", slotPath == "/" ? "" : slotPath, deviceName);
}

std::string Utils::resolveGpioChipCharDevPath(const std::string& sysfsPath) {
  auto failMsg = "Fail to resolve GpioChip CharDevPath";
  if (!fs::exists(sysfsPath)) {
    throw std::runtime_error(
        fmt::format("{}. Reason: {} doesn't exist", failMsg, sysfsPath));
  }
  std::optional<uint16_t> gpioChipNum{std::nullopt};
  for (const auto& dirEntry : fs::directory_iterator(sysfsPath)) {
    if (re2::RE2::FullMatch(
            dirEntry.path().filename().string(), kGpioChipNameRe)) {
      gpioChipNum = folly::to<uint16_t>(
          dirEntry.path().filename().string().substr(kGpioChip.length()));
    }
  }
  if (!gpioChipNum) {
    throw std::runtime_error(
        fmt::format(
            "{}. Reason: Couldn't find gpio chip under {}",
            failMsg,
            sysfsPath));
  }
  auto charDevPath = fmt::format("/dev/gpiochip{}", *gpioChipNum);
  if (!fs::exists(charDevPath)) {
    throw std::runtime_error(
        fmt::format(
            "{}. Reason: {} does not exist in the system",
            failMsg,
            charDevPath));
  }
  return charDevPath;
}

std::string Utils::resolveWatchdogCharDevPath(const std::string& sysfsPath) {
  auto failMsg = "Failed to resolve Watchdog CharDevPath";
  if (!fs::exists(sysfsPath)) {
    throw std::runtime_error(
        fmt::format("{}. Reason: {} doesn't exist", failMsg, sysfsPath));
  }

  fs::path watchdogDir = fs::path(sysfsPath) / "watchdog";
  if (!Utils().checkDeviceReadiness(
          [&]() -> bool { return fs::exists(watchdogDir); },
          fmt::format(
              "Watchdog SysfsPath is not created. Waited for at most {}s",
              kWatchdogDevCreationWaitSecs.count()),
          kWatchdogDevCreationWaitSecs)) {
    throw std::runtime_error(
        fmt::format(
            "{}. Reason: Couldn't find watchdog directory under {}",
            failMsg,
            sysfsPath));
  }

  std::optional<uint16_t> watchdogNum{std::nullopt};
  for (const auto& dirEntry : fs::directory_iterator(watchdogDir)) {
    if (re2::RE2::FullMatch(
            dirEntry.path().filename().string(), kWatchdogNameRe)) {
      watchdogNum = folly::to<uint16_t>(
          dirEntry.path().filename().string().substr(kWatchdog.length()));
    }
  }
  if (!watchdogNum) {
    throw std::runtime_error(
        fmt::format(
            "{}. Reason: Couldn't find watchdog under {}", failMsg, sysfsPath));
  }
  auto charDevPath = fmt::format("/dev/watchdog{}", *watchdogNum);
  if (!Utils().checkDeviceReadiness(
          [&]() -> bool { return fs::exists(charDevPath); },
          fmt::format(
              "Watchdog CharDevPath is not created. Waited for at most {}s",
              kWatchdogDevCreationWaitSecs.count()),
          kWatchdogDevCreationWaitSecs)) {
    throw std::runtime_error(
        fmt::format(
            "{}. Reason: {} does not exist in the system",
            failMsg,
            charDevPath));
  }
  return charDevPath;
}

std::string Utils::resolveMdioBusCharDevPath(uint32_t instanceId) {
  auto failMsg = "Failed to resolve mdio_bus CharDevPath";
  auto charDevPath = fmt::format("/dev/fb-mdio-{}", instanceId);
  if (!Utils().checkDeviceReadiness(
          [&]() -> bool { return fs::exists(charDevPath); },
          fmt::format(
              "MdioBus CharDevPath is not created. Waited for at most {}s",
              kMdioBusCharDevCreationWaitSecs.count()),
          kMdioBusCharDevCreationWaitSecs)) {
    throw std::runtime_error(
        fmt::format(
            "{}. Reason: {} does not exist in the system",
            failMsg,
            charDevPath));
  }
  return charDevPath;
}

bool Utils::checkDeviceReadiness(
    std::function<bool()>&& isDeviceReadyFunc,
    const std::string& onWaitMsg,
    std::chrono::seconds maxWaitSecs) {
  if (isDeviceReadyFunc()) {
    return true;
  }
  XLOG(INFO) << onWaitMsg;
  std::chrono::milliseconds waitIntervalMs = std::chrono::milliseconds(50);
  auto start = std::chrono::steady_clock::now();
  do {
    std::this_thread::sleep_for(waitIntervalMs);
    if (isDeviceReadyFunc()) {
      return true;
    }
  } while (std::chrono::steady_clock::now() <= start + maxWaitSecs);
  return false;
}

int Utils::getGpioLineValue(const std::string& charDevPath, int lineIndex)
    const {
  struct gpiod_chip* chip = gpiod_chip_open(charDevPath.c_str());
  // Ensure GpiodLine is destroyed before gpiod_chip_close
  int value = GpiodLine(chip, lineIndex, "gpioline").getValue();
  gpiod_chip_close(chip);
  return value;
};

std::string Utils::formatExpression(
    const std::string& expression,
    int port,
    int startPort,
    std::optional<int> led) {
  if (led.has_value()) {
    return fmt::format(
        fmt::runtime(expression),
        fmt::arg("portNum", port),
        fmt::arg("ledNum", *led),
        fmt::arg("startPort", startPort));
  } else {
    return fmt::format(
        fmt::runtime(expression),
        fmt::arg("portNum", port),
        fmt::arg("startPort", startPort));
  }
}

std::string Utils::evaluateExpression(const std::string& expression) {
  std::string decimalExpression = convertHexLiteralsToDecimal(expression);
  exprtk::symbol_table<double> symbolTable;
  exprtk::expression<double> expr;
  expr.register_symbol_table(symbolTable);
  exprtk::parser<double> parser;

  if (!parser.compile(decimalExpression, expr)) {
    throw std::runtime_error(
        fmt::format(
            "Failed to parse offset expression: {}", decimalExpression));
  }

  uint64_t value = static_cast<uint64_t>(expr.value());
  return fmt::format("0x{:x}", value);
}

std::string Utils::computeHexExpression(
    const std::string& expression,
    int port,
    int startPort,
    std::optional<int> led) {
  std::string formattedExpression =
      formatExpression(expression, port, startPort, led);
  return evaluateExpression(formattedExpression);
}

std::string Utils::convertHexLiteralsToDecimal(const std::string& expression) {
  std::string result = expression;

  // Convert hexadecimal literals to decimal since exprtk doesn't support hex
  static const re2::RE2 hexRegex("(0x[0-9a-fA-F]+)");
  std::string hexMatch;
  std::string::size_type pos = 0;

  while (re2::RE2::PartialMatch(result.substr(pos), hexRegex, &hexMatch)) {
    // Find the position of this hex pattern
    std::string::size_type hexPos = result.find(hexMatch, pos);

    if (hexPos != std::string::npos) {
      // Convert hex string to decimal using our utility function
      uint64_t decimalValue = std::stoull(hexMatch, nullptr, 16);
      std::string decimalStr = std::to_string(decimalValue);

      // Replace the hex pattern with decimal value
      result.replace(hexPos, hexMatch.length(), decimalStr);
      pos = hexPos + decimalStr.length();
    } else {
      break;
    }
  }

  return result;
}

std::vector<I2cAdapterConfig> Utils::createI2cAdapterConfigs(
    const PciDeviceConfig& pciDeviceConfig) {
  std::vector<I2cAdapterConfig> i2cAdapterConfigs;
  const auto i2cAdapterBlockConfigs = pciDeviceConfig.i2cAdapterBlockConfigs();
  for (const auto& i2cAdapterBlockConfig : *i2cAdapterBlockConfigs) {
    int endAdapterIndex = *i2cAdapterBlockConfig.startAdapterIndex() +
        *i2cAdapterBlockConfig.numAdapters();
    for (int adapterIndex = *i2cAdapterBlockConfig.startAdapterIndex();
         adapterIndex < endAdapterIndex;
         ++adapterIndex) {
      I2cAdapterConfig i2cAdapterConfig;
      i2cAdapterConfig.fpgaIpBlockConfig()->pmUnitScopedName() = fmt::format(
          "{}_{}",
          *i2cAdapterBlockConfig.pmUnitScopedNamePrefix(),
          adapterIndex);
      i2cAdapterConfig.fpgaIpBlockConfig()->deviceName() =
          *i2cAdapterBlockConfig.deviceName();

      std::string csrExpression = fmt::format(
          fmt::runtime(*i2cAdapterBlockConfig.csrOffsetCalc()),
          fmt::arg("adapterIndex", adapterIndex),
          fmt::arg(
              "startAdapterIndex", *i2cAdapterBlockConfig.startAdapterIndex()));
      i2cAdapterConfig.fpgaIpBlockConfig()->csrOffset() =
          Utils().evaluateExpression(csrExpression);

      if (!i2cAdapterBlockConfig.iobufOffsetCalc()->empty()) {
        std::string iobufExpression = fmt::format(
            fmt::runtime(*i2cAdapterBlockConfig.iobufOffsetCalc()),
            fmt::arg("adapterIndex", adapterIndex),
            fmt::arg(
                "startAdapterIndex",
                *i2cAdapterBlockConfig.startAdapterIndex()));
        i2cAdapterConfig.fpgaIpBlockConfig()->iobufOffset() =
            Utils().evaluateExpression(iobufExpression);
      }

      i2cAdapterConfig.numberOfAdapters() =
          *i2cAdapterBlockConfig.numBusesPerAdapter();

      if (i2cAdapterBlockConfig.busFreqHz().has_value()) {
        i2cAdapterConfig.busFreqHz() = *i2cAdapterBlockConfig.busFreqHz();
      }

      i2cAdapterConfigs.push_back(i2cAdapterConfig);
    }
  }
  return i2cAdapterConfigs;
}

std::vector<XcvrCtrlConfig> Utils::createXcvrCtrlConfigs(
    const PciDeviceConfig& pciDeviceConfig) {
  std::vector<XcvrCtrlConfig> xcvrCtrlConfigs;
  const auto xcvrCtrlBlockConfigs = pciDeviceConfig.xcvrCtrlBlockConfigs();
  for (const auto& xcvrCtrlBlockConfig : *xcvrCtrlBlockConfigs) {
    int endPort =
        *xcvrCtrlBlockConfig.startPort() + *xcvrCtrlBlockConfig.numPorts();
    for (int port = *xcvrCtrlBlockConfig.startPort(); port < endPort; ++port) {
      XcvrCtrlConfig xcvrCtrlConfig;
      xcvrCtrlConfig.fpgaIpBlockConfig()->pmUnitScopedName() = fmt::format(
          "{}_XCVR_CTRL_PORT_{}",
          *xcvrCtrlBlockConfig.pmUnitScopedNamePrefix(),
          port);
      xcvrCtrlConfig.fpgaIpBlockConfig()->deviceName() =
          *xcvrCtrlBlockConfig.deviceName();
      xcvrCtrlConfig.fpgaIpBlockConfig()->csrOffset() =
          Utils().computeHexExpression(
              *xcvrCtrlBlockConfig.csrOffsetCalc(),
              port,
              *xcvrCtrlBlockConfig.startPort());
      xcvrCtrlConfig.portNumber() = port;
      if (!xcvrCtrlBlockConfig.iobufOffsetCalc()->empty()) {
        xcvrCtrlConfig.fpgaIpBlockConfig()->iobufOffset() =
            Utils().computeHexExpression(
                *xcvrCtrlBlockConfig.iobufOffsetCalc(),
                port,
                *xcvrCtrlBlockConfig.startPort());
      }
      xcvrCtrlConfigs.push_back(xcvrCtrlConfig);
    }
  }
  return xcvrCtrlConfigs;
}

std::vector<LedCtrlConfig> Utils::createLedCtrlConfigs(
    const PciDeviceConfig& pciDeviceConfig) {
  std::vector<LedCtrlConfig> ledCtrlConfigs;
  const auto ledCtrlBlockConfigs = pciDeviceConfig.ledCtrlBlockConfigs();
  for (const auto& ledCtrlBlockConfig : *ledCtrlBlockConfigs) {
    int endPort =
        *ledCtrlBlockConfig.startPort() + *ledCtrlBlockConfig.numPorts();
    for (int port = *ledCtrlBlockConfig.startPort(); port < endPort; ++port) {
      for (int led = 1; led <= ledCtrlBlockConfig.ledPerPort(); ++led) {
        LedCtrlConfig ledCtrlConfig;
        ledCtrlConfig.fpgaIpBlockConfig()->pmUnitScopedName() = fmt::format(
            "{}_PORT_{}_LED_{}",
            *ledCtrlBlockConfig.pmUnitScopedNamePrefix(),
            port,
            led);
        ledCtrlConfig.fpgaIpBlockConfig()->deviceName() =
            *ledCtrlBlockConfig.deviceName();
        ledCtrlConfig.fpgaIpBlockConfig()->csrOffset() =
            Utils().computeHexExpression(
                *ledCtrlBlockConfig.csrOffsetCalc(),
                port,
                *ledCtrlBlockConfig.startPort(),
                led);
        ledCtrlConfig.portNumber() = port;
        ledCtrlConfig.ledId() = led;
        if (!ledCtrlBlockConfig.iobufOffsetCalc()->empty()) {
          ledCtrlConfig.fpgaIpBlockConfig()->iobufOffset() =
              Utils().computeHexExpression(
                  *ledCtrlBlockConfig.iobufOffsetCalc(),
                  port,
                  *ledCtrlBlockConfig.startPort(),
                  led);
        }
        ledCtrlConfigs.push_back(ledCtrlConfig);
      }
    }
  }
  return ledCtrlConfigs;
}

std::vector<FpgaIpBlockConfig> Utils::createMdioBusConfigs(
    const PciDeviceConfig& pciDeviceConfig) {
  std::vector<FpgaIpBlockConfig> mdioBusConfigs;
  const auto mdioBusBlockConfigs = pciDeviceConfig.mdioBusBlockConfigs();
  for (const auto& mdioBusBlockConfig : *mdioBusBlockConfigs) {
    int endBusIndex = *mdioBusBlockConfig.numBuses();
    for (int busIndex = 0; busIndex < endBusIndex; ++busIndex) {
      FpgaIpBlockConfig mdioBusConfig;
      mdioBusConfig.pmUnitScopedName() = fmt::format(
          "{}_{}", *mdioBusBlockConfig.pmUnitScopedNamePrefix(), busIndex + 1);
      mdioBusConfig.deviceName() = *mdioBusBlockConfig.deviceName();

      std::string csrExpression = fmt::format(
          fmt::runtime(*mdioBusBlockConfig.csrOffsetCalc()),
          fmt::arg("busIndex", busIndex));
      mdioBusConfig.csrOffset() = Utils().evaluateExpression(csrExpression);

      mdioBusConfigs.push_back(mdioBusConfig);
    }
  }
  return mdioBusConfigs;
}

std::vector<RtmCtrlConfig> Utils::createRtmCtrlConfigs(
    const PciDeviceConfig& pciDeviceConfig) {
  std::vector<RtmCtrlConfig> rtmCtrlConfigs;
  auto rtmCtrlBlockConfigs = pciDeviceConfig.rtmCtrlBlockConfigs();
  for (const auto& rtmCtrlBlockConfig : *rtmCtrlBlockConfigs) {
    int endPort =
        *rtmCtrlBlockConfig.startPort() + *rtmCtrlBlockConfig.numPorts();
    for (int port = *rtmCtrlBlockConfig.startPort(); port < endPort; ++port) {
      RtmCtrlConfig rtmCtrlConfig;
      rtmCtrlConfig.fpgaIpBlockConfig()->pmUnitScopedName() = fmt::format(
          "{}_RTM_CTRL_PORT_{}",
          *rtmCtrlBlockConfig.pmUnitScopedNamePrefix(),
          port);
      rtmCtrlConfig.fpgaIpBlockConfig()->deviceName() =
          *rtmCtrlBlockConfig.deviceName();
      rtmCtrlConfig.fpgaIpBlockConfig()->csrOffset() =
          Utils().computeHexExpression(
              *rtmCtrlBlockConfig.csrOffsetCalc(),
              port,
              *rtmCtrlBlockConfig.startPort());
      rtmCtrlConfig.portNumber() = port;
      if (!rtmCtrlBlockConfig.iobufOffsetCalc()->empty()) {
        rtmCtrlConfig.fpgaIpBlockConfig()->iobufOffset() =
            Utils().computeHexExpression(
                *rtmCtrlBlockConfig.iobufOffsetCalc(),
                port,
                *rtmCtrlBlockConfig.startPort());
      }
      rtmCtrlConfigs.push_back(rtmCtrlConfig);
    }
  }
  return rtmCtrlConfigs;
}

namespace {
bool versionMatches(
    const VersionedPmUnitConfig& versionedPmUnitConfig,
    const PmUnitVersion& version) {
  auto pmUnitVersions = versionedPmUnitConfig.pmUnitVersions();
  if (pmUnitVersions && !pmUnitVersions->empty()) {
    for (const auto& pmUnitVersion : *pmUnitVersions) {
      if (*pmUnitVersion.productionState() == *version.productionState() &&
          *pmUnitVersion.productionSubState() ==
              *version.productionSubState() &&
          *pmUnitVersion.respinVariantIndicator() ==
              *version.respinVariantIndicator()) {
        return true;
      }
    }
    return false;
  }
  // Legacy form: productSubVersion is matched against the
  // RespinVariantIndicator (EEPROM Type 10), not the ProductionSubState (Type
  // 9).
  return versionedPmUnitConfig.productSubVersion() &&
      *versionedPmUnitConfig.productSubVersion() ==
      *version.respinVariantIndicator();
}
} // namespace

PmUnitConfig Utils::resolvePmUnitConfig(
    const PlatformConfig& platformConfig,
    const std::string& pmUnitName,
    const std::optional<PmUnitVersion>& version) {
  if (!version) {
    XLOG(INFO) << fmt::format(
        "Resolved {} to its default PmUnitConfig. No version was provided.",
        pmUnitName);
    return platformConfig.pmUnitConfigs()->at(pmUnitName);
  }
  if (platformConfig.versionedPmUnitConfigs()->contains(pmUnitName)) {
    for (const auto& versionedPmUnitConfig :
         platformConfig.versionedPmUnitConfigs()->at(pmUnitName)) {
      if (versionMatches(versionedPmUnitConfig, *version)) {
        XLOG(INFO) << fmt::format(
            "Resolved {} to a versioned PmUnitConfig for version {}.{}.{}",
            pmUnitName,
            *version->productionState(),
            *version->productionSubState(),
            *version->respinVariantIndicator());
        return *versionedPmUnitConfig.pmUnitConfig();
      }
    }
  }
  XLOG(INFO) << fmt::format(
      "Resolved {} to its default PmUnitConfig. "
      "No versioned config matches version {}.{}.{}",
      pmUnitName,
      *version->productionState(),
      *version->productionSubState(),
      *version->respinVariantIndicator());
  return platformConfig.pmUnitConfigs()->at(pmUnitName);
}

std::map<std::string, PmUnitConfig> Utils::resolvePmUnitConfigs(
    const PlatformConfig& platformConfig,
    const std::map<std::string, PmUnitVersion>& pmUnitVersions) {
  // A version supplied for an unknown PmUnit would otherwise be dropped
  // silently, leaving that PmUnit resolved to its default config -- exactly the
  // mismatch callers pass versions to avoid.
  for (const auto& [pmUnitName, version] : pmUnitVersions) {
    if (!platformConfig.pmUnitConfigs()->contains(pmUnitName)) {
      throw std::invalid_argument(
          fmt::format(
              "Cannot apply version {}.{}.{}. No PmUnit named {} in {}",
              *version.productionState(),
              *version.productionSubState(),
              *version.respinVariantIndicator(),
              pmUnitName,
              *platformConfig.platformName()));
    }
  }
  std::map<std::string, PmUnitConfig> resolved;
  for (const auto& [pmUnitName, pmUnitConfig] :
       *platformConfig.pmUnitConfigs()) {
    std::optional<PmUnitVersion> version;
    if (auto it = pmUnitVersions.find(pmUnitName); it != pmUnitVersions.end()) {
      version = it->second;
    }
    resolved[pmUnitName] =
        resolvePmUnitConfig(platformConfig, pmUnitName, version);
  }
  return resolved;
}
} // namespace facebook::fboss::platform::platform_manager
