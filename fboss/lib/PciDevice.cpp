/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/PciDevice.h"
#include <pciaccess.h>
#include "fboss/lib/PciSystem.h"

#include <fmt/format.h>
#include <folly/Exception.h>
#include <folly/Singleton.h>
#include <folly/logging/xlog.h>

namespace {
constexpr auto kMaxPciRegions = 6;

constexpr auto kPciConfigCommandRegOffset = 4;
constexpr auto kPciConfigParityEnable = 0x40; // command reg bit 6
constexpr auto kPciConfigMemAccessEnable = 0x02; // command reg bit 1
} // namespace

namespace facebook::fboss {
PciDevice::~PciDevice() {
  this->close();
}

void PciDevice::open() {
  pci_id_match deviceMask;
  pci_device_iterator* deviceIter;
  int32_t retVal;

  // already open?
  if (pciDevice_ != nullptr) {
    // All is good, device created correctly
    XLOG(INFO) << fmt::format(
        "PCI devie {:04x}:{:04x} already open", vendorId_, deviceId_);
    return;
  }

  // Setup device mask - Need to make sure whole struct is
  // initialized to 0, so that appropriate fields
  // are a don't care when the iterator searches for devices.
  memset(&deviceMask, 0, sizeof(deviceMask));
  deviceMask.vendor_id = vendorId_;
  deviceMask.device_id = deviceId_;
  deviceMask.subvendor_id = PCI_MATCH_ANY;
  deviceMask.subdevice_id = PCI_MATCH_ANY;

  // Only one PCI device per process, so need to get
  // singleton instance of PciSystem before proceeding
  pciSystem_ = PciSystem::getInstance();

  deviceIter = pci_id_match_iterator_create(&deviceMask);

  if (deviceIter == nullptr) {
    folly::throwSystemError("Could not create PCI iterator");
  }

  pciDevice_ = pci_device_next(deviceIter);

  // done with iterator - destroy
  pci_iterator_destroy(deviceIter);

  if (pciDevice_ == nullptr) {
    this->close();
    folly::throwSystemError("No matching vendor and device id found");
  }

  // This gets us all of the info we want
  retVal = pci_device_probe(pciDevice_);
  if (retVal != 0) {
    folly::throwSystemErrorExplicit(
        retVal,
        fmt::format(
            "Could not probe PCI device "
            "{:04x}:{:04x}",
            vendorId_,
            deviceId_));
  }

  // All is good, device created correctly
  XLOG(DBG1) << fmt::format(
      "Created PCI access for {:04x}:{:04x}", vendorId_, deviceId_);
}

void PciDevice::close() {
  pciDevice_ = nullptr;
}

uint64_t PciDevice::getMemoryRegionAddress(uint32_t region) const {
  if (pciDevice_ == nullptr) {
    throw std::runtime_error("PCI device not initialized");
  }

  if (region >= kMaxPciRegions) {
    throw std::runtime_error("PCI region out of bounds");
  }

  return pciDevice_->regions[region].base_addr;
}

uint64_t PciDevice::getMemoryRegionSize(uint32_t region) const {
  if (pciDevice_ == nullptr) {
    throw std::runtime_error("PCI device not initialized");
  }

  if (region >= kMaxPciRegions) {
    throw std::runtime_error("PCI region out of bounds");
  }

  return pciDevice_->regions[region].size;
}

uint8_t PciDevice::getRevision() const {
  if (pciDevice_ == nullptr) {
    throw std::runtime_error("PCI device not initialized");
  }
  return pciDevice_->revision;
}

void PciDevice::setConfigControl() {
  if (pciDevice_ == nullptr) {
    throw std::runtime_error(
        "PCI device not initialized while control config attempted");
  }
  uint8_t data = kPciConfigParityEnable | kPciConfigMemAccessEnable;
  uint64_t bytesWritten;
  int rc = pci_device_cfg_write(
      pciDevice_, &data, kPciConfigCommandRegOffset, 1, &bytesWritten);
  if (rc || bytesWritten < 1) {
    XLOG(ERR) << "PCI config command register could not be set";
  } else {
    XLOG(INFO) << "PCI config command register set to enable memory access";
  }
}

} // namespace facebook::fboss
