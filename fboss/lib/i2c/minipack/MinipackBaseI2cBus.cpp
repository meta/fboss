// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/lib/i2c/minipack/MinipackBaseI2cBus.h"

#include <fmt/core.h>

namespace facebook::fboss {

MinipackBaseI2cBus::MinipackBaseI2cBus() {}

void MinipackBaseI2cBus::moduleRead(
    unsigned int module,
    const TransceiverAccessParameter& param,
    uint8_t* buf) {
  auto len = param.len;
  auto offset = param.offset;
  if (len > 128) {
    throw MinipackI2cError("Too long read");
  }
  auto pim = getPim(module);
  auto port = getQsfpPimPort(module);

  XLOG(DBG5) << fmt::format(
      "I2C read to pim {:d}, port {:d} at offset {:#x} for {:d} bytes",
      pim,
      port,
      offset,
      len);

  if (auto i2cController =
          systemContainer_->getPimContainer(pim)->getI2cController(port)) {
    uint8_t i2cAddress = param.i2cAddress
        ? *param.i2cAddress
        : TransceiverAccessParameter::ADDR_QSFP;
    i2cController->read(
        port, offset, folly::MutableByteRange(buf, len), i2cAddress);
  } else {
    systemContainer_->getPimContainer(pim)->getSpiController(port)->read(
        offset, *param.page, folly::MutableByteRange(buf, len));
  }
}

void MinipackBaseI2cBus::moduleWrite(
    unsigned int module,
    const TransceiverAccessParameter& param,
    const uint8_t* data) {
  auto len = param.len;
  auto offset = param.offset;
  if (len > 128) {
    throw MinipackI2cError("Too long write");
  }
  auto pim = getPim(module);
  auto port = getQsfpPimPort(module);

  XLOG(DBG5) << fmt::format(
      "I2C write to pim {:d}, port {:d} at offset {:#x} for {:d} bytes",
      pim,
      port,
      offset,
      len);

  if (auto i2cController =
          systemContainer_->getPimContainer(pim)->getI2cController(port)) {
    uint8_t i2cAddress = param.i2cAddress
        ? *param.i2cAddress
        : TransceiverAccessParameter::ADDR_QSFP;
    i2cController->write(port, offset, folly::ByteRange(data, len), i2cAddress);
  } else {
    systemContainer_->getPimContainer(pim)->getSpiController(port)->write(
        offset, *param.page, folly::ByteRange(data, len));
  }
}

bool MinipackBaseI2cBus::isPresent(unsigned int module) {
  auto pim = getPim(module);
  auto port = getQsfpPimPort(module);
  XLOG(DBG5) << fmt::format(
      "detecting presence of qsfp at pim:{:d}, port:{:d}", pim, port);
  return systemContainer_->getPimContainer(pim)
      ->getPimQsfpController()
      ->isQsfpPresent(port);
}

void MinipackBaseI2cBus::scanPresence(
    std::map<int32_t, ModulePresence>& presences) {
  std::set<uint8_t> pimsToScan;
  for (auto presence : presences) {
    auto pimID = getPim(presence.first + 1);
    if (systemContainer_->isValidPimID(pimID)) {
      pimsToScan.insert(pimID);
    }
  }

  for (auto pim : pimsToScan) {
    const auto& pimQsfpPresence = systemContainer_->getPimContainer(pim)
                                      ->getPimQsfpController()
                                      ->scanQsfpPresence();
    for (int port = 0; port < portsPerPim_; port++) {
      assert(pimQsfpPresence.size() > port);
      if (pimQsfpPresence[port]) {
        presences[getModule(pim, port) - 1] = ModulePresence::PRESENT;
      } else {
        presences[getModule(pim, port) - 1] = ModulePresence::ABSENT;
      }
    }
  }
}

uint8_t MinipackBaseI2cBus::getPim(int module) {
  return MinipackBaseSystemContainer::kPimStartNum +
      (module - 1) / portsPerPim_;
}

uint8_t MinipackBaseI2cBus::getQsfpPimPort(int module) {
  return (module - 1) % 16;
}

int MinipackBaseI2cBus::getModule(uint8_t pim, uint8_t port) {
  return 1 + (pim - 1) * portsPerPim_ + port;
}

void MinipackBaseI2cBus::ensureOutOfReset(unsigned int module) {
  auto pim = getPim(module);
  auto port = getQsfpPimPort(module);
  systemContainer_->getPimContainer(pim)
      ->getPimQsfpController()
      ->ensureQsfpOutOfReset(port);
}

folly::EventBase* MinipackBaseI2cBus::getEventBase(unsigned int module) {
  auto pim = getPim(module);
  auto port = getQsfpPimPort(module);
  return systemContainer_->getPimContainer(pim)
      ->getI2cController(port)
      ->getEventBase();
}

FbFpgaI2cController* MinipackBaseI2cBus::getI2cController(
    uint8_t pim,
    uint8_t idx) const {
  return systemContainer_->getPimContainer(pim)->getI2cController(idx);
}

} // namespace facebook::fboss
