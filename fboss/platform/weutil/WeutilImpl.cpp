// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/platform/weutil/WeutilImpl.h"
#include "fboss/platform/weutil/ContentValidator.h"
#include "fboss/platform/weutil/FbossEepromInterface.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

#include <folly/Conv.h>
#include <folly/json/json.h>
#include <iostream>
#include <string>

namespace {
std::string getProductionStateString(const std::string& value) {
  try {
    auto state = static_cast<facebook::fboss::platform::ProductionState>(
        folly::to<int>(value));
    return apache::thrift::util::enumNameOrThrow(state);
  } catch (std::exception&) {
    throw std::runtime_error(
        fmt::format("Invalid Production State with value: '{}'", value));
  }
}
} // namespace

namespace facebook::fboss::platform {

WeutilImpl::WeutilImpl(const std::string& eepromPath, const uint16_t offset)
    : eepromPath_(eepromPath), offset_(offset) {}

std::vector<std::pair<std::string, std::string>> WeutilImpl::getContents() {
  auto contents = FbossEepromInterface(eepromPath_, offset_).getContents();
  if (!ContentValidator().isValid(contents)) {
    throw std::runtime_error("Invalid EEPROM contents");
  }
  return contents;
}

void WeutilImpl::printInfo() {
  for (auto [key, value] : getContents()) {
    if (key == "Production State") {
      std::cout << key << ": " << getProductionStateString(value) << std::endl;
      continue;
    }
    std::cout << key << ": " << value << std::endl;
  }
}

folly::dynamic WeutilImpl::getInfoJson() {
  folly::dynamic eepromJsonObject = folly::dynamic::object;
  for (const auto& [key, value] : getContents()) {
    if (key == "CRC16") {
      continue;
    }
    eepromJsonObject[key] = value;
  }

  return eepromJsonObject;
}

} // namespace facebook::fboss::platform
