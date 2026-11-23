/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/utils/PortTestUtils.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestEnsembleIf.h"
#include "fboss/lib/CommonUtils.h"

#include <gtest/gtest.h>
#include <memory>

namespace facebook::fboss::utility {
cfg::PortSpeed getSpeed(cfg::PortProfileID profile) {
  switch (profile) {
    case cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_OPTICAL:
      return cfg::PortSpeed::XG;

    case cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC_COPPER:
      return cfg::PortSpeed::TWENTYG;

    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_COPPER:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_OPTICAL:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER_RACK_YV3_T1:
      return cfg::PortSpeed::TWENTYFIVEG;

    case cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_OPTICAL:
      return cfg::PortSpeed::FORTYG;

    case cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_CL74_COPPER:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_COPPER:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_OPTICAL:
    case cfg::PortProfileID::PROFILE_50G_1_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_50G_1_PAM4_RS544_OPTICAL:
      return cfg::PortSpeed::FIFTYG;

    case cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_COPPER:
    case cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL:
      return cfg::PortSpeed::FIFTYTHREEPOINTONETWOFIVEG;

    case cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL:
      return cfg::PortSpeed::HUNDREDANDSIXPOINTTWOFIVEG;

    case cfg::PortProfileID::PROFILE_212POINT5G_1_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_212POINT5G_1_PAM4_RS544X2N_COPPER:
      return cfg::PortSpeed::TWOHUNDREDANDTWELVEPOINTFIVEG;

    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER_RACK_YV3_T1:
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544_COPPER:
      return cfg::PortSpeed::HUNDREDG;

    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N:
    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_200G_2_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_200G_1_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_200G_2_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_200G_1_PAM4_RS544X2N_COPPER:
      return cfg::PortSpeed::TWOHUNDREDG;

    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N:
    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_2_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_400G_2_PAM4_RS544X2N_COPPER:
      return cfg::PortSpeed::FOURHUNDREDG;

    case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_COPPER:
      return cfg::PortSpeed::EIGHTHUNDREDG;

    case cfg::PortProfileID::PROFILE_1600G_8_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_1600G_8_PAM4_RS544X2N_COPPER:
      return cfg::PortSpeed::ONEPOINTSIXT;

    case cfg::PortProfileID::PROFILE_DEFAULT:
      break;
  }
  return cfg::PortSpeed::DEFAULT;
}

TransmitterTechnology getMediaType(cfg::PortProfileID profile) {
  switch (profile) {
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_CL74_COPPER:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_COPPER:
    case cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_CL74_COPPER:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_COPPER:
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_200G_2_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_COPPER_RACK_YV3_T1:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_COPPER_RACK_YV3_T1:
    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_COPPER:
    case cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_50G_1_PAM4_RS544_COPPER:
    case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_NOFEC_COPPER:
    case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_200G_1_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_400G_2_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_212POINT5G_1_PAM4_RS544X2N_COPPER:
    case cfg::PortProfileID::PROFILE_1600G_8_PAM4_RS544X2N_COPPER:
      return TransmitterTechnology::COPPER;

    case cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_RS528_OPTICAL:
    case cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC_OPTICAL:
    case cfg::PortProfileID::PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_2_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91_OPTICAL:
    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_800G_8_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL:
    case cfg::PortProfileID::PROFILE_100G_1_PAM4_RS544_OPTICAL:
    case cfg::PortProfileID::PROFILE_50G_1_PAM4_RS544_OPTICAL:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_RS528_OPTICAL:
    case cfg::PortProfileID::PROFILE_400G_2_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_800G_4_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_200G_1_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_200G_2_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_1600G_8_PAM4_RS544X2N_OPTICAL:
    case cfg::PortProfileID::PROFILE_212POINT5G_1_PAM4_RS544X2N_OPTICAL:
      return TransmitterTechnology::OPTICAL;

    case cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_20G_2_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_25G_1_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_40G_4_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_50G_2_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_NOFEC:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_CL91:
    case cfg::PortProfileID::PROFILE_100G_4_NRZ_RS528:
    case cfg::PortProfileID::PROFILE_200G_4_PAM4_RS544X2N:
    case cfg::PortProfileID::PROFILE_400G_8_PAM4_RS544X2N:
    case cfg::PortProfileID::PROFILE_DEFAULT:
      break;
  }
  return TransmitterTechnology::UNKNOWN;
}

cfg::PortSpeed getDefaultInterfaceSpeed(const cfg::AsicType& asicType) {
  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_JERICHO2:
      return cfg::PortSpeed::HUNDREDG;
    case cfg::AsicType::ASIC_TYPE_JERICHO3:
    case cfg::AsicType::ASIC_TYPE_QUMRAN4D:
      return cfg::PortSpeed::FOURHUNDREDG;
    default:
      throw FbossError(
          "Unsupported interface speed for asic type: ",
          static_cast<int>(asicType));
  }
}

cfg::PortSpeed getDefaultFabricSpeed(const cfg::AsicType& asicType) {
  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_JERICHO2:
      return cfg::PortSpeed::FIFTYTHREEPOINTONETWOFIVEG;
    case cfg::AsicType::ASIC_TYPE_JERICHO3:
    case cfg::AsicType::ASIC_TYPE_QUMRAN4D:
      return cfg::PortSpeed::HUNDREDANDSIXPOINTTWOFIVEG;
    default:
      throw FbossError(
          "Unsupported fabric speed for asic type: ",
          static_cast<int>(asicType));
  }
}

void setCreditWatchdogAndPortTx(
    TestEnsembleIf* ensemble,
    PortID port,
    bool enable) {
  bool isVoqSwitch =
      ensemble->getHwAsicTable()
          ->getHwAsic(ensemble->scopeResolver().scope(port).switchId())
          ->getSwitchType() == cfg::SwitchType::VOQ;
  auto updateCreditWatchdogAndPortTx =
      [&](const std::shared_ptr<SwitchState>& in) {
        auto switchState = in->clone();
        auto newPort =
            switchState->getPorts()->getNodeIf(port)->modify(&switchState);
        newPort->setTxEnable(enable);
        // VoQ switch specific handling
        if (isVoqSwitch) {
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0) && !defined(BRCM_SAI_SDK_DNX_GTE_14_0)
          // For VoQ switches, TX disable should be accompanied by the
          // port initial credits being reset to avoid leaking of data
          // due to the residual credit.
          newPort->setResetQueueCreditBalance(!enable);
#endif
          for (const auto& [_, switchSetting] :
               std::as_const(*switchState->getSwitchSettings())) {
            auto newSwitchSettings = switchSetting->modify(&switchState);
            newSwitchSettings->setCreditWatchdog(enable);
          }
        }
        return switchState;
      };
  ensemble->applyNewState(updateCreditWatchdogAndPortTx);
}

void enableCreditWatchdog(TestEnsembleIf* ensemble, bool enable) {
  ensemble->applyNewState([&](const std::shared_ptr<SwitchState>& in) {
    auto switchState = in->clone();
    for (const auto& [_, switchSetting] :
         std::as_const(*switchState->getSwitchSettings())) {
      auto newSwitchSettings = switchSetting->modify(&switchState);
      newSwitchSettings->setCreditWatchdog(enable);
    }
    return switchState;
  });
}

void setPortTx(TestEnsembleIf* ensemble, PortID port, bool enable) {
  auto updatePortTx = [&](const std::shared_ptr<SwitchState>& in) {
    auto switchState = in->clone();
    auto newPort =
        switchState->getPorts()->getNodeIf(port)->modify(&switchState);
    newPort->setTxEnable(enable);
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0) && !defined(BRCM_SAI_SDK_DNX_GTE_14_0)
    // For VoQ switches, TX disable should be accompanied by the
    // port initial credits being reset to avoid leaking of data
    // due to the residual credit.
    if (ensemble->getHwAsicTable()
            ->getHwAsic(ensemble->scopeResolver().scope(port).switchId())
            ->getSwitchType() == cfg::SwitchType::VOQ) {
      newPort->setResetQueueCreditBalance(!enable);
    }
#endif
    return switchState;
  };
  ensemble->applyNewState(updatePortTx);
}

void resetQueueCreditBalance(
    TestEnsembleIf* ensemble,
    PortID port,
    bool enable) {
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0) && !defined(BRCM_SAI_SDK_DNX_GTE_14_0)
  auto updateResetQueueCreditBalance =
      [&](const std::shared_ptr<SwitchState>& in) {
        auto switchState = in->clone();
        auto newPort =
            switchState->getPorts()->getNodeIf(port)->modify(&switchState);
        newPort->setResetQueueCreditBalance(enable);
        return switchState;
      };
  ensemble->applyNewState(updateResetQueueCreditBalance);
#endif
}

void checkPortsAdminState(
    TestEnsembleIf* ensemble,
    const std::vector<PortID>& portIds,
    cfg::PortState portState) {
  WITH_RETRIES({
    for (const auto& portId : portIds) {
      auto port = ensemble->getProgrammedState()->getPorts()->getNodeIf(portId);
      CHECK(port);
      EXPECT_EVENTUALLY_TRUE(port->getAdminState() == portState);
    }
  });
}

} // namespace facebook::fboss::utility
