/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fmt/format.h>
#include <folly/IPAddress.h>

#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwLinkStateDependentTest.h"
#include "fboss/agent/hw/test/HwTestAclUtils.h"
#include "fboss/agent/hw/test/HwTestFlowletSwitchingUtils.h"
#include "fboss/agent/hw/test/LoadBalancerUtils.h"
#include "fboss/agent/hw/test/ProdConfigFactory.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/UdfTestUtils.h"

using namespace facebook::fboss::utility;

namespace {
static folly::IPAddressV6 kAddr1{"2803:6080:d038:3063::"};
static folly::IPAddressV6 kAddr2{"2803:6080:d038:3000::"};
folly::CIDRNetwork kAddr1Prefix{folly::IPAddress("2803:6080:d038:3063::"), 64};
folly::CIDRNetwork kAddr2Prefix{folly::IPAddress("2803:6080:d038:3000::"), 64};
constexpr auto kAddr3 = "2803:6080:d048";
constexpr auto kAddr4 = "2803:6080:e048";
const int kLoadWeight1 = 70;
const int kLoadWeight2 = 60;
const int kQueueWeight1 = 30;
const int kQueueWeight2 = 20;
const int kInactivityIntervalUsecs1 = 128;
const int kInactivityIntervalUsecs2 = 256;
const int kFlowletTableSize1 = 1024;
const int kFlowletTableSize2 = 2048;
const int kMinFlowletTableSize = 256;
static int kUdpProto(17);
static int kUdpDstPort(4791);
constexpr auto kAclName = "flowlet";
constexpr auto kAclCounterName = "flowletStat";
constexpr auto kDstIp = "2001::/16";
const int kMaxLinks = 4;
const int kEcmpStartId = 200000;
} // namespace

namespace facebook::fboss {

class HwArsTest : public HwLinkStateDependentTest {
 protected:
  void SetUp() override {
    FLAGS_flowletSwitchingEnable = true;
    FLAGS_flowletStatsEnable = true;
    HwLinkStateDependentTest::SetUp();
    ecmpHelper_ = std::make_unique<utility::EcmpSetupAnyNPorts6>(
        getProgrammedState(),
        getHwSwitch()->needL2EntryForNeighbor(),
        RouterID(0));
  }

  int kMaxDlbGroups() {
    return getHwSwitch()->getPlatform()->getAsic()->getMaxArsGroups().value();
  }

  // native BCM has access to BRCM IDs >=200128 cannot be configured as DLB
  // SAI does not have access to BRCM IDs. So SAI implementation cannot check
  // this. So leave the tests to use just under 128 to test upto the limit
  int kNumEcmp() const {
    return getHwSwitchEnsemble()->isSai() ? 63 : 64;
  }

  // 10 is from vendor internal code
  int kDefaultScalingFactor() const {
    return getHwSwitchEnsemble()->isSai() ? 0 : 10;
  }

  // 100 is from vendor internal code
  int kDefaultLoadWeight() const {
    return getHwSwitchEnsemble()->isSai() ? 0 : 100;
  }

  int kDefaultQueueWeight() const {
    return 0;
  }

  // SAI is multiple of port speed
  int kScalingFactor1() const {
    return getHwSwitchEnsemble()->isSai() ? 10 : 100;
  }

  int kScalingFactor2() const {
    return getHwSwitchEnsemble()->isSai() ? 20 : 200;
  }

  int kDefaultSamplingRate() const {
    return getHwSwitchEnsemble()->isSai() ? 1000 : 500000;
  }

  // SAI is 0.5us but SDK doesn't support yet
  // <1us not supported on TH3
  int kMinSamplingRate() const {
    bool isTH3 = getPlatform()->getAsic()->getAsicType() ==
        cfg::AsicType::ASIC_TYPE_TOMAHAWK3;
    return getHwSwitchEnsemble()->isSai() ? (isTH3 ? 1000 : 512)
                                          : (isTH3 ? 1000000 : 1953125);
  }

  cfg::SwitchConfig initialConfig() const override {
    auto cfg = getDefaultConfig();
    return cfg;
  }

  void resolveNextHopsAddRoute(const int linkCount) {
    for (int i = 0; i < linkCount; ++i) {
      this->resolveNextHop(PortDescriptor(masterLogicalPortIds()[i]));
    }
    this->addRoute(kAddr1, 64);
  }

  void resolveNextHopsAddRoute(
      std::vector<PortID> masterLogicalPortsIds,
      const folly::IPAddressV6 addr) {
    std::vector<PortDescriptor> portDescriptorIds;
    for (auto& portId : masterLogicalPortsIds) {
      this->resolveNextHop(PortDescriptor(portId));
      portDescriptorIds.emplace_back(portId);
    }
    this->addRoute(addr, 64, portDescriptorIds);
  }

  void setupEcmpGroups(int numEcmp) {
    int count = 0;
    int maxPortIndex = 32;
    for (int i = 1; i < maxPortIndex; i++) {
      for (int j = i + 1; j < maxPortIndex; j++) {
        for (int k = j + 1; k < maxPortIndex; k++) {
          std::vector<PortID> portIds;
          portIds.push_back(masterLogicalPortIds()[i]);
          portIds.push_back(masterLogicalPortIds()[j]);
          portIds.push_back(masterLogicalPortIds()[k]);
          resolveNextHopsAddRoute(
              std::move(portIds),
              folly::IPAddressV6(fmt::format("{}:{:x}::", kAddr3, count++)));
          if (count >= numEcmp) {
            return;
          }
        }
      }
    }
  }

  void verifyEcmpGroups(const cfg::SwitchConfig& cfg, int numEcmp) {
    auto portFlowletConfig =
        getPortFlowletConfig(kScalingFactor1(), kLoadWeight1, kQueueWeight1);
    for (int i = 0; i < numEcmp; i++) {
      auto currentIp = folly::IPAddress(fmt::format("{}:{:x}::", kAddr3, i));
      folly::CIDRNetwork currentPrefix{currentIp, 64};
      EXPECT_TRUE(
          utility::verifyEcmpForFlowletSwitching(
              getHwSwitch(),
              currentPrefix,
              *cfg.flowletSwitchingConfig(),
              portFlowletConfig,
              true /* flowletEnable */));
    }
  }

  void addFlowletAcl(cfg::SwitchConfig& cfg) const {
    auto* acl = utility::addAcl_DEPRECATED(&cfg, kAclName);
    acl->proto() = kUdpProto;
    acl->l4DstPort() = kUdpDstPort;
    acl->dstIp() = kDstIp;
    acl->udfGroups() = {utility::kRoceUdfFlowletGroupName};
    acl->roceBytes() = {utility::kRoceReserved};
    acl->roceMask() = {utility::kRoceReserved};
    utility::addEtherTypeToAcl(
        getPlatform()->getAsic(), acl, cfg::EtherType::IPv6);
    cfg::MatchAction matchAction = cfg::MatchAction();
    matchAction.flowletAction() = cfg::FlowletAction::FORWARD;
    // presence of UDF configuration causes the FP group to be cleared out
    // completely and re-populates the table again. It has a bug where counters
    // are not cleared before the group is destroyed. The specific method is
    // clearFPGroup. This only affects fake SDK. So leaving counter creation for
    // fake SDK out until the issue is appropriately resolved.
    if (getPlatform()->getAsic()->getAsicType() !=
        cfg::AsicType::ASIC_TYPE_FAKE) {
      matchAction.counter() = kAclCounterName;
      std::vector<cfg::CounterType> counterTypes{
          cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
      auto counter = cfg::TrafficCounter();
      *counter.name() = kAclCounterName;
      *counter.types() = counterTypes;
      cfg.trafficCounters()->push_back(counter);
    }
    utility::addMatcher(&cfg, kAclName, matchAction);
  }

  void updatePortFlowletConfigName(cfg::SwitchConfig& cfg) const {
    auto allPorts = masterLogicalInterfacePortIds();
    std::vector<PortID> ports(
        allPorts.begin(), allPorts.begin() + allPorts.size());
    for (auto portId : ports) {
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->flowletConfigName() = "default";
    }
  }

  void updateFlowletConfigs(
      cfg::SwitchConfig& cfg,
      const cfg::SwitchingMode switchingMode =
          cfg::SwitchingMode::FLOWLET_QUALITY,
      const int flowletTableSize = kFlowletTableSize1,
      const cfg::SwitchingMode backupSwitchingMode =
          cfg::SwitchingMode::FIXED_ASSIGNMENT) const {
    auto flowletCfg = getFlowletSwitchingConfig(
        switchingMode,
        kInactivityIntervalUsecs1,
        flowletTableSize,
        kDefaultSamplingRate());
    flowletCfg.backupSwitchingMode() = backupSwitchingMode;
    cfg.flowletSwitchingConfig() = flowletCfg;
    updatePortFlowletConfigs(
        cfg, kScalingFactor1(), kLoadWeight1, kQueueWeight1);
    if (!getHwSwitchEnsemble()->isSai()) {
      cfg.udfConfig() =
          utility::addUdfAclConfig(utility::kUdfOffsetBthReserved);
      addFlowletAcl(cfg);
    }
  }

  cfg::SwitchConfig getDefaultConfig() const {
    auto cfg = utility::onePortPerInterfaceConfig(
        getHwSwitch(),
        masterLogicalPortIds(),
        getAsic()->desiredLoopbackModes());
    addLoadBalancerToConfig(cfg, getHwSwitch(), LBHash::FULL_HASH);
    return cfg;
  }

  bool supportsFuturePortLoad() const {
    return getPlatform()->getAsic()->isSupported(
        HwAsic::Feature::ARS_FUTURE_PORT_LOAD);
  }

  cfg::PortFlowletConfig getPortFlowletConfig(
      int scalingFactor,
      int loadWeight,
      int queueWeight) const {
    cfg::PortFlowletConfig portFlowletConfig;
    portFlowletConfig.scalingFactor() = scalingFactor;
    portFlowletConfig.loadWeight() = loadWeight;
    if (supportsFuturePortLoad()) {
      portFlowletConfig.queueWeight() = queueWeight;
    }
    return portFlowletConfig;
  }

  void updatePortFlowletConfigs(
      cfg::SwitchConfig& cfg,
      int scalingFactor,
      int loadWeight,
      int queueWeight) const {
    std::map<std::string, cfg::PortFlowletConfig> portFlowletCfgMap;
    auto portFlowletConfig =
        getPortFlowletConfig(scalingFactor, loadWeight, queueWeight);
    portFlowletCfgMap.insert(std::make_pair("default", portFlowletConfig));
    cfg.portFlowletConfigs() = portFlowletCfgMap;
  }

  cfg::FlowletSwitchingConfig getFlowletSwitchingConfig(
      cfg::SwitchingMode switchingMode,
      uint16_t inactivityIntervalUsecs,
      int flowletTableSize,
      int samplingRate) const {
    cfg::FlowletSwitchingConfig flowletCfg;
    flowletCfg.inactivityIntervalUsecs() = inactivityIntervalUsecs;
    flowletCfg.flowletTableSize() = flowletTableSize;
    flowletCfg.dynamicEgressLoadExponent() = 3;
    if (supportsFuturePortLoad()) {
      flowletCfg.dynamicQueueExponent() = 3;
    }
    flowletCfg.dynamicQueueMinThresholdBytes() = 100000;
    flowletCfg.dynamicQueueMaxThresholdBytes() = 200000;
    flowletCfg.dynamicSampleRate() = samplingRate;
    flowletCfg.dynamicEgressMinThresholdBytes() = 1000;
    flowletCfg.dynamicEgressMaxThresholdBytes() = 10000;
    flowletCfg.dynamicPhysicalQueueExponent() = 4;
    flowletCfg.maxLinks() = kMaxLinks;
    flowletCfg.switchingMode() = switchingMode;
    return flowletCfg;
  }

  void modifyFlowletSwitchingConfig(cfg::SwitchConfig& cfg) const {
    auto flowletCfg = getFlowletSwitchingConfig(
        cfg::SwitchingMode::PER_PACKET_QUALITY,
        kInactivityIntervalUsecs2,
        kMinFlowletTableSize,
        kMinSamplingRate());
    cfg.flowletSwitchingConfig() = flowletCfg;
  }

  void modifyFlowletSwitchingMaxLinks(
      cfg::SwitchConfig& cfg,
      const int maxLinks) const {
    if (cfg.flowletSwitchingConfig().has_value()) {
      cfg.flowletSwitchingConfig()->maxLinks() = maxLinks;
    }
  }

  bool skipTest() {
    return (
        !getPlatform()->getAsic()->isSupported(HwAsic::Feature::ARS) ||
        (getPlatform()->getAsic()->getAsicType() ==
         cfg::AsicType::ASIC_TYPE_FAKE));
  }

  std::vector<NextHopWeight> swSwitchWeights_ = {
      ECMP_WEIGHT,
      ECMP_WEIGHT,
      ECMP_WEIGHT,
      ECMP_WEIGHT};

  void resolveNextHop(PortDescriptor port) {
    applyNewState(ecmpHelper_->resolveNextHops(
        getProgrammedState(),
        {
            port,
        }));
  }

  void unresolveNextHop(PortDescriptor port) {
    applyNewState(ecmpHelper_->unresolveNextHops(getProgrammedState(), {port}));
  }

  void addRoute(folly::IPAddressV6 prefix, uint8_t mask) {
    auto nHops = swSwitchWeights_.size();
    ecmpHelper_->programRoutes(
        getRouteUpdater(),
        nHops,
        {RoutePrefixV6{prefix, mask}},
        std::vector<NextHopWeight>(
            swSwitchWeights_.begin(), swSwitchWeights_.begin() + nHops));
    applyNewState(ecmpHelper_->resolveNextHops(getProgrammedState(), nHops));
  }

  void addRoute(
      const folly::IPAddressV6 prefix,
      uint8_t mask,
      const std::vector<PortDescriptor>& ports) {
    ecmpHelper_->programRoutes(
        getRouteUpdater(),
        boost::container::flat_set<PortDescriptor>(
            std::make_move_iterator(ports.begin()),
            std::make_move_iterator(ports.begin() + ports.size())),
        {RoutePrefixV6{prefix, mask}});
  }

  void verifyPortFlowletConfig(
      cfg::PortFlowletConfig& portFlowletConfig,
      bool enable = true) {
    if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::ARS_PORT_ATTRIBUTES)) {
      EXPECT_TRUE(
          utility::validatePortFlowletQuality(
              getHwSwitch(),
              masterLogicalPortIds()[0],
              portFlowletConfig,
              enable));
    }
  }

  void verifyConfig(
      const cfg::SwitchConfig& cfg,
      bool expectFlowsetSizeZero = false) {
    auto portFlowletConfig =
        getPortFlowletConfig(kScalingFactor1(), kLoadWeight1, kQueueWeight1);
    verifyPortFlowletConfig(portFlowletConfig);

    EXPECT_TRUE(
        utility::validateFlowletSwitchingEnabled(
            getHwSwitch(), *cfg.flowletSwitchingConfig()));
    EXPECT_TRUE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(),
            kAddr1Prefix,
            *cfg.flowletSwitchingConfig(),
            portFlowletConfig,
            true));

    if (!getHwSwitchEnsemble()->isSai()) {
      utility::checkSwHwAclMatch(getHwSwitch(), getProgrammedState(), kAclName);
      std::vector<cfg::CounterType> counterTypes{
          cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
      utility::checkAclStat(
          getHwSwitch(),
          getProgrammedState(),
          {kAclName},
          kAclCounterName,
          counterTypes);
    }
  }

  void verifyModifiedConfig() {
    auto portFlowletConfig =
        getPortFlowletConfig(kScalingFactor2(), kLoadWeight2, kQueueWeight2);
    verifyPortFlowletConfig(portFlowletConfig);

    auto flowletCfg = getFlowletSwitchingConfig(
        cfg::SwitchingMode::PER_PACKET_QUALITY,
        kInactivityIntervalUsecs2,
        kMinFlowletTableSize,
        kMinSamplingRate());

    EXPECT_TRUE(
        utility::validateFlowletSwitchingEnabled(getHwSwitch(), flowletCfg));
    EXPECT_TRUE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(), kAddr1Prefix, flowletCfg, portFlowletConfig, true));
  }

  void verifyRemoveFlowletConfig() {
    auto portFlowletConfig = getPortFlowletConfig(
        kDefaultScalingFactor(), kDefaultLoadWeight(), kDefaultQueueWeight());
    verifyPortFlowletConfig(portFlowletConfig, false);

    auto flowletCfg = getFlowletSwitchingConfig(
        cfg::SwitchingMode::FIXED_ASSIGNMENT, 0, 0, 0);

    EXPECT_TRUE(utility::validateFlowletSwitchingDisabled(getHwSwitch()));
    EXPECT_TRUE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(), kAddr1Prefix, flowletCfg, portFlowletConfig, false));
  }

  void validateEcmpDetails(const cfg::FlowletSwitchingConfig& flowletCfg) {
    auto ecmpDetails = getHwSwitch()->getAllEcmpDetails();
    CHECK_EQ(ecmpDetails.size(), 1);
    for (const auto& entry : ecmpDetails) {
      if (!getHwSwitchEnsemble()->isSai()) {
        CHECK_GE(*(entry.ecmpId()), kEcmpStartId);
      }
      if (*flowletCfg.flowletTableSize() > 0) {
        EXPECT_TRUE(*(entry.flowletEnabled()));
      }
    }
  }

  void verifySkipEcmpConfig(
      const cfg::SwitchConfig& cfg,
      bool expectFlowsetSizeZero = false) {
    // Port flowlet config is not present in the config
    // verify that ports are not programmed with port flowlet config in TH4
    auto portFlowletConfig = getPortFlowletConfig(
        kDefaultScalingFactor(), kDefaultLoadWeight(), kDefaultQueueWeight());
    verifyPortFlowletConfig(portFlowletConfig, false);

    EXPECT_TRUE(
        utility::validateFlowletSwitchingEnabled(
            getHwSwitch(), *cfg.flowletSwitchingConfig()));

    if (getHwSwitch()->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::ARS_PORT_ATTRIBUTES)) {
      // verify the flowlet config is programmed in ECMP for TH4
      EXPECT_TRUE(
          utility::verifyEcmpForFlowletSwitching(
              getHwSwitch(),
              kAddr1Prefix,
              *cfg.flowletSwitchingConfig(),
              portFlowletConfig,
              true));
    } else {
      // verify the flowlet config is not programmed in ECMP for TH3
      auto flowletCfg = getFlowletSwitchingConfig(
          cfg::SwitchingMode::FIXED_ASSIGNMENT, 0, 0, 0);
      EXPECT_TRUE(
          utility::verifyEcmpForFlowletSwitching(
              getHwSwitch(),
              kAddr1Prefix,
              flowletCfg,
              portFlowletConfig,
              false));
    }

    utility::checkSwHwAclMatch(getHwSwitch(), getProgrammedState(), kAclName);
    std::vector<cfg::CounterType> counterTypes{
        cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
    utility::checkAclStat(
        getHwSwitch(),
        getProgrammedState(),
        {kAclName},
        kAclCounterName,
        counterTypes);
  }

  void flowletSwitchingWBHelper(
      cfg::SwitchingMode preMode,
      int preMaxFlows,
      int preEcmpScale,
      cfg::SwitchingMode postMode,
      int postMaxFlows,
      int postEcmpScale) {
    auto setup = [this, preMode, preMaxFlows, preEcmpScale]() {
      auto cfg = initialConfig();
      updateFlowletConfigs(cfg, preMode, preMaxFlows);
      updatePortFlowletConfigName(cfg);
      applyNewConfig(cfg);
      setupEcmpGroups(preEcmpScale);
    };

    auto verify = [this, preMode, preMaxFlows, preEcmpScale]() {
      auto cfg = initialConfig();
      updateFlowletConfigs(cfg, preMode, preMaxFlows);
      updatePortFlowletConfigName(cfg);
      verifyEcmpGroups(cfg, preEcmpScale);
    };

    auto setupPostWarmboot = [this, postMode, postMaxFlows, postEcmpScale]() {
      auto cfg = initialConfig();
      updateFlowletConfigs(cfg, postMode, postMaxFlows);
      updatePortFlowletConfigName(cfg);
      applyNewConfig(cfg);
      setupEcmpGroups(postEcmpScale);
    };

    auto verifyPostWarmboot = [this, postMode, postMaxFlows, postEcmpScale]() {
      auto cfg = initialConfig();
      updateFlowletConfigs(cfg, postMode, postMaxFlows);
      updatePortFlowletConfigName(cfg);
      verifyEcmpGroups(cfg, postEcmpScale);
    };

    verifyAcrossWarmBoots(setup, verify, setupPostWarmboot, verifyPostWarmboot);
  }

  std::unique_ptr<utility::EcmpSetupAnyNPorts<folly::IPAddressV6>> ecmpHelper_;
};

class HwArsFlowletTest : public HwArsTest {
 protected:
  void SetUp() override {
    HwArsTest::SetUp();
  }

  cfg::SwitchConfig initialConfig() const override {
    auto cfg = getDefaultConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::FLOWLET_QUALITY, kFlowletTableSize2);
    updatePortFlowletConfigName(cfg);
    return cfg;
  }
};

class HwArsSprayTest : public HwArsTest {
 protected:
  void SetUp() override {
    HwArsTest::SetUp();
  }

  cfg::SwitchConfig initialConfig() const override {
    auto cfg = getDefaultConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::PER_PACKET_QUALITY, kMinFlowletTableSize);
    updatePortFlowletConfigName(cfg);
    return cfg;
  }
};

// Test intends to excercise the scenario where we run out of the
// flowset table because size is too big
// (1) Ensure that ECMP object is created but in non dynamic mode
// (2) Once the size if fixed through the cfg, its modified to go
// back to dynamic mode
TEST_F(HwArsFlowletTest, ValidateFlowsetExceed) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  auto setup = [&]() {
    auto cfg = initialConfig();
    // go one higher than max allowed
    updateFlowletConfigs(
        cfg,
        cfg::SwitchingMode::FLOWLET_QUALITY,
        utility::KMaxFlowsetTableSize + 1);
    updatePortFlowletConfigName(cfg);
    applyNewConfig(cfg);
    resolveNextHopsAddRoute(kMaxLinks);
  };

  auto verify = [&]() {
    auto cfg = initialConfig();
    auto portFlowletConfig =
        getPortFlowletConfig(kScalingFactor1(), kLoadWeight1, kQueueWeight1);

    // ensure that DLB is not programmed as we started with high flowset limits
    // we expect flowset size is zero
    EXPECT_FALSE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(),
            kAddr1Prefix,
            *cfg.flowletSwitchingConfig(),
            portFlowletConfig,
            true /* flowletEnable */));

    utility::validateFlowSetTable(
        getHwSwitch(), false /* expectFlowsetSizeZero */);

    // modify the flowlet cfg to fix the flowlet
    cfg = initialConfig();
    auto flowletCfg = getFlowletSwitchingConfig(
        cfg::SwitchingMode::FLOWLET_QUALITY,
        kInactivityIntervalUsecs2,
        KMaxFlowsetTableSize,
        kDefaultSamplingRate());
    cfg.flowletSwitchingConfig() = flowletCfg;
    applyNewConfig(cfg);

    EXPECT_TRUE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(),
            kAddr1Prefix,
            *cfg.flowletSwitchingConfig(),
            portFlowletConfig,
            true /* flowletEnable */));

    utility::validateFlowSetTable(
        getHwSwitch(), true /* expectFlowsetSizeZero */);
  };
  verifyAcrossWarmBoots(setup, verify);
}
// verify if all 16 ECMP groups are in DLB mode
TEST_F(HwArsFlowletTest, ValidateFlowsetTableFull) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  auto setup = [&]() {
    // create 16 different ECMP objects
    // 32768 / 2048 = 16
    int numEcmp = int(utility::KMaxFlowsetTableSize / kFlowletTableSize2);
    int totalEcmp = 0;
    for (int i = 1; i < 8 && totalEcmp < numEcmp; i++) {
      for (int j = i + 1; j < 8 && totalEcmp < numEcmp; j++) {
        std::vector<PortID> portIds;
        portIds.push_back(masterLogicalPortIds()[0]);
        portIds.push_back(masterLogicalPortIds()[i]);
        portIds.push_back(masterLogicalPortIds()[j]);
        resolveNextHopsAddRoute(
            portIds,
            folly::IPAddressV6(fmt::format("{}:{:x}::", kAddr3, totalEcmp++)));
      }
    }
  };

  auto verify = [&]() {
    auto cfg = initialConfig();
    auto portFlowletConfig =
        getPortFlowletConfig(kScalingFactor1(), kLoadWeight1, kQueueWeight1);
    int numEcmp = int(utility::KMaxFlowsetTableSize / kFlowletTableSize2);

    for (int i = 0; i < numEcmp; i++) {
      auto currentIp = folly::IPAddress(fmt::format("{}:{:x}::", kAddr3, i));
      folly::CIDRNetwork currentPrefix{currentIp, 64};
      EXPECT_TRUE(
          utility::verifyEcmpForFlowletSwitching(
              getHwSwitch(),
              currentPrefix, // second route
              *cfg.flowletSwitchingConfig(),
              portFlowletConfig,
              true /* flowletEnable */));
    }
    utility::validateFlowSetTable(
        getHwSwitch(), true /* expectFlowsetSizeZero */);
  };
  verifyAcrossWarmBoots(setup, verify);
}

// This test creates more than 128 ECMP groups and
// try to update the ECMP group id 200128 to be flowlet enabled and
// verify it fails.
// For SAI, go upto 126 groups

TEST_F(HwArsSprayTest, ValidateMaxEcmpIdFlowletUpdate) {
  if (this->skipTest() ||
      (getPlatform()->getAsic()->getAsicType() ==
       cfg::AsicType::ASIC_TYPE_FAKE)) {
    GTEST_SKIP();
  }

  // Helper to filter out management ports from port list
  auto filterManagementPorts = [this](const std::vector<PortID>& ports) {
    std::vector<PortID> filtered;
    auto state = getProgrammedState();
    for (auto portId : ports) {
      auto port = state->getPorts()->getNodeIf(portId);
      if (port && port->getPortType() != cfg::PortType::MANAGEMENT_PORT) {
        filtered.push_back(portId);
      }
    }
    return filtered;
  };

  auto setup = [&]() {
    // create 128 different ECMP objects
    for (int i = 1; i <= kNumEcmp(); i++) {
      std::vector<PortID> portIds;
      portIds.push_back(masterLogicalPortIds()[i % 64]);
      portIds.push_back(masterLogicalPortIds()[(i + 1) % 64]);
      auto filteredPorts = filterManagementPorts(portIds);
      ASSERT_FALSE(filteredPorts.empty())
          << "Expected non-management ports for ECMP group creation at iteration "
          << i;
      resolveNextHopsAddRoute(
          filteredPorts,
          folly::IPAddressV6(fmt::format("{}:{:x}::", kAddr3, i)));
      std::vector<PortID> portIds2;
      portIds2.push_back(masterLogicalPortIds()[i % 64]);
      portIds2.push_back(masterLogicalPortIds()[(i + 2) % 64]);
      auto filteredPorts2 = filterManagementPorts(portIds2);
      ASSERT_FALSE(filteredPorts2.empty())
          << "Expected non-management ports for second ECMP group at iteration "
          << i;
      resolveNextHopsAddRoute(
          filteredPorts2,
          folly::IPAddressV6(fmt::format("{}:{:x}::", kAddr4, i)));
    }

    // create 1 more ECMP object
    auto finalPorts = filterManagementPorts(
        {masterLogicalPortIds()[1], masterLogicalPortIds()[4]});
    ASSERT_FALSE(finalPorts.empty())
        << "Expected non-management ports for final ECMP group";
    resolveNextHopsAddRoute(finalPorts, kAddr1);

    // getAllEcmpDetails not implemented yet in SAI
    if (!getHwSwitchEnsemble()->isSai()) {
      // check to ensure we have created more than 128 ECMP objects
      auto ecmpDetails = getHwSwitch()->getAllEcmpDetails();
      CHECK_GT(ecmpDetails.size(), kNumEcmp() * 2);
    }
    // verify the ECMP Id more than Max dlb Ecmp Id
    // not enabled with flowlet config and flowset available is zero.
    auto cfg = initialConfig();
    utility::verifyEcmpForNonFlowlet(
        getHwSwitch(), kAddr1Prefix, *cfg.flowletSwitchingConfig(), false);
  };

  auto verify = [&]() {
    // Delete the 124 ECMP groups, so that we have enough flowset resources
    // available and try to update the ECMP group Id more than Max dlb Ecmp Id
    // not enabled with flowlet config and
    // verify flowset available is more than 2K.
    for (int i = 1; i < kNumEcmp() - 1; i++) {
      ecmpHelper_->unprogramRoutes(
          getRouteUpdater(),
          {RoutePrefixV6{
              folly::IPAddressV6(fmt::format("{}:{:x}::", kAddr3, i)), 64}});
      ecmpHelper_->unprogramRoutes(
          getRouteUpdater(),
          {RoutePrefixV6{
              folly::IPAddressV6(fmt::format("{}:{:x}::", kAddr4, i)), 64}});
    }
    auto cfg = initialConfig();
    utility::verifyEcmpForNonFlowlet(
        getHwSwitch(), kAddr1Prefix, *cfg.flowletSwitchingConfig(), true);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwArsSprayTest, VerifyArsEnable) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  auto setup = [&]() {
    resolveNextHopsAddRoute(kMaxLinks - 1);
    // resolve route on this port later, so we can mimic unresolved nexthop on
    // ECMP port
    this->resolveNextHop(PortDescriptor(masterLogicalPortIds()[kMaxLinks - 1]));
  };

  auto verify = [&]() {
    // verify the flowlet config
    const auto& cfg = initialConfig();
    verifyConfig(cfg);

    // Shrink egress and verify
    this->unresolveNextHop(PortDescriptor(masterLogicalPortIds()[3]));

    verifyConfig(cfg);

    // Expand egress and verify
    this->resolveNextHop(PortDescriptor(masterLogicalPortIds()[3]));

    verifyConfig(cfg);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwArsSprayTest, VerifyPortFlowletConfigChange) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  auto setup = [&]() { resolveNextHopsAddRoute(kMaxLinks); };

  auto verify = [&]() {
    auto cfg = initialConfig();
    verifyConfig(cfg);
    // Modify the port flowlet config and verify
    updatePortFlowletConfigs(
        cfg, kScalingFactor2(), kLoadWeight2, kQueueWeight2);
    applyNewConfig(cfg);

    auto portFlowletConfig =
        getPortFlowletConfig(kScalingFactor2(), kLoadWeight2, kQueueWeight2);
    verifyPortFlowletConfig(portFlowletConfig);
    // Modify to initial config to verify after warmboot
    applyNewConfig(initialConfig());
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwArsFlowletTest, VerifyFlowletConfigChange) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  auto setup = [&]() { resolveNextHopsAddRoute(kMaxLinks); };

  auto verify = [&]() {
    // switchingMode starts out as FLOWLET_QUALITY
    auto cfg = initialConfig();
    verifyConfig(cfg);
    // Modify the flowlet config switching mode to PER_PACKET_QUALITY
    modifyFlowletSwitchingConfig(cfg);
    // Modify the port flowlet config
    updatePortFlowletConfigs(
        cfg, kScalingFactor2(), kLoadWeight2, kQueueWeight2);
    applyNewConfig(cfg);
    verifyModifiedConfig();
    // Modify to initial config to verify after warmboot
    // switching mode back to FLOWLET_QUALITY
    cfg = initialConfig();
    applyNewConfig(cfg);
    verifyConfig(cfg);
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwArsFlowletTest, VerifyFlowletConfigRemoval) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  auto setup = [&]() { resolveNextHopsAddRoute(kMaxLinks); };

  auto verify = [&]() {
    auto cfg = initialConfig();
    verifyConfig(cfg);
    // Modify the flowlet config
    modifyFlowletSwitchingConfig(cfg);
    // Modify the port flowlet config
    updatePortFlowletConfigs(
        cfg, kScalingFactor2(), kLoadWeight2, kQueueWeight2);
    applyNewConfig(cfg);
    verifyModifiedConfig();
    // Remove the flowlet configs
    cfg = getDefaultConfig();
    applyNewConfig(cfg);
    verifyRemoveFlowletConfig();
    // Modify to initial config to verify after warmboot
    applyNewConfig(initialConfig());
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwArsFlowletTest, VerifyGetEcmpDetails) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  auto setup = [&]() { resolveNextHopsAddRoute(kMaxLinks); };

  auto verify = [&]() {
    auto cfg = initialConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::FLOWLET_QUALITY, kFlowletTableSize2);
    updatePortFlowletConfigName(cfg);
    applyNewConfig(cfg);
    verifyConfig(cfg);

    // start a new thread to verify the ecmp details
    std::atomic<bool> done{false};
    auto flowletCfg = *cfg.flowletSwitchingConfig();
    std::thread ecmpDetails([&flowletCfg, &done, this]() {
      while (!done) {
        validateEcmpDetails(flowletCfg);
      }
    });

    // Modify the flowlet config
    modifyFlowletSwitchingConfig(cfg);
    // Modify the port flowlet config
    updatePortFlowletConfigs(
        cfg, kScalingFactor2(), kLoadWeight2, kQueueWeight2);
    applyNewConfig(cfg);
    verifyModifiedConfig();

    done = true;
    ecmpDetails.join();

    // Modify to initial config to verify after warmboot
    applyNewConfig(initialConfig());
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwArsFlowletTest, VerifyEcmpFlowletSwitchingEnable) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  // This test setup static ECMP and update the static ECMP to DLB ECMP and
  // revert the DLB ECMP to static ECMP and verify it.
  auto setup = [&]() { resolveNextHopsAddRoute(kMaxLinks); };

  auto verify = [&]() {
    auto cfg = getDefaultConfig();
    applyNewConfig(cfg);

    auto portFlowletConfig =
        getPortFlowletConfig(kScalingFactor1(), kLoadWeight1, kQueueWeight1);
    updateFlowletConfigs(cfg);

    EXPECT_TRUE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(),
            kAddr1Prefix,
            *cfg.flowletSwitchingConfig(),
            portFlowletConfig,
            false /* flowletEnable */));

    // Modify the flowlet config to convert ECMP to DLB
    updatePortFlowletConfigName(cfg);
    applyNewConfig(cfg);
    // verify the flowlet config
    verifyConfig(cfg);

    // the ECMP for this route should be created as DLB
    resolveNextHopsAddRoute(
        {masterLogicalPortIds()[2], masterLogicalPortIds()[3]}, kAddr2);
    EXPECT_TRUE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(),
            kAddr2Prefix,
            *cfg.flowletSwitchingConfig(),
            portFlowletConfig,
            true /* flowletEnable */));

    // modify switchingMode to per_packet
    cfg = getDefaultConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::PER_PACKET_QUALITY, kFlowletTableSize1);
    updatePortFlowletConfigName(cfg);
    applyNewConfig(cfg);
    // verify the flowlet config
    verifyConfig(cfg);

    // modify max_flows and keep mode the same
    cfg = getDefaultConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::PER_PACKET_QUALITY, kMinFlowletTableSize);
    updatePortFlowletConfigName(cfg);
    applyNewConfig(cfg);
    // verify the flowlet config
    verifyConfig(cfg);

    // Remove the flowlet configs
    cfg = getDefaultConfig();
    applyNewConfig(cfg);
    // verify removal of flowlet config
    verifyRemoveFlowletConfig();
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwArsFlowletTest, ValidateEcmpDetailsThread) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  auto setup = [&]() {
    // create 2 different ECMP objects
    resolveNextHopsAddRoute(
        {masterLogicalPortIds()[0], masterLogicalPortIds()[1]}, kAddr1);
    resolveNextHopsAddRoute(
        {masterLogicalPortIds()[2], masterLogicalPortIds()[3]}, kAddr2);
  };

  auto verify = [&]() {
    auto cfg = initialConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::FLOWLET_QUALITY, kFlowletTableSize2);
    updatePortFlowletConfigName(cfg);
    applyNewConfig(cfg);
    auto portFlowletConfig =
        getPortFlowletConfig(kScalingFactor1(), kLoadWeight1, kQueueWeight1);

    EXPECT_TRUE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(),
            kAddr2Prefix, // second route
            *cfg.flowletSwitchingConfig(),
            portFlowletConfig,
            true /* flowletEnable */));

    // start a new thread to poll the ecmp details
    std::atomic<bool> done{false};
    auto hwSwitch = getHwSwitch();
    std::thread readEcmpDetails([&hwSwitch, &done]() {
      while (!done) {
        auto ecmpDetails = hwSwitch->getAllEcmpDetails();
      }
    });

    // remove ECMP object for the first route
    ecmpHelper_->unprogramRoutes(
        getRouteUpdater(), {RoutePrefixV6{kAddr1, 64}});

    EXPECT_TRUE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(),
            kAddr2Prefix, // second route
            *cfg.flowletSwitchingConfig(),
            portFlowletConfig,
            true /* flowletEnable */));

    done = true;
    readEcmpDetails.join();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwArsFlowletTest, ValidateFlowletStatsThread) {
  if (this->skipTest() ||
      (getPlatform()->getAsic()->getAsicType() ==
       cfg::AsicType::ASIC_TYPE_FAKE)) {
    GTEST_SKIP();
  }

  auto setup = [&]() {
    // create 2 different ECMP objects
    resolveNextHopsAddRoute(
        {masterLogicalPortIds()[0], masterLogicalPortIds()[1]}, kAddr1);
    resolveNextHopsAddRoute(
        {masterLogicalPortIds()[2], masterLogicalPortIds()[3]}, kAddr2);
  };

  auto verify = [&]() {
    auto cfg = initialConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::FLOWLET_QUALITY, kFlowletTableSize2);
    updatePortFlowletConfigName(cfg);
    applyNewConfig(cfg);
    auto portFlowletConfig =
        getPortFlowletConfig(kScalingFactor1(), kLoadWeight1, kQueueWeight1);

    EXPECT_TRUE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(),
            kAddr2Prefix, // second route
            *cfg.flowletSwitchingConfig(),
            portFlowletConfig,
            true /* flowletEnable */));

    // start a new thread to poll the flowlet stats
    std::atomic<bool> done{false};
    auto hwSwitch = getHwSwitch();
    std::thread readFlowletStats([&hwSwitch, &done]() {
      while (!done) {
        auto l3EcmpDlbFailPackets =
            hwSwitch->getHwFlowletStats().l3EcmpDlbFailPackets().value();
        EXPECT_EQ(l3EcmpDlbFailPackets, 0);
      }
    });

    // remove ECMP object for the first route
    ecmpHelper_->unprogramRoutes(
        getRouteUpdater(), {RoutePrefixV6{kAddr1, 64}});

    EXPECT_TRUE(
        utility::verifyEcmpForFlowletSwitching(
            getHwSwitch(),
            kAddr2Prefix, // second route
            *cfg.flowletSwitchingConfig(),
            portFlowletConfig,
            true /* flowletEnable */));

    done = true;
    readFlowletStats.join();
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(HwArsFlowletTest, VerifySkipEcmpFlowletSwitchingEnable) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  // This test verifies ability to configure ECMP groups without port config
  auto setup = [&]() {
    auto cfg = getDefaultConfig();
    updateFlowletConfigs(cfg);
    applyNewConfig(cfg);
    resolveNextHopsAddRoute(kMaxLinks);
  };

  auto verify = [&]() {
    auto cfg = getDefaultConfig();
    updateFlowletConfigs(cfg);
    // verify the flowlet config is not programmed in ECMP for TH3
    // since egress is not updated with port flowlet config
    // and the flowlet config is programmed in ECMP for TH4
    verifySkipEcmpConfig(cfg);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Verify ability to create upto 128 ECMP DLB groups in spray mode
TEST_F(HwArsSprayTest, VerifySprayModeScale) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }
  auto numEcmp = kMaxDlbGroups();

  auto setup = [this, numEcmp]() {
    auto cfg = initialConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::PER_PACKET_QUALITY, kMinFlowletTableSize);
    updatePortFlowletConfigName(cfg);
    applyNewConfig(cfg);
    setupEcmpGroups(numEcmp);
  };

  auto verify = [this, numEcmp]() {
    auto cfg = initialConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::PER_PACKET_QUALITY, kMinFlowletTableSize);
    updatePortFlowletConfigName(cfg);
    verifyEcmpGroups(cfg, numEcmp);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Verify warmboot from 16 ECMP (flowlet) to 96 ECMP (per-packet)
TEST_F(HwArsFlowletTest, VerifyModeFlowletToSpray) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }
  int numEcmp = 48;
  // CS00012344837
  if (getPlatform()->getAsic()->getAsicType() ==
      cfg::AsicType::ASIC_TYPE_TOMAHAWK3) {
    numEcmp = 30;
  }
  flowletSwitchingWBHelper(
      cfg::SwitchingMode::FLOWLET_QUALITY,
      kFlowletTableSize2,
      16,
      cfg::SwitchingMode::PER_PACKET_QUALITY,
      kMinFlowletTableSize,
      numEcmp);
}

// Verify warmboot from 16 ECMP (per-packet) to 16 ECMP (flowlet)
//
// Ideally we want to test 96 per-packet ECMP to 16 flowlet ECMP
// It becomes really hard to verify post conversion because there
// will be 16 flowlet ECMPs and 80 disabled ECMPs post-warmboot.
TEST_F(HwArsFlowletTest, VerifyModeSprayToFlowlet) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  flowletSwitchingWBHelper(
      cfg::SwitchingMode::PER_PACKET_QUALITY,
      kMinFlowletTableSize,
      8,
      cfg::SwitchingMode::FLOWLET_QUALITY,
      kFlowletTableSize2,
      8);
}

TEST_F(HwArsFlowletTest, VerifyModeSprayFlowletSizeChange) {
  if (this->skipTest()) {
    GTEST_SKIP();
  }

  // Update max_flows size from 2048 -> 256
  flowletSwitchingWBHelper(
      cfg::SwitchingMode::PER_PACKET_QUALITY,
      kFlowletTableSize2,
      8,
      cfg::SwitchingMode::PER_PACKET_QUALITY,
      kMinFlowletTableSize,
      8);
}

// This test is to explicitly verify ECMP ID management in BcmEgress
// Not a production scenario where primary mode is random spray
//
// Create 64 ECMP groups and verify all of them created beyond 200128
TEST_F(HwArsTest, VerifyEcmpIdAllocationForNonDynamicEcmp) {
  if (this->skipTest() ||
      (getPlatform()->getAsic()->getAsicType() ==
       cfg::AsicType::ASIC_TYPE_FAKE)) {
    GTEST_SKIP();
  }

  auto setup = [&]() {
    FLAGS_enable_ecmp_resource_manager = true;
    auto cfg = initialConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::PER_PACKET_RANDOM, kMinFlowletTableSize);
    updatePortFlowletConfigName(cfg);
    applyNewConfig(cfg);
    setupEcmpGroups(32);
  };

  auto verify = [&]() {
    auto cfg = initialConfig();
    updateFlowletConfigs(
        cfg, cfg::SwitchingMode::PER_PACKET_RANDOM, kMinFlowletTableSize);
    for (int i = 1; i < 32; i++) {
      auto currentIp = folly::IPAddress(fmt::format("{}:{:x}::", kAddr3, i));
      folly::CIDRNetwork currentPrefix{currentIp, 64};
      utility::verifyEcmpForNonFlowlet(
          getHwSwitch(), currentPrefix, *cfg.flowletSwitchingConfig(), true);
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

// Same as above, backup would never DLB type but this checks the throw
// condition if ever such a request happens
TEST_F(HwArsTest, VerifyEcmpIdAllocationForDynamicEcmp) {
  if (this->skipTest() ||
      (getPlatform()->getAsic()->getAsicType() ==
       cfg::AsicType::ASIC_TYPE_FAKE)) {
    GTEST_SKIP();
  }

  auto numEcmp = kMaxDlbGroups();

  auto setup = [&]() {
    FLAGS_enable_ecmp_resource_manager = true;
    auto cfg = initialConfig();
    updateFlowletConfigs(
        cfg,
        cfg::SwitchingMode::PER_PACKET_QUALITY,
        kMinFlowletTableSize,
        cfg::SwitchingMode::PER_PACKET_QUALITY);
    updatePortFlowletConfigName(cfg);
    applyNewConfig(cfg);
    setupEcmpGroups(numEcmp);
  };

  auto verify = [&]() {
    FLAGS_enable_ecmp_resource_manager = true;
    auto cfg = initialConfig();
    updateFlowletConfigs(
        cfg,
        cfg::SwitchingMode::PER_PACKET_QUALITY,
        kMinFlowletTableSize,
        cfg::SwitchingMode::PER_PACKET_QUALITY);
    verifyEcmpGroups(cfg, numEcmp);

    // create 1 more ECMP object, this should throw since no more DLB groups
    // available
    EXPECT_THROW(
        resolveNextHopsAddRoute(
            {masterLogicalPortIds()[1], masterLogicalPortIds()[5]}, kAddr2),
        FbossError);
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
