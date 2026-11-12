// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <fmt/format.h>
#include <filesystem>
#include <map>
#include <string>

#include "fboss/lib/DevMap.h"
#include "fboss/lib/SysfsUtils.h"

namespace fs = std::filesystem;

namespace {
// "kDevMapRoot", "kSubDirMap" and leaf symlink files are created by the
// Linux udevd at boot up time.
// Please do NOT modify "kDevMapRoot" or "kSubDirMap" unless the udev
// rules are updated.
const fs::path kDevMapRoot = "/run/devmap";
const std::map<facebook::fboss::FbDeviceType, std::string> kSubDirMap = {
    {facebook::fboss::FbDeviceType::I2C_CONTROLLER, "i2c-busses"},
    {facebook::fboss::FbDeviceType::GPIO_CONTROLLER, "gpiochips"},
    {facebook::fboss::FbDeviceType::MDIO_CONTROLLER, "mdio-busses"},
    {facebook::fboss::FbDeviceType::EEPROM, "eeproms"},
    {facebook::fboss::FbDeviceType::SENSOR, "sensors"},
    {facebook::fboss::FbDeviceType::FPGA, "fpgas"},
    {facebook::fboss::FbDeviceType::CPLD, "cplds"},
};
} // namespace

namespace facebook::fboss {
fs::path resolveDevice(FbDeviceType type, const std::string& name) {
  fs::path symPath = kDevMapRoot / kSubDirMap.at(type) / name;
  if (!fs::is_symlink(symPath)) {
    throw std::runtime_error(
        fmt::format("Invalid device name: {} is not a symlink", name));
  }

  return fs::read_symlink(symPath);
}

fs::path resolveI2cSysfs(const std::string& name) {
  fs::path cdevPath = resolveDevice(FbDeviceType::I2C_CONTROLLER, name);
  return sysI2cDevices / cdevPath.filename();
}
} // namespace facebook::fboss
