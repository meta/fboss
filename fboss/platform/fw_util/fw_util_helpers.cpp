#include "fw_util_helpers.h"
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include "fboss/platform/helpers/PlatformUtils.h"

namespace facebook::fboss::platform::fw_util {

void checkCmdStatus(const std::vector<std::string>& cmd, int exitStatus) {
  // Check command status
  std::string cmdStr;
  for (const auto& arg : cmd) {
    cmdStr += arg + " ";
  }
  if (exitStatus < 0) {
    throw std::runtime_error(
        "Running command " + cmdStr + " failed with exit status " +
        std::to_string(exitStatus) + ", errno " + folly::errnoStr(errno));
  } else if (!WIFEXITED(exitStatus)) {
    throw std::runtime_error(
        "Running Command " + cmdStr + " exited abnormally with exit status " +
        std::to_string(exitStatus) +
        ", WEXITSTATUS: " + std::to_string(WEXITSTATUS(exitStatus)));
  }
}
std::string getUpgradeToolBinaryPath(const std::string& toolName) {
  // Get upgrade tool binary path
  std::string binaryPath;
  if (std::filesystem::exists("/usr/local/bin/" + toolName)) {
    binaryPath = "/usr/local/bin/" + toolName;
  } else {
    XLOG(INFO)
        << "Tool not found in /usr/local/bin, so it must be in the same directory as the FIS run_script";
    char* scriptPath = std::getenv("script_dir");
    if (scriptPath == nullptr) {
      throw std::runtime_error(
          "script_dir environment variable not set, cannot determine upgrade tool binary path");
    }
    binaryPath = std::string(scriptPath) + "/" + toolName;
  }

  // Check if the binary exists at the determined location
  if (!std::filesystem::exists(binaryPath)) {
    throw std::runtime_error(toolName + " binary not found at: " + binaryPath);
  }

  return binaryPath;
}

void verifySha1sum(
    const std::string& fpd,
    const std::string& configSha1sum,
    const std::string& binaryFile) {
  // Verify SHA1 sum
  // Execute the sha1sum command
  XLOG(INFO) << "Verifying sha1sum of " << binaryFile;
  std::vector<std::string> sha1sumCmd = {"/usr/bin/sha1sum", binaryFile};
  auto [exitStatus, standardOut] = PlatformUtils().runCommand(sha1sumCmd);
  checkCmdStatus(sha1sumCmd, exitStatus);

  // Parse the output to extract the SHA1 sum
  std::size_t spacePos = standardOut.find(' ');
  if (spacePos == std::string::npos) {
    throw std::runtime_error("Invalid output from sha1sum command");
  }
  std::string sha1sum = standardOut.substr(0, spacePos);

  // Check if the SHA1 sum matches
  if (configSha1sum != sha1sum) {
    throw std::runtime_error(
        fmt::format(
            "{} config file sha1sum {} is different from current binary sha1sum of {}",
            fpd,
            configSha1sum,
            sha1sum));
  }
}

std::string getGpioChip(const std::string& gpiochip) {
  auto [exitStatus, gpiodetectOutput] =
      PlatformUtils().runCommand({"/usr/bin/gpiodetect"});
  XLOG(INFO) << "Gpio detect output: " << gpiodetectOutput;
  checkCmdStatus({"/usr/bin/gpiodetect"}, exitStatus);

  // Execute the grep command with the output of gpiodetect as input
  auto [exitStatus2, gpioChipStr] = PlatformUtils().runCommandWithStdin(
      {"/usr/bin/grep", "-Ei", gpiochip}, gpiodetectOutput);

  checkCmdStatus({"/usr/bin/grep", "-Ei", gpiochip}, exitStatus2);
  // Execute the awk command with the output of grep as input
  auto [exitStatus3, gpioChipNumber] = PlatformUtils().runCommandWithStdin(
      {"/usr/bin/awk", "{print $1}"}, gpioChipStr);
  checkCmdStatus({"/usr/bin/awk", "{print $1}"}, exitStatus3);
  // Remove trailing whitespace
  gpioChipNumber = folly::trimWhitespace(gpioChipNumber).str();
  XLOG(INFO) << "Using GPIO: " << gpioChipNumber;
  return gpioChipNumber;
}

std::string toLower(std::string str) {
  std::string lowerCaseStr = str;
  folly::toLowerAscii(lowerCaseStr);
  return lowerCaseStr;
}

} // namespace facebook::fboss::platform::fw_util
