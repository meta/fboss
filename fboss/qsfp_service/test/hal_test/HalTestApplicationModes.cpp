// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <algorithm>
#include <utility>

#include <fmt/core.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/qsfp_service/module/cmis/CmisModule.h"
#include "fboss/qsfp_service/module/properties/TransceiverPropertiesManager.h"
#include "fboss/qsfp_service/test/hal_test/HalTest.h"
#include "fboss/qsfp_service/test/hal_test/HalTestUtils.h"

#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

namespace facebook::fboss {

namespace {

void verifyMediaInterfaceCodes(
    hal_test::TransceiverTestResult& result,
    QsfpModule* module,
    int tcvrId,
    const std::string& comboDescription,
    const SpeedCombination& combo) {
  module->refresh();
  auto expectedCodes =
      hal_test::getExpectedMediaInterfaceCodes(comboDescription, combo);

  auto info = module->getTransceiverInfo();
  HAL_CHECK_FATAL_VOID(
      result,
      info.tcvrState()->settings().has_value(),
      fmt::format("Transceiver {} has no settings", tcvrId));
  HAL_CHECK_FATAL_VOID(
      result,
      info.tcvrState()->settings()->mediaInterface().has_value(),
      fmt::format(
          "Transceiver {} has no mediaInterface after programming mode {}",
          tcvrId,
          comboDescription));

  const auto& mediaInterfaces = *info.tcvrState()->settings()->mediaInterface();

  HAL_CHECK_FATAL_VOID(
      result,
      mediaInterfaces.size() == expectedCodes.size(),
      fmt::format(
          "Transceiver {} unexpected number of media interface lanes for mode {}: got {} expected {}",
          tcvrId,
          comboDescription,
          mediaInterfaces.size(),
          expectedCodes.size()));

  for (size_t i = 0; i < expectedCodes.size(); i++) {
    HAL_CHECK(
        result,
        *mediaInterfaces[i].code() == expectedCodes[i],
        fmt::format(
            "Transceiver {} lane {} media interface code mismatch for mode {}: expected {} got {}",
            tcvrId,
            i,
            comboDescription,
            apache::thrift::util::enumNameSafe(expectedCodes[i]),
            apache::thrift::util::enumNameSafe(*mediaInterfaces[i].code())));
  }

  // Several raw media codes can map to the same MediaInterfaceCode (e.g. the
  // 800G ZR applications), so also check the raw code of the active
  // application on each media lane.
  for (const auto& port : *combo.ports()) {
    if (port.mediaLaneCode()->getType() != MediaInterfaceUnion::Type::smfCode) {
      continue;
    }
    const auto expectedRaw = *port.mediaLaneCode()->smfCode();
    const auto start = *port.mediaLanes()->start();
    for (int lane = start; lane < start + *port.mediaLanes()->count(); lane++) {
      if (lane >= static_cast<int>(mediaInterfaces.size())) {
        break;
      }
      const auto& media = *mediaInterfaces[lane].media();
      HAL_CHECK(
          result,
          media.getType() == MediaInterfaceUnion::Type::smfCode &&
              *media.smfCode() == expectedRaw,
          fmt::format(
              "Transceiver {} media lane {} raw media code mismatch for mode {}: expected {} got {}",
              tcvrId,
              lane,
              comboDescription,
              apache::thrift::util::enumNameSafe(expectedRaw),
              media.getType() == MediaInterfaceUnion::Type::smfCode
                  ? apache::thrift::util::enumNameSafe(*media.smfCode())
                  : std::string("non-SMF")));
    }
  }
}

void bringModuleToReady(
    hal_test::TransceiverTestResult& result,
    CmisModule* cmisModule,
    int tcvrId) {
  cmisModule->setModuleLowPowerModeLocked();
  cmisModule->releaseModuleLowPowerModeLocked();
  HAL_CHECK_FATAL_VOID(
      result,
      cmisModule->moduleReadyStatePoll(),
      fmt::format(
          "Transceiver {} did not reach ready state after releasing low power",
          tcvrId));
}

// Sanitize a SpeedCombination description for use as a gtest name.
// Keeps only alphanumeric and '_'. All other characters become '_'.
// e.g. "800G-DR4 + 400G-DR4" -> "800G_DR4___400G_DR4"
std::string sanitizeTestName(const std::string& desc) {
  std::string result;
  result.reserve(desc.size());
  for (char c : desc) {
    result += (std::isalnum(c) || c == '_') ? c : '_';
  }
  return result;
}

} // namespace

// Test that programs a transceiver with a specific speed combination,
// performing a hard reset first.
class T1HalTestProgramMode : public T1HalTest {
 public:
  explicit T1HalTestProgramMode(std::string comboDescription)
      : comboDescription_(std::move(comboDescription)) {}

  void TestBody() override {
    forEachTransceiverParallel(
        [this](hal_test::TransceiverTestResult& result, int tcvrId) {
          auto* module = getModule(tcvrId);
          auto* cmisModule = dynamic_cast<CmisModule*>(module);
          HAL_CHECK_FATAL_VOID(
              result,
              cmisModule != nullptr,
              fmt::format("Transceiver {} is not CMIS", tcvrId));
          getImpl(tcvrId)->triggerQsfpHardReset();
          module->detectPresence();
          bringModuleToReady(result, cmisModule, tcvrId);
          if (!result.passed) {
            return;
          }
          module->refresh();
          auto mediaInterface = module->getModuleMediaInterface();
          HAL_CHECK_FATAL_VOID(
              result,
              TransceiverPropertiesManager::isKnown(mediaInterface),
              fmt::format(
                  "Transceiver {} has unknown media interface code", tcvrId));
          const auto& combo =
              TransceiverPropertiesManager::findSpeedCombination(
                  mediaInterface, comboDescription_);
          auto programState =
              hal_test::createProgramTransceiverState(combo, module);
          hal_test::programTransceiverUntilComplete(module, programState, true);
          verifyMediaInterfaceCodes(
              result, module, tcvrId, comboDescription_, combo);
        },
        [this](int tcvrId) {
          auto* module = getModule(tcvrId);
          module->refresh();
          auto mediaInterface = module->getModuleMediaInterface();
          if (!TransceiverPropertiesManager::isKnown(mediaInterface)) {
            XLOG(INFO) << "Transceiver " << tcvrId
                       << " has unknown media interface, skipping";
            return false;
          }
          const auto& props =
              TransceiverPropertiesManager::getProperties(mediaInterface);
          const SpeedCombination* found = nullptr;
          for (const auto& combo : *props.supportedSpeedCombinations()) {
            if (*combo.combinationName() == comboDescription_) {
              found = &combo;
              break;
            }
          }
          if (found == nullptr) {
            XLOG(INFO) << "Transceiver " << tcvrId << " does not support mode "
                       << comboDescription_ << ", skipping";
            return false;
          }
          // ZR modes are programmed with the AppSel the module advertises for
          // the mode's media interface code; vendor-specific codes may be
          // absent on some modules
          if (hal_test::isZrModule(module) &&
              !hal_test::moduleAdvertisesCombo(module, *found)) {
            XLOG(INFO) << "Transceiver " << tcvrId
                       << " does not advertise an application for mode "
                       << comboDescription_ << ", skipping";
            return false;
          }
          return true;
        });
  }

 private:
  std::string comboDescription_;
};

// Test that performs a speed change transition (from -> to -> from) without
// hard reset.
class T2HalTestSpeedChange : public T2HalTest {
 public:
  T2HalTestSpeedChange(std::string fromDesc, std::string toDesc)
      : fromDesc_(std::move(fromDesc)), toDesc_(std::move(toDesc)) {}

  void TestBody() override {
    forEachTransceiverParallel(
        [this](hal_test::TransceiverTestResult& result, int tcvrId) {
          auto* module = getModule(tcvrId);
          auto mediaInterface = module->getModuleMediaInterface();
          HAL_CHECK_FATAL_VOID(
              result,
              TransceiverPropertiesManager::isKnown(mediaInterface),
              fmt::format(
                  "Transceiver {} has unknown media interface code", tcvrId));
          const auto& fromCombo =
              TransceiverPropertiesManager::findSpeedCombination(
                  mediaInterface, fromDesc_);
          const auto& toCombo =
              TransceiverPropertiesManager::findSpeedCombination(
                  mediaInterface, toDesc_);
          auto fromState =
              hal_test::createProgramTransceiverState(fromCombo, module);
          hal_test::programTransceiverUntilComplete(module, fromState, true);
          verifyMediaInterfaceCodes(
              result, module, tcvrId, fromDesc_, fromCombo);
          if (!result.passed) {
            return;
          }
          auto toState =
              hal_test::createProgramTransceiverState(toCombo, module);
          hal_test::programTransceiverUntilComplete(module, toState, true);
          verifyMediaInterfaceCodes(result, module, tcvrId, toDesc_, toCombo);
          if (!result.passed) {
            return;
          }
          hal_test::programTransceiverUntilComplete(module, fromState, true);
          verifyMediaInterfaceCodes(
              result, module, tcvrId, fromDesc_, fromCombo);
        },
        [this](int tcvrId) {
          auto* module = getModule(tcvrId);
          module->refresh();
          auto mediaInterface = module->getModuleMediaInterface();
          if (!TransceiverPropertiesManager::isKnown(mediaInterface)) {
            return false;
          }
          const auto& props =
              TransceiverPropertiesManager::getProperties(mediaInterface);
          bool supported = false;
          for (const auto& transition : *props.speedChangeTransitions()) {
            if (transition.size() == 2 && transition[0] == fromDesc_ &&
                transition[1] == toDesc_) {
              supported = true;
              break;
            }
          }
          if (!supported) {
            XLOG(INFO) << "Transceiver " << tcvrId
                       << " does not support transition " << fromDesc_ << " -> "
                       << toDesc_ << ", skipping";
            return false;
          }
          return true;
        });
  }

 private:
  std::string fromDesc_;
  std::string toDesc_;
};

void registerApplicationModeTests() {
  for (const auto& desc : hal_test::getAllSpeedCombinationDescriptions()) {
    testing::RegisterTest(
        "T1ProgramMode/T1HalTestProgramMode",
        ("programMode/" + sanitizeTestName(desc)).c_str(),
        nullptr,
        nullptr,
        __FILE__,
        __LINE__,
        [desc]() -> T1HalTestProgramMode* {
          return new T1HalTestProgramMode(desc);
        });
  }

  for (auto& [from, to] : hal_test::getAllSpeedChangeTransitions()) {
    auto testName = sanitizeTestName(from) + "_to_" + sanitizeTestName(to);
    testing::RegisterTest(
        "T2SpeedChange/T2HalTestSpeedChange",
        ("speedChange/" + testName).c_str(),
        nullptr,
        nullptr,
        __FILE__,
        __LINE__,
        [fromDesc = std::move(from),
         toDesc = std::move(to)]() mutable -> T2HalTestSpeedChange* {
          return new T2HalTestSpeedChange(
              std::move(fromDesc), std::move(toDesc));
        });
  }
}

} // namespace facebook::fboss
