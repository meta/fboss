// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/i2c/I2cRdWrIo.h"
#include <errno.h>
#include <folly/String.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <memory>
#include "fboss/lib/i2c/I2cDevError.h"

namespace facebook::fboss {

void I2cRdWrIo::write(
    uint8_t addr,
    uint8_t offset,
    const uint8_t* buf,
    int len) {
  // Create (len + 1) byte buffer, put device register offset in first byte,
  // then put the rest of the data in buffer
  uint8_t outbuf[len + 1];
  outbuf[0] = offset;
  memcpy(&outbuf[1], buf, sizeof(uint8_t) * len);
  struct i2c_rdwr_ioctl_data packets;
  struct i2c_msg messages[1];

  messages[0].addr = addr;
  messages[0].flags = 0;
  messages[0].len = sizeof(outbuf);
  messages[0].buf = outbuf;

  packets.msgs = messages;
  packets.nmsgs = 1;

  if (ioctl(fd(), I2C_RDWR, &packets) < 0) {
    throw I2cDevImplError(
        fmt::format(
            "write() failed to write to {}, errno = {}",
            devName(),
            folly::errnoStr(errno)));
  }
}

void I2cRdWrIo::read(uint8_t addr, uint8_t offset, uint8_t* buf, int len) {
  struct i2c_rdwr_ioctl_data packets;
  struct i2c_msg messages[2];

  messages[0].addr = addr;
  messages[0].flags = 0;
  messages[0].len = sizeof(offset);
  messages[0].buf = &offset;

  messages[1].addr = addr;
  messages[1].flags = I2C_M_RD;
  messages[1].len = len;
  messages[1].buf = buf;

  packets.msgs = messages;
  packets.nmsgs = 2;

  if (ioctl(fd(), I2C_RDWR, &packets) < 0) {
    throw I2cDevImplError(
        fmt::format(
            "read() failed to read from port {}, errno = {}",
            devName(),
            folly::errnoStr(errno)));
  }
}

} // namespace facebook::fboss
