// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/rma-showtech/Utils.h"

#include <gpiod.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

#include <fmt/core.h>
#include <fmt/std.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <re2/re2.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/GpiodLine.h"
#include "fboss/platform/config_lib/ConfigLib.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_types.h"
#include "fboss/platform/rma-showtech/FanImpl.h"
#include "fboss/platform/rma-showtech/PsuHelper.h"

using namespace facebook::fboss::platform::showtech_config;

namespace facebook::fboss::platform {

void Utils::printHostDetails() {
  std::cout << "##### Host Information #####" << std::endl;
  std::cout << "#### SYSTEM TIME ####" << std::endl;
  std::cout << platformUtils_.execCommand("date").second << std::endl;
  std::cout << "#### HOSTNAME ####" << std::endl;
  std::cout << platformUtils_.execCommand("hostname").second << std::endl;
  std::cout << "#### Linux Kernel Version ####" << std::endl;
  std::cout << platformUtils_.execCommand("uname -r").second << std::endl;
  std::cout << "#### UPTIME ####" << std::endl;
  std::cout << platformUtils_.execCommand("uptime").second << std::endl;
  std::cout << platformUtils_.execCommand("last reboot").second << std::endl;
  std::cout << "#### Platform Name ####" << std::endl;
  std::cout << platformUtils_
                   .execCommand("cat /var/facebook/fboss/platform_name")
                   .second
            << std::endl;
  std::cout << "#### FRU ID ####" << std::endl;
  std::cout
      << platformUtils_.execCommand("cat /var/facebook/fboss/fruid.json").second
      << std::endl;
  std::cout << "#### DMI Information ####" << std::endl;
  std::cout << platformUtils_.execCommand("dmidecode -t system").second
            << std::endl;
}

void Utils::printFbossDetails() {
  std::cout << "##### FBOSS Information #####" << std::endl;
  runFbossCliCmd("product");
  runFbossCliCmd("version agent");
  runFbossCliCmd("environment sensor");
  runFbossCliCmd("environment temperature");
  runFbossCliCmd("environment fan");
  runFbossCliCmd("environment power");
}

void Utils::printWeutilDetails() {
  std::cout << "##### WEUTIL dump of all EEPROMs #####" << std::endl;
  std::cout << platformUtils_.execCommand("weutil --all").second << std::endl;
}

void Utils::printFwutilDetails() {
  std::cout << "##### FWUTIL dump of all Programmables #####" << std::endl;
  std::cout << platformUtils_
                   .execCommand(
                       "fw_util --fw_action version --fw_target_name all")
                   .second
            << std::endl;
}

void Utils::printMfrVersionDetails() {
  if (!config_.regDumpDevices() || config_.regDumpDevices()->empty()) {
    return;
  }

  std::cout << "##### Manufacturer Version Strings #####" << std::endl;
  for (const auto& p : *config_.regDumpDevices()) {
    std::filesystem::path devPath(p);
    std::string label = devPath.filename().string();
    auto mfrVersionPath = (devPath / "mfr_version").string();
    if (std::filesystem::exists(mfrVersionPath)) {
      try {
        std::cout << label << ": " << readSysfs(mfrVersionPath) << std::endl;
      } catch (const std::exception& e) {
        std::cerr
            << fmt::format(
                   "Failed to read manufacturer version string for {}: {}",
                   label,
                   e.what())
            << std::endl;
      }
    }
  }

  std::cout << std::endl;
}

void Utils::printLspciDetails() {
  std::cout << "##### LSPCI #####" << std::endl;
  std::string cmd = "lspci -vvv";
  std::cout << platformUtils_.execCommand(cmd).second << std::endl;
}

void Utils::printPortDetails() {
  std::cout << "##### Port Information #####" << std::endl;
  runFbossCliCmd("port");
  runFbossCliCmd("fabric");
  runFbossCliCmd("lldp");
  runFbossCliCmd("interface counters");
  runFbossCliCmd("interface errors");
  runFbossCliCmd("interface flaps");
  runFbossCliCmd("interface phy");
  runFbossCliCmd("transceiver");
  if (!std::filesystem::exists("/etc/ramdisk")) {
    std::cout << "#### wedge_qsfp_util ####" << std::endl;
    auto [ret, output] =
        platformUtils_.execCommand("timeout 30 wedge_qsfp_util");
    std::cout << output << std::endl;
    if (ret == 124) {
      std::cout << "Error: wedge_qsfp_util timed out after 30 seconds"
                << std::endl;
    }
  }
}

void Utils::printSensorDetails() {
  std::cout << "##### Sensor Information #####" << std::endl;
  std::cout << "#### SENSORS ####" << std::endl;
  std::cout << platformUtils_.execCommand("sensors").second << std::endl;
  std::cout << "#### Dump from sensor_service ####" << std::endl;
  std::cout << platformUtils_.execCommand("sensor_service_client").second
            << std::endl;
}

void Utils::printI2cDetails() {
  std::cout << "##### I2C Scan Information #####" << std::endl;
  auto [ret, output] = platformUtils_.execCommand("i2cdetect -l");
  std::cout << output << std::endl;

  auto i2cBuses = i2cHelper_.findI2cBuses();
  for (const auto& [busNum, busName] : i2cBuses) {
    if (config_.i2cBusIgnore()->contains(busName)) {
      std::cout << fmt::format("Skipping bus `i2c-{} - {}`", busNum, busName)
                << std::endl;
      continue;
    }
    auto cmd = fmt::format("time i2cdetect -y {}", busNum);
    std::cout << fmt::format("#### Running `{}` for {} ####", cmd, busName)
              << std::endl;
    std::cout << platformUtils_.execCommand(cmd).second << std::endl;
  }
}

void Utils::printFileContent(const std::string& path) {
  std::string content;
  if (folly::readFile(path.c_str(), content)) {
    std::cout << content;
  } else {
    std::cerr << "Error: failed to read " << path << std::endl;
  }
}

namespace {
const RE2 kPagedRegDumpRe(R"(p(\d+)_reg_dump)");
} // namespace

std::string Utils::regDumpEntryLabel(const std::filesystem::path& entry) {
  auto fileName = entry.filename().string();
  int pageNum;
  if (fileName == "global_reg_dump") {
    return "Global Register Dump";
  } else if (RE2::FullMatch(fileName, kPagedRegDumpRe, &pageNum)) {
    return fmt::format("Page {} Register Dump", pageNum);
  }
  std::cerr << "Failed to create register dump entry label from " << fileName
            << std::endl;
  return fileName;
}

// order entries: global first, then p0, p1, ..., then any unrecognized names
// format for the name is always either global_reg_dump or px_reg_dump
bool Utils::regDumpEntryOrder(
    const std::filesystem::path& a,
    const std::filesystem::path& b) {
  auto sortKey = [](const std::filesystem::path& entry) {
    auto name = entry.filename().string();
    if (name == "global_reg_dump") {
      return std::make_tuple(0, 0, name);
    }
    int pageNum = 0;
    if (RE2::FullMatch(name, kPagedRegDumpRe, &pageNum)) {
      return std::make_tuple(1, pageNum, name);
    }
    std::cerr << "Failed to get page number from filename " << name
              << std::endl;
    return std::make_tuple(2, 0, name);
  };
  return sortKey(a) < sortKey(b);
}

std::filesystem::path Utils::getDebugfsPathFromHwmonPath(
    const std::filesystem::path& hwmonPath) {
  auto sysfsRootDevDir = hwmonPath.parent_path().parent_path();
  std::string i2cName = sysfsRootDevDir.filename().string();
  std::string deviceName = readSysfs(sysfsRootDevDir / "name");

  return std::filesystem::path(
      fmt::format("/sys/kernel/debug/{}-{}", deviceName, i2cName));
}

void Utils::printRegDumpDetails() {
  std::cout << "##### Register Dump Information #####\n\n";
  if (!config_.regDumpDevices() || config_.regDumpDevices()->empty()) {
    std::cout << "No device to regDump found from config\n\n";
    return;
  }

  for (const auto& p : *config_.regDumpDevices()) {
    std::cout << fmt::format("#### Register Dump for {} ####\n\n", p);

    std::error_code ec;
    std::filesystem::path hwmonPath = std::filesystem::canonical(p, ec);
    if (ec) {
      std::cerr << "Error resolving path: " << ec.message() << std::endl;
      continue;
    }
    std::filesystem::path debugfsPath;
    try {
      debugfsPath = getDebugfsPathFromHwmonPath(hwmonPath);
    } catch (const std::exception& e) {
      std::cerr << "Error resolving debugfs path: " << e.what() << std::endl;
      continue;
    }

    if (!std::filesystem::is_directory(debugfsPath, ec) || ec) {
      std::cerr << "Debugfs path not found: " << debugfsPath << std::endl;
      continue;
    }

    // paged device: collect, sort (global first, then p0, p1, ...), then dump
    std::vector<std::filesystem::path> entries;
    for (const auto& entry :
         std::filesystem::directory_iterator(debugfsPath, ec)) {
      entries.push_back(entry.path());
    }
    if (ec) {
      std::cerr << "Failed to iterate reg_dump directory: " << ec.message()
                << std::endl;
      continue;
    }

    // order the entries with global first, then p0 to pn
    std::sort(entries.begin(), entries.end(), regDumpEntryOrder);
    for (const auto& entry : entries) {
      std::cout << "### " << regDumpEntryLabel(entry) << " ###\n\n";
      printFileContent(entry.string());
      std::cout << std::endl;
    }
  }
}

void Utils::printI2cDumpDetails() {
  std::cout << "##### I2C Dump Information #####" << std::endl;
  if (config_.i2cDumpDevices()->empty()) {
    std::cout << "No device to i2cdump found from configs\n" << std::endl;
    return;
  }

  for (const auto& path : *config_.i2cDumpDevices()) {
    std::cout << fmt::format("#### i2cdump for {} ####", path) << std::endl;
    auto i2cInfo = getI2cInfoForDevice(path);
    if (i2cInfo) {
      auto [bus, devAddr] = *i2cInfo;
      auto cmd = fmt::format("timeout 15 i2cdump -f -y {} {} b", bus, devAddr);

      std::cout << fmt::format("Running `{}`", cmd) << std::endl;
      auto [ret, output] = platformUtils_.execCommand(cmd);
      std::cout << output << std::endl;
      if (ret == 124) {
        std::cout << "Error: command timed out after 15 seconds" << std::endl;
      }
    }
  }
}

void Utils::printPsuDetails() {
  std::cout << "##### PSU Information #####" << std::endl;

  for (const auto& psu : *config_.psus()) {
    std::cout << fmt::format("#### PSU Details {} ####", psu) << std::endl;

    auto i2cInfo = getI2cInfoForDevice(psu);
    if (i2cInfo) {
      auto [busNum, deviceAddr] = *i2cInfo;
      try {
        PsuHelper(busNum, deviceAddr).dumpRegisters();
      } catch (const std::exception& e) {
        std::cout << fmt::format(
                         "Error: failed to dump registers: {}", e.what())
                  << std::endl;
      }
    }
    std::cout << std::endl;
  }
}

void Utils::printGpioDetails() {
  std::cout << "##### GPIO Information #####" << std::endl;
  if (config_.gpios()->empty()) {
    std::cout << "No GPIO chip found from configs\n" << std::endl;
    return;
  }
  for (const auto& gpio : *config_.gpios()) {
    printGpio(gpio);
  }
}

void Utils::printPemDetails() {
  std::cout << "##### PEM Information #####" << std::endl;
  if (config_.pems()->empty()) {
    std::cout << "No PEM found found from config\n" << std::endl;
    return;
  }
  for (const auto& pem : *config_.pems()) {
    std::cout << fmt::format("#### {} ####", *pem.name()) << std::endl;
    printSysfsAttribute("present", *pem.presenceSysfsPath());
    printSysfsAttribute("input_ok", *pem.inputOkSysfsPath());
    printSysfsAttribute("status", *pem.statusSysfsPath());
  }
  std::cout << std::endl;
}

void Utils::printFanDetails() {
  std::cout << "##### Fan Information #####" << std::endl;
  std::string fanServiceConfJson = ConfigLib().getFanServiceConfig();
  auto fanServiceConfig = apache::thrift::SimpleJSONSerializer::deserialize<
      fan_service::FanServiceConfig>(fanServiceConfJson);
  if (fanServiceConfig.fans()->empty()) {
    std::cout << "No fans found from configs\n" << std::endl;
    return;
  }

  for (int i = 0; i < 2; ++i) {
    if (i > 0) {
      std::cout << "Sleeping for 0.5s...\n" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    for (const auto& fan : *fanServiceConfig.fans()) {
      std::string presenceStr{"ReadError"}, rpmStr{"ReadError"},
          pwmPercentStr{"ReadError"};

      FanImpl fanImpl(fan);

      try {
        presenceStr = fanImpl.readFanIsPresentOnDevice() ? "True" : "False";
      } catch (const std::exception& e) {
        std::cout << "Error in reading Fan Presence: " << e.what() << std::endl;
        continue;
      }
      try {
        rpmStr = std::to_string(fanImpl.readFanRpm());
      } catch (const std::exception& e) {
        std::cout << "Error in reading Fan RPM: " << e.what() << std::endl;
      }
      try {
        pwmPercentStr = fmt::format("{}%", fanImpl.readFanPwmPercent());
      } catch (const std::exception& e) {
        std::cout << "Error in reading Fan PWM: " << e.what() << std::endl;
      }
      std::cout << fmt::format(
                       "{} -> present={}, rpm={}, pwmPercent={}",
                       *fan.fanName(),
                       presenceStr,
                       rpmStr,
                       pwmPercentStr)
                << std::endl;
    }
    std::cout << std::endl;
  }
}

void Utils::printFanspinnerDetails() {
  std::cout << "##### Fanspinner Information #####" << std::endl;
  if (config_.fanspinners()->empty()) {
    std::cout << "No fanspinner found from config\n" << std::endl;
    return;
  }
  for (const auto& fs : *config_.fanspinners()) {
    std::cout << fmt::format("#### Fanspinner Path: {} ####", *fs.path())
              << std::endl;
    for (const auto& attr : *fs.sysfsAttributes()) {
      printSysfsAttribute(*attr.name(), *attr.path());
    }
  }
  std::cout << std::endl;
}

void Utils::printNvmeDetails() {
  std::cout << "##### Nvme Information #####" << std::endl;

  auto listCmd = "nvme list";
  auto [ret, output] = platformUtils_.execCommand(listCmd);
  if (ret != 0) {
    std::cout << fmt::format(
                     "Error: `{}` exited with non-zero status: {}\n",
                     listCmd,
                     ret)
              << std::endl;
    return;
  }

  // Parse nvme-list output for devices
  std::vector<std::string> nvmeDevices;
  std::vector<std::string> lines;
  folly::split('\n', output, lines, true);
  if (lines.size() > 2) {
    for (size_t i = 2; i < lines.size(); ++i) {
      std::vector<std::string> fields;
      folly::split(' ', lines[i], fields, true);
      if (!fields.empty()) {
        nvmeDevices.emplace_back(fields[0]);
      }
    }
  }

  if (nvmeDevices.empty()) {
    std::cout << fmt::format("No nvme device found from `{}`\n", listCmd)
              << std::endl;
    return;
  }

  std::array<std::string, 4> cmds{
      "nvme smart-log {}",
      "nvme error-log {}",
      "nvme id-ctrl {} -H",
      "nvme id-ns {} -H"};
  for (const auto& nvmeDevice : nvmeDevices) {
    for (const auto& cmdStr : cmds) {
      auto cmd = fmt::format(fmt::runtime(cmdStr), nvmeDevice);
      std::cout << fmt::format("#### Running `{}` ####", cmd) << std::endl;
      std::cout << platformUtils_.execCommand(cmd).second << std::endl;
    }
  }
}

void Utils::printPowerGoodDetails() {
  std::cout << "##### Power Good Information #####" << std::endl;

  if (config_.scPowerGood() && config_.scPowerGood()->sysfsAttributes()) {
    std::cout << "Reading scPowerGood from sysfs" << std::endl;
    for (const auto& pgSysfs : *config_.scPowerGood()->sysfsAttributes()) {
      printSysfsAttribute(*pgSysfs.name(), *pgSysfs.path());
    }
  } else if (config_.scPowerGood() && config_.scPowerGood()->gpioAttribute()) {
    std::cout << "Reading scPowerGood from gpio" << std::endl;
    auto pgGpio = *config_.scPowerGood()->gpioAttribute();
    printGpio(pgGpio);
  } else {
    std::cout << "No powergood info found from config\n";
  }
  std::cout << std::endl;
}

bool Utils::readRegVal(
    int bus,
    const std::string& addr,
    const std::string& regStr,
    uint8_t& value) {
  auto [exitCode, output] = platformUtils_.execCommand(
      fmt::format("i2cget -y -f {} {} {}", std::to_string(bus), addr, regStr));

  if (exitCode != 0) {
    return false;
  }

  auto valueStr = output.erase(output.find_last_not_of("\n\r") + 1);
  try {
    value = static_cast<uint8_t>(std::stoul(valueStr, nullptr, 0));
  } catch (const std::exception& e) {
    std::cout << fmt::format(
                     "Error: failed to convert value {} to uint8_t: {}",
                     valueStr,
                     e.what())
              << std::endl;
    return false;
  }
  return true;
}

void Utils::printRegValueTable(
    const std::vector<std::tuple<std::string, int, int>>& parsedItems,
    size_t nameWidth,
    const std::optional<std::bitset<8>>& expectValueBits) {
  constexpr size_t bitWidth = 3; //"BIT" header and content length
  constexpr size_t valueWidth = 5; //"Value" header and content length
  constexpr size_t verdictWidth = 8; //"Verdict" header and content length
  constexpr size_t indentWidth = 2;

  auto getValueText = [&](size_t index) -> std::string {
    const auto& [name, value, bitIndex] = parsedItems[index];
    return std::to_string(value);
  };

  auto getVerdict = [&](size_t index) -> std::string {
    if (!expectValueBits) {
      return "";
    }
    const auto& [name, value, bitIndex] = parsedItems[index];
    const auto expectValue = expectValueBits->test(bitIndex) ? 1 : 0;
    if (value == expectValue) {
      return "expected";
    } else {
      return fmt::format("expect:{}", expectValue);
    }
  };

  // Build header
  std::string headerRow;
  headerRow = fmt::format(
      "  | {:<{}} | {:<{}} | {:<{}} |",
      "Name",
      nameWidth,
      "BIT",
      bitWidth,
      "Value",
      valueWidth);
  if (expectValueBits) {
    headerRow += fmt::format(" {:<{}} |", "Verdict", verdictWidth);
  }

  size_t maxRowWidth = headerRow.size();

  const std::string border = std::string(indentWidth, ' ') +
      std::string(maxRowWidth - indentWidth, '-');
  // Print header
  std::cout << border << std::endl;
  std::cout << headerRow << std::endl;
  std::cout << border << std::endl;

  // Print data rows
  for (size_t i = 0; i < parsedItems.size(); ++i) {
    const auto& [name, value, bitIndex] = parsedItems[i];
    const auto bitText = fmt::format("[{}]", bitIndex);
    const auto valueText = getValueText(i);
    const auto verdict = getVerdict(i);

    std::string dataRow;
    dataRow = fmt::format(
        "  | {:<{}} | {:<{}} | {:<{}} |",
        name,
        nameWidth,
        bitText,
        bitWidth,
        valueText,
        valueWidth);
    if (expectValueBits) {
      dataRow += fmt::format(" {:<{}} |", verdict, verdictWidth);
    }
    std::cout << dataRow << std::endl;
  }
  // print bottom
  std::cout << border << std::endl;
}

void Utils::parseRegValue(
    const uint8_t regValue,
    const showtech_config::parseRule& rule) {
  auto& mask = rule.mask().value();

  static const RE2 bitsetPattern(R"(0b[01]{8})");

  if (!RE2::FullMatch(mask, bitsetPattern)) {
    std::cout << fmt::format("Invalid register mask : {}", mask) << std::endl;
    return;
  }

  constexpr auto maxBits = 8;
  std::bitset<maxBits> maskBits(mask.substr(2));
  std::bitset<maxBits> valueBits(regValue);
  auto& nameList = rule.parameterList().value();
  auto numNames = nameList.size();
  if (numNames != maskBits.count()) {
    std::cout
        << fmt::format(
               "parse rule is unreasonable, number of names {} does not match mask bits {}",
               numNames,
               maskBits.count())
        << std::endl;
    return;
  }

  std::vector<std::tuple<std::string, int, int>> parsedData;
  parsedData.reserve(numNames);
  std::optional<std::bitset<maxBits>> expectedValueBits;

  if (rule.expectedValue()) {
    auto& expectedValue = rule.expectedValue().value();
    if (!RE2::FullMatch(expectedValue, bitsetPattern)) {
      std::cout << fmt::format(
                       "Invalid register expect value : {}", expectedValue)
                << std::endl;
    } else {
      expectedValueBits = std::bitset<maxBits>(expectedValue.substr(2));
    }
  }

  // Use fix length 30 for name width
  constexpr size_t maxNameWidth = 30;
  int nameIndex = 0;
  for (auto bit = 0; bit < maxBits; bit++) {
    if (maskBits.test(bit)) {
      auto regName = nameList[nameIndex];
      if (regName.size() > maxNameWidth) {
        regName.resize(maxNameWidth);
        std::cout
            << fmt::format(
                   "Warning: name is too long, will be truncated to {}, pls adjust config",
                   regName)
            << std::endl;
      }
      parsedData.emplace_back(regName, valueBits.test(bit) ? 1 : 0, bit);
      nameIndex++;
    }
  }

  if (nameIndex == 0) {
    return;
  }

  printRegValueTable(parsedData, maxNameWidth, expectedValueBits);
}

void Utils::printDeviceRegistersDetails() {
  std::cout << "##### I2C Devices Register Details #####" << std::endl;
  if (config_.i2cDumpDeviceRegs()) {
    for (const auto& i2cDev : *config_.i2cDumpDeviceRegs()) {
      auto& devicePath = *i2cDev.devicePath();
      auto i2cInfo = getI2cInfoForDevice(devicePath, true);
      if (!i2cInfo) {
        std::cout << fmt::format("Invalid I2C device path : {}", devicePath)
                  << std::endl;
        continue;
      }
      auto [i2cBus, addr] = *i2cInfo;
      auto i2cAddr = fmt::format("0x{:02x}", addr);

      std::cout << fmt::format(
                       "##Registers in {} on bus {}, address {}##",
                       std::filesystem::path(devicePath).filename().string(),
                       i2cBus,
                       i2cAddr)
                << std::endl;

      for (const auto& regSet : *i2cDev.regSet()) {
        for (const auto& reg : *regSet.regList()) {
          uint8_t regValue = 0;
          auto readResult = fmt::format(
              "  Register [{}] on {}: ", *reg.regName(), *reg.regAddr());
          if (readRegVal(i2cBus, i2cAddr, *reg.regAddr(), regValue)) {
            auto rawRegValue = fmt::format("0x{:02x}", regValue);
            readResult += fmt::format("Raw Value [{}]", rawRegValue);
            std::cout << readResult << std::endl;
            if (regSet.parseRule()) {
              parseRegValue(regValue, *regSet.parseRule());
            }
          } else {
            readResult += "Read error";
            std::cout << readResult << std::endl;
          }
        }
      }
    }
  } else {
    std::cout << "No i2c device registers found from config\n" << std::endl;
  }
  std::cout << std::endl;
}

void Utils::printServiceLogs(const std::string& service) const {
  std::string cmd;
  auto logFile = fmt::format("/var/facebook/logs/fboss/{}.log", service);
  if (std::filesystem::exists(logFile)) {
    cmd = fmt::format("cat {}", logFile);
  } else {
    cmd = fmt::format("journalctl -u {}", service);
  }
  std::cout << execCommandWithLimit(cmd).second << std::endl;
}

void Utils::printLogs() {
  std::cout << "##### Platform Manager Log #####" << std::endl;
  printServiceLogs("platform_manager");

  std::cout << "##### Sensor Service Log #####" << std::endl;
  printServiceLogs("sensor_service");

  std::cout << "##### Fan Service Log #####" << std::endl;
  printServiceLogs("fan_service");

  std::cout << "##### Data Corral Log #####" << std::endl;
  printServiceLogs("data_corral_service");

  std::cout << "##### QSFP Service Log #####" << std::endl;
  printServiceLogs("qsfp_service");

  std::cout << "##### fboss_sw_agent Log #####" << std::endl;
  printServiceLogs("fboss_sw_agent");

  std::cout << "##### fboss_hw_agent@0 Log #####" << std::endl;
  printServiceLogs("fboss_hw_agent@0");

  std::cout << "##### dmesg Log #####" << std::endl;
  std::cout << execCommandWithLimit("dmesg").second << std::endl;

  std::cout << "##### Boot Console Log #####" << std::endl;
  std::cout << execCommandWithLimit("cat /var/log/boot.log").second
            << std::endl;

  std::cout << "##### Linux Messages Log #####" << std::endl;
  std::cout << execCommandWithLimit("cat /var/log/messages").second
            << std::endl;
}

void Utils::runFbossCliCmd(const std::string& cmd) {
  if (!std::filesystem::exists("/etc/ramdisk")) {
    auto fullCmd = fmt::format("fboss2 show {}", cmd);
    std::cout << fmt::format("#### {} ####", fullCmd) << std::endl;
    std::cout << platformUtils_.execCommand(fullCmd).second << std::endl;
  }
}

std::pair<int, std::string> Utils::execCommandWithLimit(
    const std::string& cmd,
    int maxLines) const {
  auto [ret, output] = platformUtils_.execCommand(cmd);
  std::vector<std::string> lines;
  folly::split('\n', output, lines);

  int totalLines = lines.size();
  if (totalLines <= maxLines) {
    return {ret, output};
  }

  // truncate output
  int headCount = (maxLines + 1) / 2;
  int tailCount = maxLines / 2;
  std::string first =
      folly::join('\n', folly::range(lines.begin(), lines.begin() + headCount));
  std::string last =
      folly::join('\n', folly::range(lines.end() - tailCount, lines.end()));

  return {
      ret,
      fmt::format(
          "=== Output exceeds {} lines (total: {}). "
          "Showing first {} and last {} lines ===\n\n"
          "{}\n\n"
          "=== {} lines truncated ===\n\n"
          "{}\n",
          maxLines,
          totalLines,
          headCount,
          tailCount,
          first,
          totalLines - maxLines,
          last)};
}

void Utils::printSysfsAttribute(
    const std::string& label,
    const std::string& path) {
  std::cout << label << ": ";
  try {
    std::cout << readSysfs(path) << std::endl;
  } catch (const std::exception& e) {
    std::cout << fmt::format(
                     "Error: failed to read sysfs path {}: {}", path, e.what())
              << std::endl;
  }
}

std::optional<std::tuple<int, int>> Utils::getI2cInfoForDevice(
    const std::string& path,
    bool skipLog) {
  std::string i2cPath{};
  try {
    i2cPath = std::filesystem::read_symlink(path).string();
  } catch (const std::filesystem::filesystem_error& ex) {
    std::cout << fmt::format(
                     "Error: failed to resolve device symlink {}:{}",
                     path,
                     ex.what())
              << std::endl;
    return std::nullopt;
  }

  int busNum;
  int deviceAddr;
  RE2 i2cPattern(R"(/sys/bus/i2c/devices/(\d+)-([0-9a-fA-F]+))");

  if (!RE2::PartialMatch(i2cPath, i2cPattern, &busNum, RE2::Hex(&deviceAddr))) {
    std::cout << "Error: Could not extract i2c bus and address from path: "
              << i2cPath << std::endl;
    return std::nullopt;
  }

  if (!skipLog) {
    std::cout << fmt::format(
                     "Extracted i2c bus: {}, device address: 0x{:04x}",
                     busNum,
                     deviceAddr)
              << std::endl;
  }
  return std::make_tuple(busNum, deviceAddr);
}

void Utils::printGpio(const Gpio& gpio) {
  std::cout << fmt::format("#### GPIO Chip Details {} ####", *gpio.path())
            << std::endl;
  struct gpiod_chip* chip = gpiod_chip_open(gpio.path()->c_str());
  for (const auto& line : *gpio.lines()) {
    std::cout << fmt::format(
        "line {:>3}:   {:<15} -> ", *line.lineIndex(), *line.name());
    try {
      std::cout << GpiodLine(chip, *line.lineIndex(), *line.name())
                       .getValuePreservingDirection()
                << std::endl;
    } catch (const std::exception& e) {
      std::cout << fmt::format("Error: failed to read gpio line: {}", e.what())
                << std::endl;
    }
  }
  gpiod_chip_close(chip);
  std::cout << std::endl;
}

} // namespace facebook::fboss::platform
