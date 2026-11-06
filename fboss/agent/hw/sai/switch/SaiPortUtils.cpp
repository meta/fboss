/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiPortUtils.h"
#if defined(BRCM_SAI_SDK_DNX)
#include <experimental/saiportextensions.h>
#endif
#include "fboss/agent/FbossError.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss::utility {

sai_port_flow_control_mode_t getSaiPortPauseMode(cfg::PortPause pause) {
  if (*pause.tx() && *pause.rx()) {
    return SAI_PORT_FLOW_CONTROL_MODE_BOTH_ENABLE;
  } else if (*pause.tx()) {
    return SAI_PORT_FLOW_CONTROL_MODE_TX_ONLY;
  } else if (*pause.rx()) {
    return SAI_PORT_FLOW_CONTROL_MODE_RX_ONLY;
  } else {
    return SAI_PORT_FLOW_CONTROL_MODE_DISABLE;
  }
}

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
int getSaiPortLoopbackMode(cfg::PortLoopbackMode loopbackMode) {
  switch (loopbackMode) {
    case cfg::PortLoopbackMode::NONE:
      return SAI_PORT_LOOPBACK_MODE_NONE;
    case cfg::PortLoopbackMode::PHY:
      return SAI_PORT_LOOPBACK_MODE_PHY;
    case cfg::PortLoopbackMode::MAC:
      return SAI_PORT_LOOPBACK_MODE_MAC;
    case cfg::PortLoopbackMode::NIF:
#if defined(BRCM_SAI_SDK_DNX)
      // experimental and hence not added to _sai_port_loopback_mode_t
      return SAI_PORT_LOOPBACK_MODE_NIF;
#else
      break;
#endif
  }
  throw FbossError("Bad loopback mode: ", (int)loopbackMode);
}

cfg::PortLoopbackMode getCfgPortLoopbackMode(sai_port_loopback_mode_t mode) {
  switch (static_cast<int>(mode)) {
    case SAI_PORT_LOOPBACK_MODE_NONE:
      return cfg::PortLoopbackMode::NONE;
    case SAI_PORT_LOOPBACK_MODE_PHY:
      return cfg::PortLoopbackMode::PHY;
    case SAI_PORT_LOOPBACK_MODE_MAC:
      return cfg::PortLoopbackMode::MAC;
#if defined(BRCM_SAI_SDK_DNX)
    case SAI_PORT_LOOPBACK_MODE_NIF:
      return cfg::PortLoopbackMode::NIF;
#endif
  }
  throw FbossError("Bad sai_port_loopback mode: ", (int)mode);
}

#endif
sai_port_internal_loopback_mode_t getSaiPortInternalLoopbackMode(
    cfg::PortLoopbackMode loopbackMode) {
  switch (loopbackMode) {
    case cfg::PortLoopbackMode::NONE:
      return SAI_PORT_INTERNAL_LOOPBACK_MODE_NONE;
    case cfg::PortLoopbackMode::PHY:
      return SAI_PORT_INTERNAL_LOOPBACK_MODE_PHY;
    case cfg::PortLoopbackMode::MAC:
      return SAI_PORT_INTERNAL_LOOPBACK_MODE_MAC;
    default:
      return SAI_PORT_INTERNAL_LOOPBACK_MODE_NONE;
  }
}

cfg::PortLoopbackMode getCfgPortInternalLoopbackMode(
    sai_port_internal_loopback_mode_t mode) {
  switch (mode) {
    case SAI_PORT_INTERNAL_LOOPBACK_MODE_NONE:
      return cfg::PortLoopbackMode::NONE;
    case SAI_PORT_INTERNAL_LOOPBACK_MODE_PHY:
      return cfg::PortLoopbackMode::PHY;
    case SAI_PORT_INTERNAL_LOOPBACK_MODE_MAC:
      return cfg::PortLoopbackMode::MAC;
    default:
      return cfg::PortLoopbackMode::NONE;
  }
}

sai_port_media_type_t getSaiPortMediaType(
    TransmitterTechnology transmitterTech,
    cfg::PortSpeed speed) {
  switch (transmitterTech) {
    case TransmitterTechnology::COPPER:
      return SAI_PORT_MEDIA_TYPE_COPPER;

    case TransmitterTechnology::OPTICAL:
      return SAI_PORT_MEDIA_TYPE_FIBER;

    case TransmitterTechnology::BACKPLANE:
      return SAI_PORT_MEDIA_TYPE_BACKPLANE;
    case TransmitterTechnology::UNKNOWN:
      switch (speed) {
        case cfg::PortSpeed::FORTYG:
        case cfg::PortSpeed::HUNDREDG:
          return SAI_PORT_MEDIA_TYPE_FIBER;
        case cfg::PortSpeed::THREEPOINTTWOT:
          return SAI_PORT_MEDIA_TYPE_NOT_PRESENT;
        default:
          return SAI_PORT_MEDIA_TYPE_COPPER;
      }
  }
  return SAI_PORT_MEDIA_TYPE_UNKNOWN;
}

sai_port_media_type_t getSaiCablePropagationDelayMediaType(
    TransmitterTechnology transmitterTech) {
  switch (transmitterTech) {
    case TransmitterTechnology::COPPER:
      return SAI_PORT_MEDIA_TYPE_COPPER;
    case TransmitterTechnology::OPTICAL:
    case TransmitterTechnology::BACKPLANE:
      return SAI_PORT_MEDIA_TYPE_FIBER;
    default:
      return SAI_PORT_MEDIA_TYPE_UNKNOWN;
  }
  return SAI_PORT_MEDIA_TYPE_UNKNOWN;
}

sai_port_media_type_t getSaiPortMediaFromInterfaceType(
    phy::InterfaceType interfaceType) {
  switch (interfaceType) {
    case phy::InterfaceType::KR:
    case phy::InterfaceType::KR2:
    case phy::InterfaceType::KR4:
    case phy::InterfaceType::KR8:
    case phy::InterfaceType::CAUI4_C2C:
    case phy::InterfaceType::CAUI4_C2M:
    case phy::InterfaceType::CAUI:
    case phy::InterfaceType::SR2:
      return SAI_PORT_MEDIA_TYPE_BACKPLANE;

    case phy::InterfaceType::CR:
    case phy::InterfaceType::CR2:
    case phy::InterfaceType::CR4:
    case phy::InterfaceType::CR8:
      return SAI_PORT_MEDIA_TYPE_COPPER;

    case phy::InterfaceType::SR:
    case phy::InterfaceType::SR4:
    case phy::InterfaceType::SR8:
    case phy::InterfaceType::XLAUI:
    case phy::InterfaceType::SFI:
      return SAI_PORT_MEDIA_TYPE_FIBER;
    default:
      throw facebook::fboss::FbossError(
          "Unsupported interface type: ",
          apache::thrift::util::enumNameSafe(interfaceType));
  }
  return SAI_PORT_MEDIA_TYPE_UNKNOWN;
}

sai_port_fec_mode_t getSaiPortFecMode(phy::FecMode fec) {
  sai_port_fec_mode_t mode = SAI_PORT_FEC_MODE_NONE;
  switch (fec) {
    case phy::FecMode::NONE:
      mode = SAI_PORT_FEC_MODE_NONE;
      break;

    case phy::FecMode::CL74:
      mode = SAI_PORT_FEC_MODE_FC;
      break;

    case phy::FecMode::CL91:
    case phy::FecMode::RS528:
    case phy::FecMode::RS544:
    case phy::FecMode::RS544_2N:
    case phy::FecMode::RS545:
      mode = SAI_PORT_FEC_MODE_RS;
      break;
  }
  return mode;
}

phy::FecMode getFecModeFromSaiFecMode(
    sai_port_fec_mode_t fec,
    cfg::PortProfileID profileID) {
  phy::FecMode mode;
  switch (fec) {
    case SAI_PORT_FEC_MODE_NONE:
      mode = phy::FecMode::NONE;
      break;

    case SAI_PORT_FEC_MODE_RS:
      switch (profileID) {
        case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91:
        case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER:
        case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL:
        case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER_RACK_YV3_T1:
          mode = phy::FecMode::CL91;
          break;
        case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528:
        case cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_COPPER:
        case cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_OPTICAL:
        case cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_COPPER:
        case cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_OPTICAL:
        case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER:
        case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL:
          mode = phy::FecMode::RS528;
          break;
        case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544X2N_COPPER:
        case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL:
        case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_COPPER:
        case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N:
        case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N:
        case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER:
        case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL:
        case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL:
        case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER:
        case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL:
        case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL:
        case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_COPPER:
        case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_COPPER:
        case cfg::PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_OPTICAL:
        case cfg::PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_COPPER:
        case cfg::PortProfileID::PROFILE_200G_1_PAM4_RS544X2N_OPTICAL:
        case cfg::PortProfileID::PROFILE_200G_1_PAM4_RS544X2N_COPPER:
        case cfg::PortProfileID::PROFILE_200G_2_PAM4_RS544X2N_OPTICAL:
        case cfg::PortProfileID::PROFILE_200G_2_PAM4_RS544_COPPER:
        case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544_COPPER:
        case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544_COPPER:
        case cfg::PortProfileID::PROFILE_400G_2_PAM4_RS544X2N_OPTICAL:
        case cfg::PortProfileID::PROFILE_400G_2_PAM4_RS544X2N_COPPER:
          mode = phy::FecMode::RS544_2N;
          break;
        case cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_COPPER:
        case cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL:
          mode = phy::FecMode::RS545;
          break;
        case cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_COPPER:
        case cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL:
        case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544_OPTICAL:
        case cfg::PortProfileID::PROFILE_50G_1_PAM4_RS544_OPTICAL:
        case cfg::PortProfileID::PROFILE_50G_1_PAM4_RS544_COPPER:
          mode = phy::FecMode::RS544;
          break;
        default:
          mode = phy::FecMode::NONE;
      }
      break;

    case SAI_PORT_FEC_MODE_FC:
      mode = phy::FecMode::CL74;
      break;
  }
  return mode;
}

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
sai_port_fec_mode_extended_t getSaiPortExtendedFecMode(phy::FecMode fec) {
  sai_port_fec_mode_extended_t mode = SAI_PORT_FEC_MODE_EXTENDED_NONE;
  switch (fec) {
    case phy::FecMode::NONE:
    case phy::FecMode::CL91:
    case phy::FecMode::RS545:
      mode = SAI_PORT_FEC_MODE_EXTENDED_NONE;
      break;
    case phy::FecMode::RS528:
      mode = SAI_PORT_FEC_MODE_EXTENDED_RS528;
      break;
    case phy::FecMode::RS544:
      mode = SAI_PORT_FEC_MODE_EXTENDED_RS544;
      break;
    case phy::FecMode::RS544_2N:
      mode = SAI_PORT_FEC_MODE_EXTENDED_RS544_INTERLEAVED;
      break;
    case phy::FecMode::CL74:
      mode = SAI_PORT_FEC_MODE_EXTENDED_FC;
      break;
  }
  return mode;
}

phy::FecMode getFecModeFromSaiExtendedFecMode(
    sai_port_fec_mode_extended_t fec,
    cfg::PortProfileID /* profileID */) {
  phy::FecMode mode = phy::FecMode::NONE;
  switch (fec) {
    case SAI_PORT_FEC_MODE_EXTENDED_NONE:
      mode = phy::FecMode::NONE;
      break;
    case SAI_PORT_FEC_MODE_EXTENDED_RS528:
      mode = phy::FecMode::RS528;
      break;
    case SAI_PORT_FEC_MODE_EXTENDED_RS544:
      mode = phy::FecMode::RS544;
      break;
    case SAI_PORT_FEC_MODE_EXTENDED_RS544_INTERLEAVED:
      mode = phy::FecMode::RS544_2N;
      break;
    case SAI_PORT_FEC_MODE_EXTENDED_FC:
      mode = phy::FecMode::CL74;
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 19, 0)
    // phy::FecMode has no distinct value for the ETC and CL172 interleaved
    // variants or for low latency RS544, so they collapse onto the closest
    // mode FBOSS models. FBOSS never programs them - getSaiPortExtendedFecMode
    // cannot produce them - so this only affects what hardware reports back.
    case SAI_PORT_FEC_MODE_EXTENDED_RS544_INTERLEAVED_ETC:
    case SAI_PORT_FEC_MODE_EXTENDED_RS544_INTERLEAVED_CL172:
      mode = phy::FecMode::RS544_2N;
      break;
    case SAI_PORT_FEC_MODE_EXTENDED_RS544_LOW_LATENCY:
      mode = phy::FecMode::RS544;
      break;
#endif
  }
  return mode;
}
#endif

sai_port_ptp_mode_t getSaiPortPtpMode(bool enable) {
  // NOTE: SAI_PORT_PTP_MODE_TWO_STEP_TIMESTAMP is not supported
  return enable ? SAI_PORT_PTP_MODE_SINGLE_STEP_TIMESTAMP
                : SAI_PORT_PTP_MODE_NONE;
}

bool isPortOperUp(sai_port_oper_status_t operStatus) {
#if defined(BRCM_SAI_SDK_GTE_11_0) && defined(BRCM_SAI_SDK_DNX)
  auto operStatusExt =
      static_cast<sai_port_oper_status_extensions_t>(operStatus);
  return (
      operStatus == SAI_PORT_OPER_STATUS_UP ||
      operStatusExt == SAI_PORT_OPER_STATUS_FAILED ||
      operStatusExt == SAI_PORT_OPER_STATUS_WRONG_CONNECTIVITY);
#else
  return operStatus == SAI_PORT_OPER_STATUS_UP;
#endif
}
} // namespace facebook::fboss::utility
