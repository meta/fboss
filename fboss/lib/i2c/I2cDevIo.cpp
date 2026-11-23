/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/i2c/I2cDevIo.h"
#include <errno.h>
#include <fcntl.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <memory>
#include "fboss/lib/i2c/I2cDevError.h"
#include "fboss/lib/i2c/I2cRdWrIo.h"
#include "fboss/lib/i2c/I2cSmbusIo.h"

namespace facebook::fboss {

I2cIoType I2cDevIo::getIoType() {
  long funcs;
  if (ioctl(fd_, I2C_FUNCS, &funcs) < 0) {
    throw I2cDevIoError("ioctl[I2C_FUNCS] failed");
  }
  if (funcs & I2C_RDWR) {
    return I2cIoType::I2cIoTypeRdWr;
  } else if (funcs & I2C_FUNC_SMBUS_BLOCK_DATA) {
    return I2cIoType::I2cIoTypeSmbus;
  }
  return I2cIoType::I2cIoTypeUnknown;
}

I2cDevIo::I2cDevIo(
    const std::string& devName,
    std::optional<I2cIoType> ioType) {
  devName_ = devName;
  openFile();
  createI2cDevImpl(ioType);
}

I2cDevIo::~I2cDevIo() {
  close(fd_);
}

void I2cDevIo::write(
    uint8_t addr,
    uint8_t offset,
    const uint8_t* buf,
    int len) {
  if (!i2cDevImpl_) {
    throw I2cDevIoError(fmt::format("I2cDevIo uninitialized on {}", devName_));
  }
  i2cDevImpl_->write(addr, offset, buf, len);
}

void I2cDevIo::read(uint8_t addr, uint8_t offset, uint8_t* buf, int len) {
  if (!i2cDevImpl_) {
    throw I2cDevIoError(fmt::format("I2cDevIo uninitialized on {}", devName_));
  }
  i2cDevImpl_->read(addr, offset, buf, len);
}

void I2cDevIo::openFile() {
  fd_ = open(devName_.c_str(), O_RDWR);
  if (fd_ < 0) {
    // Crash the program because the callers are not handling this exception
    throw I2cDevIoError(
        fmt::format(
            "I2cDevIo() failed to open {}, retVal = {:d}, errno = {}",
            devName_,
            fd_,
            folly::errnoStr(errno)));
  }
}

void I2cDevIo::createI2cDevImpl(std::optional<I2cIoType> ioType) {
  if (!ioType.has_value()) {
    ioType = getIoType();
  }
  if (ioType == I2cIoType::I2cIoTypeRdWr) {
    XLOG(INFO) << "Creating I2cRdWrIo for " << devName_;
    i2cDevImpl_ = std::make_unique<I2cRdWrIo>(devName_, fd_);
  } else if (ioType == I2cIoType::I2cIoTypeSmbus) {
    XLOG(INFO) << "Creating I2cSmbusIo for " << devName_;
    i2cDevImpl_ = std::make_unique<I2cSmbusIo>(devName_, fd_);
  } else if (ioType == I2cIoType::I2cIoTypeForTest) {
    XLOG(INFO) << "Doing nothing for test " << devName_;
  } else {
    throw I2cDevIoError(
        "Neither of I2C_RDWR, I2C_SMBUS and I2C_FUNC_SMBUS_BLOCK_DATA are supported");
  }
}

} // namespace facebook::fboss
