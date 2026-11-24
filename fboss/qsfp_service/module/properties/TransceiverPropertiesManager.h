// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_properties_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

namespace facebook::fboss {

class TransceiverPropertiesManager {
 public:
  // Initialize with built-in default config
  static void initDefault();

  // Initialize from JSON config file path (overrides default)
  static void init(const std::string& configPath);

  // Initialize from a JSON string
  static void initFromJson(const std::string& configJson);

  // Core property lookups (return defaults for unknown codes)
  static int getNumHostLanes(MediaInterfaceCode code);
  static int getNumMediaLanes(MediaInterfaceCode code);
  static bool getDoesNotSupportVdm(MediaInterfaceCode code);
  static bool getDoesNotSupportRxOutputControl(MediaInterfaceCode code);

  // Derive MediaInterfaceCode from EEPROM SMF data.
  // Matches against firstApplicationAdvertisement (mediaInterfaceCode,
  // hostStartLanes count, hostInterfaceCode, maxNumBanks) and smfLength.
  // maxNumBanks defaults to 1 so single-bank callers/entries match unchanged;
  // it disambiguates two codes whose advertisement is otherwise identical but
  // differ in bank count (e.g. DR4_8x800G at 4 banks vs DR4_2x800G at 1 bank).
  static MediaInterfaceCode deriveSmfCode(
      uint8_t smfByte,
      const std::vector<int>& hostStartLanes,
      uint8_t hostInterfaceCode,
      int smfLength,
      int maxNumBanks = 1);

  // Get the full SmfTransceiverProperties for a code.
  // Throws FbossError if not initialized or code is unknown.
  static const SmfTransceiverProperties& getProperties(MediaInterfaceCode code);
  static const SmfTransceiverProperties& getProperties(int32_t codeValue);

  // Get speed combinations for a MediaInterfaceCode as arrays of raw
  // EEPROM byte values (SMFMediaInterfaceCode or ActiveCuHostInterfaceCode).
  // Returns the combinations cast to the requested InterfaceCode type.
  // Empty if code is unknown or has no speed combinations.
  static constexpr size_t kMaxOsfpNumLanes = 8;
  template <typename InterfaceCode>
  static std::vector<std::array<InterfaceCode, kMaxOsfpNumLanes>>
  getSpeedCombinations(MediaInterfaceCode code);

  // Get all distinct media interface codes for a given module type and speed.
  // Iterates supportedSpeedCombinations and collects unique mediaLaneCodes
  // from ports matching the requested speed. Returns empty if code is unknown
  // or no combos match.
  template <typename InterfaceCode>
  static std::vector<InterfaceCode> getMediaCodesForSpeed(
      MediaInterfaceCode code,
      cfg::PortSpeed speed);

  // Derive expected media lanes for a port given its host lanes and speed.
  // Searches all speed combinations for a matching PortSpeedConfig by
  // startHostLane, numHostLanes, and speed, then returns
  // {startMediaLane..startMediaLane+numMediaLanes-1}. Returns empty if no match
  // found.
  static std::vector<int> getExpectedMediaLanes(
      MediaInterfaceCode code,
      const std::vector<int>& hostLanes,
      cfg::PortSpeed speed);

  // Find a SpeedCombination by description for a given MediaInterfaceCode.
  // Throws FbossError if code is unknown or description not found.
  static const SpeedCombination& findSpeedCombination(
      MediaInterfaceCode code,
      const std::string& description);

  // Get all MediaInterfaceCodes known in the config
  static std::vector<MediaInterfaceCode> getKnownCodes();

  // Check if a code is known in the config
  static bool isKnown(MediaInterfaceCode code);
  static bool isKnown(int32_t codeValue);

  // Check if the manager has been initialized
  static bool isInitialized();

  // Look up the MediaInterfaceCode for a raw media lane code byte.
  // Uses the mediaInterfaceCode field from PortSpeedConfig entries. Ports of
  // different widths can share a media lane code (ZR 0x68 is both 1x800G and
  // 2x400G), so pass the port's host lane count when known. Without it, or if
  // no port has that width, the first port in the config with the code wins.
  static MediaInterfaceCode mediaLaneCodeToMediaInterfaceCode(
      uint8_t mediaLaneCodeByte,
      std::optional<int> numHostLanes = std::nullopt);

  // Reset state (for testing)
  static void reset();

 private:
  // SMF derivation index: smfCode byte → list of candidate entries
  struct SmfCandidate {
    int32_t codeValue{};
    size_t hostStartLanesCount{};
    uint8_t hostInterfaceCode{};
    std::optional<int32_t> smfLength;
    int32_t maxNumBanks{1};
  };

  static TransceiverPropertiesManager& instance();

  TransceiverPropertiesConfig config_;
  bool initialized_{false};

  // Code→Properties index (built at init time from smfTransceivers)
  std::unordered_map<int32_t, const SmfTransceiverProperties*> codeToPropsMap_;

  // mediaLaneCode → MediaInterfaceCode reverse map (built at init time)
  std::unordered_map<int32_t, MediaInterfaceCode>
      mediaLaneCodeToMediaInterfaceCodeMap_;
  // {mediaLaneCode, numHostLanes} → MediaInterfaceCode
  std::map<std::pair<int32_t, int>, MediaInterfaceCode>
      mediaLaneCodeAndHostLanesToMediaInterfaceCodeMap_;

  // SMF derivation index: smfCode byte → list of candidate entries
  std::unordered_map<uint8_t, std::vector<SmfCandidate>> smfDerivationMap_;
};

template <typename InterfaceCode>
static InterfaceCode extractFromMediaInterfaceUnion(
    const MediaInterfaceUnion& u) {
  if constexpr (std::is_same_v<InterfaceCode, SMFMediaInterfaceCode>) {
    if (auto v = u.smfCode()) {
      return *v;
    }
  } else if constexpr (std::is_same_v<
                           InterfaceCode,
                           ActiveCuHostInterfaceCode>) {
    if (auto v = u.activeCuCode()) {
      return *v;
    }
  }
  return static_cast<InterfaceCode>(0);
}

template <typename InterfaceCode>
std::vector<
    std::array<InterfaceCode, TransceiverPropertiesManager::kMaxOsfpNumLanes>>
TransceiverPropertiesManager::getSpeedCombinations(MediaInterfaceCode code) {
  std::vector<std::array<InterfaceCode, kMaxOsfpNumLanes>> result;
  const auto& props = getProperties(code);
  if (props.supportedSpeedCombinations()->empty()) {
    return result;
  }
  for (const auto& combo : *props.supportedSpeedCombinations()) {
    // Place each port's mediaLaneCode at its hostLanes position
    std::array<InterfaceCode, kMaxOsfpNumLanes> arr{};
    size_t totalLanes = 0;
    bool valid = true;
    for (const auto& port : *combo.ports()) {
      auto start = static_cast<size_t>(*port.hostLanes()->start());
      auto count = static_cast<size_t>(*port.hostLanes()->count());
      auto portCode =
          extractFromMediaInterfaceUnion<InterfaceCode>(*port.mediaLaneCode());
      for (size_t i = 0; i < count; i++) {
        if (start + i >= kMaxOsfpNumLanes) {
          valid = false;
          break;
        }
        arr[start + i] = portCode;
      }
      totalLanes += count;
    }
    // Include valid combinations (ZR modules may have fewer media lanes)
    if (valid && totalLanes > 0) {
      result.push_back(arr);
    }
  }
  return result;
}

template <typename InterfaceCode>
std::vector<InterfaceCode> TransceiverPropertiesManager::getMediaCodesForSpeed(
    MediaInterfaceCode code,
    cfg::PortSpeed speed) {
  std::vector<InterfaceCode> result;
  const auto& props = getProperties(code);
  if (props.supportedSpeedCombinations()->empty()) {
    return result;
  }
  for (const auto& combo : *props.supportedSpeedCombinations()) {
    for (const auto& port : *combo.ports()) {
      if (static_cast<cfg::PortSpeed>(*port.speed()) == speed) {
        auto mediaCode = extractFromMediaInterfaceUnion<InterfaceCode>(
            *port.mediaLaneCode());
        if (std::find(result.begin(), result.end(), mediaCode) ==
            result.end()) {
          result.push_back(mediaCode);
        }
      }
    }
  }
  return result;
}

} // namespace facebook::fboss
