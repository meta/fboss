// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
#include "fboss/agent/hw/bcm/BcmEcmpUtils.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/test/HwTestThriftHandler.h"

extern "C" {
#include <bcm/l3.h>
#include <bcm/switch.h>
}

namespace facebook::fboss::utility {

const RouterID kRid(0);

bool HwTestThriftHandler::verifyEcmpForFlowletSwitchingHandler(
    std::unique_ptr<CIDRNetwork> ip,
    std::unique_ptr<::facebook::fboss::state::SwitchSettingsFields> settings,
    bool flowletEnabled) {
  XLOG(INFO)
      << "verifyEcmpForFlowletSwitchingHandler: Starting verification for IP "
      << folly::IPAddress(*ip->IPAddress()).str() << "/"
      << static_cast<int>(*ip->mask())
      << ", flowletEnabled: " << (flowletEnabled ? "true" : "false");

  const auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch_);

  // Convert CIDRNetwork to folly::CIDRNetwork
  folly::CIDRNetwork follyPrefix{
      folly::IPAddress(*ip->IPAddress()), static_cast<uint8_t>(*ip->mask())};

  auto ecmp = getEgressIdForRoute(
      bcmSwitch, follyPrefix.first, follyPrefix.second, kRid);
  XLOG(DBG2) << "verifyEcmpForFlowletSwitchingHandler: Got ECMP ID " << ecmp
             << " for route";
  bcm_l3_egress_ecmp_t existing;
  bcm_l3_egress_ecmp_t_init(&existing);
  existing.ecmp_intf = ecmp;
  existing.flags |= BCM_L3_WITH_ID;
  int pathsInHwCount;
  bool isVerified = true;
  bcm_l3_ecmp_get(bcmSwitch->getUnit(), &existing, 0, nullptr, &pathsInHwCount);
  XLOG(DBG2) << "verifyEcmpForFlowletSwitchingHandler: Found " << pathsInHwCount
             << " paths in hardware for ECMP ID " << ecmp;

  // Extract flowlet configuration from settings
  auto flowletCfg = settings->flowletSwitchingConfig();
  uint32 flowletTableSize =
      flowletCfg ? static_cast<uint16>(*flowletCfg->flowletTableSize()) : 0;

  XLOG(DBG2) << "verifyEcmpForFlowletSwitchingHandler: Flowlet table size: "
             << flowletTableSize;

  if (flowletEnabled && flowletCfg) {
    auto dynamicMode = getFlowletDynamicMode(*flowletCfg->switchingMode());
    XLOG(DBG2)
        << "verifyEcmpForFlowletSwitchingHandler: Flowlet enabled with dynamic mode: "
        << dynamicMode
        << ", inactivity interval: " << *flowletCfg->inactivityIntervalUsecs()
        << " usecs";
    if ((existing.dynamic_mode != dynamicMode) ||
        (existing.dynamic_age != *flowletCfg->inactivityIntervalUsecs()) ||
        (existing.dynamic_size != flowletTableSize)) {
      XLOG(ERR)
          << "verifyEcmpForFlowletSwitchingHandler: Flowlet configuration mismatch - "
          << "dynamic_mode expected: " << dynamicMode
          << ", actual: " << existing.dynamic_mode << ", dynamic_age expected: "
          << *flowletCfg->inactivityIntervalUsecs()
          << ", actual: " << existing.dynamic_age
          << ", dynamic_size expected: " << flowletTableSize
          << ", actual: " << existing.dynamic_size;
      isVerified = false;
    }
  } else {
    if ((existing.dynamic_mode != BCM_L3_ECMP_DYNAMIC_MODE_DISABLED) ||
        (existing.dynamic_size != 0)) {
      XLOG(ERR)
          << "verifyEcmpForFlowletSwitchingHandler: Flowlet should be disabled but found - "
          << "dynamic_mode: " << existing.dynamic_mode
          << " (expected: " << BCM_L3_ECMP_DYNAMIC_MODE_DISABLED << "), "
          << "dynamic_size: " << existing.dynamic_size << " (expected: 0)";
      isVerified = false;
    }
  }
  if ((ecmp > 200128) || (ecmp < 200000)) {
    XLOG(ERR)
        << "verifyEcmpForFlowletSwitchingHandler: ECMP ID out of expected range - "
        << "ecmp: " << ecmp << " (expected range: 200000-200128)";
    isVerified = false;
  }

  auto ecmp_members = getEcmpGroupInHw(bcmSwitch, ecmp, pathsInHwCount);
  XLOG(DBG2) << "verifyEcmpForFlowletSwitchingHandler: Retrieved "
             << ecmp_members.size() << " ECMP members from hardware";

  for (const auto& ecmp_member : ecmp_members) {
    int status = -1;
    bcm_l3_egress_ecmp_member_status_get(
        bcmSwitch->getUnit(), ecmp_member, &status);
    if (flowletEnabled) {
      if (status < BCM_L3_ECMP_DYNAMIC_MEMBER_HW) {
        XLOG(ERR)
            << "verifyEcmpForFlowletSwitchingHandler: ECMP member status insufficient for flowlet - "
            << "member: " << ecmp_member << ", status: " << status
            << " (expected >= " << BCM_L3_ECMP_DYNAMIC_MEMBER_HW << ")";
        isVerified = false;
      }
    }
  }
  XLOG(INFO) << "verifyEcmpForFlowletSwitchingHandler: Verification "
             << (isVerified ? "PASSED" : "FAILED") << " for IP "
             << folly::IPAddress(*ip->IPAddress()).str() << "/"
             << static_cast<int>(*ip->mask());
  return isVerified;
}

/**
 * @brief Verifies the port flowlet configuration for a given network prefix
 * @param prefix Network prefix (CIDRNetwork) to verify flowlet configuration
 * for
 * @param cfg Port flowlet configuration containing scaling factor, load weight,
 * and queue weight
 * @param flowletEnable Boolean indicating whether flowlet switching is enabled
 * @return True if the port flowlet configuration in hardware matches the
 * expected configuration, false otherwise. For TH3 devices, verifies that
 * scaling factor, load weight, and queue weight match the provided
 * configuration. For TH4 devices (with ARS_PORT_ATTRIBUTES support), verifies
 * that default values (-1) are set since port flowlet configs are not set in
 * the egress object.
 */

bool HwTestThriftHandler::verifyPortFlowletConfig(
    std::unique_ptr<CIDRNetwork> prefix,
    std::unique_ptr<cfg::PortFlowletConfig> cfg,
    bool flowletEnable) {
  const auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch_);

  // Convert CIDRNetwork to folly::CIDRNetwork
  folly::CIDRNetwork follyPrefix{
      folly::IPAddress(*prefix->IPAddress()),
      static_cast<uint8_t>(*prefix->mask())};

  auto ecmp = getEgressIdForRoute(
      bcmSwitch, follyPrefix.first, follyPrefix.second, kRid);
  int pathsInHwCount;
  bool isVerified = true;

  bcm_l3_egress_ecmp_t existing;
  bcm_l3_egress_ecmp_t_init(&existing);
  existing.ecmp_intf = ecmp;
  existing.flags |= BCM_L3_WITH_ID;
  bcm_l3_ecmp_get(bcmSwitch->getUnit(), &existing, 0, nullptr, &pathsInHwCount);

  auto ecmp_members = getEcmpGroupInHw(bcmSwitch, ecmp, pathsInHwCount);
  for (const auto& ecmp_member : ecmp_members) {
    int status = -1;
    bcm_l3_egress_ecmp_member_status_get(
        bcmSwitch->getUnit(), ecmp_member, &status);
    if (flowletEnable) {
      if (status < BCM_L3_ECMP_DYNAMIC_MEMBER_HW) {
        isVerified = false;
      }
    }
    bcm_l3_egress_t egress;
    bcm_l3_egress_t_init(&egress);
    bcm_l3_egress_get(0, ecmp_member, &egress);
    if (flowletEnable &&
        !(hwSwitch_->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::ARS_PORT_ATTRIBUTES))) {
      // verify the port flowlet config values only in TH3
      // since this port flowlet configs are set in egress object in TH3
      CHECK_EQ(egress.dynamic_scaling_factor, *cfg->scalingFactor());
      CHECK_EQ(egress.dynamic_load_weight, *cfg->loadWeight());
      CHECK_EQ(egress.dynamic_queue_size_weight, *cfg->queueWeight());
    } else {
      // verify the default values in TH4
      // since this won't be set in egress object in TH4
      CHECK_EQ(egress.dynamic_scaling_factor, -1);
      CHECK_EQ(egress.dynamic_load_weight, -1);
      CHECK_EQ(egress.dynamic_queue_size_weight, -1);
    }
  }
  return isVerified;
}

bool HwTestThriftHandler::validateFlowSetTable(
    const bool expectFlowsetSizeZero) {
  bool isVerified = true;

  XLOG(DBG2) << "validateFlowSetTable with "
             << "expectFlowsetSizeZero: " << expectFlowsetSizeZero;
#if (                                      \
    defined(BCM_SDK_VERSION_GTE_6_5_26) && \
    !defined(BCM_SDK_VERSION_GTE_6_5_28))

  int maxEntries = 0;
  const auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch_);

  int rv = bcm_switch_object_count_get(
      bcmSwitch->getUnit(), bcmSwitchObjectEcmpDynamicFlowSetMax, &maxEntries);
  bcmCheckError(rv, "Failed to get bcmSwitchObjectEcmpDynamicFlowSetMax");

  int usedEntries = 0;
  rv = bcm_switch_object_count_get(
      bcmSwitch->getUnit(),
      bcmSwitchObjectEcmpDynamicFlowSetUsed,
      &usedEntries);
  bcmCheckError(rv, "Failed to get bcmSwitchObjectEcmpDynamicFlowSetUsed");

  int freeEntries = 0;
  rv = bcm_switch_object_count_get(
      bcmSwitch->getUnit(),
      bcmSwitchObjectEcmpDynamicFlowSetFree,
      &freeEntries);
  bcmCheckError(rv, "Failed to get bcmSwitchObjectEcmpDynamicFlowSetFree");

  XLOG(DBG2) << "version check passed - maxEntries: " << maxEntries
             << ", usedEntries: " << usedEntries
             << ", freeEntries: " << freeEntries;

  if (expectFlowsetSizeZero) {
    CHECK_EQ(freeEntries, 0);
  }

  if (maxEntries != 32768) {
    XLOG(ERR) << "Max Entries are not as expected: " << maxEntries;
    isVerified = false;
  }

  if ((maxEntries - usedEntries) != freeEntries) {
    XLOG(ERR) << "Free entries: " << freeEntries
              << ", unexpected when looking at the used entries : "
              << usedEntries << " , max entries: " << maxEntries;
    isVerified = false;
  }
#else
  XLOG(DBG2)
      << "version check failed - required BCM_SDK_VERSION_GTE_6_5_26 && !BCM_SDK_VERSION_GTE_6_5_28";
#endif
  return isVerified;
}

bool HwTestThriftHandler::verifyEcmpForNonFlowlet(
    std::unique_ptr<CIDRNetwork> prefix,
    std::unique_ptr<::facebook::fboss::state::SwitchSettingsFields> settings,
    bool expectFlowsetFree) {
  const auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch_);

  // Convert CIDRNetwork to folly::CIDRNetwork
  folly::CIDRNetwork follyPrefix{
      folly::IPAddress(*prefix->IPAddress()),
      static_cast<uint8_t>(*prefix->mask())};

  auto ecmp = getEgressIdForRoute(
      bcmSwitch, follyPrefix.first, follyPrefix.second, kRid);

  bcm_l3_egress_ecmp_t existing;
  bcm_l3_egress_ecmp_t_init(&existing);
  existing.ecmp_intf = ecmp;
  existing.flags |= BCM_L3_WITH_ID;
  int pathsInHwCount;
  bcm_l3_ecmp_get(bcmSwitch->getUnit(), &existing, 0, nullptr, &pathsInHwCount);

  // Extract flowlet configuration from settings
  auto flowletCfg = settings->flowletSwitchingConfig();

  // Check dynamic mode matches expected non-DLB behavior.
  // If primary switchingMode is already non-DLB (PER_PACKET_RANDOM),
  // ECMP uses that mode. Otherwise, check backup mode for overflow scenarios.
  if (flowletCfg) {
    auto primaryMode = *flowletCfg->switchingMode();
    if (primaryMode == cfg::SwitchingMode::PER_PACKET_QUALITY ||
        primaryMode == cfg::SwitchingMode::FLOWLET_QUALITY) {
      // DLB primary mode - check backup mode for overflow ECMP behavior
      auto backupMode = *flowletCfg->backupSwitchingMode();
      CHECK_EQ(existing.dynamic_mode, getFlowletDynamicMode(backupMode));
    } else {
      CHECK_EQ(existing.dynamic_mode, getFlowletDynamicMode(primaryMode));
    }
  }
  CHECK_EQ(existing.dynamic_age, 0);
  CHECK_EQ(existing.dynamic_size, 0);

  // TH3 only supports flowlet. Below checks don't apply
  if (hwSwitch_->getPlatform()->getAsic()->getAsicType() ==
      cfg::AsicType::ASIC_TYPE_TOMAHAWK3) {
    return true;
  }

  int freeEntries = 0;
#if (                                      \
    defined(BCM_SDK_VERSION_GTE_6_5_26) && \
    !defined(BCM_SDK_VERSION_GTE_6_5_28))
  bcm_switch_object_count_get(
      bcmSwitch->getUnit(),
      bcmSwitchObjectEcmpDynamicFlowSetFree,
      &freeEntries);
#endif
  if (expectFlowsetFree) {
    CHECK_GE(freeEntries, 256);
  } else {
    // CS00012398177
    CHECK_EQ(freeEntries, 256);
  }
  return true;
}

} // namespace facebook::fboss::utility
