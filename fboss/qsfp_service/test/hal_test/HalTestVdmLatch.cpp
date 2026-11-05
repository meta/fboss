// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <fmt/core.h>
#include <folly/io/IOBuf.h>
#include <folly/logging/xlog.h>

#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#include "fboss/qsfp_service/module/cmis/CmisModule.h"
#include "fboss/qsfp_service/module/properties/TransceiverPropertiesManager.h"
#include "fboss/qsfp_service/test/hal_test/HalTest.h"
#include "fboss/qsfp_service/test/hal_test/HalTestUtils.h"

namespace facebook::fboss {

namespace {
// CMIS 5.2 Page 2Fh VDM freeze handshake registers: FreezeRequest is byte 144
// bit 7, FreezeDone is byte 145 bit 7.
constexpr int kVdmPage2f = 0x2f;
constexpr int kVdmFreezeRequestOffset = 144;
constexpr int kVdmFreezeDoneOffset = 145;
constexpr uint8_t kVdmFreezeBit = 0x80;
constexpr int kUsecVdmFreezePollInterval = 50000; // 50 ms
constexpr int kUsecVdmFreezeBudget = 2000000; // 2 s

// Result of one freeze/measure cycle. ioError (with a message in error) is set
// when an underlying I2C read/write failed, so a real I/O failure is surfaced
// distinctly rather than being silently reported as a freeze-latch timeout.
struct VdmFreezeResult {
  bool ioError{false};
  bool freezeDone{false};
  int64_t freezeUsec{0};
  std::string error;
};

// Returns std::nullopt when the read fails (null/short IOBuf), so a failed read
// is not confused with a genuine 0 register value.
std::optional<uint8_t> readVdmByte(QsfpModule* module, int offset) {
  TransceiverIOParameters param;
  param.page() = kVdmPage2f;
  param.offset() = offset;
  param.length() = 1;
  auto buf = module->readTransceiver(std::move(param));
  if (!buf || buf->length() == 0) {
    return std::nullopt;
  }
  return *buf->data();
}

// Returns whether the underlying register write succeeded.
bool writeVdmByte(QsfpModule* module, int offset, uint8_t value) {
  TransceiverIOParameters param;
  param.page() = kVdmPage2f;
  param.offset() = offset;
  param.length() = 1;
  return module->writeTransceiver(std::move(param), &value);
}

// Set/clear the FreezeRequest bit (2Fh:144 bit7) via read-modify-write. Returns
// false (with error populated) if the read or write failed, so a failed read
// can't clobber the other bits of the register with a substituted 0.
bool setVdmFreezeRequest(QsfpModule* module, bool freeze, std::string& error) {
  auto cur = readVdmByte(module, kVdmFreezeRequestOffset);
  if (!cur) {
    error = "failed to read FreezeRequest register";
    return false;
  }
  const uint8_t next =
      freeze ? (*cur | kVdmFreezeBit) : (*cur & ~kVdmFreezeBit);
  if (!writeVdmByte(module, kVdmFreezeRequestOffset, next)) {
    error = freeze ? "failed to write FreezeRequest"
                   : "failed to write FreezeRequest release";
    return false;
  }
  return true;
}

// Freeze the VDM reporting registers and poll FreezeDone until it is set or the
// 2s budget elapses, then unfreeze. This blocking freeze+poll lives in the test
// because only the test needs to synchronously verify/measure the handshake;
// production drives it asynchronously across refresh cycles.
VdmFreezeResult timeVdmFreeze(QsfpModule* module) {
  VdmFreezeResult res;
  if (!setVdmFreezeRequest(module, /*freeze=*/true, res.error)) {
    res.ioError = true;
    return res;
  }

  const auto start = std::chrono::steady_clock::now();
  const auto deadline = start + std::chrono::microseconds(kUsecVdmFreezeBudget);
  while (true) {
    auto done = readVdmByte(module, kVdmFreezeDoneOffset);
    if (!done) {
      res.ioError = true;
      res.error = "failed to read FreezeDone register";
      break;
    }
    if ((*done & kVdmFreezeBit) != 0) {
      res.freezeDone = true;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      break;
    }
    /* sleep override */
    usleep(kUsecVdmFreezePollInterval);
  }
  res.freezeUsec = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();

  // Always attempt to release the freeze so we never leave the module frozen,
  // even if the poll above hit an I/O error.
  std::string releaseError;
  if (!setVdmFreezeRequest(module, /*freeze=*/false, releaseError)) {
    res.ioError = true;
    if (res.error.empty()) {
      res.error = releaseError;
    }
  }
  return res;
}

// Freeze only applies to statistics (min/max/average) reporting registers, so
// a module without any statistics observable need not raise FreezeDone
bool moduleHasVdmStatistics(QsfpModule* module) {
  auto* cmis = dynamic_cast<CmisModule*>(module);
  return cmis != nullptr && cmis->hasVdmStatisticsObservables();
}
} // namespace

// Verify the VDM freeze latch on every VDM-capable module, across all
// application modes the module supports. VDM freeze time scales with the number
// of programmed datapaths, so we program each supported speed combination and,
// in that mode, freeze the reporting registers and, if the module has VDM
// statistics observables, check it raises FreezeDone (2Fh:145 bit7) within a 2s
// budget. Modules that do not advertise VDM (or whose media interface is
// unknown) are skipped.
TEST_F(T2HalTest, vdmLatchFreeze) {
  forEachTransceiverParallel(
      [this](hal_test::TransceiverTestResult& result, int tcvrId) {
        auto* module = getModule(tcvrId);

        // Bring the module out of low power; hard-fail if it is not ready.
        HAL_CHECK_FATAL_VOID(
            result,
            hal_test::ensureModuleReady(module),
            fmt::format(
                "Transceiver {} not ready after removing low power", tcvrId));

        // Populate the module cache + diags capability so isVdmSupported() and
        // the media-interface mode enumeration below work.
        module->refresh();
        module->setDiagsCapability();

        // Only modules that advertise VDM have the freeze/unfreeze registers.
        if (!module->isVdmSupported()) {
          result.skipped = true;
          return;
        }

        auto mediaInterface = module->getModuleMediaInterface();
        if (!TransceiverPropertiesManager::isKnown(mediaInterface)) {
          XLOG(INFO) << "Transceiver " << tcvrId
                     << " has unknown media interface; skipping VDM latch test";
          result.skipped = true;
          return;
        }

        const bool freezeDoneRequired = moduleHasVdmStatistics(module);
        if (!freezeDoneRequired) {
          XLOG(INFO) << "Transceiver " << tcvrId
                     << " has no VDM statistics observables; not requiring "
                     << "FreezeDone";
        }

        // Cycle through every application mode the module supports; the VDM
        // freeze time scales with the number of programmed datapaths.
        const auto& props =
            TransceiverPropertiesManager::getProperties(mediaInterface);
        for (const auto& combo : *props.supportedSpeedCombinations()) {
          const auto& comboName = *combo.combinationName();

          // Hard reset -> ensure ready -> program, so each mode is exercised
          // from a clean, freshly-programmed state.
          getImpl(tcvrId)->triggerQsfpHardReset();
          HAL_CHECK_FATAL_VOID(
              result,
              hal_test::ensureModuleReady(module),
              fmt::format(
                  "Transceiver {} not ready after reset for mode {}",
                  tcvrId,
                  comboName));
          module->refresh();

          auto programState =
              hal_test::createProgramTransceiverState(combo, module);
          hal_test::programTransceiverUntilComplete(
              module, programState, true /* needResetDataPath */);

          auto freeze = timeVdmFreeze(module);
          XLOG(INFO) << "Transceiver " << tcvrId << " mode " << comboName
                     << " VDM freeze: freeze=" << freeze.freezeUsec
                     << "us, done=" << freeze.freezeDone
                     << " (budget=" << kUsecVdmFreezeBudget << "us)";

          // Surface an I2C read/write failure distinctly so it is not
          // misattributed to the module missing the freeze deadline.
          HAL_CHECK(
              result,
              !freeze.ioError,
              fmt::format(
                  "Transceiver {} mode {} VDM freeze I2C error: {}",
                  tcvrId,
                  comboName,
                  freeze.error));
          if (!freeze.ioError && freezeDoneRequired) {
            HAL_CHECK(
                result,
                freeze.freezeDone,
                fmt::format(
                    "Transceiver {} mode {} VDM FreezeDone not set within the "
                    "{}us budget (freeze={}us)",
                    tcvrId,
                    comboName,
                    kUsecVdmFreezeBudget,
                    freeze.freezeUsec));
          }
        }
      });
}

} // namespace facebook::fboss
