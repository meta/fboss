// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/i2c/I2cSmbusIo.h"
#include <errno.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <memory>

namespace facebook::fboss {

void I2cSmbusIo::write(
    uint8_t addr,
    uint8_t offset,
    const uint8_t* buf,
    int len) {
  auto ret_val = ioctl(fd(), I2C_SLAVE, addr);
  if (ret_val < 0) {
    throw I2cDevImplError("Setting I2C_SLAVE failed in write()");
  }
  for (int i = 0; i < len; i += I2C_SMBUS_BLOCK_MAX) {
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data smbus_data;
    // A maximum of I2C_SMBUS_BLOCK_MAX are allowed at a time. Therefore, pick
    // the minimum of bytes remaining to write and I2C_SMBUS_BLOCK_MAX
    auto bytesToWrite = std::min(I2C_SMBUS_BLOCK_MAX, len - i);
    // First byte in smbus_data.block is the length. Second byte onwards is the
    // data we want to write
    smbus_data.block[0] = bytesToWrite;
    for (int byteIdx = 0; byteIdx < bytesToWrite; byteIdx++) {
      smbus_data.block[1 + byteIdx] = buf[i + byteIdx];
    }
    args.read_write = I2C_SMBUS_WRITE;
    args.command = offset + i; // command = offset
    args.size = I2C_SMBUS_I2C_BLOCK_DATA;
    args.data = &smbus_data;
    ret_val = ioctl(fd(), I2C_SMBUS, &args);
    if (ret_val < 0) {
      throw I2cDevImplError(
          "Writing offset " + std::to_string(args.command) + " failed with " +
          std::to_string(errno));
    }
  }
}

void I2cSmbusIo::read(uint8_t addr, uint8_t offset, uint8_t* buf, int len) {
  auto ret_val = ioctl(fd(), I2C_SLAVE, addr);
  if (ret_val < 0) {
    throw I2cDevImplError("Setting I2C_SLAVE failed in read()");
  }
  for (int i = 0; i < len; i += I2C_SMBUS_BLOCK_MAX) {
    struct i2c_smbus_ioctl_data args;
    union i2c_smbus_data smbus_data;

    // A maximum of I2C_SMBUS_BLOCK_MAX are allowed at a time. Therefore, pick
    // the minimum of bytes remaining to write and I2C_SMBUS_BLOCK_MAX
    auto bytesToRead = std::min(I2C_SMBUS_BLOCK_MAX, len - i);
    // First byte in smbus_data.block is the length. Second byte onwards is
    // where the read data would be populated.
    smbus_data.block[0] = bytesToRead;
    args.read_write = I2C_SMBUS_READ;
    args.command = offset + i; // command = offset
    args.size = I2C_SMBUS_I2C_BLOCK_DATA;
    args.data = &smbus_data;
    ret_val = ioctl(fd(), I2C_SMBUS, &args);
    if (ret_val < 0) {
      throw I2cDevImplError(
          "Reading byte " + std::to_string(args.command) + " failed with " +
          std::to_string(errno));
    }
    for (int byteIdx = 0; byteIdx < bytesToRead; byteIdx++) {
      buf[i + byteIdx] = smbus_data.block[1 + byteIdx];
    }
  }
}

} // namespace facebook::fboss
