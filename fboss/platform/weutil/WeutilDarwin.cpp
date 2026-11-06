// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <folly/Conv.h>
#include <folly/ScopeGuard.h>
#include <folly/json/json.h>
#include <filesystem>

#include "fboss/platform/helpers/PlatformUtils.h"
#include "fboss/platform/weutil/WeutilDarwin.h"
#include "fboss/platform/weutil/prefdl/Prefdl.h"

using namespace facebook::fboss::platform;

namespace {
const std::string kFlashromGetFlashType = "flashrom -p internal ";

// Create a fresh, private, root-owned temp directory for this run's SPI dump.
// mkdtemp yields a 0700 directory with an unpredictable name, so a local
// attacker cannot pre-create the directory (or plant symlinks inside it) to
// redirect the root-owned flashrom/dd writes that follow -- unlike the old
// fixed, predictable /tmp/WeutilDarwin path.
std::string createSecureRunDir() {
  std::string tmpl = "/tmp/WeutilDarwin.XXXXXX";
  if (::mkdtemp(tmpl.data()) == nullptr) {
    throw std::runtime_error(
        folly::to<std::string>("Cannot create temp directory from: ", tmpl));
  }
  return tmpl;
}

// Map weutil fields to prefld fields
const std::unordered_map<std::string, std::string> kMapping{
    {"Product Name", "SID"},
    {"Product Part Number", "SKU"},
    {"System Assembly Part Number", "ASY"},
    {"ODM PCBA Part Number", "PCA"},
    {"Product Version", "HwVer"},
    {"Product Sub-Version", "HwSubVer"},
    {"System Manufacturing Date", "MfgTime2"},
    {"Local MAC", "MAC"},
    {"Product Serial Number", "SerialNumber"},
};

// weutil output fields and default value for all FBOSS switches w/wo OpenBMC
const std::vector<std::pair<std::string, std::string>> weFields_{
    {"Wedge EEPROM", "CHASSIS"},
    {"Version", "0"},
    {"Product Name", ""},
    {"Product Part Number", ""},
    {"System Assembly Part Number", ""},
    {"Facebook PCBA Part Number", ""},
    {"Facebook PCB Part Number", ""},
    {"ODM PCBA Part Number", ""},
    {"ODM PCBA Serial Number", ""},
    {"Product Production State", "0"},
    {"Product Version", ""},
    {"Product Sub-Version", ""},
    {"Product Serial Number", ""},
    {"Product Asset Tag", ""},
    {"System Manufacturer", ""},
    {"System Manufacturing Date", ""},
    {"PCB Manufacturer", ""},
    {"Assembled At", ""},
    {"Local MAC", ""},
    {"Extended MAC Base", ""},
    {"Extended MAC Address Size", ""},
    {"Location on Fabric", ""},
    {"CRC8", "0x0"},
};

const std::unordered_set<std::string> kFlashType = {
    "MX25L12805D",
    "N25Q128..3E"};

std::string getFlashType(const std::string& str) {
  for (auto& it : kFlashType) {
    if (str.find(it) != std::string::npos) {
      return it;
    }
  }
  return "";
}

} // namespace

namespace facebook::fboss::platform {
WeutilDarwin::WeutilDarwin(const std::string& eepromPath) {
  std::string fruPath;
  std::optional<std::string> runDir;
  // Remove the per-run temp directory on all paths (success or exception),
  // after PrefdlBase has parsed the file below.
  auto cleanup = folly::makeGuard([&runDir]() {
    if (runDir) {
      std::error_code ec;
      std::filesystem::remove_all(*runDir, ec);
    }
  });
  if (eepromPath == "") {
    runDir = createSecureRunDir();
    fruPath = genSpiPrefdlFile(*runDir);
  } else {
    fruPath = eepromPath;
  }
  eepromParser_ = std::make_unique<PrefdlBase>(fruPath);
}

std::string WeutilDarwin::genSpiPrefdlFile(const std::string& runDir) {
  int exitStatus = 0;
  std::string standardOut;

  const std::string layoutPath = runDir + "/layout";
  const std::string biosPath = runDir + "/bios";
  const std::string prefdlPath = runDir + "/system-prefdl-bin";

  const std::string createLayout =
      "echo \"00001000:0001efff prefdl\" > " + layoutPath;
  std::tie(exitStatus, standardOut) = PlatformUtils().execCommand(createLayout);
  if (exitStatus != 0) {
    throw std::runtime_error("Cannot create layout file with: " + createLayout);
  }

  // Get flash type
  std::tie(exitStatus, standardOut) =
      PlatformUtils().execCommand(kFlashromGetFlashType + " 2>&1 ");

  /* Since flashrom will return 1 for "flashrom -p internal"
   * we ignore retVal == 1
   */

  if ((exitStatus != 0) && (exitStatus != 1)) {
    throw std::runtime_error(
        "Cannot get flash type with: " + kFlashromGetFlashType);
  }

  const std::string flashromGetContent =
      " -l " + layoutPath + " -i prefdl -r " + biosPath + " > /dev/null 2>&1";
  std::string getPrefdl;
  std::string flashType = getFlashType(standardOut);

  if (!flashType.empty()) {
    getPrefdl = folly::to<std::string>(
        kFlashromGetFlashType, " -c ", flashType, flashromGetContent);
  } else {
    getPrefdl =
        folly::to<std::string>(kFlashromGetFlashType, flashromGetContent);
  }

  std::tie(exitStatus, standardOut) = PlatformUtils().execCommand(getPrefdl);
  if (exitStatus != 0) {
    throw std::runtime_error(
        folly::to<std::string>(
            "Cannot create BIOS file with: ",
            getPrefdl,
            " ",
            ", return value: ",
            std::to_string(exitStatus)));
  }

  const std::string ddCommand = "dd if=" + biosPath + " of=" + prefdlPath +
      " bs=1 skip=8192 count=61440 > /dev/null 2>&1";
  std::tie(exitStatus, standardOut) = PlatformUtils().execCommand(ddCommand);
  if (exitStatus != 0) {
    throw std::runtime_error("Cannot create prefdl file with: " + ddCommand);
  }

  return prefdlPath;
}

std::vector<std::pair<std::string, std::string>> WeutilDarwin::getContents() {
  std::vector<std::pair<std::string, std::string>> ret;
  for (auto& item : weFields_) {
    auto it = kMapping.find(item.first);
    ret.emplace_back(
        item.first,
        it == kMapping.end() ? item.second
                             : eepromParser_->getField(it->second));
  }
  return ret;
}

void WeutilDarwin::printInfo() {
  for (auto item : weFields_) {
    if (item.first == "Wedge EEPROM") {
      std::cout << item.first << folly::to<std::string>(" ", item.second, ":")
                << std::endl;
    } else {
      auto it = kMapping.find(item.first);
      std::cout << folly::to<std::string>(item.first, ": ")
                << (it == kMapping.end() ? item.second
                                         : eepromParser_->getField(it->second))
                << std::endl;
    }
  }
}

folly::dynamic WeutilDarwin::getInfoJson() {
  folly::dynamic wedgeJsonInfo = folly::dynamic::object;

  for (const auto& item : weFields_) {
    if (item.first != "Wedge EEPROM") {
      auto it = kMapping.find(item.first);
      wedgeJsonInfo[item.first] =
          (it == kMapping.end() ? item.second
                                : eepromParser_->getField(it->second));
    }
  }

  // For Darwin BMC-less, we need to fill empty field "Extended MAC Address
  // Size" and "Extended MAC Base" (same as "Local MAC") as a workaround for
  // creating /var/facebook/fboss/fruid.json (T110038028)
  if (wedgeJsonInfo["Extended MAC Base"] == "") {
    wedgeJsonInfo["Extended MAC Base"] = wedgeJsonInfo["Local MAC"];
    wedgeJsonInfo["Extended MAC Address Size"] = "1";
  }

  return wedgeJsonInfo;
}

} // namespace facebook::fboss::platform
