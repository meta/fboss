/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/qsfp_service/test/hw_test/HwTransceiverUtils.h"

#include <algorithm>
#include <set>

#include <fmt/format.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/agent/test/utils/PortTestUtils.h"
#include "fboss/lib/config/PlatformConfigUtils.h"
#include "fboss/qsfp_service/TransceiverManager.h"
#include "fboss/qsfp_service/module/cmis/CmisHelper.h"
#include "fboss/qsfp_service/module/properties/TransceiverPropertiesManager.h"

namespace facebook::fboss::utility {

void HwTransceiverUtils::verifyTempAndVccFlags(
    std::map<std::string, TransceiverInfo>& portToTransceiverInfoMap) {
  for (auto& [_, transceiverInfo] : portToTransceiverInfoMap) {
    auto tcvrID = *transceiverInfo.tcvrState()->port();
    auto& tcvrStats = *transceiverInfo.tcvrStats();
    EXPECT_FALSE(
        *tcvrStats.sensor()->temp()->flags().value_or({}).alarm()->high())
        << fmt::format("{:d} has high temp alarm flag", tcvrID);
    EXPECT_FALSE(
        *tcvrStats.sensor()->temp()->flags().value_or({}).warn()->high())
        << fmt::format("{:d} has high temp warn flag", tcvrID);
    EXPECT_FALSE(
        *tcvrStats.sensor()->vcc()->flags().value_or({}).alarm()->high())
        << fmt::format("{:d} has high vcc alarm flag", tcvrID);
    EXPECT_FALSE(
        *tcvrStats.sensor()->vcc()->flags().value_or({}).warn()->high())
        << fmt::format("{:d} has high vcc warn flag", tcvrID);
  }
}

void HwTransceiverUtils::verifyTcvrErrorStates(
    std::map<std::string, TransceiverInfo>& portToTransceiverInfoMap) {
  for (auto& [_, transceiverInfo] : portToTransceiverInfoMap) {
    auto& tcvrState = *transceiverInfo.tcvrState();
    auto tcvrID = *tcvrState.port();
    EXPECT_TRUE(tcvrState.errorStates()->empty()) << fmt::format(
        "{:d} has error states {:s}",
        tcvrID,
        folly::join(",", *tcvrState.errorStates()));
  }
}

void HwTransceiverUtils::verifyPortNameToLaneMap(
    const std::vector<PortID>& portIDs,
    cfg::PortProfileID profile,
    const PlatformMapping* platformMapping,
    std::map<int32_t, TransceiverInfo>& tcvrInfos) {
  if (profile == cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_COPPER ||
      profile == cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL) {
    // We use these profiles on Meru400biu and Meru400bfu with 200G optics in
    // a hacky configuration which invalidates the verification of media/host
    // lanes in this function.
    return;
  }
  const auto& platformPorts = platformMapping->getPlatformPorts();
  const auto& chips = platformMapping->getChips();
  for (auto portID : portIDs) {
    auto hostLanesFromPlatformMapping =
        platformMapping->getTransceiverHostLanes(
            PlatformPortProfileConfigMatcher(
                profile, portID, std::nullopt /* portConfigOverrideFactor */));
    EXPECT_NE(hostLanesFromPlatformMapping.size(), 0);
    auto platformPortItr = platformPorts.find(static_cast<int32_t>(portID));
    ASSERT_NE(platformPortItr, platformPorts.end());
    auto tcvrIds = utility::getTransceiverIds(platformPortItr->second, chips);
    ASSERT_EQ(tcvrIds.size(), 1);
    auto portName = *platformPortItr->second.mapping()->name();

    auto tcvrInfoItr = tcvrInfos.find(tcvrIds[0]);
    ASSERT_NE(tcvrInfoItr, tcvrInfos.end());

    if (!TransceiverManager::opticalOrActiveCable(
            *tcvrInfoItr->second.tcvrState())) {
      // Passive DAC is never programmed, so its lane map is derived from the
      // widest advertised application instead of the port's own datapath. On
      // breakout subports that yields the module's full lane set for the first
      // subport and an empty set for the rest.
      XLOG(INFO) << " Skip verifying lane map: " << portName
                 << ", for passive copper cable";
      continue;
    }

    auto& hostLaneMap = *tcvrInfoItr->second.tcvrState()->portNameToHostLanes();
    // Verify port exists in the map
    EXPECT_NE(hostLaneMap.find(portName), hostLaneMap.end());
    // Verify we have the same host lanes in the map as returned by platform
    // mapping
    EXPECT_EQ(
        hostLaneMap[portName].size(), hostLanesFromPlatformMapping.size());
    for (auto lane : hostLanesFromPlatformMapping) {
      XLOG(INFO) << "Verifying lane " << lane << " on " << portName;
      EXPECT_NE(
          std::find(
              hostLaneMap[portName].begin(), hostLaneMap[portName].end(), lane),
          hostLaneMap[portName].end());
    }

    auto& mediaLaneMap =
        *tcvrInfoItr->second.tcvrState()->portNameToMediaLanes();
    std::vector<int> expectedMediaLanes;
    auto moduleMediaInterface =
        tcvrInfoItr->second.tcvrState()->moduleMediaInterface().ensure();

    // Try config-driven lookup first for expected media lanes
    auto portProfileConfig =
        platformMapping->getPortProfileConfig(PlatformPortProfileConfigMatcher(
            profile, portID, std::nullopt /* portConfigOverrideFactor */));
    std::vector<int> configMediaLanes;
    if (TransceiverPropertiesManager::isKnown(moduleMediaInterface)) {
      configMediaLanes = TransceiverPropertiesManager::getExpectedMediaLanes(
          moduleMediaInterface,
          std::vector<int>(
              hostLaneMap[portName].begin(), hostLaneMap[portName].end()),
          *portProfileConfig->speed());
    }
    if (!configMediaLanes.empty()) {
      expectedMediaLanes = configMediaLanes;
    } else {
      // Fallback for modules not in config (single-port, copper, SFF)
      switch (moduleMediaInterface) {
        case MediaInterfaceCode::CWDM4_100G:
          if (profile == cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_OPTICAL) {
            expectedMediaLanes = {0, 1};
          } else if (
              profile == cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL ||
              profile == cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_OPTICAL) {
            auto hostLanes = hostLaneMap[portName];
            ASSERT_TRUE(
                hostLanes == std::vector{0} || hostLanes == std::vector{1});
            expectedMediaLanes = hostLanes;
          } else {
            expectedMediaLanes = {0, 1, 2, 3};
          }
          break;
        case MediaInterfaceCode::CR8_800G:
          switch (profile) {
            case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544X2N_COPPER: {
              auto itr = hostLaneMap.find(portName);
              CHECK(itr != hostLaneMap.end());
              auto& hostLaneVector = itr->second;
              CHECK_EQ(hostLaneVector.size(), 1);
              expectedMediaLanes = hostLaneVector;
            } break;
            case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_COPPER:
              if (std::find(
                      hostLaneMap[portName].begin(),
                      hostLaneMap[portName].end(),
                      0) != hostLaneMap[portName].end()) {
                expectedMediaLanes = {0, 1, 2, 3};
              } else {
                expectedMediaLanes = {4, 5, 6, 7};
              }
              break;
            case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_COPPER:
              expectedMediaLanes = {0, 1, 2, 3, 4, 5, 6, 7};
              break;
            default:
              throw FbossError(
                  "Unhandled profile ",
                  apache::thrift::util::enumNameSafe(profile));
          }
          break;
        case MediaInterfaceCode::FR1_100G:
        case MediaInterfaceCode::LR_10G:
        case MediaInterfaceCode::SR_10G:
        case MediaInterfaceCode::BASE_T_10G:
        case MediaInterfaceCode::CR_10G:
        case MediaInterfaceCode::ZR_800G:
        case MediaInterfaceCode::CR1_100G:
          expectedMediaLanes = {0};
          break;
        case MediaInterfaceCode::UNKNOWN:
        case MediaInterfaceCode::CR4_100G:
        case MediaInterfaceCode::CR4_200G:
        case MediaInterfaceCode::CR4_400G:
        case MediaInterfaceCode::CR8_400G:
          expectedMediaLanes = {};
          break;
        case MediaInterfaceCode::FR8_800G:
          expectedMediaLanes = {0, 1, 2, 3, 4, 5, 6, 7};
          break;
        default:
          throw FbossError(
              "Unsupported moduleMediaInterface ",
              apache::thrift::util::enumNameSafe(moduleMediaInterface));
      }
    }

    XLOG(INFO) << "Verifying that " << portName << " uses media lanes "
               << folly::join(",", expectedMediaLanes);
    // Verify port exists in the map
    EXPECT_NE(mediaLaneMap.find(portName), mediaLaneMap.end());
    EXPECT_EQ(mediaLaneMap.at(portName), expectedMediaLanes);

    // Expect both tcvrState and tcvrStats to have the same map
    EXPECT_EQ(
        *tcvrInfoItr->second.tcvrState()->portNameToHostLanes(),
        *tcvrInfoItr->second.tcvrStats()->portNameToHostLanes());
    EXPECT_EQ(
        *tcvrInfoItr->second.tcvrState()->portNameToMediaLanes(),
        *tcvrInfoItr->second.tcvrStats()->portNameToMediaLanes());
  }
}

void HwTransceiverUtils::verifyTransceiverSettings(
    const TcvrState& tcvrState,
    const std::string& portName,
    cfg::PortProfileID profile) {
  auto id = *tcvrState.port();
  if (!*tcvrState.present()) {
    XLOG(INFO) << " Skip verifying: " << id << ", not present";
    return;
  }

  XLOG(INFO) << " Verifying: " << id << ", portName = " << portName;
  // Only testing QSFP and SFP transceivers right now
  EXPECT_TRUE(
      *tcvrState.transceiver() == TransceiverType::QSFP ||
      *tcvrState.transceiver() == TransceiverType::SFP);

  if (!TransceiverManager::opticalOrActiveCable(tcvrState)) {
    XLOG(INFO) << " Skip verifying tcvr settings: " << *tcvrState.port()
               << ", for passive copper cable";
  } else if (
      profile != cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_COPPER &&
      profile != cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL) {
    // We use these profiles on Meru400biu and Meru400bfu with 200G optics in
    // a hacky configuration which invalidates the verification of optics
    // settings in this function.
    verifyOpticsSettings(tcvrState, portName, profile);
  }

  verifyMediaInterfaceCompliance(tcvrState, profile, portName);

  if (TransceiverManager::opticalOrActiveCable(tcvrState) &&
      profile != cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_COPPER &&
      profile != cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL) {
    // We use these profiles on Meru400biu and Meru400bfu with 200G optics in
    // a hacky configuration which invalidates the verification of datapath in
    // this function.
    verifyDataPathEnabled(tcvrState, portName);
  }
}

void HwTransceiverUtils::verifyOpticsSettings(
    const TcvrState& tcvrState,
    const std::string& portName,
    cfg::PortProfileID profile) {
  auto settings = apache::thrift::can_throw(*tcvrState.settings());
  const auto& portToMediaMap = *tcvrState.portNameToMediaLanes();
  const auto& portToHostMap = *tcvrState.portNameToHostLanes();

  CHECK(portToMediaMap.find(portName) != portToMediaMap.end());
  CHECK(portToHostMap.find(portName) != portToHostMap.end());

  const auto& relevantMediaLanes = portToMediaMap.at(portName);
  const auto& relevantHostLanes = portToHostMap.at(portName);

  EXPECT_GT(relevantMediaLanes.size(), 0);
  EXPECT_GT(relevantHostLanes.size(), 0);

  // Identify Tunable Module (C-Band or L-Band ZR).
  auto modTech = tcvrState.moduleTechnology().value();
  bool isTunableOptics = modTech == ModuleTechnology::TUNABLE_C_BAND ||
      modTech == ModuleTechnology::TUNABLE_L_BAND;

  for (auto& mediaLane :
       apache::thrift::can_throw(*settings.mediaLaneSettings())) {
    if (std::find(
            relevantMediaLanes.begin(),
            relevantMediaLanes.end(),
            *mediaLane.lane()) == relevantMediaLanes.end()) {
      continue;
    }
    EXPECT_FALSE(*mediaLane.txDisable())
        << "Transceiver:" << *tcvrState.port() << ", Lane=" << *mediaLane.lane()
        << " txDisable doesn't match expected";
    if (*tcvrState.transceiver() == TransceiverType::QSFP) {
      // For tunable optics (ZR modules), txSquelch disable should always be
      // true
      bool expectedTxSquelch = isTunableOptics ||
          (profile ==
           cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL);
      EXPECT_EQ(*mediaLane.txSquelch(), expectedTxSquelch)
          << "Transceiver:" << *tcvrState.port()
          << ", Lane=" << *mediaLane.lane()
          << " txSquelch doesn't match expected." << " Profile was "
          << apache::thrift::util::enumNameSafe(profile)
          << ", isTunableOptics=" << isTunableOptics;
    }
  }

  if (*tcvrState.transceiver() == TransceiverType::QSFP) {
    // Disable low power mode
    EXPECT_TRUE(
        *settings.powerControl() == PowerControlState::POWER_OVERRIDE ||
        *settings.powerControl() == PowerControlState::HIGH_POWER_OVERRIDE);
    bool isCpoModule =
        (tcvrState.identifier().value() == TransceiverModuleIdentifier::CPO);

    // No need to Check CDR for AEC cables T234995630
    bool isAec =
        (tcvrState.moduleTechnology().value() == ModuleTechnology::AEC);
    // LPO Modules dont have a DSP, so we dont need to check for CDR.
    bool isLpoModule =
        (tcvrState.moduleTechnology().value() == ModuleTechnology::LPO);
    if (isCpoModule) {
      EXPECT_EQ(*settings.cdrTx(), FeatureState::ENABLED);
    } else if (!(isAec || isLpoModule)) {
      EXPECT_EQ(*settings.cdrTx(), FeatureState::ENABLED);
      EXPECT_EQ(*settings.cdrRx(), FeatureState::ENABLED);
    }

    for (auto& hostLane :
         apache::thrift::can_throw(*settings.hostLaneSettings())) {
      if (std::find(
              relevantHostLanes.begin(),
              relevantHostLanes.end(),
              *hostLane.lane()) == relevantHostLanes.end()) {
        continue;
      }
      // For tunable optics (ZR modules), rxSquelch disable should always be
      // true
      bool expectedRxSquelch = isTunableOptics ||
          (profile ==
           cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL);
      EXPECT_EQ(*hostLane.rxSquelch(), expectedRxSquelch)
          << "Transceiver:" << *tcvrState.port()
          << ", Lane=" << *hostLane.lane()
          << " rxSquelch doesn't match expected."
          << " Profile was " << apache::thrift::util::enumNameSafe(profile)
          << ", isTunableOptics=" << isTunableOptics;
    }
  }
}

void HwTransceiverUtils::verifyMediaInterfaceCompliance(
    const TcvrState& tcvrState,
    cfg::PortProfileID profile,
    const std::string& portName) {
  auto settings = apache::thrift::can_throw(*tcvrState.settings());
  auto mgmtInterface =
      apache::thrift::can_throw(*tcvrState.transceiverManagementInterface());
  EXPECT_TRUE(
      mgmtInterface == TransceiverManagementInterface::SFF8472 ||
      mgmtInterface == TransceiverManagementInterface::SFF ||
      mgmtInterface == TransceiverManagementInterface::CMIS);
  auto allMediaInterfaces =
      apache::thrift::can_throw(*settings.mediaInterface());
  std::vector<MediaInterfaceId> mediaInterfaces;
  // Filter out the mediaInterfaces for this specific port
  for (auto mediaLane : tcvrState.portNameToMediaLanes()->at(portName)) {
    ASSERT_TRUE(mediaLane < allMediaInterfaces.size());
    mediaInterfaces.push_back(allMediaInterfaces[mediaLane]);
  }

  if (isAoc(tcvrState)) {
    // An AOC programs like an active cable regardless of the profile's
    // copper/optical suffix, so the per-profile verifiers below (which
    // read smfCode or expect a copper transmitter) don't apply.
    verifyActiveOpticalCableProfile(mgmtInterface, mediaInterfaces, profile);
    return;
  }

  switch (profile) {
    case cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_OPTICAL:
      verify10gProfile(tcvrState, mgmtInterface, mediaInterfaces);
      break;

    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_OPTICAL:
      verify25gProfile(mgmtInterface, mediaInterfaces);
      break;

    case cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_OPTICAL:
      verify50gProfile(mgmtInterface, mediaInterfaces);
      break;

    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544_OPTICAL:
    case cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL:
      verify100gProfile(mgmtInterface, mediaInterfaces);
      break;

    case cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC:
      // TODO
      break;

    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N:
    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL:
    case cfg::PortProfileID::PROFILE_200G_1_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_200G_2_PAM4_RS544X2N_OPTICAL:
      verify200gProfile(mgmtInterface, mediaInterfaces);
      break;

    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_2_PAM4_RS544X2N_OPTICAL:
      verify400gProfile(mgmtInterface, mediaInterfaces);
      break;

    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_COPPER:
      verifyCopper400gProfile(tcvrState, mediaInterfaces);
      break;

    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER:
      verifyCopper100gProfile(tcvrState, mediaInterfaces);
      break;

    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER:
      verifyCopper200gProfile(tcvrState, mediaInterfaces);
      break;
    case cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_COPPER:
      verifyCopper53gProfile(tcvrState, mediaInterfaces);
      break;

    case cfg::PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL:
      verifyOpticalProfileForSpeed(
          mgmtInterface, mediaInterfaces, cfg::PortSpeed::EIGHTHUNDREDG);
      break;
    case cfg::PortProfileID::PROFILE_1600G_8_PAM4_RS544X2N_OPTICAL:
      verifyOpticalProfileForSpeed(
          mgmtInterface, mediaInterfaces, cfg::PortSpeed::ONEPOINTSIXT);
      break;
    case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_COPPER:
      verifyCopper800gProfile(tcvrState, mediaInterfaces);
      break;
    case cfg::PortProfileID::PROFILE_1600G_8_PAM4_RS544X2N_COPPER:
      verifyCopper1600gProfile(tcvrState, mediaInterfaces);
      break;
    default:
      throw FbossError(
          "Unhandled profile ", apache::thrift::util::enumNameSafe(profile));
  }
}

void HwTransceiverUtils::verify10gProfile(
    const TcvrState& tcvrState,
    const TransceiverManagementInterface mgmtInterface,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  EXPECT_EQ(mgmtInterface, TransceiverManagementInterface::SFF8472);
  EXPECT_EQ(*tcvrState.transceiver(), TransceiverType::SFP);

  for (const auto& mediaId : mediaInterfaces) {
    EXPECT_TRUE(
        *mediaId.media()->ethernet10GComplianceCode() ==
            Ethernet10GComplianceCode::LR_10G ||
        *mediaId.media()->ethernet10GComplianceCode() ==
            Ethernet10GComplianceCode::CR_10G);

    EXPECT_TRUE(
        *mediaId.code() == MediaInterfaceCode::LR_10G ||
        *mediaId.code() == MediaInterfaceCode::CR_10G);
  }
}

void HwTransceiverUtils::verify25gProfile(
    const TransceiverManagementInterface mgmtInterface,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  for (const auto& mediaId : mediaInterfaces) {
    EXPECT_EQ(mgmtInterface, TransceiverManagementInterface::SFF);
    auto specComplianceCode =
        *mediaId.media()->extendedSpecificationComplianceCode();
    EXPECT_EQ(specComplianceCode, ExtendedSpecComplianceCode::CWDM4_100G);
  }
}

void HwTransceiverUtils::verify50gProfile(
    const TransceiverManagementInterface mgmtInterface,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  for (const auto& mediaId : mediaInterfaces) {
    EXPECT_EQ(mgmtInterface, TransceiverManagementInterface::SFF);
    auto specComplianceCode =
        *mediaId.media()->extendedSpecificationComplianceCode();
    EXPECT_EQ(specComplianceCode, ExtendedSpecComplianceCode::CWDM4_100G);
  }
}

void HwTransceiverUtils::verify100gProfile(
    const TransceiverManagementInterface mgmtInterface,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  for (const auto& mediaId : mediaInterfaces) {
    if (mgmtInterface == TransceiverManagementInterface::SFF) {
      auto specComplianceCode =
          *mediaId.media()->extendedSpecificationComplianceCode();
      EXPECT_TRUE(
          specComplianceCode == ExtendedSpecComplianceCode::CWDM4_100G ||
          specComplianceCode == ExtendedSpecComplianceCode::FR1_100G ||
          specComplianceCode == ExtendedSpecComplianceCode::CR4_100G);
      EXPECT_TRUE(
          mediaId.code() == MediaInterfaceCode::CWDM4_100G ||
          mediaId.code() == MediaInterfaceCode::FR1_100G ||
          mediaId.code() == MediaInterfaceCode::CR4_100G);
    } else if (mgmtInterface == TransceiverManagementInterface::CMIS) {
      EXPECT_TRUE(
          *mediaId.media()->smfCode() == SMFMediaInterfaceCode::CWDM4_100G ||
          *mediaId.media()->smfCode() == SMFMediaInterfaceCode::FR1_100G ||
          *mediaId.media()->smfCode() == SMFMediaInterfaceCode::DR1_100G);
      EXPECT_TRUE(
          *mediaId.code() == MediaInterfaceCode::CWDM4_100G ||
          *mediaId.code() == MediaInterfaceCode::FR1_100G ||
          *mediaId.code() == MediaInterfaceCode::DR1_100G);
    }
  }
}

void HwTransceiverUtils::verify200gProfile(
    const TransceiverManagementInterface mgmtInterface,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  EXPECT_EQ(mgmtInterface, TransceiverManagementInterface::CMIS);

  for (const auto& mediaId : mediaInterfaces) {
    EXPECT_TRUE(
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::FR4_200G ||
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::LR4_200G ||
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::DR2_200G ||
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::DR1_200G ||
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::FR1_200G ||
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::FR2_200G ||
        // 800G ZR in 3x200G mode
        *mediaId.media()->smfCode() ==
            SMFMediaInterfaceCode::ZR_OROADM_FLEXO_6E_DPO_600G);
    EXPECT_TRUE(
        *mediaId.code() == MediaInterfaceCode::FR4_200G ||
        *mediaId.code() == MediaInterfaceCode::LR4_200G ||
        *mediaId.code() == MediaInterfaceCode::DR2_200G ||
        *mediaId.code() == MediaInterfaceCode::DR1_200G ||
        *mediaId.code() == MediaInterfaceCode::FR1_200G ||
        *mediaId.code() == MediaInterfaceCode::FR2_200G ||
        *mediaId.code() == MediaInterfaceCode::ZR_800G);
  }
}

void HwTransceiverUtils::verify400gProfile(
    const TransceiverManagementInterface mgmtInterface,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  EXPECT_EQ(mgmtInterface, TransceiverManagementInterface::CMIS);

  for (const auto& mediaId : mediaInterfaces) {
    EXPECT_TRUE(
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::FR4_400G ||
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::LR4_10_400G ||
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::DR4_400G ||
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::DR2_400G ||
        *mediaId.media()->smfCode() == SMFMediaInterfaceCode::FR2_400G);
    EXPECT_TRUE(
        *mediaId.code() == MediaInterfaceCode::FR4_400G ||
        *mediaId.code() == MediaInterfaceCode::LR4_400G_10KM ||
        *mediaId.code() == MediaInterfaceCode::DR4_400G ||
        *mediaId.code() == MediaInterfaceCode::DR2_400G ||
        *mediaId.code() == MediaInterfaceCode::FR2_400G);
  }
}

void HwTransceiverUtils::verifyCopper400gProfile(
    const TcvrState& tcvrState,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  const auto& cable = *tcvrState.cable();
  const auto& transmitterTech = *cable.transmitterTech();
  const bool isActive = TransceiverManager::activeCable(tcvrState);
  auto mgmtInterface =
      apache::thrift::can_throw(*tcvrState.transceiverManagementInterface());

  EXPECT_EQ(mgmtInterface, TransceiverManagementInterface::CMIS);
  EXPECT_EQ(TransmitterTechnology::COPPER, transmitterTech);

  for (const auto& mediaId : mediaInterfaces) {
    if (isActive) {
      EXPECT_TRUE(*mediaId.code() == MediaInterfaceCode::CR4_400G);
      EXPECT_TRUE(
          *mediaId.media()->activeCuCode() ==
          ActiveCuHostInterfaceCode::AUI_PAM4_4S_400G);
    } else {
      EXPECT_TRUE(*mediaId.code() == MediaInterfaceCode::CR8_400G);
    }
  }
}

void HwTransceiverUtils::verifyCopper100gProfile(
    const TcvrState& tcvrState,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  EXPECT_EQ(
      TransmitterTechnology::COPPER,
      *(tcvrState.cable().value_or({}).transmitterTech()));

  const bool isActive = TransceiverManager::activeCable(tcvrState);
  if (!isActive) {
  }
  for (const auto& mediaId : mediaInterfaces) {
    if (isActive) {
      EXPECT_TRUE(
          *mediaId.media()->activeCuCode() ==
          ActiveCuHostInterfaceCode::AUI_PAM4_1S_100G);
    } else {
      EXPECT_TRUE(*mediaId.code() == MediaInterfaceCode::CR4_100G);
    }
  }
}

void HwTransceiverUtils::verifyCopper200gProfile(
    const TcvrState& tcvrState,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  EXPECT_EQ(
      TransmitterTechnology::COPPER,
      *(tcvrState.cable().value_or({}).transmitterTech()));

  for (const auto& mediaId : mediaInterfaces) {
    EXPECT_TRUE(*mediaId.code() == MediaInterfaceCode::CR4_200G);
  }
}

void HwTransceiverUtils::verifyCopper53gProfile(
    const TcvrState& tcvrState,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  EXPECT_EQ(
      TransmitterTechnology::COPPER,
      *(tcvrState.cable().value_or({}).transmitterTech()));

  for (const auto& mediaId : mediaInterfaces) {
    EXPECT_TRUE(
        *mediaId.code() == MediaInterfaceCode::CR4_200G ||
        *mediaId.code() == MediaInterfaceCode::CR8_400G);
  }
}

void HwTransceiverUtils::verifyOpticalProfileForSpeed(
    const TransceiverManagementInterface mgmtInterface,
    const std::vector<MediaInterfaceId>& mediaInterfaces,
    cfg::PortSpeed speed) {
  EXPECT_EQ(mgmtInterface, TransceiverManagementInterface::CMIS);

  // Collect valid SMF codes for this speed from config
  std::set<SMFMediaInterfaceCode> validSmfCodes;
  for (auto code : TransceiverPropertiesManager::getKnownCodes()) {
    auto codes = TransceiverPropertiesManager::getMediaCodesForSpeed<
        SMFMediaInterfaceCode>(code, speed);
    validSmfCodes.insert(codes.begin(), codes.end());
  }

  for (const auto& mediaId : mediaInterfaces) {
    EXPECT_NE(
        validSmfCodes.find(*mediaId.media()->smfCode()), validSmfCodes.end())
        << "Unexpected smfCode: "
        << apache::thrift::util::enumNameSafe(*mediaId.media()->smfCode());
  }
}

void HwTransceiverUtils::verifyCopper800gProfile(
    const TcvrState& tcvrState,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  const auto& cable = *tcvrState.cable();
  const auto& transmitterTech = *cable.transmitterTech();
  const bool isActive = TransceiverManager::activeCable(tcvrState);
  auto mgmtInterface =
      apache::thrift::can_throw(*tcvrState.transceiverManagementInterface());

  EXPECT_EQ(mgmtInterface, TransceiverManagementInterface::CMIS);
  EXPECT_EQ(TransmitterTechnology::COPPER, transmitterTech);

  for (const auto& mediaId : mediaInterfaces) {
    EXPECT_TRUE(*mediaId.code() == MediaInterfaceCode::CR8_800G);
    if (isActive) {
      EXPECT_TRUE(
          *mediaId.media()->activeCuCode() ==
          ActiveCuHostInterfaceCode::AUI_PAM4_8S_800G);
    }
  }
}

bool HwTransceiverUtils::isAoc(const TcvrState& tcvrState) {
  return TransceiverManager::activeCable(tcvrState) &&
      *tcvrState.cable()->transmitterTech() == TransmitterTechnology::OPTICAL;
}

void HwTransceiverUtils::verifyActiveOpticalCableProfile(
    const TransceiverManagementInterface mgmtInterface,
    const std::vector<MediaInterfaceId>& mediaInterfaces,
    cfg::PortProfileID profile) {
  EXPECT_EQ(mgmtInterface, TransceiverManagementInterface::CMIS);

  const auto& speedApplications = CmisHelper::getActiveSpeedApplication();
  auto expectedCodesIt = speedApplications.find(utility::getSpeed(profile));
  if (expectedCodesIt == speedApplications.end()) {
    throw FbossError(
        "No active cable application for profile ",
        apache::thrift::util::enumNameSafe(profile));
  }
  const auto& expectedCodes = expectedCodesIt->second;
  const auto& activeMediaMap = CmisHelper::getActiveMediaInterfaceMapping();

  for (const auto& mediaId : mediaInterfaces) {
    auto activeCuCode = *mediaId.media()->activeCuCode();
    EXPECT_TRUE(
        std::find(expectedCodes.begin(), expectedCodes.end(), activeCuCode) !=
        expectedCodes.end())
        << "Unexpected activeCuCode "
        << apache::thrift::util::enumNameSafe(activeCuCode) << " for profile "
        << apache::thrift::util::enumNameSafe(profile);
    auto mediaCodeIt = activeMediaMap.find(activeCuCode);
    ASSERT_TRUE(mediaCodeIt != activeMediaMap.end());
    EXPECT_EQ(*mediaId.code(), mediaCodeIt->second);
  }
}

void HwTransceiverUtils::verifyCopper1600gProfile(
    const TcvrState& tcvrState,
    const std::vector<MediaInterfaceId>& mediaInterfaces) {
  const auto& cable = *tcvrState.cable();
  const auto& transmitterTech = *cable.transmitterTech();
  auto mgmtInterface =
      apache::thrift::can_throw(*tcvrState.transceiverManagementInterface());

  EXPECT_EQ(mgmtInterface, TransceiverManagementInterface::CMIS);
  EXPECT_EQ(TransmitterTechnology::COPPER, transmitterTech);
  // 1.6T over copper only exists as an active cable: a passive DAC carries no
  // 200G/lane SerDes on any of our platforms.
  EXPECT_TRUE(TransceiverManager::activeCable(tcvrState));

  for (const auto& mediaId : mediaInterfaces) {
    EXPECT_EQ(*mediaId.code(), MediaInterfaceCode::CR8_1600G);
    EXPECT_EQ(
        *mediaId.media()->activeCuCode(),
        ActiveCuHostInterfaceCode::AUI_PAM4_8_1600G);
  }
}

void HwTransceiverUtils::verifyDataPathEnabled(
    const TcvrState& tcvrState,
    const std::string& portName) {
  auto mgmtInterface = tcvrState.transceiverManagementInterface();

  const auto& portToHostMap = *tcvrState.portNameToHostLanes();
  CHECK(portToHostMap.find(portName) != portToHostMap.end());
  const auto& relevantHostLanes = portToHostMap.at(portName);
  EXPECT_GT(relevantHostLanes.size(), 0);

  if (!mgmtInterface) {
    throw FbossError(
        "Transceiver:",
        *tcvrState.port(),
        " is missing transceiverManagementInterface");
  }
  // Right now, we only reset data path for CMIS module
  if (*mgmtInterface == TransceiverManagementInterface::CMIS) {
    // All lanes should have `false` Deinit
    if (auto hostLaneSignals = tcvrState.hostLaneSignals()) {
      for (const auto& laneSignals : *hostLaneSignals) {
        if (std::find(
                relevantHostLanes.begin(),
                relevantHostLanes.end(),
                *laneSignals.lane()) == relevantHostLanes.end()) {
          continue;
        }
        EXPECT_TRUE(laneSignals.dataPathDeInit().has_value())
            << "Transceiver:" << *tcvrState.port()
            << ", Lane=" << *laneSignals.lane()
            << " is missing dataPathDeInit signal";
        EXPECT_FALSE(*laneSignals.dataPathDeInit())
            << "Transceiver:" << *tcvrState.port()
            << ", Lane=" << *laneSignals.lane()
            << " dataPathDeInit doesn't match expected";
      }
    } else {
      throw FbossError(
          "Transceiver:", *tcvrState.port(), " is missing hostLaneSignals");
    }
  }
}

void HwTransceiverUtils::verifyDiagsCapability(
    const TcvrState& tcvrState,
    std::optional<DiagsCapability> diagsCapability,
    bool skipCheckingIndividualCapability) {
  auto mgmtInterface = tcvrState.transceiverManagementInterface();
  if (!mgmtInterface) {
    throw FbossError(
        "Transceiver:",
        *tcvrState.port(),
        " is missing transceiverManagementInterface");
  }
  auto mediaIntfCode = tcvrState.moduleMediaInterface();
  if (!mediaIntfCode) {
    throw FbossError(
        "Transceiver:", *tcvrState.port(), " is missing moduleMediaInterface");
  }
  XLOG(INFO) << "Verifying DiagsCapability for Transceiver="
             << *tcvrState.port() << ", mgmtInterface="
             << apache::thrift::util::enumNameSafe(*mgmtInterface)
             << ", moduleMediaInterface="
             << apache::thrift::util::enumNameSafe(*mediaIntfCode);

  switch (*mgmtInterface) {
    case TransceiverManagementInterface::CMIS: {
      if (!TransceiverManager::opticalOrActiveCable(tcvrState)) {
        // FlatMem modules don't support diagsCapability
        return;
      }
      EXPECT_TRUE(diagsCapability.has_value());
      if (!skipCheckingIndividualCapability) {
        EXPECT_TRUE(*diagsCapability->diagnostics());
        // Use config to determine VDM support.
        if (TransceiverPropertiesManager::isKnown(*mediaIntfCode)) {
          EXPECT_EQ(
              *diagsCapability->vdm(),
              !TransceiverPropertiesManager::getDoesNotSupportVdm(
                  *mediaIntfCode));
        }
        EXPECT_TRUE(*diagsCapability->cdb());
        EXPECT_TRUE(*diagsCapability->prbsLine());
        EXPECT_TRUE(*diagsCapability->prbsSystem());
        EXPECT_TRUE(*diagsCapability->loopbackLine());
        EXPECT_TRUE(*diagsCapability->loopbackSystem());
        EXPECT_TRUE(*diagsCapability->txOutputControl());
        if (TransceiverPropertiesManager::isKnown(*mediaIntfCode)) {
          if (!TransceiverPropertiesManager::getDoesNotSupportRxOutputControl(
                  *mediaIntfCode)) {
            EXPECT_TRUE(*diagsCapability->rxOutputControl());
          }
        }
      }
      return;
    }
    case TransceiverManagementInterface::SFF:
      // Only FR1_100G has diagsCapability
      if (*mediaIntfCode == MediaInterfaceCode::FR1_100G) {
        EXPECT_TRUE(diagsCapability.has_value());
        if (!skipCheckingIndividualCapability) {
          EXPECT_TRUE(*diagsCapability->prbsLine());
          EXPECT_TRUE(*diagsCapability->prbsSystem());
          EXPECT_TRUE(*diagsCapability->txOutputControl());
        }
      }
      return;
    case TransceiverManagementInterface::SFF8472:
    case TransceiverManagementInterface::NONE:
    case TransceiverManagementInterface::UNKNOWN:
      EXPECT_FALSE(diagsCapability.has_value());
      return;
  }
  throw FbossError(
      "Transceiver:",
      *tcvrState.port(),
      " is using unrecognized transceiverManagementInterface:",
      apache::thrift::util::enumNameSafe(*mgmtInterface));
}

void HwTransceiverUtils::verifyDatapathResetTimestamp(
    const std::string& portName,
    const TcvrState& tcvrState,
    const TcvrStats& tcvrStats,
    time_t timeReference,
    bool expectedReset) {
  if (TransceiverManager::opticalOrActiveCmisCable(tcvrState)) {
    auto& datapathResetTimestamp = *tcvrStats.lastDatapathResetTime();
    if (expectedReset) {
      ASSERT_TRUE(
          datapathResetTimestamp.find(portName) !=
          datapathResetTimestamp.end());
      EXPECT_GT(datapathResetTimestamp.at(portName), timeReference);
    } else {
      if (datapathResetTimestamp.find(portName) !=
          datapathResetTimestamp.end()) {
        EXPECT_LE(datapathResetTimestamp.at(portName), timeReference);
      }
    }
  }
}

} // namespace facebook::fboss::utility
