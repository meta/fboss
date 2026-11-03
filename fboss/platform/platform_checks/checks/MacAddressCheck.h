#pragma once

#include <folly/MacAddress.h>

#include <string>
#include <unordered_map>
#include <utility>

#include "fboss/platform/platform_checks/PlatformCheck.h"

namespace facebook::fboss::platform::platform_checks {

/**
 * Validates that the management interface MAC address matches the EEPROM MAC
 * address.
 */
class MacAddressCheck : public PlatformCheck {
 public:
  explicit MacAddressCheck(
      CheckTarget target = {},
      std::string interfaceName = "eth0")
      : PlatformCheck(std::move(target)),
        interfaceName_(std::move(interfaceName)) {}

  CheckResult run() override;

  CheckType getType() const override {
    return CheckType::MAC_ADDRESS_CHECK;
  }

  std::string getName() const override {
    return "Mac Address Correctness";
  }

  std::string getDescription() const override {
    return "Validates that management interface MAC address matches EEPROM MAC address";
  }

 protected:
  virtual folly::MacAddress getMacAddress(const std::string& interface);
  virtual std::unordered_map<std::string, folly::MacAddress>
  getEepromMacAddressList();

 private:
  std::string interfaceName_;
};

} // namespace facebook::fboss::platform::platform_checks
