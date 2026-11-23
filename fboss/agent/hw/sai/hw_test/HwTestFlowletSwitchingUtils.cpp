/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/HwTestFlowletSwitchingUtils.h"
#include "fboss/agent/test/TestUtils.h"

#include <fmt/format.h>
#include <gtest/gtest.h>
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/hw/sai/api/SaiApiTable.h"
#include "fboss/agent/hw/sai/switch/SaiArsManager.h"
// NOLINTNEXTLINE(facebook-unused-include-check)
#include "fboss/agent/hw/sai/switch/SaiArsProfileManager.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouteManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiVirtualRouterManager.h"
#include "fboss/agent/test/TestEnsembleIf.h"

#include "folly/testing/TestUtil.h"

namespace facebook::fboss {
class TestEnsembleIf;
}

namespace facebook::fboss::utility {

void verifyArsProfile(
    const HwSwitch* hw,
    ArsProfileSaiId arsProfileSaiId,
    const cfg::FlowletSwitchingConfig& flowletCfg) {
  ArsProfileSaiId nullArsProfileSaiId{SAI_NULL_OBJECT_ID};
  EXPECT_NE(arsProfileSaiId, nullArsProfileSaiId);

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0) && !defined(CHENAB_SAI_SDK)
  auto& arsProfileApi = SaiApiTable::getInstance()->arsProfileApi();

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
  auto samplingInterval = arsProfileApi.getAttribute(
      arsProfileSaiId,
      SaiArsProfileTraits::Attributes::ExtensionSamplingIntervalNanosec());
  EXPECT_EQ(*flowletCfg.dynamicSampleRate(), samplingInterval);
#else
  auto samplingInterval = arsProfileApi.getAttribute(
      arsProfileSaiId, SaiArsProfileTraits::Attributes::SamplingInterval());
  auto expectedInterval =
      std::ceil(static_cast<double>(*flowletCfg.dynamicSampleRate()) / 1000);
  EXPECT_EQ(expectedInterval, samplingInterval);
#endif
  auto randomSeed = arsProfileApi.getAttribute(
      arsProfileSaiId, SaiArsProfileTraits::Attributes::RandomSeed());
  EXPECT_EQ(SaiArsProfileManager::kArsRandomSeed, randomSeed);
  auto portLoadPastWeight = arsProfileApi.getAttribute(
      arsProfileSaiId, SaiArsProfileTraits::Attributes::PortLoadPastWeight());
  EXPECT_EQ(*flowletCfg.dynamicEgressLoadExponent(), portLoadPastWeight);
  auto loadPastMinVal = arsProfileApi.getAttribute(
      arsProfileSaiId, SaiArsProfileTraits::Attributes::LoadPastMinVal());
  EXPECT_EQ(*flowletCfg.dynamicEgressMinThresholdBytes(), loadPastMinVal);
  auto loadPastMaxVal = arsProfileApi.getAttribute(
      arsProfileSaiId, SaiArsProfileTraits::Attributes::LoadPastMaxVal());
  EXPECT_EQ(*flowletCfg.dynamicEgressMaxThresholdBytes(), loadPastMaxVal);
  if (hw->getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::ARS_FUTURE_PORT_LOAD)) {
    auto portLoadFutureWeight = arsProfileApi.getAttribute(
        arsProfileSaiId,
        SaiArsProfileTraits::Attributes::PortLoadFutureWeight());
    EXPECT_EQ(*flowletCfg.dynamicQueueExponent(), portLoadFutureWeight);
    auto loadFutureMinVal = arsProfileApi.getAttribute(
        arsProfileSaiId, SaiArsProfileTraits::Attributes::LoadFutureMinVal());
    EXPECT_EQ(*flowletCfg.dynamicQueueMinThresholdBytes(), loadFutureMinVal);
    auto loadFutureMaxVal = arsProfileApi.getAttribute(
        arsProfileSaiId, SaiArsProfileTraits::Attributes::LoadFutureMaxVal());
    EXPECT_EQ(*flowletCfg.dynamicQueueMaxThresholdBytes(), loadFutureMaxVal);
  }
  auto portLoadExponent = arsProfileApi.getAttribute(
      arsProfileSaiId, SaiArsProfileTraits::Attributes::PortLoadExponent());
  EXPECT_EQ(*flowletCfg.dynamicPhysicalQueueExponent(), portLoadExponent);
  auto loadCurrentMinVal = arsProfileApi.getAttribute(
      arsProfileSaiId, SaiArsProfileTraits::Attributes::LoadCurrentMinVal());
  EXPECT_EQ(
      *flowletCfg.dynamicQueueMinThresholdBytes() >> 1, loadCurrentMinVal);
  auto loadCurrentMaxVal = arsProfileApi.getAttribute(
      arsProfileSaiId, SaiArsProfileTraits::Attributes::LoadCurrentMaxVal());
  EXPECT_EQ(
      *flowletCfg.dynamicQueueMaxThresholdBytes() >> 1, loadCurrentMaxVal);
#endif
}

void verifyArs(
    const HwSwitch* hw,
    ArsSaiId arsSaiId,
    const cfg::FlowletSwitchingConfig& cfg) {
  ArsSaiId nullArsSaiId{SAI_NULL_OBJECT_ID};
  EXPECT_NE(arsSaiId, nullArsSaiId);

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  auto& arsApi = SaiApiTable::getInstance()->arsApi();
  const auto saiSwitch = static_cast<const SaiSwitch*>(hw);

  auto mode = arsApi.getAttribute(arsSaiId, SaiArsTraits::Attributes::Mode());
  auto switchingMode =
      saiSwitch->managerTable()->arsManager().cfgSwitchingModeToSai(
          *cfg.switchingMode());
  EXPECT_EQ(switchingMode, mode);
  if (hw->getPlatform()->getAsic()->getAsicVendor() !=
      HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
    auto idleTime =
        arsApi.getAttribute(arsSaiId, SaiArsTraits::Attributes::IdleTime());
    EXPECT_EQ(*cfg.inactivityIntervalUsecs(), idleTime);
    auto maxFlows =
        arsApi.getAttribute(arsSaiId, SaiArsTraits::Attributes::MaxFlows());
    EXPECT_EQ(*cfg.flowletTableSize(), maxFlows);
  }
#endif
}

void verifyPortArsAttributes(
    const HwSwitch* hw,
    PortSaiId portSaiId,
    const cfg::PortFlowletConfig& cfg,
    bool enable) {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  auto& portApi = SaiApiTable::getInstance()->portApi();
  auto arsEnable =
      portApi.getAttribute(portSaiId, SaiPortTraits::Attributes::ArsEnable());
  EXPECT_EQ(enable, arsEnable);
  if (hw->getPlatform()->getAsic()->getAsicVendor() !=
      HwAsic::AsicVendor::ASIC_VENDOR_CHENAB) {
    auto portLoadScalingFactor = portApi.getAttribute(
        portSaiId, SaiPortTraits::Attributes::ArsPortLoadScalingFactor());
    EXPECT_EQ(*cfg.scalingFactor(), portLoadScalingFactor);
    auto portLoadPastWeight = portApi.getAttribute(
        portSaiId, SaiPortTraits::Attributes::ArsPortLoadPastWeight());
    EXPECT_EQ(*cfg.loadWeight(), portLoadPastWeight);
    if (hw->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::ARS_FUTURE_PORT_LOAD)) {
      auto portLoadFutureWeight = portApi.getAttribute(
          portSaiId, SaiPortTraits::Attributes::ArsPortLoadFutureWeight());
      EXPECT_EQ(*cfg.queueWeight(), portLoadFutureWeight);
    }
  }
#endif
}

bool validateFlowletSwitchingEnabled(
    const HwSwitch* hw,
    const cfg::FlowletSwitchingConfig& flowletCfg) {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  const auto saiSwitch = static_cast<const SaiSwitch*>(hw);
  SwitchSaiId switchId =
      saiSwitch->managerTable()->switchManager().getSwitchSaiId();
  auto arsProfileId = SaiApiTable::getInstance()->switchApi().getAttribute(
      switchId, SaiSwitchTraits::Attributes::ArsProfile());

  verifyArsProfile(hw, static_cast<ArsProfileSaiId>(arsProfileId), flowletCfg);
#endif
  return true;
}

NextHopGroupSaiId getNextHopGroupSaiId(
    const SaiSwitch* saiSwitch,
    const folly::CIDRNetwork& ip) {
  const auto* managerTable = saiSwitch->managerTable();
  const auto* vrHandle =
      managerTable->virtualRouterManager().getVirtualRouterHandle(RouterID(0));
  auto routeEntry = SaiRouteTraits::RouteEntry{
      saiSwitch->getSaiSwitchId(), vrHandle->virtualRouter->adapterKey(), ip};
  const auto* routeHandle =
      managerTable->routeManager().getRouteHandle(routeEntry);
  return static_cast<NextHopGroupSaiId>(
      routeHandle->nextHopGroupHandle()->adapterKey());
}

bool verifyEcmpForFlowletSwitching(
    const HwSwitch* hw,
    const folly::CIDRNetwork& ip,
    const cfg::FlowletSwitchingConfig& flowletCfg,
    const cfg::PortFlowletConfig& cfg,
    const bool flowletEnable) {
  bool isVerified = true;

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  const auto saiSwitch = static_cast<const SaiSwitch*>(hw);
  auto nextHopGroupSaiId = getNextHopGroupSaiId(saiSwitch, ip);
  auto arsId = SaiApiTable::getInstance()->nextHopGroupApi().getAttribute(
      nextHopGroupSaiId, SaiNextHopGroupTraits::Attributes::ArsObjectId());

  if (flowletEnable) {
    isVerified = (arsId != SAI_NULL_OBJECT_ID);
    if (isVerified) {
      verifyArs(hw, static_cast<ArsSaiId>(arsId), flowletCfg);
    }
  } else {
    isVerified = (arsId == SAI_NULL_OBJECT_ID);
  }
#endif

  return isVerified;
}

bool verifyEcmpForNonFlowlet(
    const HwSwitch* hw,
    const folly::CIDRNetwork& ip,
    const cfg::FlowletSwitchingConfig& /* unused */,
    const bool flowletEnable) {
  bool isVerified = true;

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  const auto saiSwitch = static_cast<const SaiSwitch*>(hw);
  auto nextHopGroupSaiId = getNextHopGroupSaiId(saiSwitch, ip);
  auto arsId = SaiApiTable::getInstance()->nextHopGroupApi().getAttribute(
      nextHopGroupSaiId, SaiNextHopGroupTraits::Attributes::ArsObjectId());

  if (flowletEnable) {
    isVerified = (arsId != SAI_NULL_OBJECT_ID);
  } else {
    isVerified = (arsId == SAI_NULL_OBJECT_ID);
  }
#endif
  return isVerified;
}

bool validatePortFlowletQuality(
    const HwSwitch* hw,
    const PortID& portId,
    const cfg::PortFlowletConfig& cfg,
    bool enable) {
  const auto& portManager =
      static_cast<const SaiSwitch*>(hw)->managerTable()->portManager();
  auto portHandle = portManager.getPortHandle(portId);
  auto saiPortId = portHandle->port->adapterKey();

  verifyPortArsAttributes(hw, static_cast<PortSaiId>(saiPortId), cfg, enable);
  return true;
}

bool validateFlowletSwitchingDisabled(const HwSwitch* hw) {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  const auto saiSwitch = static_cast<const SaiSwitch*>(hw);
  SwitchSaiId switchId =
      saiSwitch->managerTable()->switchManager().getSwitchSaiId();

  auto arsProfileId = SaiApiTable::getInstance()->switchApi().getAttribute(
      switchId, SaiSwitchTraits::Attributes::ArsProfile());

  return arsProfileId == SAI_NULL_OBJECT_ID;
#else
  return false;
#endif
}

void runCint(TestEnsembleIf* ensemble, const std::string& cintStr) {
  folly::test::TemporaryFile file;
  XLOG(INFO) << " Cint file " << file.path().c_str();
  folly::writeFull(file.fd(), cintStr.c_str(), cintStr.size());
  auto cmd = fmt::format("cint {}\n", file.path().c_str());
  std::string out;
  ensemble->runDiagCommand(cmd, out, std::nullopt);
}

void setEcmpMemberStatus(const TestEnsembleIf* ensemble) {
  auto asic = checkSameAndGetAsicForTesting(ensemble->getL3Asics());
  if (asic->getAsicVendor() != HwAsic::AsicVendor::ASIC_VENDOR_BCM) {
    return;
  }
  constexpr auto kSetEcmpMemberStatus = R"(
  cint_reset();
  bcm_l3_egress_ecmp_member_status_set(0, 100003, BCM_L3_ECMP_DYNAMIC_MEMBER_FORCE_UP);
  bcm_l3_egress_ecmp_member_status_set(0, 100004, BCM_L3_ECMP_DYNAMIC_MEMBER_FORCE_UP);
  bcm_l3_egress_ecmp_member_status_set(0, 100005, BCM_L3_ECMP_DYNAMIC_MEMBER_FORCE_UP);
  bcm_l3_egress_ecmp_member_status_set(0, 100006, BCM_L3_ECMP_DYNAMIC_MEMBER_FORCE_UP);
  bcm_l3_egress_ecmp_member_status_set(0, 100007, BCM_L3_ECMP_DYNAMIC_MEMBER_FORCE_UP);
  bcm_l3_egress_ecmp_member_status_set(0, 100008, BCM_L3_ECMP_DYNAMIC_MEMBER_FORCE_UP);
  bcm_l3_egress_ecmp_member_status_set(0, 100009, BCM_L3_ECMP_DYNAMIC_MEMBER_FORCE_UP);
  bcm_l3_egress_ecmp_member_status_set(0, 100010, BCM_L3_ECMP_DYNAMIC_MEMBER_FORCE_UP);
  )";
  runCint(const_cast<TestEnsembleIf*>(ensemble), kSetEcmpMemberStatus);
}

bool validateFlowSetTable(
    const HwSwitch* /*unit*/,
    const bool /*expectFlowsetSizeZero*/) {
  return false;
}

int getL3EcmpDlbFailPackets(const TestEnsembleIf* /*ensemble*/) {
  throw FbossError("getL3EcmpDlbFailPackets : unsupported in Sai Switch");
}

} // namespace facebook::fboss::utility
