// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/BspTransceiverCpldAccess.h"

#include <folly/Range.h>
#include <folly/logging/xlog.h>
#include <stdint.h>
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/bsp/gen-cpp2/bsp_platform_mapping_types.h"

namespace facebook {
namespace fboss {

BspTransceiverCpldAccess::BspTransceiverCpldAccess(
    uint32_t tcvr,
    BspTransceiverMapping& tcvrMapping)
    : BspTransceiverAccessImpl(tcvr, tcvrMapping) {}

void BspTransceiverCpldAccess::init(bool forceReset) {
  if (forceReset) {
    holdReset();
  }
  releaseReset();
}

void BspTransceiverCpldAccess::holdReset() {
  CHECK(tcvrMapping_.accessControl()->reset()->sysfsPath());
  CHECK(tcvrMapping_.accessControl()->reset()->mask());
  CHECK(tcvrMapping_.accessControl()->reset()->resetHoldHi());
  auto resetPath = *tcvrMapping_.accessControl()->reset()->sysfsPath();
  uint8_t resetMask =
      static_cast<uint8_t>(*tcvrMapping_.accessControl()->reset()->mask());
  uint8_t resetHoldHi = static_cast<uint8_t>(
      *tcvrMapping_.accessControl()->reset()->resetHoldHi());

  try {
    auto status = std::stoi(readSysfs(resetPath), nullptr, 16);
    if (resetHoldHi) {
      status = status | resetMask;
    } else {
      status = status & ~resetMask;
    }
    writeSysfs(resetPath, std::to_string(status));
    /* sleep override */
    usleep(100);
  } catch (std::exception&) {
    XLOG(ERR) << fmt::format(
        "BspTransceiverCpldAccessTrace: init() failed to hold reset TCVR {:d} (1 base)",
        tcvrID_);
  }
}

void BspTransceiverCpldAccess::releaseReset() {
  CHECK(tcvrMapping_.accessControl()->reset()->sysfsPath());
  CHECK(tcvrMapping_.accessControl()->reset()->mask());
  CHECK(tcvrMapping_.accessControl()->reset()->resetHoldHi());
  auto resetPath = *tcvrMapping_.accessControl()->reset()->sysfsPath();
  uint8_t resetMask =
      static_cast<uint8_t>(*tcvrMapping_.accessControl()->reset()->mask());
  uint8_t resetHoldHi = static_cast<uint8_t>(
      *tcvrMapping_.accessControl()->reset()->resetHoldHi());

  try {
    auto status = std::stoi(readSysfs(resetPath), nullptr, 16);
    if (resetHoldHi) {
      status = status & ~resetMask;
    } else {
      status = status | resetMask;
    }
    writeSysfs(resetPath, std::to_string(status));
  } catch (std::exception&) {
    XLOG(ERR) << fmt::format(
        "BspTransceiverCpldAccessTrace: init() failed to release reset TCVR {:d} (1 base)",
        tcvrID_);
  }
}

bool BspTransceiverCpldAccess::isPresent() {
  bool retVal = false;
  CHECK(tcvrMapping_.accessControl()->presence()->sysfsPath());
  CHECK(tcvrMapping_.accessControl()->presence()->mask());
  CHECK(tcvrMapping_.accessControl()->presence()->presentHoldHi());
  auto presencePath = *tcvrMapping_.accessControl()->presence()->sysfsPath();
  auto presenceMask = *tcvrMapping_.accessControl()->presence()->mask();
  auto presentHoldHi =
      *tcvrMapping_.accessControl()->presence()->presentHoldHi();

  try {
    auto status = std::stoi(readSysfs(presencePath), nullptr, 16);
    auto presenceBits = status & presenceMask;
    if (presentHoldHi) {
      retVal = presenceBits;
    } else {
      retVal = !presenceBits;
    }
  } catch (std::exception&) {
    XLOG(ERR) << fmt::format(
        "BspTransceiverCpldAccessTrace: isPresent() failed to get Present status for TCVR {:d} (1 base)",
        tcvrID_);
  }
  return retVal;
}

} // namespace fboss
} // namespace facebook
