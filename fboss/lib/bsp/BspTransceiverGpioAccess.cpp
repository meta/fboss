// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/BspTransceiverGpioAccess.h"
#include <fmt/format.h>
#include <stdint.h>
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/GpiodLine.h"
#include "fboss/lib/bsp/gen-cpp2/bsp_platform_mapping_types.h"

namespace {
constexpr auto kBspGpioActiveLow = 0;
constexpr auto kBspGpioInactiveLow = 1;
} // namespace

namespace facebook {
namespace fboss {

BspTransceiverGpioAccess::BspTransceiverGpioAccess(
    uint32_t tcvr,
    BspTransceiverMapping& tcvrMapping)
    : BspTransceiverAccessImpl(tcvr, tcvrMapping) {
  auto gpioChipName = tcvrMapping_.accessControl()->gpioChip();
  CHECK(gpioChipName.has_value());
  const std::string gpioChipSymLink = *gpioChipName;
  // Open GPIO chip
  chip_ = gpiod_chip_open(gpioChipSymLink.c_str());

  if (nullptr == chip_) {
    throw BspTransceiverGpioAccessError(
        fmt::format(
            "QSFP {}, Unable to open gpio chip {}", tcvr, gpioChipSymLink));
  }
}

BspTransceiverGpioAccess::~BspTransceiverGpioAccess() {
  if (chip_ != nullptr) {
    gpiod_chip_close(chip_); // Release GPIO chip
  }
}

bool BspTransceiverGpioAccess::isPresent() {
  bool retVal = false;
  auto gpioOffset = tcvrMapping_.accessControl()->presence()->gpioOffset();
  CHECK(gpioOffset);

  try {
    GpiodLine present(
        chip_, *gpioOffset, fmt::format("QSFP {} Present GPIO", tcvrID_));

    // Read the Present status.
    retVal = (present.getValue() == kBspGpioActiveLow);
  } catch (std::exception& ex) {
    throw BspTransceiverGpioAccessError(
        fmt::format("QSFP {}, IsPresent fails with {}", tcvrID_, ex.what()));
  }

  return retVal;
}

void BspTransceiverGpioAccess::init(bool forceReset) {
  if (forceReset) {
    holdReset();
  }
  releaseReset();
}

void BspTransceiverGpioAccess::holdReset() {
  auto gpioOffset = tcvrMapping_.accessControl()->reset()->gpioOffset();
  CHECK(gpioOffset);
  try {
    GpiodLine reset(
        chip_, *gpioOffset, fmt::format("QSFP {}  Reset GPIO", tcvrID_));

    reset.setValue(kBspGpioInactiveLow, kBspGpioActiveLow);
    /* sleep override */
    usleep(100);
  } catch (std::exception& ex) {
    throw BspTransceiverGpioAccessError(
        fmt::format("QSFP {}: hold reset fails with {}", tcvrID_, ex.what()));
  }
}

void BspTransceiverGpioAccess::releaseReset() {
  auto gpioOffset = tcvrMapping_.accessControl()->reset()->gpioOffset();
  CHECK(gpioOffset);
  try {
    GpiodLine reset(
        chip_, *gpioOffset, fmt::format("QSFP {}  Reset GPIO", tcvrID_));
    reset.setValue(kBspGpioInactiveLow, kBspGpioInactiveLow);
  } catch (std::exception& ex) {
    throw BspTransceiverGpioAccessError(
        fmt::format(
            "QSFP {}: release reset fails with {}", tcvrID_, ex.what()));
  }
}

} // namespace fboss
} // namespace facebook
