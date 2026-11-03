// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fboss/lib/bsp/BspPlatformMapping.h"
#include "fboss/lib/bsp/gen-cpp2/bsp_platform_mapping_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/qsfp_service_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_properties_types.h"
#include "fboss/qsfp_service/module/Transceiver.h"
#include "fboss/qsfp_service/module/cmis/CmisModule.h"
#include "fboss/qsfp_service/test/hal_test/BspTransceiverImpl.h"
#include "fboss/qsfp_service/test/hal_test/gen-cpp2/hal_test_config_types.h"

namespace facebook::fboss::hal_test {

// Create a BspTransceiverImpl for a transceiver. Uses the entry's BSP path
// overrides if all three are set (non-FBOSS mode); otherwise looks the
// transceiver up in the platform's BspPlatformMapping. Throws FbossError if the
// entry has partial overrides, if the non-override path is taken with a null
// bspMapping, or if the transceiver is absent from bspMapping.
std::unique_ptr<BspTransceiverImpl> createBspTransceiverImpl(
    const HalTestTransceiverEntry& entry,
    const BspPlatformMapping* bspMapping);

// Create a QsfpModule + its BspTransceiverImpl.
struct HalTestModule {
  std::unique_ptr<QsfpModule> module;
  std::unique_ptr<BspTransceiverImpl> impl;
};

HalTestModule createQsfpModule(
    const HalTestTransceiverEntry& entry,
    const BspPlatformMapping* bspMapping,
    const std::map<std::string, std::string>& fwHandleMap);

// Create QsfpModules for all transceivers in the config.
std::map<int, HalTestModule> createAllQsfpModules(const HalTestConfig& config);

// Load HalTestConfig from a JSON file.
HalTestConfig loadHalTestConfig(const std::string& configPath);

// Build a ProgramTransceiverState from a SpeedCombination's port list. For a ZR
// module the tunable optics config is attached to every port, since
// CmisModule::customizeTransceiverLocked throws without it.
// NOTE: this touches the module -- makeTunableOpticsConfig below refreshes it
// to read the band -- so it is not a pure builder.
ProgramTransceiverState createProgramTransceiverState(
    const SpeedCombination& combo,
    QsfpModule* module);

// True if the module is an 800G ZR (coherent, tunable) optic.
bool isZrModule(QsfpModule* module);

// Build the tunable optics config a ZR module needs before it can be
// programmed: qsfp_service refuses to bring a ZR optic to high power without
// one. Picks the first channel of whichever band the module's laser supports
// (C-Band or L-Band) -- any in-band channel works for a HAL test. Refreshes the
// module first, so it is safe to call at any point in a test. Returns nullopt
// if the module is not ZR or advertises neither tunable band.
std::optional<cfg::OpticalChannelConfig> makeTunableOpticsConfig(
    QsfpModule* module);

// AppSel code the module advertises for a port's media interface code, host
// lane count and start lane, or nullopt if it doesn't advertise one.
std::optional<uint8_t> lookupAppSelCode(
    QsfpModule* module,
    const PortSpeedConfig& port);

// Like lookupAppSelCode, but throws if the module doesn't advertise it.
uint8_t findAppSelCode(QsfpModule* module, const PortSpeedConfig& port);

// True if the module advertises an application for every port in combo.
bool moduleAdvertisesCombo(QsfpModule* module, const SpeedCombination& combo);

// Attach the module's tunable optics config to every port in state, with each
// port's AppSel looked up from the module's advertised applications for that
// port's media interface code in combo. No-op for non-ZR modules. Returns true
// if a config was attached.
bool applyTunableOpticsConfig(
    QsfpModule* module,
    ProgramTransceiverState& state,
    const SpeedCombination& combo);

// Program a transceiver, retrying until datapath init/deinit completes.
//
// HAL tests have no transceiver state machine to retry an incomplete datapath
// init/deinit, so this replicates what the state machine does in production:
// keep calling programTransceiver until it stops throwing "not yet completed".
// dataPathProgram polls only a short window per call and persists its start
// timer across calls, so each retry resumes polling rather than re-triggering
// the datapath. The wait is bounded by the test's own timeout; any other error
// is a real failure and is rethrown immediately.
void programTransceiverUntilComplete(
    QsfpModule* module,
    ProgramTransceiverState& programTcvrState,
    bool needResetDataPath);

// Build expected per-lane MediaInterfaceCodes from a SpeedCombination.
std::vector<MediaInterfaceCode> getExpectedMediaInterfaceCodes(
    const std::string& comboDescription,
    const SpeedCombination& combo);

// Collect all speed combination descriptions from TransceiverPropertiesManager.
std::vector<std::string> getAllSpeedCombinationDescriptions();

// Collect all speed-change transitions from TransceiverPropertiesManager.
std::vector<std::pair<std::string, std::string>> getAllSpeedChangeTransitions();

// Detect the module and bring it out of the post-reset low-power state, waiting
// until it reports ready so subsequent I2C reads / refresh succeed. A module
// fresh out of a hard reset comes up in low power and not yet ready, so paged
// reads fail until this runs. No-op (beyond detectPresence) for non-CMIS
// modules. Transient I2C errors are logged, not thrown. Returns true if the
// module is ready (or is non-CMIS), false if it did not reach ready state.
bool ensureModuleReady(QsfpModule* module);

// Upgrade firmware on a module to the specified versions.
// Returns true if an upgrade was performed.
bool upgradeFirmware(QsfpModule* module, const cfg::Firmware& desiredFw);

// Apply firmware upgrades specified in startup configs.
// Returns the number of modules that were upgraded.
int applyStartupFirmwareUpgrades(
    const HalTestConfig& config,
    std::map<int, HalTestModule>& modules);

} // namespace facebook::fboss::hal_test
