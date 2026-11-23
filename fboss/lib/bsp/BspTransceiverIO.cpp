// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/BspTransceiverIO.h"

#include <folly/Range.h>
#include <folly/logging/xlog.h>
#include <stdint.h>
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/bsp/gen-cpp2/bsp_platform_mapping_types.h"

namespace facebook::fboss {

BspTransceiverIO::BspTransceiverIO(
    uint32_t tcvr,
    BspTransceiverMapping& tcvrMapping)
    : I2cController("BspTransceiverIOController" + std::to_string(tcvr)) {
  auto i2cDevName = *tcvrMapping.io()->devicePath();
  i2cDev_ = std::make_unique<I2cDevIo>(i2cDevName);
  tcvrMapping_ = tcvrMapping;
  tcvrID_ = *tcvrMapping_.tcvrId();
  XLOG(DBG5) << fmt::format(
      "BspTransceiverIOTrace: BspTransceiverIO() successfully opened tcvr {:d}, {}",
      tcvr,
      i2cDevName);
}

void BspTransceiverIO::write(
    const TransceiverAccessParameter& param,
    const uint8_t* buf) {
  auto startTime = std::chrono::steady_clock::now();

  uint8_t addr = param.i2cAddress ? *param.i2cAddress
                                  : TransceiverAccessParameter::ADDR_QSFP;
  uint8_t offset = param.offset;
  uint8_t len = param.len;
  // Increment the counter for I2C write transaction issued
  incrWriteTotal();

  try {
    i2cDev_->write(addr, offset, buf, len);
    XLOG(DBG5) << fmt::format(
        "BspTransceiverIOTrace: write() successfully wrote to tcvr {:d}",
        tcvrID_);

    // Increment the number of I2c bytes written successfully
    incrWriteBytes(len);
  } catch (const I2cDevIoError& ex) {
    incrWriteFailed();
    throw BspTransceiverIOError(
        fmt::format(
            "BspTransceiverIO::write() failed to write to tcvr {:d}: {}",
            tcvrID_,
            ex.what()));
  }

  if (ioRdWrProfilingInProgress_) {
    auto endTime = std::chrono::steady_clock::now();
    auto writeTime = endTime - startTime;
    ioWriteProfilingTime_ +=
        std::chrono::duration_cast<std::chrono::milliseconds>(writeTime);
  }
}

void BspTransceiverIO::read(
    const TransceiverAccessParameter& param,
    uint8_t* buf) {
  auto startTime = std::chrono::steady_clock::now();

  uint8_t addr = param.i2cAddress ? *param.i2cAddress
                                  : TransceiverAccessParameter::ADDR_QSFP;
  uint8_t offset = param.offset;
  uint8_t len = param.len;
  // Increment the counter for I2C read transaction issued
  incrReadTotal();

  try {
    i2cDev_->read(addr, offset, buf, len);
    XLOG(DBG5) << fmt::format(
        "BspTransceiverIOTrace: read() successfully read from tcvr {:d}",
        tcvrID_);
    // Increment the number of I2c bytes read successfully
    incrReadBytes(len);
  } catch (const I2cDevIoError& ex) {
    incrReadFailed();
    throw BspTransceiverIOError(
        fmt::format(
            "BspTransceiverIO::read() failed to read from tcvr {:d}: {}",
            tcvrID_,
            ex.what()));
  }

  if (ioRdWrProfilingInProgress_) {
    auto endTime = std::chrono::steady_clock::now();
    auto readTime = endTime - startTime;
    ioReadProfilingTime_ +=
        std::chrono::duration_cast<std::chrono::milliseconds>(readTime);
  }
}

void BspTransceiverIO::i2cTimeProfilingStart() {
  ioRdWrProfilingInProgress_ = true;
  ioWriteProfilingTime_.zero();
  ioReadProfilingTime_.zero();
}

void BspTransceiverIO::i2cTimeProfilingEnd() {
  ioRdWrProfilingInProgress_ = false;
}

/*
 * Return the result of i2c timing profiling in the format of
 * <readTimeMsec, writeTimeMsec>
 */
std::pair<uint64_t, uint64_t> BspTransceiverIO::getI2cTimeProfileMsec() {
  return std::make_pair(
      ioReadProfilingTime_.count(), ioWriteProfilingTime_.count());
}

} // namespace facebook::fboss
