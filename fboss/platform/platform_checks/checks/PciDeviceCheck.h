/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "fboss/platform/platform_checks/PlatformCheck.h"
#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"

namespace facebook::fboss::platform::platform_checks {

class PciDeviceCheck : public PlatformCheck {
 public:
  explicit PciDeviceCheck(CheckTarget target = {})
      : PlatformCheck(std::move(target)) {}

  CheckResult run() override;

  CheckType getType() const override {
    return CheckType::PCI_DEVICE_CHECK;
  }

  std::string getName() const override {
    return "PCI Devices Exist";
  }

  std::string getDescription() const override {
    return "Validates that all expected PCI devices are present on the system";
  }

 protected:
  virtual std::vector<std::filesystem::path> getPciDevicePaths() const;

 private:
  struct PciDevice {
    std::string vendor;
    std::string device;
    std::string subsystemVendor;
    std::string subsystemDevice;

    std::string toString() const {
      return vendor + ":" + device + ":" + subsystemVendor + ":" +
          subsystemDevice;
    }
  };

  // Devices present on the host, as PciDevice::toString(). All attributes
  // are read in one batch: over SSH every read is a round trip.
  std::set<std::string> readPresentDevices() const;
};

} // namespace facebook::fboss::platform::platform_checks
