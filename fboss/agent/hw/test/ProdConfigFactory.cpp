/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/ProdConfigFactory.h"
#include "fboss/agent/test/TestUtils.h"

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwTestCoppUtils.h"
#include "fboss/agent/hw/test/dataplane_tests/HwTestPfcUtils.h"
#include "fboss/agent/test/utils/AclTestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/DscpMarkingUtils.h"
#include "fboss/agent/test/utils/NetworkAITestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PfcTestUtils.h"
#include "fboss/agent/test/utils/QueuePerHostTestUtils.h"
#include "fboss/agent/test/utils/UdfTestUtils.h"

namespace {
auto constexpr kTopLabel = 5000;
} // namespace

namespace facebook::fboss::utility {

namespace {
// Prod RTSW-family PGs: PG2/PG6 lossless, PG7 lossy.
const std::vector<int>& kLosslessPgIds() {
  static const std::vector<int> pgIds{2, 6};
  return pgIds;
}
const std::vector<int>& kLossyPgIds() {
  static const std::vector<int> pgIds{7};
  return pgIds;
}
// Prod masks the BTH opcode to its low 7 bits, unlike the 0xFF
// utility::kUdfRoceOpcodeMask used elsewhere. Same result for opcode 17.
constexpr int8_t kProdRoceOpcodeMask = 0x7F;

// verifyFlowletAcls() looks this up by name, and on SAI reads the counter back
// under the ACL's own name.
void addUdfRoceOpcodeAcl(cfg::SwitchConfig& config, bool isSai) {
  cfg::AclEntry acl;
  acl.name() = utility::kUdfAclRoceOpcodeName;
  acl.actionType() = cfg::AclActionType::PERMIT;
  auto asicType = checkSameAndGetAsicType(config);
  if (asicType == cfg::AsicType::ASIC_TYPE_CHENAB ||
      asicType == cfg::AsicType::ASIC_TYPE_CHENAB2) {
    acl.etherType() = cfg::EtherType::IPv6;
  }
  if (isSai) {
    utility::addUdfTableToAcl(
        &acl,
        utility::kUdfAclRoceOpcodeGroupName,
        {utility::kUdfRoceOpcodeAck},
        {kProdRoceOpcodeMask});
  } else {
    acl.udfGroups() = {utility::kUdfAclRoceOpcodeGroupName};
    acl.roceOpcode() = utility::kUdfRoceOpcodeAck;
  }
  utility::addAcl(&config, acl, cfg::AclStage::INGRESS);
  utility::addAclStat(
      &config,
      utility::kUdfAclRoceOpcodeName,
      isSai ? utility::kUdfAclRoceOpcodeName : utility::kUdfAclRoceOpcodeStats,
      {cfg::CounterType::PACKETS, cfg::CounterType::BYTES});
}
} // namespace

/*
 * Setup and enable Olympic QoS on the given config. Common among all platforms;
 * most use only Olympic QoS, but some roles such as MHNIC RSWs use this
 * together with queue-per-host mapping.
 */
void addOlympicQosToConfig(
    cfg::SwitchConfig& config,
    const HwAsic* hwAsic,
    bool enableStrictPriority) {
  addOlympicQosMaps(config, {hwAsic});
  if (enableStrictPriority) {
    addFswRswAllSPOlympicQueueConfig(&config, {hwAsic});
  } else {
    addOlympicQueueConfig(&config, {hwAsic});
  }
}

void addOlympicQosToConfig(
    cfg::SwitchConfig& config,
    const HwSwitch* hwSwitch,
    bool enableStrictPriority) {
  addOlympicQosToConfig(
      config, hwSwitch->getPlatform()->getAsic(), enableStrictPriority);
}

void addNetworkAIQosToConfig(cfg::SwitchConfig& config, const HwAsic* hwAsic) {
  addNetworkAIQosMaps(config, {hwAsic});
  auto streamType =
      *hwAsic->getQueueStreamTypes(cfg::PortType::INTERFACE_PORT).begin();
  // queue configuration is different
  addNetworkAIQueueConfig(
      &config, streamType, cfg::QueueScheduling::STRICT_PRIORITY, hwAsic);
}

void addNetworkAIQosToConfig(
    cfg::SwitchConfig& config,
    const HwSwitch* hwSwitch) {
  auto hwAsic = hwSwitch->getPlatform()->getAsic();
  addNetworkAIQosToConfig(config, hwAsic);
}

void addLoadBalancerToConfig(
    cfg::SwitchConfig& config,
    const HwSwitch* hwSwitch,
    LBHash hashType) {
  addLoadBalancerToConfig(config, hwSwitch->getPlatform()->getAsic(), hashType);
}

void addMplsConfig(cfg::SwitchConfig& config) {
  config.staticMplsRoutesWithNhops()->resize(1);
  config.staticMplsRoutesWithNhops()[0].ingressLabel() = kTopLabel;
  std::vector<NextHopThrift> nexthops;
  nexthops.resize(1);
  MplsAction swap;
  swap.action() = MplsActionCode::SWAP;
  swap.swapLabel() = 101;
  nexthops[0].mplsAction() = swap;
  nexthops[0].address() = network::toBinaryAddress(folly::IPAddress("1::2"));
  config.staticMplsRoutesWithNhops()[0].nexthops() = nexthops;
}

/*
 * Straightforward convenience function. Uses values defined in getUplinksCount,
 * from HwInitAndExitBenchmarkHelper.cpp, to return the number of uplinks for
 * each platform.
 */
uint16_t uplinksCountFromSwitch(PlatformType mode) {
  using PM = PlatformType;
  switch (mode) {
    case PM::PLATFORM_WEDGE100:
    case PM::PLATFORM_WEDGE400C:
    case PM::PLATFORM_WEDGE800CACT:
    case PM::PLATFORM_WEDGE800CNHP:
    case PM::PLATFORM_WEDGE800BACT:
    case PM::PLATFORM_WEDGE800BNHP:
    case PM::PLATFORM_WEDGE400:
    case PM::PLATFORM_YAMP:
    case PM::PLATFORM_MORGAN800CC:
    case PM::PLATFORM_MINIPACK:
    case PM::PLATFORM_ELBERT:
    case PM::PLATFORM_FUJI:
    case PM::PLATFORM_GALAXY_LC:
    case PM::PLATFORM_GALAXY_FC:
    case PM::PLATFORM_DARWIN:
    case PM::PLATFORM_DARWIN48V:
    case PM::PLATFORM_TAHAN800BC:
    case PM::PLATFORM_TAHANSB800BC:
    case PM::PLATFORM_ICECUBE800BC:
    case PM::PLATFORM_ICETEA800BC:
    case PM::PLATFORM_MONTBLANC:
    case PM::PLATFORM_M4061NHP:
    case PM::PLATFORM_M4062NHP:
    case PM::PLATFORM_M5120CSC:
      return 4;
    case PM::PLATFORM_MINIPACK3N:
    case PM::PLATFORM_YANGRA:
    case PM::PLATFORM_YANGRA2:
      return 64;
    default:
      throw FbossError(
          "provided PlatformType: ",
          mode,
          " has not been defined for uplinksCountFromSwitch");
  }
}

uint16_t uplinksCountFromSwitch(const HwSwitch* hwSwitch) {
  return uplinksCountFromSwitch(hwSwitch->getPlatform()->getType());
}

cfg::PortSpeed getPortSpeed(PlatformType platformType) {
  cfg::PortSpeed portSpeed = cfg::PortSpeed::HUNDREDG;
  // override speed for certain platforms based on the
  // mode of the asic
  switch (platformType) {
    case PlatformType::PLATFORM_FUJI:
    case PlatformType::PLATFORM_ELBERT:
      portSpeed = cfg::PortSpeed::TWOHUNDREDG;
      break;
    case PlatformType::PLATFORM_MONTBLANC:
    case PlatformType::PLATFORM_M4061NHP:
      portSpeed = cfg::PortSpeed::FOURHUNDREDG;
      break;
    case PlatformType::PLATFORM_MINIPACK3N:
      // Spectrum4, 64 x 800G; every interface controlling port supports 800G.
      portSpeed = cfg::PortSpeed::EIGHTHUNDREDG;
      break;
    case PlatformType::PLATFORM_TAHAN800BC:
    case PlatformType::PLATFORM_TAHANSB800BC:
    case PlatformType::PLATFORM_WEDGE800BACT:
    case PlatformType::PLATFORM_WEDGE800BNHP:
    case PlatformType::PLATFORM_WEDGE800CACT:
    case PlatformType::PLATFORM_WEDGE800CNHP:
      portSpeed = cfg::PortSpeed::FOURHUNDREDG;
      break;
    default:
      /* do nothing */
      break;
  }
  return portSpeed;
}

cfg::PortSpeed getPortSpeed(const HwSwitch* hwSwitch) {
  return getPortSpeed(hwSwitch->getPlatform()->getType());
}
/*
 * Adds queue-per-host mapping to config, based on HwQueuePerHostRouteTests.cpp.
 * Other helper functions (i.e. addRoutes, updateRoutesClassID) are called on
 * the test fixture's setup() phase.
 */
void addQueuePerHostToConfig(cfg::SwitchConfig& config, bool isSai) {
  utility::addQueuePerHostQueueConfig(&config);
  utility::addQueuePerHostAcls(&config, isSai);
}

cfg::SwitchConfig createProdRtswConfig(
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& masterLogicalPortIds,
    const HwSwitchEnsemble* ensemble) {
  auto platform = hwSwitch->getPlatform();
  auto hwAsic = platform->getAsic();

  // its the same speed used for the uplink and downlink for now
  auto portSpeed = getPortSpeed(hwSwitch);

  std::vector<PortID> uplinks, downlinks;
  // Create initial config to which we can add the rest of the features.
  auto config = createRtswUplinkDownlinkConfig(
      hwSwitch,
      const_cast<HwSwitchEnsemble*>(ensemble),
      masterLogicalPortIds,
      portSpeed,
      hwAsic->desiredLoopbackModes(),
      uplinks,
      downlinks);

  addCpuQueueConfig(config, ensemble->getL3Asics(), ensemble->isSai());

  if (hwAsic->isSupported(HwAsic::Feature::L3_QOS)) {
    addNetworkAIQosToConfig(config, hwSwitch);
  }
  if (hwAsic->isSupported(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    addLoadBalancerToConfig(config, hwSwitch, LBHash::FULL_HASH);
  }

  setDefaultCpuTrafficPolicyConfig(
      config, std::vector<const HwAsic*>({hwAsic}), ensemble->isSai());
  if (hwSwitch->getPlatform()->getAsic()->isSupported(HwAsic::Feature::PFC)) {
    // pfc works reliably only in mmu lossless mode
    utility::addUplinkDownlinkPfcConfig(config, hwSwitch, uplinks, downlinks);
  }
  return config;
}

/*
 * Creates and returns a SwitchConfig which is as close to what you would find
 * in a production RSW as possible. If more features are desired, they can be
 * added in this function.
 *
 * Can be used anywhere it might be useful to have a prod RSW config.
 */
cfg::SwitchConfig createProdRswConfig(
    const std::vector<const HwAsic*>& asics,
    PlatformType platformType,
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai,
    bool enableStrictPriority) {
  auto hwAsic = checkSameAndGetAsicForTesting(asics);
  auto numUplinks = uplinksCountFromSwitch(platformType);

  // its the same speed used for the uplink and downlink for now
  auto uplinkSpeed = getPortSpeed(platformType);
  auto downlinkSpeed = getPortSpeed(platformType);

  // Create initial config to which we can add the rest of the features.
  auto config = createUplinkDownlinkConfig(
      platformMapping,
      hwAsic,
      platformType,
      supportsAddRemovePort,
      masterLogicalPortIds,
      numUplinks,
      uplinkSpeed,
      downlinkSpeed,
      hwAsic->desiredLoopbackModes());

  addCpuQueueConfig(config, asics, isSai);

  if (hwAsic->isSupported(HwAsic::Feature::L3_QOS)) {
    addOlympicQosToConfig(config, hwAsic, enableStrictPriority);
  }
  setDefaultCpuTrafficPolicyConfig(
      config, std::vector<const HwAsic*>({hwAsic}), isSai);
  if (hwAsic->isSupported(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    addLoadBalancerToConfig(config, hwAsic, LBHash::FULL_HASH);
  }

  if (hwAsic->isSupported(HwAsic::Feature::MPLS) && !isSai) {
    addMplsConfig(config);
  }
  return config;
}

cfg::SwitchConfig createProdRswConfig(
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai,
    bool enableStrictPriority) {
  return createProdRswConfig(
      std::vector<const HwAsic*>({hwSwitch->getPlatform()->getAsic()}),
      hwSwitch->getPlatform()->getType(),
      hwSwitch->getPlatform()->getPlatformMapping(),
      hwSwitch->getPlatform()->supportsAddRemovePort(),
      masterLogicalPortIds,
      isSai,
      enableStrictPriority);
}
/*
 * Returns a prod-setting FSW config. Can be modified as desired for more
 * features to be added to config.
 */
cfg::SwitchConfig createProdFswConfig(
    const HwAsic* hwAsic,
    PlatformType platformType,
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai,
    bool enableStrictPriority) {
  auto numUplinks = uplinksCountFromSwitch(platformType);

  // its the same speed used for the uplink and downlink for now
  auto uplinkSpeed = getPortSpeed(platformType);
  auto downlinkSpeed = getPortSpeed(platformType);

  auto config = createUplinkDownlinkConfig(
      platformMapping,
      hwAsic,
      platformType,
      supportsAddRemovePort,
      masterLogicalPortIds,
      numUplinks,
      uplinkSpeed,
      downlinkSpeed,
      hwAsic->desiredLoopbackModes());

  addCpuQueueConfig(config, {hwAsic}, isSai);
  if (hwAsic->isSupported(HwAsic::Feature::L3_QOS)) {
    addOlympicQosToConfig(config, hwAsic, enableStrictPriority);
  }
  setDefaultCpuTrafficPolicyConfig(
      config, std::vector<const HwAsic*>({hwAsic}), isSai);
  if (hwAsic->isSupported(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    addLoadBalancerToConfig(config, hwAsic, LBHash::HALF_HASH);
  }
  if (hwAsic->isSupported(HwAsic::Feature::MPLS) && !isSai) {
    addMplsConfig(config);
  }
  return config;
}

cfg::SwitchConfig createProdFswConfig(
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai,
    bool enableStrictPriority) {
  return createProdFswConfig(
      hwSwitch->getPlatform()->getAsic(),
      hwSwitch->getPlatform()->getType(),
      hwSwitch->getPlatform()->getPlatformMapping(),
      hwSwitch->getPlatform()->supportsAddRemovePort(),
      masterLogicalPortIds,
      isSai,
      enableStrictPriority);
}

cfg::SwitchConfig createProdRswMhnicConfig(
    const HwAsic* hwAsic,
    PlatformType platformType,
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai) {
  auto numUplinks = uplinksCountFromSwitch(platformType);
  auto uplinkSpeed = getPortSpeed(platformType);
  auto downlinkSpeed = getPortSpeed(platformType);

  auto config = createUplinkDownlinkConfig(
      platformMapping,
      hwAsic,
      platformType,
      supportsAddRemovePort,
      masterLogicalPortIds,
      numUplinks,
      uplinkSpeed,
      downlinkSpeed,
      hwAsic->desiredLoopbackModes());

  addCpuQueueConfig(config, {hwAsic}, isSai);
  setDefaultCpuTrafficPolicyConfig(
      config, std::vector<const HwAsic*>({hwAsic}), isSai);
  if (hwAsic->isSupported(HwAsic::Feature::L3_QOS)) {
    addQueuePerHostToConfig(config, isSai);
    // DSCP Marking ACLs must be programmed AFTER queue-per-host ACLs or else
    // traffic matching DSCP Marking ACLs will only hit DSCP Marking ACLs and
    // thus suffer from noisy neighbor.
    // The queue-per-host ACLs match on traffic to downlinks.
    // Thus, putting DSCP Marking ACLs before queue-per-host ACLs would cause
    // noisy neighbor problem for traffic between ports connected to the same
    // switch.
    utility::addDscpMarkingAcls(hwAsic, &config, isSai);
  }
  if (hwAsic->isSupported(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    addLoadBalancerToConfig(config, hwAsic, LBHash::FULL_HASH);
  }
  if (hwAsic->isSupported(HwAsic::Feature::MPLS) && !isSai) {
    addMplsConfig(config);
  }
  return config;
}

cfg::SwitchConfig createProdRswMhnicConfig(
    const HwSwitch* hwSwitch,
    const std::vector<PortID>& masterLogicalPortIds,
    bool isSai) {
  return createProdRswMhnicConfig(
      hwSwitch->getPlatform()->getAsic(),
      hwSwitch->getPlatform()->getType(),
      hwSwitch->getPlatform()->getPlatformMapping(),
      hwSwitch->getPlatform()->supportsAddRemovePort(),
      masterLogicalPortIds,
      isSai);
}
cfg::SwitchConfig createProdMmuLosslessRoleConfig(
    const std::vector<const HwAsic*>& asics,
    PlatformType platformType,
    const PlatformMapping* platformMapping,
    bool supportsAddRemovePort,
    const std::vector<PortID>& masterLogicalPortIds,
    const TestEnsembleIf* ensemble,
    ProdMmuLosslessRole role,
    bool isSai) {
  auto hwAsic = checkSameAndGetAsicForTesting(asics);
  auto portSpeed = getPortSpeed(platformType);

  auto config = createUplinkDownlinkConfig(
      platformMapping,
      hwAsic,
      platformType,
      supportsAddRemovePort,
      masterLogicalPortIds,
      uplinksCountFromSwitch(platformType),
      portSpeed,
      portSpeed,
      hwAsic->desiredLoopbackModes());

  const bool addArsConfig = hwAsic->isSupported(HwAsic::Feature::ARS);
  // Append to the table createUplinkDownlinkConfig() already made rather than
  // re-adding the group: addAclTableGroup() overwrites an existing group,
  // dropping its tables.
  if (addArsConfig && FLAGS_enable_acl_table_group) {
    if (auto* aclTableGroup =
            utility::getAclTableGroup(config, cfg::AclStage::INGRESS)) {
      for (auto& aclTable : *aclTableGroup->aclTables()) {
        aclTable.udfGroups()->push_back(utility::kUdfAclRoceOpcodeGroupName);
        aclTable.udfGroups()->push_back(utility::kRoceUdfFlowletGroupName);
      }
    }
  }

  // STSW/SUSW sit at the top of the hierarchy and have no uplinks.
  std::vector<PortID> uplinks, downlinks;
  for (const auto& portId : masterLogicalPortIds) {
    // May have been dropped as subsumed by a prior speed update.
    if (utility::findCfgPortIf(config, portId) == config.ports()->end()) {
      continue;
    }
    downlinks.push_back(portId);
  }
  if (role == ProdMmuLosslessRole::RTSW || role == ProdMmuLosslessRole::FTSW) {
    auto splitAt = downlinks.begin() + downlinks.size() / 2;
    uplinks.assign(downlinks.begin(), splitAt);
    downlinks.erase(downlinks.begin(), splitAt);
  }

  addCpuQueueConfig(config, asics, isSai);
  if (hwAsic->isSupported(HwAsic::Feature::L3_QOS)) {
    addNetworkAIQosToConfig(config, hwAsic);
  }
  setDefaultCpuTrafficPolicyConfig(
      config, std::vector<const HwAsic*>({hwAsic}), isSai);
  if (hwAsic->isSupported(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
    addLoadBalancerToConfig(config, hwAsic, LBHash::FULL_HASH);
  }

  // Gated on L3_QOS as well as PFC: setupUplinkDownlinkPfc() merges into the
  // QoS policy added above, which only exists when L3_QOS is supported.
  if (hwAsic->isSupported(HwAsic::Feature::PFC) &&
      hwAsic->isSupported(HwAsic::Feature::L3_QOS)) {
    utility::PfcQosMapParams qosMapParams;
    qosMapParams.tcToPg = {{0, kLossyPgIds().front()}};
    utility::setupUplinkDownlinkPfc(
        ensemble,
        config,
        uplinks,
        downlinks,
        kLosslessPgIds(),
        kLossyPgIds(),
        qosMapParams);
  }

  if (addArsConfig) {
    std::vector<PortID> allLinks(uplinks);
    allLinks.insert(allLinks.end(), downlinks.begin(), downlinks.end());
    // Prod runs per-packet spray with flowlet disabled, i.e. FIXED_ASSIGNMENT,
    // as the backup mode. Chenab does not support future port load, so gate it
    // on the ASIC.
    utility::addFlowletConfigs(
        config,
        allLinks,
        isSai,
        cfg::SwitchingMode::PER_PACKET_QUALITY,
        cfg::SwitchingMode::FIXED_ASSIGNMENT,
        hwAsic->isSupported(HwAsic::Feature::ARS_FUTURE_PORT_LOAD));
    config.udfConfig() =
        utility::addUdfAclConfig(kUdfOffsetBthOpcode | kUdfOffsetBthReserved);
    addUdfRoceOpcodeAcl(config, isSai);
    // Name must match prod for verifyFlowletAcls() to find it.
    const std::string kSelectiveEnableAclName{"flowlet-selective-enable"};
    const std::string kSelectiveEnableStatsName{"flowlet-selective-stats"};
    utility::addFlowletAcl(
        config,
        isSai,
        kSelectiveEnableAclName,
        isSai ? kSelectiveEnableAclName : kSelectiveEnableStatsName);
  }

  return config;
}

} // namespace facebook::fboss::utility
