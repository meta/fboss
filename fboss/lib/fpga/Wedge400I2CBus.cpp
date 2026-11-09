// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/lib/fpga/Wedge400I2CBus.h"

#include "fboss/agent/Utils.h"
#include "fboss/lib/fpga/Wedge400Fpga.h"
#include "fboss/lib/usb/TransceiverI2CApi.h"

#include <fmt/core.h>
#include <folly/logging/xlog.h>

namespace {
constexpr uint32_t kPortsPerFpga = 24;

inline uint8_t getFpgaPort(int module) {
  /*
   * The port # on the front panel of W400 is like:
   *        1 -- 8 , 9 -- 16
   *       17 -- 32,33 -- 48
   *       FPGA 5.0,FPGA 8.0
   * so the left side and the right side are controlled by two different FPGAs.
   * Also the tcvr idx 0 starts on the lower half of the ports, meaning tcvr0 on
   * left FPGA is the port 17 above and the port 9 above is the tcvr17 on the
   * right FPGA. That's what the function is based on plus the 1 off between 1
   * based module # and 0 based tcvr idx.
   * module [1 -- 8] return [16 -- 23]
   * module [9 -- 16] return [16 -- 23]
   * module [17 -- 32] return [0 -- 15]
   * module [33 -- 48] return [0 -- 15]
   */
  if (module <= 8) {
    return module + 15;
  } else if (module <= 16) {
    return module + 7;
  } else if (module <= 32) {
    return module - 17;
  } else {
    return module - 33;
  }
}

inline bool isOnLeftFpga(int module) {
  return module <= 8 || (module >= 17 && module <= 32);
}
} // namespace

namespace facebook::fboss {
Wedge400I2CBus::Wedge400I2CBus() {
  auto wedge400Fpga = Wedge400Fpga::getInstance();
  // TODO(pgardideh): don't store these use wedge400Fpga directly
  leftFpga_ = wedge400Fpga->getLeftFpga();
  rightFpga_ = wedge400Fpga->getRightFpga();

  // Initialize the real time I2C access controllers.
  for (uint32_t rtc = 0; rtc < I2C_CTRL_PER_PIM; rtc++) {
    leftI2cControllers_[rtc] =
        std::make_unique<FbFpgaI2cController>(leftFpga_, rtc, 1);
  }

  for (uint32_t rtc = 0; rtc < I2C_CTRL_PER_PIM; rtc++) {
    rightI2cControllers_[rtc] =
        std::make_unique<FbFpgaI2cController>(rightFpga_, rtc, 2);
  }
}

Wedge400I2CBus::~Wedge400I2CBus() {}

void Wedge400I2CBus::moduleRead(
    unsigned int module,
    const TransceiverAccessParameter& param,
    uint8_t* buf) {
  // I2C Access should always have a i2c address
  CHECK(param.i2cAddress.has_value());
  auto offset = param.offset;
  auto len = param.len;
  auto i2cAddress = *(param.i2cAddress);
  if (len > 128) {
    throw I2cError("Too long to read");
  }

  XLOG(DBG5) << fmt::format(
      "I2C read to module {:d} at offset {:#x} for {:d} bytes",
      module - 1,
      offset,
      len);

  auto port = getFpgaPort(module);

  getI2cController(module)->read(
      getI2cControllerChannel(port),
      offset,
      folly::MutableByteRange(buf, len),
      i2cAddress);
}

void Wedge400I2CBus::moduleWrite(
    unsigned int module,
    const TransceiverAccessParameter& param,
    const uint8_t* data) {
  // I2C Access should always have a i2c address
  CHECK(param.i2cAddress.has_value());
  auto offset = param.offset;
  auto len = param.len;
  auto i2cAddress = *(param.i2cAddress);
  if (len > 128) {
    throw I2cError("Too long to write");
  }

  XLOG(DBG5) << fmt::format(
      "I2C write to module {:d} at offset {:#x} for {:d} bytes",
      module - 1,
      offset,
      len);

  auto port = getFpgaPort(module);

  getI2cController(module)->write(
      getI2cControllerChannel(port),
      offset,
      folly::ByteRange(data, len),
      i2cAddress);
}

bool Wedge400I2CBus::isPresent(unsigned int module) {
  XLOG(DBG5) << fmt::format("detecting presence of module:{:d}", module);
  auto port = getFpgaPort(module);
  return isOnLeftFpga(module) ? leftFpga_->isQsfpPresent(port)
                              : rightFpga_->isQsfpPresent(port);
}

void Wedge400I2CBus::scanPresence(
    std::map<int32_t, ModulePresence>& presences) {
  auto leftQsfpPresence = leftFpga_->getQsfpsPresence();
  auto rightQsfpPresence = rightFpga_->getQsfpsPresence();

  for (int port = 0; port < kPortsPerFpga * 2; port++) {
    if (isOnLeftFpga(port) ? ((leftQsfpPresence >> getFpgaPort(port)) & 1)
                           : ((rightQsfpPresence >> getFpgaPort(port)) & 1)) {
      presences[port] = ModulePresence::PRESENT;
    } else {
      presences[port] = ModulePresence::ABSENT;
    }
  }
}

void Wedge400I2CBus::ensureOutOfReset(unsigned int module) {
  auto port = getFpgaPort(module);
  isOnLeftFpga(module) ? leftFpga_->ensureQsfpOutOfReset(port)
                       : rightFpga_->ensureQsfpOutOfReset(port);
}

/* Get the i2c transaction stats from I2C controllers in Wedge400. In case
 * of wedge400 there are six FbFpgaI2cController for left side modules and
 * six for right side modules. This function put the I2c controller stats
 * from each controller in a vector and returns to the caller
 */
std::vector<I2cControllerStats> Wedge400I2CBus::getI2cControllerStats() {
  std::vector<I2cControllerStats> i2cControllerCurrentStats;

  for (uint32_t rtc = 0; rtc < I2C_CTRL_PER_PIM; ++rtc) {
    // Get the I2c controller platform stats from the i2c controller
    // class for all the controllers
    i2cControllerCurrentStats.push_back(
        leftI2cControllers_[rtc]->getI2cControllerPlatformStats());
    i2cControllerCurrentStats.push_back(
        rightI2cControllers_[rtc]->getI2cControllerPlatformStats());
  }
  return i2cControllerCurrentStats;
}

folly::EventBase* Wedge400I2CBus::getEventBase(unsigned int module) {
  auto port = getFpgaPort(module);
  auto i2cCtrlIdx = getI2cControllerIdx(port);
  return isOnLeftFpga(module)
      ? leftI2cControllers_[i2cCtrlIdx]->getEventBase()
      : rightI2cControllers_[i2cCtrlIdx]->getEventBase();
}

FbFpgaI2cController* Wedge400I2CBus::getI2cController(unsigned int module) {
  auto port = getFpgaPort(module);
  auto i2cCtrlIdx = getI2cControllerIdx(port);
  return isOnLeftFpga(module) ? leftI2cControllers_[i2cCtrlIdx].get()
                              : rightI2cControllers_[i2cCtrlIdx].get();
}

} // namespace facebook::fboss
