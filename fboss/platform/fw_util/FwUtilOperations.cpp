#include <folly/logging/xlog.h>
#include <filesystem>
#include <fstream>
#include "fboss/platform/fw_util/FwUtilImpl.h"
#include "fboss/platform/fw_util/fw_util_helpers.h"
#include "fboss/platform/helpers/PlatformUtils.h"

namespace facebook::fboss::platform::fw_util {

void FwUtilImpl::doJtagOperation(
    const JtagConfig& config,
    const std::string& fpd) {
  XLOG(INFO) << "enabling jtag register for " << fpd
             << " as preUpgrade operation";
  if (!config.path()->empty()) {
    // print debug statement of what's being writen to which path
    XLOG(INFO) << "writing: " << *config.value()
               << " to path: " << *config.path();
    std::ofstream outputFile(*config.path());
    outputFile << std::to_string(*config.value());
    outputFile.close();
  } else {
    XLOG(ERR) << "Invalid JTAG configuration";
  }
}

void FwUtilImpl::doGpiosetOperation(
    const GpiosetConfig& config,
    const std::string& fpd) {
  XLOG(INFO) << "setting gpio register for " << fpd
             << " as preUpgrade operation";
  std::string gpioChip = getGpioChip(*config.gpioChip());
  // Execute the gpioset command
  std::vector<std::string> gpiosetCmd = {
      "/usr/bin/gpioset",
      gpioChip,
      *config.gpioChipPin() + "=" + *config.gpioChipValue()};
  auto [exitStatus, standardOut] = PlatformUtils().runCommand(gpiosetCmd);
  checkCmdStatus(gpiosetCmd, exitStatus);
}

void FwUtilImpl::doWriteToPortOperation(
    const WriteToPortConfig& args,
    const std::string& platformName) {
  XLOG(INFO) << "Writing specific bytes to I/O port for " << platformName
             << " preUpgrade";

  // Open the port file in binary mode
  std::ofstream port(
      args.portFile().value(), std::ios_base::out | std::ios_base::binary);
  if (!port.is_open()) {
    XLOG(ERR) << "Failed to open " << args.portFile().value();
    return;
  }

  // Seek to the specified offset
  port.seekp(
      static_cast<int>(strtol(args.hexOffset().value().c_str(), nullptr, 16)));
  if (!port.good()) {
    XLOG(ERR) << "Failed to seek to offset " << args.hexOffset().value();
    return;
  }

  // Write the byte value to the port file
  char data = static_cast<char>(
      strtol(args.hexByteValue().value().c_str(), nullptr, 16));
  port.write(&data, 1);
  if (!port.good()) {
    XLOG(ERR) << "Failed to write to " << args.portFile().value();
    return;
  }

  port.close();
}

// Besides getting GPIO value, the gpioget command is also used to reset the
// input/output line direction. The operation below will reset the direction.
void FwUtilImpl::doGpiogetOperation(
    const GpiogetConfig& config,
    const std::string& fpd) {
  XLOG(INFO) << "Getting GPIO value for " << fpd;
  std::string gpioChip = getGpioChip(*config.gpioChip());
  std::vector<std::string> gpiogetCmd = {
      "/usr/bin/gpioget", gpioChip, *config.gpioChipPin()};
  auto [exitStatus, standardOut] = PlatformUtils().runCommand(gpiogetCmd);
  checkCmdStatus(gpiogetCmd, exitStatus);
}

void FwUtilImpl::performJamUpgrade(const JamConfig& jamConfig) {
  // Check if the firmware binary file is present
  if (!std::filesystem::exists(fwBinaryFile_)) {
    throw std::runtime_error(
        "Firmware binary file not found: " + fwBinaryFile_);
  }

  // Determine the JAM binary location
  std::string jamBinaryPath = getUpgradeToolBinaryPath("jam");

  // Construct the JAM command with the provided configuration
  std::vector<std::string> jamCmd = {jamBinaryPath};
  if (apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          jamConfig.jamExtraArgs())) {
    for (const auto& arg : *jamConfig.jamExtraArgs()) {
      jamCmd.push_back(arg);
    }
  }

  jamCmd.push_back(fwBinaryFile_);

  // Execute the JAM command
  auto [exitStatus, standardOut] = PlatformUtils().runCommand(jamCmd);

  // we want to see the output of the command first
  XLOG(INFO) << standardOut;
  // let's check the exist status which will throw an exception if the command
  // failed.
  checkCmdStatus(jamCmd, exitStatus);
}

void FwUtilImpl::performXappUpgrade(const XappConfig& xappConfig) {
  // Check if the firmware binary file is present
  if (!std::filesystem::exists(fwBinaryFile_)) {
    throw std::runtime_error(
        "Firmware binary file not found: " + fwBinaryFile_);
  }

  // Determine the XAPP binary location
  std::string xappBinaryPath = getUpgradeToolBinaryPath("xapp");

  // Construct the XAPP command with the provided configuration
  std::vector<std::string> xappCmd = {xappBinaryPath};
  if (apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          xappConfig.xappExtraArgs())) {
    for (const auto& arg : *xappConfig.xappExtraArgs()) {
      xappCmd.push_back(arg);
    }
  }
  xappCmd.push_back(fwBinaryFile_);

  // Execute the XAPP command
  auto [exitStatus, standardOut] = PlatformUtils().runCommand(xappCmd);

  // we want to see the output of the command first
  XLOG(INFO) << standardOut;

  checkCmdStatus(xappCmd, exitStatus);
}

void FwUtilImpl::performPsuUtilUpgrade(const PsuUtilConfig& psuUtilConfig) {
  // Check if the firmware binary file is present
  if (!std::filesystem::exists(fwBinaryFile_)) {
    throw std::runtime_error(
        "Firmware binary file not found: " + fwBinaryFile_);
  }

  // Determine the psu_util binary location
  std::string psuUtilBinaryPath = getUpgradeToolBinaryPath("psu_util");

  // Construct the psu_util command with the provided configuration
  std::vector<std::string> psuUtilCmd = {psuUtilBinaryPath};
  for (const auto& arg : *psuUtilConfig.psuUtilExtraArgs()) {
    psuUtilCmd.push_back(arg);
  }

  psuUtilCmd.push_back(fwBinaryFile_);

  XLOG(INFO) << "Running: " << folly::join(" ", psuUtilCmd);

  // Don't pipe stdout/stderr — let psu_util print directly to terminal
  // so the operator sees real-time progress during firmware upgrade.
  // This is important because PSU upgrades may trigger a reboot,
  // and captured output would be lost.
  folly::Subprocess p(psuUtilCmd, folly::Subprocess::Options());
  int exitStatus = p.wait().exitStatus();

  checkCmdStatus(psuUtilCmd, exitStatus);
}

} // namespace facebook::fboss::platform::fw_util
