/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/bcm/BcmPortUtils.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/bcm/BcmError.h"

#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

namespace {
constexpr int kPfcDeadlockDetectionTimerLimit = 15;
constexpr int kPfcDeadlockRecoveryTimerLimit = 15;
constexpr int kDefaultTh3PfcDeadlockRecoveryTimer = 0;
constexpr int kDefaultTh3PfcDeadlockDetectionTimer = 0;
constexpr int kDefaultTh4PfcDeadlockRecoveryTimer = 100;
constexpr int kDefaultTh4PfcDeadlockDetectionTimer = 1;
} // namespace

namespace facebook::fboss {

const PortSpeed2TransmitterTechAndMode& getSpeedToTransmitterTechAndMode() {
  // This allows mapping from a speed and port transmission technology
  // to a broadcom supported interface
  static const PortSpeed2TransmitterTechAndMode kPortTypeMapping = {
      {cfg::PortSpeed::HUNDREDG,
       {{TransmitterTechnology::COPPER, BCM_PORT_IF_CR4},
        {TransmitterTechnology::OPTICAL, BCM_PORT_IF_CAUI},
        // What to default to
        {TransmitterTechnology::UNKNOWN, BCM_PORT_IF_CAUI}}},
      {cfg::PortSpeed::FIFTYG,
       {{TransmitterTechnology::COPPER, BCM_PORT_IF_CR2},
        {TransmitterTechnology::OPTICAL, BCM_PORT_IF_CAUI},
        // What to default to
        {TransmitterTechnology::UNKNOWN, BCM_PORT_IF_CR2}}},
      {cfg::PortSpeed::FORTYG,
       {{TransmitterTechnology::COPPER, BCM_PORT_IF_CR4},
        {TransmitterTechnology::OPTICAL, BCM_PORT_IF_XLAUI},
        // What to default to
        {TransmitterTechnology::UNKNOWN, BCM_PORT_IF_XLAUI}}},
      {cfg::PortSpeed::TWENTYFIVEG,
       {{TransmitterTechnology::COPPER, BCM_PORT_IF_CR},
        {TransmitterTechnology::OPTICAL, BCM_PORT_IF_CAUI},
        // What to default to
        {TransmitterTechnology::UNKNOWN, BCM_PORT_IF_CR}}},
      {cfg::PortSpeed::TWENTYG,
       {{TransmitterTechnology::COPPER, BCM_PORT_IF_CR},
        // We don't expect 20G optics
        // What to default to
        {TransmitterTechnology::UNKNOWN, BCM_PORT_IF_CR}}},
      {cfg::PortSpeed::XG,
       {{TransmitterTechnology::COPPER, BCM_PORT_IF_CR},
        {TransmitterTechnology::OPTICAL, BCM_PORT_IF_SFI},
        // What to default to
        {TransmitterTechnology::UNKNOWN, BCM_PORT_IF_CR}}},
      {cfg::PortSpeed::GIGE,
       {{TransmitterTechnology::COPPER, BCM_PORT_IF_GMII},
        // We don't expect 1G optics
        // What to default to
        {TransmitterTechnology::UNKNOWN, BCM_PORT_IF_GMII}}}};
  return kPortTypeMapping;
}

uint32_t getDesiredPhyLaneConfig(
    TransmitterTechnology tech,
    cfg::PortSpeed speed) {
  // see shared/port.h for what these various shifts mean. I elide
  // them here simply because they are very verbose.
  if (tech == TransmitterTechnology::BACKPLANE) {
    if (speed == cfg::PortSpeed::FORTYG) {
      // DFE + BACKPLANE + NRZ
      return 0x8004;
    } else if (speed == cfg::PortSpeed::HUNDREDG) {
      // DFE + BACKPLANE + PAM4 + NS
      return 0x5004;
    }
  } else if (tech == TransmitterTechnology::COPPER) {
    if (speed == cfg::PortSpeed::FORTYG) {
      // DFE + COPPER + NRZ
      return 0x8024;
    } else if (speed == cfg::PortSpeed::HUNDREDG) {
      // DFE + COPPER + PAM4 + NS
      return 0x5024;
    }
  }
  // TODO: add more mappings + better error message
  throw std::runtime_error("Unsupported tech+speed in port_resource");
}

} // namespace facebook::fboss

namespace facebook::fboss::utility {

bcm_port_phy_fec_t phyFecModeToBcmPortPhyFec(phy::FecMode fec) {
  switch (fec) {
    case phy::FecMode::NONE:
      return bcmPortPhyFecNone;
    case phy::FecMode::CL74:
      return bcmPortPhyFecBaseR;
    case phy::FecMode::CL91:
      return bcmPortPhyFecRsFec;
    case phy::FecMode::RS528:
      return bcmPortPhyFecRsFec;
    case phy::FecMode::RS544:
      return bcmPortPhyFecRs544;
    case phy::FecMode::RS544_2N:
      return bcmPortPhyFecRs544_2xN;
    case phy::FecMode::RS545:
      return bcmPortPhyFecRs545;
  };
  throw facebook::fboss::FbossError(
      "Unsupported fec type: ", apache::thrift::util::enumNameSafe(fec));
}

phy::FecMode bcmPortPhyFecToPhyFecMode(bcm_port_phy_fec_t fec) {
  switch (fec) {
    case bcmPortPhyFecNone:
      return phy::FecMode::NONE;
    case bcmPortPhyFecBaseR:
      return phy::FecMode::CL74;
    case bcmPortPhyFecRsFec:
      return phy::FecMode::RS528;
    case bcmPortPhyFecRs544:
      return phy::FecMode::RS544;
    case bcmPortPhyFecRs544_2xN:
      return phy::FecMode::RS544_2N;
    default:
      throw facebook::fboss::FbossError("Unsupported fec type: ", fec);
  };
}

uint32_t getDesiredPhyLaneConfig(const phy::ProfileSideConfig& profileCfg) {
  uint32_t laneConfig = 0;

  // PortResource setting needs interface mode from profile config
  uint32_t medium = 0;
  if (auto interfaceMode = profileCfg.interfaceMode()) {
    switch (*interfaceMode) {
      case phy::InterfaceMode::KR:
      case phy::InterfaceMode::KR2:
      case phy::InterfaceMode::KR4:
      case phy::InterfaceMode::KR8:
      case phy::InterfaceMode::CAUI4_C2C:
      case phy::InterfaceMode::CAUI4_C2M:
        medium = BCM_PORT_RESOURCE_PHY_LANE_CONFIG_MEDIUM_BACKPLANE;
        break;
      case phy::InterfaceMode::CR:
      case phy::InterfaceMode::CR2:
      case phy::InterfaceMode::CR4:
        medium = BCM_PORT_RESOURCE_PHY_LANE_CONFIG_MEDIUM_COPPER_CABLE;
        break;
      case phy::InterfaceMode::SR:
      case phy::InterfaceMode::SR4:
      case phy::InterfaceMode::XLAUI:
      case phy::InterfaceMode::SFI:
        medium = BCM_PORT_RESOURCE_PHY_LANE_CONFIG_MEDIUM_OPTICS;
        break;
      default:
        throw facebook::fboss::FbossError(
            "Unsupported interface mode: ",
            apache::thrift::util::enumNameSafe(*interfaceMode));
    }
  } else {
    throw facebook::fboss::FbossError("No iphy interface mode in profileCfg");
  }
  BCM_PORT_RESOURCE_PHY_LANE_CONFIG_MEDIUM_SET(laneConfig, medium);

  switch (folly::copy(profileCfg.modulation().value())) {
    case phy::IpModulation::PAM4:
      // PAM4 + NS
      BCM_PORT_RESOURCE_PHY_LANE_CONFIG_FORCE_PAM4_SET(laneConfig);
      BCM_PORT_RESOURCE_PHY_LANE_CONFIG_FORCE_NS_SET(laneConfig);
      break;
    case phy::IpModulation::NRZ:
      // NRZ
      BCM_PORT_RESOURCE_PHY_LANE_CONFIG_FORCE_NRZ_SET(laneConfig);
      break;
  };

  // always enable DFE
  BCM_PORT_RESOURCE_PHY_LANE_CONFIG_DFE_SET(laneConfig);

  return laneConfig;
}

bcm_gport_t getPortGport(int unit, int port) {
  bcm_gport_t portGport;
  auto rv = bcm_port_gport_get(unit, port, &portGport);
  facebook::fboss::bcmCheckError(rv, "failed to get gport for port");
  return portGport;
}

int getPortItm(utility::BcmChip chip, BcmPort* bcmPort) {
  auto port = bcmPort->getBcmPortId();
  auto pipe = bcmPort->determinePipe();

  int itm = -1;
  switch (chip) {
    case utility::BcmChip::TOMAHAWK3:
      /*
       * TH3 ports belong to one of the 8 pipes, and these
       * pipes are split equally between the 2 ITMs.
       */
      if (pipe == 0 || pipe == 1 || pipe == 6 || pipe == 7) {
        itm = 0;
      } else if (pipe >= 2 && pipe <= 5) {
        itm = 1;
      } else {
        throw FbossError(
            "Port ", port, " pipe ", pipe, " not associated w/ any ITM");
      }
      break;
    case utility::BcmChip::TOMAHAWK4:
      if (pipe == 0 || pipe == 1 || pipe == 2 || pipe == 3 || pipe == 12 ||
          pipe == 13 || pipe == 14 || pipe == 15) {
        itm = 0;
      } else if (pipe >= 4 && pipe <= 11) {
        itm = 1;
      } else {
        throw FbossError(
            "Port ", port, " pipe ", pipe, " not associated w/ any ITM");
      }
      break;
    default:
      throw FbossError(
          "Unsupported platform ", chip, " for retrieving ITM for port");
  }
  return itm;
}

bcm_port_loopback_t fbToBcmLoopbackMode(cfg::PortLoopbackMode inMode) {
  switch (inMode) {
    case cfg::PortLoopbackMode::NONE:
      return BCM_PORT_LOOPBACK_NONE;
    case cfg::PortLoopbackMode::PHY:
      return BCM_PORT_LOOPBACK_PHY;
    case cfg::PortLoopbackMode::MAC:
      return BCM_PORT_LOOPBACK_MAC;
    case cfg::PortLoopbackMode::NIF:
      throw FbossError("Nif loopback is unsupported for native BCM");
  }
  CHECK(0) << "Should never reach here";
}

int getBcmPfcDeadlockDetectionTimerGranularity(int deadlockDetectionTimeMsec) {
  /*
   * BCM can configure a value 0-15 with a granularity of 1msec,
   * 10msec, 100msec as the PfcDeadlockDetectionTime, selecting
   * the granularity based on the attempted configuration.
   */
  int granularity{};

  if (deadlockDetectionTimeMsec <= kPfcDeadlockDetectionTimerLimit) {
    granularity = bcmCosqPFCDeadlockTimerInterval1MiliSecond;
  } else if (
      deadlockDetectionTimeMsec / 10 <= kPfcDeadlockDetectionTimerLimit) {
    granularity = bcmCosqPFCDeadlockTimerInterval10MiliSecond;
  } else {
    granularity = bcmCosqPFCDeadlockTimerInterval100MiliSecond;
  }
  return granularity;
}

int getAdjustedPfcDeadlockDetectionTimerValue(int deadlockDetectionTimeMsec) {
  /*
   * BCM supports a deadlock detection timer granularity of 1msec,
   * 10msec and 100msec for the DeadlockDetectionTime. With a
   * value 0-15 possible in each granularity, the possible values
   * for the timer are:
   * 1msec  : 0, 1, 2, ... 15 msec
   * 10msec : 0, 10, 20 ... 150 msec
   * 100msec: 0, 100, 200 ... 1500 msec
   */
  int adjustedDeadlockDetectionTimer{};

  if (deadlockDetectionTimeMsec <= kPfcDeadlockDetectionTimerLimit) {
    // 0 <= timer <= 15
    adjustedDeadlockDetectionTimer = deadlockDetectionTimeMsec;
  } else if (
      deadlockDetectionTimeMsec / 10 <= kPfcDeadlockDetectionTimerLimit) {
    // 0 <= timer <= 159
    adjustedDeadlockDetectionTimer = 10 * (deadlockDetectionTimeMsec / 10);
  } else if (
      deadlockDetectionTimeMsec / 100 <= kPfcDeadlockDetectionTimerLimit) {
    // 0 <= timer <= 1599
    adjustedDeadlockDetectionTimer = 100 * (deadlockDetectionTimeMsec / 100);
  } else {
    // timer > 1599
    adjustedDeadlockDetectionTimer = 100 * kPfcDeadlockDetectionTimerLimit;
  }
  return adjustedDeadlockDetectionTimer;
}

int getAdjustedPfcDeadlockRecoveryTimerValue(
    cfg::AsicType type,
    int timerMsec) {
  if (type == cfg::AsicType::ASIC_TYPE_TOMAHAWK4) {
    // should be in multiples of 100ms, min 100ms, max 1500ms
    int adjustedDeadlockRecoveryTimer{0};
    if (timerMsec / 100 == 0) {
      adjustedDeadlockRecoveryTimer = 100;
    } else if (timerMsec / 100 <= kPfcDeadlockRecoveryTimerLimit) {
      adjustedDeadlockRecoveryTimer = 100 * (timerMsec / 100);
    } else {
      adjustedDeadlockRecoveryTimer = 100 * kPfcDeadlockRecoveryTimerLimit;
    }
    return adjustedDeadlockRecoveryTimer;
  }
  // No adjustment neeeded for TH3
  return timerMsec;
}

int getDefaultPfcDeadlockDetectionTimer(cfg::AsicType type) {
  if (type == cfg::AsicType::ASIC_TYPE_TOMAHAWK4) {
    return kDefaultTh4PfcDeadlockDetectionTimer;
  } else if (type == cfg::AsicType::ASIC_TYPE_TOMAHAWK3) {
    return kDefaultTh3PfcDeadlockDetectionTimer;
  } else if (type == cfg::AsicType::ASIC_TYPE_FAKE) {
    return 0;
  }
  throw FbossError(
      "Platform type ", type, " does not support pfc watchdog detection");
}

int getDefaultPfcDeadlockRecoveryTimer(cfg::AsicType type) {
  if (type == cfg::AsicType::ASIC_TYPE_TOMAHAWK4) {
    return kDefaultTh4PfcDeadlockRecoveryTimer;
  } else if (type == cfg::AsicType::ASIC_TYPE_TOMAHAWK3) {
    return kDefaultTh3PfcDeadlockRecoveryTimer;
  } else if (type == cfg::AsicType::ASIC_TYPE_FAKE) {
    return 0;
  }
  throw FbossError(
      "Platform type ", type, " does not support pfc watchdog recovery");
}

} // namespace facebook::fboss::utility
