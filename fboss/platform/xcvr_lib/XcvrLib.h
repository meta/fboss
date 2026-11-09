// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fboss/platform/platform_manager/SystemInterface.h"
#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"

namespace facebook::fboss {

/**
 * XcvrLib — Primitive-only transceiver information library.
 *
 * Derives all transceiver mapping information from PlatformConfig
 * (platform_manager.json).
 * Usage:
 *   XcvrLib xcvr("montblanc");  // loads config via ConfigLib
 *   auto resetPath = xcvr.getXcvrResetSysfsPath(1);
 *   auto ledPath = xcvr.getLedSysfsPath(1, 1, LedColor::BLUE);
 */
class XcvrLib {
 public:
  // Construct from platform name (loads config via ConfigLib)
  explicit XcvrLib(
      const std::string& platformName,
      std::shared_ptr<
          platform::platform_manager::package_manager::SystemInterface>
          systemInterface = std::make_shared<
              platform::platform_manager::package_manager::SystemInterface>());

  // Construct from a pre-parsed PlatformConfig
  explicit XcvrLib(
      const platform::platform_manager::PlatformConfig& pmConfig,
      std::shared_ptr<
          platform::platform_manager::package_manager::SystemInterface>
          systemInterface = std::make_shared<
              platform::platform_manager::package_manager::SystemInterface>());

  // --- Platform-level queries ---

  int getNumTransceivers() const;

  // --- Transceiver info ---

  // Returns the number of LEDs for a specific transceiver, as determined
  // from the ledCtrlBlockConfigs in PlatformConfig
  std::optional<int> getNumLedsForTransceiver(int xcvrId) const;

  // --- Reset and presence pin info ---

  int getResetMask() const;
  int getPresenceMask() const;
  int getResetHoldHi() const;

  // --- Transceiver path queries ---

  // Returns e.g. "/run/devmap/xcvrs/xcvr_io_1"
  std::optional<std::string> getXcvrIODevicePath(int xcvrId) const;

  // Returns e.g. "/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_reset_1"
  std::optional<std::string> getXcvrResetSysfsPath(int xcvrId) const;

  // Returns e.g. "/run/devmap/xcvrs/xcvr_ctrl_1/xcvr_present_1"
  std::optional<std::string> getXcvrPresenceSysfsPath(int xcvrId) const;

  // --- LED path queries ---

  enum class LedColor { BLUE, AMBER, GREEN };

  // Returns the primary LED color for this platform.
  LedColor getPrimaryLedColor() const;

  // Returns e.g. "/sys/class/leds/port1_led1:blue:status"
  std::optional<std::string>
  getLedSysfsPath(int xcvrId, int ledNumForXcvr, LedColor color) const;

  // --- Lane mapping ---

  // Returns the number of lanes for a specific transceiver
  std::optional<int> getNumLanesForTransceiver(int xcvrId) const;

 private:
  bool isValidXcvrId(int xcvrId) const;

  void validateXcvrInfos();
  void buildPerTransceiverLedCounts();

  // Uncached computation backing getResetHoldHi(); reads sysfs for Arista.
  int computeResetHoldHi() const;

  // Version of the BSP driver bound to the xcvr_ctrl devices, or std::nullopt
  // if none is bound or reports a parseable version.
  std::optional<platform::platform_manager::package_manager::BspVersion>
  getLoadedBspVersion() const;

  struct XcvrInfo {
    std::optional<int> numLeds;
    std::optional<int> numLanes;
  };

  platform::platform_manager::PlatformConfig pmConfig_;
  std::shared_ptr<platform::platform_manager::package_manager::SystemInterface>
      systemInterface_;
  // Platform-wide value; computed lazily once (getLoadedBspVersion() reads
  // sysfs) and reused across the per-transceiver mapping-build loop.
  mutable std::optional<int> resetHoldHiCache_;
  int numXcvrs_{0};
  // Indexed by xcvrId (1-based; index 0 unused)
  std::vector<XcvrInfo> xcvrInfos_;
};

} // namespace facebook::fboss
