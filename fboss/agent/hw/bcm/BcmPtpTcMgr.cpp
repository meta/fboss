/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/bcm/BcmPtpTcMgr.h"
#include <folly/logging/xlog.h>
#include "fboss/agent/hw/bcm/BcmAddressFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmFieldProcessorUtils.h"
#include "fboss/agent/hw/bcm/BcmPlatform.h"
#include "fboss/agent/hw/bcm/BcmPortTable.h"

extern "C" {
#include <bcm/switch.h>
#include <bcm/time.h>
}

namespace facebook::fboss {

// static
int BcmPtpTcMgr::getTsBitModeArg(cfg::AsicType asicType) {
  // TODO(rajukumarfb5368): integrate with HwAsic::isSupported() or a variant
  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK:
      return bcmTimesyncTimestampingMode32bit;
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK3:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
      return bcmTimesyncTimestampingMode48bit;
    default:
      throw FbossError(
          "Device type ",
          asicType,
          " not supported yet, please only use TH or TH3");
  }
}

void BcmPtpTcMgr::enablePortTimesyncConfig(
    bcm_port_timesync_config_t* port_timesync_config) {
  /* Set port timesync configuration struct flags to 1-step TC and mandatory
   * update of all packets' correction field (CF) */
  port_timesync_config->flags |= BCM_PORT_TIMESYNC_DEFAULT;
  port_timesync_config->flags |=
      BCM_PORT_TIMESYNC_ONE_STEP_TIMESTAMP; /* Set to OSTS - One-Step
                                               TimeStamping */
  port_timesync_config->flags |=
      BCM_PORT_TIMESYNC_TIMESTAMP_CFUPDATE_ALL; /* Always update correction
                                                   field */
}

void BcmPtpTcMgr::enablePcsBasedOneStepTimestamping(bcm_port_t port) {
  const auto unit = hw_->getUnit();

  // TODO(rajukumarfb5368): Use bcm_port_control_phy_timesync_set() in SDK
  // > 6.5.18 to configure timestamp offset and adjust. Ref:
  // https://www.internalfb.com/diff/D24411684 (Base vs V11)

  bcm_port_phy_timesync_config_t phy;
  bcm_port_phy_timesync_config_t_init(&phy);
  phy.flags = BCM_PORT_PHY_TIMESYNC_ENABLE; /* Enable PHY based timesync */
  phy.flags |= BCM_PORT_PHY_TIMESYNC_ONE_STEP_ENABLE; /* Set PHY timesync to
                                                         one-step TS */

  BCM_CHECK_ERROR(
      bcm_port_phy_timesync_config_set(unit, port, &phy),
      "failed to enable TC on port ",
      port);
}

void BcmPtpTcMgr::disablePcsBasedOneStepTimestamping(bcm_port_t port) {
  const auto unit = hw_->getUnit();

  bcm_port_phy_timesync_config_t phy;
  bcm_port_phy_timesync_config_t_init(&phy);

  BCM_CHECK_ERROR(
      bcm_port_phy_timesync_config_set(unit, port, &phy),
      "failed to disable TC on port ",
      port);
}

// static
bool BcmPtpTcMgr::isPcsBasedOneStepTimestampingEnabled(
    const BcmSwitchIf* hw,
    bcm_port_t port) {
  const auto unit = hw->getUnit();

  bcm_port_phy_timesync_config_t phy;
  bcm_port_phy_timesync_config_t_init(&phy);

  BCM_CHECK_ERROR(
      bcm_port_phy_timesync_config_get(unit, port, &phy),
      "failed to get phy timesync config on port ",
      port);

  return (phy.flags & BCM_PORT_PHY_TIMESYNC_ENABLE) &&
      (phy.flags & BCM_PORT_PHY_TIMESYNC_ONE_STEP_ENABLE);
}

void BcmPtpTcMgr::enableTimeInterface() {
  bcm_time_interface_t time_interface; /* device time (abstract) interface
                                          configuration struct */
  bcm_time_interface_t_init(&time_interface);
  time_interface.flags = BCM_TIME_ENABLE;
  bcm_time_interface_add(hw_->getUnit(), &time_interface);
}

void BcmPtpTcMgr::disableTimeInterface() {
  bcm_time_interface_delete_all(hw_->getUnit());
}

// static
bool BcmPtpTcMgr::isTimeInterfaceEnabled(const BcmSwitchIf* hw) {
  bcm_time_interface_t time_interface;
  bcm_time_interface_t_init(&time_interface);

  const auto unit = hw->getUnit();
  bcm_time_interface_get(unit, &time_interface);

  return time_interface.flags & BCM_TIME_ENABLE;
}

// static
bool BcmPtpTcMgr::isPtpTcSupported(const BcmSwitchIf* hw) {
  return hw->getPlatform()->getAsic()->isSupported(HwAsic::Feature::PTP_TC);
}

// static
bool BcmPtpTcMgr::isPtpTcPcsSupported(const BcmSwitchIf* hw) {
  return hw->getPlatform()->getAsic()->isSupported(HwAsic::Feature::PTP_TC_PCS);
}

/* separate configuration for MAC based timestamping devices - TH, and
 * PCS based timestamping devices - TH3 */
void BcmPtpTcMgr::enablePtpTc() {
  if (!isPtpTcSupported(hw_)) {
    XLOG(DBG2) << "[PTP] Ignore configuration of unsupported feature";
    return;
  }

  XLOG(DBG2) << "[PTP] Start enable";

  const auto unit = hw_->getUnit();

  bcm_port_timesync_config_t port_timesync_config;
  bcm_port_timesync_config_t_init(&port_timesync_config);
  enablePortTimesyncConfig(&port_timesync_config);

  bcm_port_config_t portConfig;
  bcm_port_config_t_init(&portConfig);
  BCM_CHECK_ERROR(
      bcm_port_config_get(unit, &portConfig),
      "failed to get port configuration");

  int count = 0;
  bcm_port_t port;
  BCM_PBMP_ITER(portConfig.port, port) {
    if (isPtpTcPcsSupported(hw_)) {
      enablePcsBasedOneStepTimestamping(port);
    }

    // TODO(rajukumarfb5368): high level description of bcm_*() calls needed

    BCM_CHECK_ERROR(
        bcm_port_timesync_config_set(
            unit, port, 1 /* config_count */, &port_timesync_config),
        "Error in enabling TC correction for port ",
        port);

    static const int tsBitModeArg =
        getTsBitModeArg(hw_->getPlatform()->getAsic()->getAsicType());
    BCM_CHECK_ERROR(
        bcm_switch_control_port_set(
            unit, port, bcmSwitchTimesyncEgressTimestampingMode, tsBitModeArg),
        "Error in enabling TC correction for port ",
        port);

    count++;
  }

  enableTimeInterface();
  XLOG(DBG2) << "[PTP] Enabled timestamping on " << count << " ports";
}

void BcmPtpTcMgr::disablePtpTc() {
  if (!isPtpTcSupported(hw_)) {
    XLOG(DBG2) << "[PTP] Ignore configuration of unsupported feature";
    return;
  }

  XLOG(DBG2) << "[PTP] Start disable";

  const auto unit = hw_->getUnit();
  bcm_port_config_t portConfig;
  bcm_port_config_t_init(&portConfig);
  BCM_CHECK_ERROR(
      bcm_port_config_get(unit, &portConfig),
      "failed to get port configuration");

  int count = 0;
  bcm_port_t port;
  BCM_PBMP_ITER(portConfig.port, port) {
    if (isPtpTcPcsSupported(hw_)) {
      disablePcsBasedOneStepTimestamping(port);
    }

    BCM_CHECK_ERROR(
        bcm_port_timesync_config_set(unit, port, 0 /* config_count */, nullptr),
        "Error in disabling TC correction for port ",
        port);

    // Reset to whatever is default arg for the given asic/sdk
    // For TH4 its 48-bit
    static const int tsBitModeArg =
        getTsBitModeArg(hw_->getPlatform()->getAsic()->getAsicType());
    BCM_CHECK_ERROR(
        bcm_switch_control_port_set(
            unit, port, bcmSwitchTimesyncEgressTimestampingMode, tsBitModeArg),
        "Error in disabling TC egress timestamping mode for port ",
        port);

    count++;
  }

  disableTimeInterface();
  XLOG(DBG2) << "[PTP] Disabled timestamping on " << count << " ports";
}

// static
bool BcmPtpTcMgr::isPtpTcEnabled(const BcmSwitchIf* hw) {
  bcm_port_config_t portConfig;
  bcm_port_config_t_init(&portConfig);

  XLOG(DBG3) << "Check if PTP TC is enabled";

  const auto unit = hw->getUnit();
  BCM_CHECK_ERROR(
      bcm_port_config_get(unit, &portConfig),
      "failed to get port configuration");

  bcm_port_t port;
  BCM_PBMP_ITER(portConfig.port, port) {
    if (isPtpTcPcsSupported(hw)) {
      if (!isPcsBasedOneStepTimestampingEnabled(hw, port)) {
        return false;
      }
    }

    bcm_port_timesync_config_t port_timesync_config;
    bcm_port_timesync_config_t_init(&port_timesync_config);
    int array_count = -1;
    BCM_CHECK_ERROR(
        bcm_port_timesync_config_get(
            unit,
            port,
            1 /* array_size */,
            &port_timesync_config,
            &array_count),
        "failed to get port timesync configuration");
    if (array_count != 1) {
      return false;
    }
    if (!(port_timesync_config.flags & BCM_PORT_TIMESYNC_DEFAULT)) {
      return false;
    }
    if (!(port_timesync_config.flags & BCM_PORT_TIMESYNC_ONE_STEP_TIMESTAMP)) {
      return false;
    }
    if (!(port_timesync_config.flags &
          BCM_PORT_TIMESYNC_TIMESTAMP_CFUPDATE_ALL)) {
      return false;
    }

    int tsBitModeArgFound = -1;
    BCM_CHECK_ERROR(
        bcm_switch_control_port_get(
            unit,
            port,
            bcmSwitchTimesyncEgressTimestampingMode,
            &tsBitModeArgFound),
        "failed to get control port configuration");
    static const int tsBitModeArg =
        getTsBitModeArg(hw->getPlatform()->getAsic()->getAsicType());
    if (tsBitModeArg != tsBitModeArgFound) {
      return false;
    }
  }

  return isTimeInterfaceEnabled(hw);
}

} // namespace facebook::fboss
