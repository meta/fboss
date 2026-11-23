#pragma once

#include <unordered_map>

#include <fmt/core.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#include "fboss/qsfp_service/module/cmis/CmisModule.h"

namespace facebook {
namespace fboss {
template <typename InterfaceCode>
using MediaInterfaceMap = std::unordered_map<InterfaceCode, MediaInterfaceCode>;
using SmfMediaInterfaceMap = MediaInterfaceMap<SMFMediaInterfaceCode>;
using ActiveMediaInterfaceMap = MediaInterfaceMap<ActiveCuHostInterfaceCode>;

template <typename InterfaceCode>
using SpeedApplicationMap = std::
    unordered_map<facebook::fboss::cfg::PortSpeed, std::vector<InterfaceCode>>;
using SmfSpeedApplicationMap = SpeedApplicationMap<SMFMediaInterfaceCode>;
using ActiveSpeedApplicationMap =
    SpeedApplicationMap<ActiveCuHostInterfaceCode>;

template <typename InterfaceCode>
using ValidSpeedCombinations =
    std::vector<std::array<InterfaceCode, CmisModule::kMaxOsfpNumLanes>>;
using SmfValidSpeedCombinations = ValidSpeedCombinations<SMFMediaInterfaceCode>;
using ActiveValidSpeedCombinations =
    ValidSpeedCombinations<ActiveCuHostInterfaceCode>;

template <typename InterfaceCode>
using MultiportSpeedConfig =
    std::array<InterfaceCode, CmisModule::kMaxOsfpNumLanes>;

class CmisHelper final {
 private:
  // Disable Construction of this class.
  CmisHelper() = delete;
  ~CmisHelper() = delete;
  CmisHelper(CmisHelper const&) = delete;
  CmisHelper& operator=(CmisHelper const&) = delete;

 public:
  static const SmfSpeedApplicationMap& getSmfSpeedApplicationMapping() {
    // Store the mapping between port speed and SMF MediaApplicationCode.
    // We use the module Media Interface ID, which is located at the second byte
    // of the APPLICATION_ADVERTISING field, as Application ID here, which is
    // the code from Table4-7 SM media interface codes in Sff-8024.
    static const SmfSpeedApplicationMap smfSpeedApplicationMapping_ = {
        {facebook::fboss::cfg::PortSpeed::FIFTYTHREEPOINTONETWOFIVEG,
         {SMFMediaInterfaceCode::FR4_200G}},
        {facebook::fboss::cfg::PortSpeed::HUNDREDG,
         {SMFMediaInterfaceCode::CWDM4_100G, SMFMediaInterfaceCode::FR1_100G}},
    };
    return smfSpeedApplicationMapping_;
  }

  // This function is not used in config-driven mode
  // DO NOT UPDATE THIS FUNCTION
  static const SmfMediaInterfaceMap& getSmfMediaInterfaceMapping() {
    static const SmfMediaInterfaceMap smfMediaInterfaceMapping_ = {
        {SMFMediaInterfaceCode::CWDM4_100G, MediaInterfaceCode::CWDM4_100G},
        {SMFMediaInterfaceCode::FR1_100G, MediaInterfaceCode::FR1_100G},
        {SMFMediaInterfaceCode::FR4_200G, MediaInterfaceCode::FR4_200G},
        {SMFMediaInterfaceCode::LR4_200G, MediaInterfaceCode::LR4_200G},
        {SMFMediaInterfaceCode::FR4_400G, MediaInterfaceCode::FR4_400G},
        {SMFMediaInterfaceCode::LR4_10_400G, MediaInterfaceCode::LR4_400G_10KM},
        {SMFMediaInterfaceCode::DR4_400G, MediaInterfaceCode::DR4_400G},
        {SMFMediaInterfaceCode::FR8_800G, MediaInterfaceCode::FR8_800G},
        {SMFMediaInterfaceCode::DR4_800G, MediaInterfaceCode::DR4_800G},
        {SMFMediaInterfaceCode::DR2_400G, MediaInterfaceCode::DR2_400G},
        {SMFMediaInterfaceCode::DR2_200G, MediaInterfaceCode::DR2_200G},
        {SMFMediaInterfaceCode::DR1_200G, MediaInterfaceCode::DR1_200G},
        {SMFMediaInterfaceCode::DR1_100G, MediaInterfaceCode::DR1_100G},
        {SMFMediaInterfaceCode::ZR_OROADM_FLEXO_8E_DPO_800G,
         MediaInterfaceCode::ZR_800G},
        {SMFMediaInterfaceCode::ZR_OIF_ZRA_800G, MediaInterfaceCode::ZR_800G},
        {SMFMediaInterfaceCode::ZR_VENDOR_CUSTOM, MediaInterfaceCode::ZR_800G},
    };
    return smfMediaInterfaceMapping_;
  }

  static const ActiveMediaInterfaceMap& getActiveMediaInterfaceMapping() {
    static const ActiveMediaInterfaceMap activeMediaInterfaceMapping_ = {
        {ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G,
         MediaInterfaceCode::CR1_100G},
        {ActiveCuHostInterfaceCode::AUI_PAM4_2S_200G,
         MediaInterfaceCode::CR4_200G},
        {ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
         MediaInterfaceCode::CR4_400G},
        {ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
         MediaInterfaceCode::CR8_800G},
        {ActiveCuHostInterfaceCode::AUI_PAM4_1_200G,
         MediaInterfaceCode::CR1_200G},
        {ActiveCuHostInterfaceCode::AUI_PAM4_2_400G,
         MediaInterfaceCode::CR2_400G},
        {ActiveCuHostInterfaceCode::AUI_PAM4_4_800G,
         MediaInterfaceCode::CR4_800G},
        {ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G,
         MediaInterfaceCode::CR8_1600G},
    };
    return activeMediaInterfaceMapping_;
  }

  static const ActiveSpeedApplicationMap& getActiveSpeedApplication() {
    // Map for Speed to Application select for Active Cables
    static const ActiveSpeedApplicationMap activeSpeedApplicationMapping_ = {
        {cfg::PortSpeed::HUNDREDG,
         {ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G}},
        // Order is not significant: consumers disambiguate the 100G/lane and
        // 200G/lane entries on the advertised host lane count.
        {cfg::PortSpeed::TWOHUNDREDG,
         {ActiveCuHostInterfaceCode::AUI_PAM4_2S_200G,
          ActiveCuHostInterfaceCode::AUI_PAM4_1_200G}},
        {cfg::PortSpeed::FOURHUNDREDG,
         {ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
          ActiveCuHostInterfaceCode::AUI_PAM4_2_400G}},
        {cfg::PortSpeed::EIGHTHUNDREDG,
         {ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
          ActiveCuHostInterfaceCode::AUI_PAM4_4_800G}},
        {cfg::PortSpeed::ONEPOINTSIXT,
         {ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G}}};
    return activeSpeedApplicationMapping_;
  }

  static const ActiveValidSpeedCombinations& getActiveValidSpeedCombinations() {
    // Valid speed combinations for AEC. Note that for now, only
    // 8x100G CR8_800G has been tested.
    static const ActiveValidSpeedCombinations
        activeOsfpValidSpeedCombinations_ = {
            /* These rates are not supported/tested as of now
                        {
                            // 2xCR4_200G
                            ActiveCuHostInterfaceCode::AUI_PAM4_2S_200G,
                            ActiveCuHostInterfaceCode::AUI_PAM4_2S_200G,
                            ActiveCuHostInterfaceCode::AUI_PAM4_2S_200G,
                            ActiveCuHostInterfaceCode::AUI_PAM4_2S_200G,
                            ActiveCuHostInterfaceCode::AUI_PAM4_2S_200G,
                            ActiveCuHostInterfaceCode::AUI_PAM4_2S_200G,
                            ActiveCuHostInterfaceCode::AUI_PAM4_2S_200G,
                            ActiveCuHostInterfaceCode::AUI_PAM4_2S_200G,
                        },
            */
            {
                // 2xCR4_100G
                ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G,
            },
            {
                // 2xCR4_400G
                ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G,
            },
            {
                // 8x100G CR8_800G
                ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G,
            },
            {
                // 8x200G CR1_200G
                ActiveCuHostInterfaceCode::AUI_PAM4_1_200G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1_200G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1_200G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1_200G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1_200G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1_200G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1_200G,
                ActiveCuHostInterfaceCode::AUI_PAM4_1_200G,
            },
            {
                // 4x400G CR2_400G
                ActiveCuHostInterfaceCode::AUI_PAM4_2_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_2_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_2_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_2_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_2_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_2_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_2_400G,
                ActiveCuHostInterfaceCode::AUI_PAM4_2_400G,
            },
            {
                // 2x800G CR4_800G
                ActiveCuHostInterfaceCode::AUI_PAM4_4_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4_800G,
                ActiveCuHostInterfaceCode::AUI_PAM4_4_800G,
            },
            {
                // 1x1600G CR8_1600G
                ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G,
                ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G,
            },
        };
    return activeOsfpValidSpeedCombinations_;
  }

  /*
   * getMediaInterfaceCode
   * A function used to translate the Module interface map based on what
   * is carried in its media side (for SMF, its media interface code, for AEC
   * its the host interface code) given a translation map which the module type
   * supports. if the Module media interface code is does not have a pair in the
   * map, we will return UNKNOWN (0) based on the Table4-7 SM media interface
   * codes in Sff-8024 which specify 0 as an UNKNOWN application.
   */
  template <typename InterfaceCode>
  static MediaInterfaceCode getMediaInterfaceCode(
      InterfaceCode mediaInterfaceCode,
      const MediaInterfaceMap<InterfaceCode>& speedMap) {
    const auto itr = speedMap.find(mediaInterfaceCode);
    if (itr != speedMap.end()) {
      return itr->second;
    }
    return MediaInterfaceCode::UNKNOWN;
  }

  /*
   * getApplicationFromApSelCode
   *
   * Returns the interface code if ApSelCode is present in the module
   * capabilities.
   */
  template <typename InterfaceCode>
  static InterfaceCode getApplicationFromApSelCode(
      uint8_t apSelCode,
      const CmisModule::ApplicationAdvertisingFields& moduleCapabilities) {
    for (const auto& capability : moduleCapabilities) {
      if (capability.ApSelCode == apSelCode) {
        return static_cast<InterfaceCode>(capability.moduleMediaInterface);
      }
    }
    return InterfaceCode::UNKNOWN;
  }

  /*
   * getInterfaceCode
   *
   * a Function used to get the Supported interface codes supported
   * for each speed. One of the values in the returned vector will be
   * programmed to the hardware, so returning a uint8_t
   */
  template <typename InterfaceCode>
  static std::vector<uint8_t> getInterfaceCode(
      facebook::fboss::cfg::PortSpeed speed,
      const SpeedApplicationMap<InterfaceCode>& mapping) {
    std::vector<uint8_t> interfaceCodes;
    const auto itr = mapping.find(speed);
    if (itr != mapping.end()) {
      for (auto& val : itr->second) {
        interfaceCodes.push_back(static_cast<uint8_t>(val));
      }
      return interfaceCodes;
    }
    return {};
  }

  /*
   * getMediaIntfCodeFromSpeed
   *
   * Returns the media interface code for a given speed and the number of lanes
   * on this optics. This function uses the optics static value from register
   * cache. Pl note that the different optics can implement the same media
   * interface code using diferent number of lanes. ie: QSFP 400G-FR4 implements
   * 400G-FR4 (code 0x1d) using 8 lanes of 50G serdes on host whereas OSFP
   * 2x400G-FR4 implements the same 400G-FR4 (code 0x1d) using 4 lanes of 100G
   * serdes on host.
   */
  template <typename InterfaceCode>
  static InterfaceCode getMediaIntfCodeFromSpeed(
      cfg::PortSpeed speed,
      uint8_t numLanes,
      const CmisModule::ApplicationAdvertisingFields& moduleCapabilities,
      const SpeedApplicationMap<InterfaceCode>& mapping) {
    auto appCodes = CmisHelper::getInterfaceCode<InterfaceCode>(speed, mapping);
    if (appCodes.empty()) {
      return InterfaceCode::UNKNOWN;
    }

    for (auto application : appCodes) {
      for (const auto& capability : moduleCapabilities) {
        if (capability.moduleMediaInterface == application &&
            capability.hostLaneCount == numLanes) {
          return (InterfaceCode)application;
        }
      }
    }
    return InterfaceCode::UNKNOWN;
  }

  /*
   * checkSpeedCombo
   *
   * Given a port: desired speed, start host lane, number of lanes, the module
   * capabilities the current transceiver HW speed config (if transceiver has
   * multiple ports and we are only configuring one port) and the possible speed
   * combinations, check if the desired speed combination is valid and
   * supported.
   */

  template <typename InterfaceCode>
  static bool checkSpeedCombo(
      const cfg::PortSpeed speed,
      const uint8_t startHostLane,
      const uint8_t numLanes,
      const uint8_t laneMask,
      const std::string& tcvrName,
      const CmisModule::ApplicationAdvertisingFields& moduleCapabilities,
      const CmisModule::AllLaneConfig& currHwSpeedConfig,
      const ValidSpeedCombinations<InterfaceCode>& speedCombinations,
      const SpeedApplicationMap<InterfaceCode>& mapping) {
    // Sanity check
    auto desiredMediaIntfCode =
        CmisHelper::getMediaIntfCodeFromSpeed<InterfaceCode>(
            speed, numLanes, moduleCapabilities, mapping);
    if (desiredMediaIntfCode == InterfaceCode::UNKNOWN) {
      XLOG(ERR) << "Transceiver " << tcvrName << ": " << "Unsupported Speed "
                << apache::thrift::util::enumNameSafe(speed);
      return false;
    }

    // Find what will be the new config after applying the requested config
    std::array<InterfaceCode, CmisModule::kMaxOsfpNumLanes> desiredSpeedConfig;
    for (int laneId = 0; laneId < CmisModule::kMaxOsfpNumLanes; laneId++) {
      if (laneId >= startHostLane && laneId < startHostLane + numLanes) {
        desiredSpeedConfig[laneId] = desiredMediaIntfCode;
      } else {
        desiredSpeedConfig[laneId] =
            CmisHelper::getApplicationFromApSelCode<InterfaceCode>(
                currHwSpeedConfig[laneId], moduleCapabilities);
      }
    }

    // Check if this is supported speed config combo on this optics and return
    for (auto& validSpeedCombo : speedCombinations) {
      bool combolValid = true;
      for (int laneId = 0; laneId < CmisModule::kMaxOsfpNumLanes; laneId++) {
        if (validSpeedCombo[laneId] != desiredSpeedConfig[laneId]) {
          combolValid = false;
          break;
        }
      }
      if (combolValid) {
        XLOG(INFO)
            << "Transceiver " << tcvrName << ": "
            << fmt::format(
                   "Found the valid speed combo of media intf id {:s} for lanemask {:#x}",
                   apache::thrift::util::enumNameSafe(desiredMediaIntfCode),
                   laneMask);
        return true;
      }
    }
    XLOG(ERR)
        << "Transceiver " << tcvrName << ": "
        << fmt::format(
               "Could not find the valid speed combo of media intf id {:s} for lanemask {:#x}",
               apache::thrift::util::enumNameSafe(desiredMediaIntfCode),
               laneMask);
    return false;
  }

  /*
   * getSpeedComboAsUint8
   *
   * Translate the valid speed combination to a
   * vector of uint8_t so that the values can be applied
   * to the module. Per CMIS spec, the value written to the
   * HW is uint8_t.
   */
  template <typename InterfaceCode>
  static std::vector<uint8_t> getSpeedComboAsUint8(
      const MultiportSpeedConfig<InterfaceCode>& validCombo) {
    std::vector<uint8_t> validComboVec;
    for (auto& intfCode : validCombo) {
      validComboVec.push_back(static_cast<uint8_t>(intfCode));
    }
    return validComboVec;
  }

  /*
   * getValidMultiportSpeedConfig
   *
   * Returns the valid speed config for all the lanes of the multi-port optics
   * which matches closely with the supported speed combo on the optics. If no
   * valid speed combo is found then returns nullopt
   */
  template <typename InterfaceCode>
  static std::vector<uint8_t> getValidMultiportSpeedConfig(
      const cfg::PortSpeed speed,
      const uint8_t startHostLane,
      const uint8_t numLanes,
      const uint8_t laneMask,
      const std::string& tcvrName,
      const CmisModule::ApplicationAdvertisingFields& moduleCapabilities,
      const ValidSpeedCombinations<InterfaceCode>& speedCombinations,
      const SpeedApplicationMap<InterfaceCode>& mapping) {
    auto desiredMediaIntfCode =
        CmisHelper::getMediaIntfCodeFromSpeed<InterfaceCode>(
            speed, numLanes, moduleCapabilities, mapping);
    if (desiredMediaIntfCode == InterfaceCode::UNKNOWN) {
      XLOG(ERR) << "Transceiver " << tcvrName << ": " << "Unsupported Speed "
                << apache::thrift::util::enumNameSafe(speed);
      return {};
    }

    CHECK_LE(startHostLane + numLanes, CmisModule::kMaxOsfpNumLanes);
    for (auto& validSpeedCombo : speedCombinations) {
      bool combolValid = true;
      for (int laneId = startHostLane; laneId < startHostLane + numLanes;
           laneId++) {
        if (validSpeedCombo[laneId] != desiredMediaIntfCode) {
          combolValid = false;
          break;
        }
      }
      if (combolValid) {
        std::string speedCfgCombo;
        for (int laneId = 0; laneId < CmisModule::kMaxOsfpNumLanes; laneId++) {
          speedCfgCombo +=
              apache::thrift::util::enumNameSafe(validSpeedCombo[laneId]);
          speedCfgCombo += " ";
        }
        XLOG(INFO)
            << "Transceiver " << tcvrName << ": "
            << fmt::format(
                   "Returning the valid speed combo of media intf id {:s} for lanemask {:#x} = {:s}",
                   apache::thrift::util::enumNameSafe(desiredMediaIntfCode),
                   laneMask,
                   speedCfgCombo);
        return getSpeedComboAsUint8<InterfaceCode>(validSpeedCombo);
      }
    }
    XLOG(ERR)
        << "Transceiver " << tcvrName << ": "
        << fmt::format(
               "No valid speed combo found for speed {:s} and lanemask {:#x}",
               apache::thrift::util::enumNameSafe(speed),
               laneMask);
    return {};
  }
}; // namespace fboss

} // namespace fboss
} // namespace facebook
