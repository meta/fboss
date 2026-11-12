/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/test/HwPortUtils.h"

#include "fboss/agent/hw/sai/api/PortApi.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortUtils.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/HwTestPortUtils.h"
#include "fboss/agent/platforms/common/utils/GalaxyLedUtils.h"
#include "fboss/agent/platforms/common/utils/Wedge100LedUtils.h"
#include "fboss/agent/platforms/common/utils/Wedge400LedUtils.h"
#include "fboss/agent/platforms/sai/SaiBcmPlatformPort.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

#include "fboss/agent/FbossError.h"

#include <gtest/gtest.h>

namespace facebook::fboss::utility {

namespace {
SaiPortTraits::AdapterKey getPortAdapterKey(const HwSwitch* hw, PortID port) {
  auto saiSwitch = static_cast<const SaiSwitch*>(hw);
  auto handle = saiSwitch->managerTable()->portManager().getPortHandle(port);
  CHECK(handle);
  return handle->port->adapterKey();
}
} // namespace
bool portEnabled(const HwSwitch* hw, PortID port) {
  auto key = getPortAdapterKey(hw, port);
  SaiPortTraits::Attributes::AdminState state;
  SaiApiTable::getInstance()->portApi().getAttribute(key, state);
  return state.value();
}

cfg::PortSpeed curPortSpeed(const HwSwitch* hw, PortID port) {
  auto key = getPortAdapterKey(hw, port);
  SaiPortTraits::Attributes::Speed speed;
  SaiApiTable::getInstance()->portApi().getAttribute(key, speed);
  return cfg::PortSpeed(speed.value());
}

void assertPort(
    const HwSwitch* hw,
    PortID port,
    bool enabled,
    cfg::PortSpeed speed) {
  CHECK_EQ(enabled, portEnabled(hw, port));
  if (enabled) {
    // Only verify speed on enabled ports
    CHECK_EQ(
        static_cast<int>(speed),
        static_cast<int>(utility::curPortSpeed(hw, port)));
  }
}

void assertPortStatus(const HwSwitch* hw, PortID port) {
  CHECK(portEnabled(hw, port));
}

void assertPortsLoopbackMode(
    const HwSwitch* hw,
    const std::map<PortID, int>& port2LoopbackMode) {
  for (auto portAndLoopBackMode : port2LoopbackMode) {
    assertPortLoopbackMode(
        hw, portAndLoopBackMode.first, portAndLoopBackMode.second);
  }
}

int getLoopbackMode(cfg::PortLoopbackMode loopbackMode) {
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  return static_cast<int>(getSaiPortLoopbackMode(loopbackMode));
#else
  return static_cast<int>(getSaiPortInternalLoopbackMode(loopbackMode));
#endif
}

void assertPortSampleDestination(
    const HwSwitch* /*hw*/,
    PortID /*port*/,
    int /*expectedSampleDestination*/) {
  throw FbossError("sampling is unsupported for SAI");
}

void assertPortsSampleDestination(
    const HwSwitch* /*hw*/,
    const std::map<PortID, int>& /*port2SampleDestination*/) {
  throw FbossError("sampling is unsupported for SAI");
}

void assertPortLoopbackMode(
    const HwSwitch* hw,
    PortID port,
    int expectedLoopbackMode) {
  auto key = getPortAdapterKey(hw, port);
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  SaiPortTraits::Attributes::PortLoopbackMode loopbackMode;
  SaiApiTable::getInstance()->portApi().getAttribute(key, loopbackMode);
  CHECK_EQ(expectedLoopbackMode, loopbackMode.value());
#else
  SaiPortTraits::Attributes::InternalLoopbackMode internalLoopbackMode;
  SaiApiTable::getInstance()->portApi().getAttribute(key, internalLoopbackMode);
  CHECK_EQ(expectedLoopbackMode, internalLoopbackMode.value());
#endif
}

void cleanPortConfig(
    cfg::SwitchConfig* config,
    std::vector<PortID> allPortsinGroup) {
  // remove portCfg not in allPortsinGroup
  auto removed = std::remove_if(
      config->ports()->begin(),
      config->ports()->end(),
      [&allPortsinGroup](auto portCfg) {
        auto portID = static_cast<PortID>(*portCfg.logicalID());
        for (auto id : allPortsinGroup) {
          if (portID == id) {
            return false;
          }
        }
        return true;
      });
  config->ports()->erase(removed, config->ports()->end());
}

void assertQUADMode(
    HwSwitch* hw,
    cfg::PortSpeed enabledLaneSpeed,
    std::vector<PortID> allPortsinGroup) {
  utility::assertPort(hw, allPortsinGroup[0], true, enabledLaneSpeed);
}

void assertDUALMode(
    HwSwitch* hw,
    cfg::PortSpeed enabledLaneSpeed,
    cfg::PortSpeed /*disabledLaneSpeed*/,
    std::vector<PortID> allPortsinGroup) {
  bool odd_lane;
  auto portItr = allPortsinGroup.begin();
  for (; portItr != allPortsinGroup.end(); portItr++) {
    odd_lane = (*portItr - allPortsinGroup.front()) % 2 == 0 ? true : false;
    if (odd_lane) {
      utility::assertPort(hw, *portItr, true, enabledLaneSpeed);
    }
  }
}
void assertSINGLEMode(
    HwSwitch* hw,
    cfg::PortSpeed enabledLaneSpeed,
    cfg::PortSpeed /*disabledLaneSpeed*/,
    std::vector<PortID> allPortsinGroup) {
  utility::assertPort(hw, allPortsinGroup[0], true, enabledLaneSpeed);
}

void verifyInterfaceMode(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const phy::ProfileSideConfig& expectedProfileConfig) {
  auto* saiPlatform = static_cast<SaiPlatform*>(platform);
  if (!saiPlatform->getAsic()->isSupported(
          HwAsic::Feature::PORT_INTERFACE_TYPE) ||
      !saiPlatform->supportInterfaceType()) {
    return;
  }
  auto* saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
  auto* saiPortHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(portID);

  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto speed = portApi.getAttribute(
      saiPortHandle->port->adapterKey(), SaiPortTraits::Attributes::Speed{});

  if (!expectedProfileConfig.medium()) {
    throw FbossError(
        "Missing medium info in profile ",
        apache::thrift::util::enumNameSafe(profileID));
  }
  auto transmitterTech = *expectedProfileConfig.medium();
  auto expectedInterfaceType = saiPlatform->getInterfaceType(
      transmitterTech, static_cast<cfg::PortSpeed>(speed));
  auto programmedInterfaceType = portApi.getAttribute(
      saiPortHandle->port->adapterKey(),
      SaiPortTraits::Attributes::InterfaceType{});
  EXPECT_EQ(expectedInterfaceType.value(), programmedInterfaceType);
}

void verifyTxSettting(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const std::vector<phy::PinConfig>& expectedPinConfigs) {
  auto* saiPlatform = static_cast<SaiPlatform*>(platform);
  if (!saiPlatform->isSerdesApiSupported() ||
      !saiPlatform->getAsic()->isSupported(
          HwAsic::Feature::SAI_PORT_SERDES_PROGRAMMING)) {
    return;
  }

  auto numExpectedTxLanes = 0;
  std::vector<phy::TxSettings> txSettings;
  for (const auto& pinConfig : expectedPinConfigs) {
    if (auto tx = pinConfig.tx()) {
      txSettings.push_back(*tx);
      ++numExpectedTxLanes;
    }
  }
  if (numExpectedTxLanes == 0) {
    return;
  }

  auto* saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
  auto* saiPortHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(portID);
  // Prepare expected SaiPortSerdesTraits::CreateAttributes
  SaiPortSerdesTraits::CreateAttributes expectedTx =
      saiSwitch->managerTable()->portManager().serdesAttributesFromSwPinConfigs(
          saiPortHandle->port->adapterKey(),
          expectedPinConfigs,
          saiPortHandle->serdes);

  auto serdes = saiPortHandle->serdes;

  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto pre = portApi.getAttribute(
      serdes->adapterKey(), SaiPortSerdesTraits::Attributes::TxFirPre1{});
  auto main = portApi.getAttribute(
      serdes->adapterKey(), SaiPortSerdesTraits::Attributes::TxFirMain{});
  auto post = portApi.getAttribute(
      serdes->adapterKey(), SaiPortSerdesTraits::Attributes::TxFirPost1{});

  std::vector<sai_uint32_t> pre2;
  std::vector<sai_uint32_t> post2;
  std::vector<sai_uint32_t> post3;

  EXPECT_EQ(pre, GET_OPT_ATTR(PortSerdes, TxFirPre1, expectedTx));
  EXPECT_EQ(main, GET_OPT_ATTR(PortSerdes, TxFirMain, expectedTx));
  EXPECT_EQ(post, GET_OPT_ATTR(PortSerdes, TxFirPost1, expectedTx));

  if (auto expectedPre3 =
          std::get<std::optional<SaiPortSerdesTraits::Attributes::TxFirPre3>>(
              expectedTx)) {
    auto pre3 = portApi.getAttribute(
        serdes->adapterKey(), SaiPortSerdesTraits::Attributes::TxFirPre3{});
    EXPECT_EQ(pre3, expectedPre3->value());
  }

  auto asicVendor = saiPlatform->getAsic()->getAsicVendor();
  auto asicType = saiPlatform->getAsic()->getAsicType();
  if (saiPlatform->getAsic()->isSupported(
          HwAsic::Feature::SAI_CONFIGURE_SIX_TAP)) {
    pre2 = portApi.getAttribute(
        serdes->adapterKey(), SaiPortSerdesTraits::Attributes::TxFirPre2{});
    EXPECT_EQ(pre2, GET_OPT_ATTR(PortSerdes, TxFirPre2, expectedTx));
    if (asicVendor != HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
      post2 = portApi.getAttribute(
          serdes->adapterKey(), SaiPortSerdesTraits::Attributes::TxFirPost2{});

      if (auto expectedPost2 = std::get<
              std::optional<SaiPortSerdesTraits::Attributes::TxFirPost2>>(
              expectedTx)) {
        EXPECT_EQ(post2, GET_OPT_ATTR(PortSerdes, TxFirPost2, expectedTx));
      }

      // TH6 SDK does not support POST3 readback (returns sentinel 32767)
      if (asicType != cfg::AsicType::ASIC_TYPE_TOMAHAWK6) {
        post3 = portApi.getAttribute(
            serdes->adapterKey(),
            SaiPortSerdesTraits::Attributes::TxFirPost3{});

        if (auto expectedPost3 = std::get<
                std::optional<SaiPortSerdesTraits::Attributes::TxFirPost3>>(
                expectedTx)) {
          EXPECT_EQ(post3, GET_OPT_ATTR(PortSerdes, TxFirPost3, expectedTx));
        }
      }
    }
  }

  // Also verify sixtap attributes against expected pin config
  EXPECT_EQ(pre.size(), txSettings.size());
  for (int i = 0; i < txSettings.size(); ++i) {
    auto expectedTxFromPin = txSettings[i];
    if (expectedTxFromPin.firPre1().has_value()) {
      EXPECT_EQ(pre[i], expectedTxFromPin.firPre1());
    } else {
      EXPECT_EQ(pre[i], expectedTxFromPin.pre());
    }
    if (expectedTxFromPin.firMain().has_value()) {
      EXPECT_EQ(main[i], expectedTxFromPin.firMain());
    } else {
      EXPECT_EQ(main[i], expectedTxFromPin.main());
    }
    if (expectedTxFromPin.firPost1().has_value()) {
      EXPECT_EQ(post[i], expectedTxFromPin.firPost1());
    } else {
      EXPECT_EQ(post[i], expectedTxFromPin.post());
    }
    if (saiPlatform->getAsic()->isSupported(
            HwAsic::Feature::SAI_CONFIGURE_SIX_TAP)) {
      if (expectedTxFromPin.firPre2().has_value()) {
        EXPECT_EQ(pre2[i], expectedTxFromPin.firPre2());
      } else {
        EXPECT_EQ(pre2[i], expectedTxFromPin.pre2());
      }
      if (asicVendor != HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
        if (expectedTxFromPin.firPost2().has_value()) {
          EXPECT_EQ(post2[i], expectedTxFromPin.firPost2());
        } else {
          EXPECT_EQ(post2[i], expectedTxFromPin.post2());
        }
        if (asicType != cfg::AsicType::ASIC_TYPE_TOMAHAWK6) {
          if (expectedTxFromPin.firPost3().has_value()) {
            EXPECT_EQ(post3[i], expectedTxFromPin.firPost3());
          } else {
            EXPECT_EQ(post3[i], expectedTxFromPin.post3());
          }
        }
      }
    }
  }

  if (saiPlatform->getAsic()->isSupported(
          HwAsic::Feature::SAI_CONFIGURE_SIX_TAP) &&
      (saiPlatform->getAsic()->getAsicVendor() ==
       HwAsic::AsicVendor::ASIC_VENDOR_TAJO)) {
    SaiPortSerdesTraits::CreateAttributes expectedSerdes =
        saiSwitch->managerTable()
            ->portManager()
            .serdesAttributesFromSwPinConfigs(
                saiPortHandle->port->adapterKey(), expectedPinConfigs, serdes);

    if (auto expectedTxLutMode =
            std::get<std::optional<SaiPortSerdesTraits::Attributes::TxLutMode>>(
                expectedSerdes)) {
      auto txLutMode = portApi.getAttribute(
          serdes->adapterKey(), SaiPortSerdesTraits::Attributes::TxLutMode{});
      EXPECT_EQ(txLutMode, expectedTxLutMode->value());
    }
  }

  if (auto expectedDriveCurrent =
          std::get<std::optional<SaiPortSerdesTraits::Attributes::IDriver>>(
              expectedTx)) {
    auto driverCurrent = portApi.getAttribute(
        serdes->adapterKey(), SaiPortSerdesTraits::Attributes::IDriver{});
    EXPECT_EQ(driverCurrent, expectedDriveCurrent->value());
  }

#if !defined(CHENAB_SAI_SDK)
  // Also need to check Preemphasis is set correctly
  if (saiPlatform->getAsic()->getPortSerdesPreemphasis().has_value()) {
    EXPECT_EQ(
        portApi.getAttribute(
            serdes->adapterKey(),
            SaiPortSerdesTraits::Attributes::Preemphasis{}),
        GET_OPT_ATTR(PortSerdes, Preemphasis, expectedTx));
  }
#endif
}

bool verifyLedStatus(Platform* platform, PortID port, bool up) {
  auto* saiPlatform = static_cast<SaiPlatform*>(platform);
  SaiPlatformPort* platformPort = saiPlatform->getPort(port);
  uint32_t currentVal = platformPort->getCurrentLedState();
  uint32_t expectedVal = 0;
  switch (platform->getType()) {
    case PlatformType::PLATFORM_WEDGE100: {
      expectedVal = static_cast<uint32_t>(Wedge100LedUtils::getExpectedLEDState(
          platform->getLaneCount(platformPort->getCurrentProfile()), up, up));
    } break;
    case PlatformType::PLATFORM_GALAXY_FC:
    case PlatformType::PLATFORM_GALAXY_LC: {
      expectedVal = GalaxyLedUtils::getExpectedLEDState(up, up);
    } break;
    case PlatformType::PLATFORM_WEDGE400:
    case PlatformType::PLATFORM_WEDGE400_GRANDTETON:
    case PlatformType::PLATFORM_WEDGE400C:
    case PlatformType::PLATFORM_WEDGE400C_GRANDTETON: {
      expectedVal = static_cast<uint32_t>(Wedge400LedUtils::getLedState(
          platform->getLaneCount(platformPort->getCurrentProfile()), up, up));
    } break;
    default:
      return true;
  }
  return currentVal == expectedVal;
}

void verifyRxSettting(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const std::vector<phy::PinConfig>& expectedPinConfigs) {
  auto* saiPlatform = static_cast<SaiPlatform*>(platform);
  if (!saiPlatform->isSerdesApiSupported() ||
      !saiPlatform->getAsic()->isSupported(
          HwAsic::Feature::SAI_PORT_SERDES_PROGRAMMING)) {
    return;
  }

  auto numExpectedRxLanes = 0;
  for (const auto& pinConfig : expectedPinConfigs) {
    if (auto tx = pinConfig.rx()) {
      ++numExpectedRxLanes;
    }
  }

  auto* saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
  auto* saiPortHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(portID);
  auto serdes = saiPortHandle->serdes;

  if (!serdes) {
    EXPECT_TRUE(numExpectedRxLanes == 0);
    return;
  }
  if (numExpectedRxLanes == 0) {
    // not all platforms may have these settings
    return;
  }

  // Prepare expected SaiPortSerdesTraits::CreateAttributes
  SaiPortSerdesTraits::CreateAttributes expectedSerdes =
      saiSwitch->managerTable()->portManager().serdesAttributesFromSwPinConfigs(
          saiPortHandle->port->adapterKey(), expectedPinConfigs, serdes);

  auto& portApi = SaiApiTable::getInstance()->portApi();
  if (auto expectedRxCtlCode =
          std::get<std::optional<SaiPortSerdesTraits::Attributes::RxCtleCode>>(
              expectedSerdes)) {
    auto rxCtlCode = portApi.getAttribute(
        serdes->adapterKey(), SaiPortSerdesTraits::Attributes::RxCtleCode{});
    EXPECT_EQ(rxCtlCode, expectedRxCtlCode->value());
  }
  if (auto expectedRxDspMode =
          std::get<std::optional<SaiPortSerdesTraits::Attributes::RxDspMode>>(
              expectedSerdes)) {
    auto rxDspMode = portApi.getAttribute(
        serdes->adapterKey(), SaiPortSerdesTraits::Attributes::RxDspMode{});
    EXPECT_EQ(rxDspMode, expectedRxDspMode->value());
  }
  if (auto expectedRxAfeTrim =
          std::get<std::optional<SaiPortSerdesTraits::Attributes::RxAfeTrim>>(
              expectedSerdes)) {
    auto rxAafeTrim = portApi.getAttribute(
        serdes->adapterKey(), SaiPortSerdesTraits::Attributes::RxAfeTrim{});
    EXPECT_EQ(rxAafeTrim, expectedRxAfeTrim->value());
  }
  if (auto expectedRxAcCouplingBypass = std::get<
          std::optional<SaiPortSerdesTraits::Attributes::RxAcCouplingByPass>>(
          expectedSerdes)) {
    auto rxAcCouplingBypass = portApi.getAttribute(
        serdes->adapterKey(),
        SaiPortSerdesTraits::Attributes::RxAcCouplingByPass{});
    EXPECT_EQ(rxAcCouplingBypass, expectedRxAcCouplingBypass->value());
  }
}

void verifyFec(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const phy::ProfileSideConfig& expectedProfileConfig) {
  auto* saiPlatform = static_cast<SaiPlatform*>(platform);
  auto* saiSwitch = static_cast<SaiSwitch*>(saiPlatform->getHwSwitch());
  auto* saiPortHandle =
      saiSwitch->managerTable()->portManager().getPortHandle(portID);

  // retrieve configured fec.
  auto expectedFec = utility::getSaiPortFecMode(*expectedProfileConfig.fec());

  // Convert expectedFec back to FecMode to verify
  // utility::getFecModeFromSaiFecMode
  EXPECT_EQ(
      utility::getFecModeFromSaiFecMode(expectedFec, profileID),
      *expectedProfileConfig.fec());

  // retrieve programmed fec.
  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto programmedFec = portApi.getAttribute(
      saiPortHandle->port->adapterKey(), SaiPortTraits::Attributes::FecMode{});

  EXPECT_EQ(expectedFec, programmedFec);

  // Verify the getPortFecMode function
  EXPECT_EQ(saiSwitch->getPortFECMode(portID), *expectedProfileConfig.fec());
}

void enableSixtapProgramming() {
  FLAGS_sai_configure_six_tap = true;
};

} // namespace facebook::fboss::utility
