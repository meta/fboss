// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <boost/container/flat_set.hpp>

#include "fboss/agent/LoadBalancerUtils.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestRunner.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/Srv6TestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

template <typename EcmpTestHelperT, bool kWideEcmp>
class AgentLoadBalancerTest
    : public AgentHwTest,
      public utility::HwLoadBalancerTestRunner<EcmpTestHelperT, false> {
 public:
  using Runner = utility::HwLoadBalancerTestRunner<EcmpTestHelperT, false>;
  using Test = AgentHwTest;
  using SETUP_FN = typename Runner::SETUP_FN;
  using VERIFY_FN = typename Runner::VERIFY_FN;
  using SETUP_POSTWB_FN = typename Runner::SETUP_POSTWB_FN;
  using VERIFY_POSTWB_FN = typename Runner::VERIFY_POSTWB_FN;

  void SetUp() override {
    AgentHwTest::SetUp();
    Runner::setEcmpHelper();
  }

  // FrontPanel variants inject traffic from masterLogicalInterfacePortIds()
  // at index ecmpWidth (=8), so we need at least 9 INTERFACE_PORTs. CPU and
  // spray variants are unaffected but harmless at 9.
  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return 9;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (!kWideEcmp) {
      return {
          ProductionFeature::ECMP_LOAD_BALANCER,
      };
    } else {
      return {
          ProductionFeature::ECMP_LOAD_BALANCER,
          ProductionFeature::WIDE_ECMP,
      };
    }
  }

  void runLoadBalanceTest(
      unsigned int ecmpWidth,
      const cfg::LoadBalancer& loadBalancer,
      const std::vector<NextHopWeight>& weights,
      bool loopThroughFrontPanel = false,
      bool loadBalanceExpected = true,
      uint8_t deviation = 25) {
    Runner::runLoadBalanceTest(
        ecmpWidth,
        loadBalancer,
        weights,
        loopThroughFrontPanel,
        loadBalanceExpected,
        deviation);
  }

  void runEcmpShrinkExpandLoadBalanceTest(
      unsigned int ecmpWidth,
      const cfg::LoadBalancer& loadBalancer,
      uint8_t deviation = 25) {
    Runner::runEcmpShrinkExpandLoadBalanceTest(
        ecmpWidth, loadBalancer, deviation);
  }

  void runTestAcrossWarmBoots(
      SETUP_FN setup,
      VERIFY_FN verify,
      SETUP_POSTWB_FN setupPostWarmboot,
      VERIFY_POSTWB_FN verifyPostWarmboot) override {
    Test::verifyAcrossWarmBoots(
        std::move(setup),
        std::move(verify),
        std::move(setupPostWarmboot),
        std::move(verifyPostWarmboot));
  }

  TestEnsembleIf* getEnsemble() override {
    return getAgentEnsemble();
  }

  const TestEnsembleIf* getEnsemble() const override {
    // isFeatureSupported();
    return getAgentEnsemble();
  }

  void learnFrontPanelSrcMac(PortID frontPanelPort) override {
    // pumpTraffic's default SA.
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    learnL2EntryIfPending(
        utility::MacAddressGenerator().get(intfMac.u64HBO() + 1),
        frontPanelPort);
  }
};

template <typename EcmpDataPlateUtils, bool kWideEcmp = false>
class AgentIpLoadBalancerTest
    : public AgentLoadBalancerTest<EcmpDataPlateUtils, kWideEcmp> {
 public:
  std::unique_ptr<EcmpDataPlateUtils> getECMPHelper() override {
    if (!this->getEnsemble()) {
      // run during listing produciton features
      return nullptr;
    }
    return std::make_unique<EcmpDataPlateUtils>(
        this->getEnsemble(), RouterID(0));
  }
};

template <typename EcmpDataPlateUtils, bool kWideEcmp = false>
class AgentIp2MplsLoadBalancerTest
    : public AgentLoadBalancerTest<EcmpDataPlateUtils, kWideEcmp> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentLoadBalancerTest<EcmpDataPlateUtils, kWideEcmp>::
        getProductionFeaturesVerified();
    features.push_back(ProductionFeature::MPLS);
    return features;
  }

  std::unique_ptr<EcmpDataPlateUtils> getECMPHelper() override {
    if (!this->getEnsemble()) {
      // run during listing produciton features
      return nullptr;
    }
    return std::make_unique<EcmpDataPlateUtils>(
        this->getEnsemble(), RouterID(0), utility::kHwTestLabelStacks());
  }
};

template <
    typename EcmpDataPlateUtils,
    LabelForwardingAction::LabelForwardingType type,
    bool kWideEcmp = false>
class AgentMpls2MplsLoadBalancerTest
    : public AgentLoadBalancerTest<EcmpDataPlateUtils, kWideEcmp> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentLoadBalancerTest<EcmpDataPlateUtils, kWideEcmp>::
        getProductionFeaturesVerified();
    features.push_back(ProductionFeature::MPLS);
    return features;
  }
  std::unique_ptr<EcmpDataPlateUtils> getECMPHelper() override {
    if (!this->getEnsemble()) {
      // run during listing produciton features
      return nullptr;
    }
    return std::make_unique<EcmpDataPlateUtils>(
        this->getEnsemble(),
        MPLSHdr::Label(utility::kHwTestMplsLabel, 0, false, 127),
        type);
  }
};

template <typename EcmpDataPlateUtils, bool loadAware = true>
class AgentLoadBalancerTestSpray
    : public AgentLoadBalancerTest<EcmpDataPlateUtils, false> {
 private:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(), ensemble.masterLogicalPortIds());
    cfg.udfConfig() = utility::addUdfAclConfig(utility::kUdfOffsetBthReserved);
    auto switchingMode = loadAware ? cfg::SwitchingMode::PER_PACKET_QUALITY
                                   : cfg::SwitchingMode::PER_PACKET_RANDOM;
    utility::addFlowletConfigs(
        cfg, ensemble.masterLogicalPortIds(), ensemble.isSai(), switchingMode);
    utility::addFlowletAcl(
        cfg,
        ensemble.isSai(),
        utility::kFlowletAclName,
        utility::kFlowletAclCounterName,
        false);
    return cfg;
  }

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_flowletSwitchingEnable = true;
  }

 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentLoadBalancerTest<EcmpDataPlateUtils, false>::
        getProductionFeaturesVerified();
    if constexpr (loadAware) {
      features.push_back(ProductionFeature::ARS_SPRAY);
    } else {
      features.push_back(ProductionFeature::ECMP_RANDOM_SPRAY);
    }
    return features;
  }

  std::unique_ptr<EcmpDataPlateUtils> getECMPHelper() override {
    if (!this->getEnsemble()) {
      // run during listing produciton features
      return nullptr;
    }
    return std::make_unique<EcmpDataPlateUtils>(
        this->getEnsemble(), RouterID(0));
  }
};

template <typename EcmpDataPlateUtils, bool kWideEcmp = false>
class AgentLoadBalancerTestUdf
    : public AgentLoadBalancerTest<EcmpDataPlateUtils, kWideEcmp> {
 private:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(), ensemble.masterLogicalPortIds());

    // Add UDF configuration
    auto asic = ensemble.getSw()->getHwAsicTable()->getHwAsic(SwitchID(0));
    cfg.udfConfig() = utility::addUdfHashConfig(asic->getAsicType());
    return cfg;
  }

 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    auto features = AgentLoadBalancerTest<EcmpDataPlateUtils, kWideEcmp>::
        getProductionFeaturesVerified();
    features.push_back(ProductionFeature::UDF_HASH);
    return features;
  }

  std::unique_ptr<EcmpDataPlateUtils> getECMPHelper() override {
    if (!this->getEnsemble()) {
      // run during listing production features
      return nullptr;
    }
    return std::make_unique<EcmpDataPlateUtils>(
        this->getEnsemble(), RouterID(0));
  }
};

class AgentLoadBalancerTestV4
    : public AgentIpLoadBalancerTest<utility::HwIpV4EcmpDataPlaneTestUtil> {};

class AgentLoadBalancerTestV4Wide : public AgentIpLoadBalancerTest<
                                        utility::HwIpV4EcmpDataPlaneTestUtil,
                                        true> {};

class AgentLoadBalancerTestV6
    : public AgentIpLoadBalancerTest<utility::HwIpV6EcmpDataPlaneTestUtil> {};

class AgentLoadBalancerTestV6Wide : public AgentIpLoadBalancerTest<
                                        utility::HwIpV6EcmpDataPlaneTestUtil,
                                        true> {};

class AgentLoadBalancerTestV4ToMpls
    : public AgentIp2MplsLoadBalancerTest<
          utility::HwIpV4EcmpDataPlaneTestUtil> {};

class AgentLoadBalancerTestV4ToMplsWide
    : public AgentIp2MplsLoadBalancerTest<
          utility::HwIpV4EcmpDataPlaneTestUtil,
          true> {};

class AgentLoadBalancerTestV6ToMpls
    : public AgentIp2MplsLoadBalancerTest<
          utility::HwIpV6EcmpDataPlaneTestUtil> {};

class AgentLoadBalancerTestV6ToMplsWide
    : public AgentIp2MplsLoadBalancerTest<
          utility::HwIpV6EcmpDataPlaneTestUtil,
          true> {};

class AgentLoadBalancerTestV4InMplsSwap
    : public AgentMpls2MplsLoadBalancerTest<
          utility::HwMplsV4EcmpDataPlaneTestUtil,
          LabelForwardingAction::LabelForwardingType::SWAP> {};

class AgentLoadBalancerTestV4InMplsSwapWide
    : public AgentMpls2MplsLoadBalancerTest<
          utility::HwMplsV4EcmpDataPlaneTestUtil,
          LabelForwardingAction::LabelForwardingType::SWAP,
          true> {};

class AgentLoadBalancerTestV6InMplsSwap
    : public AgentMpls2MplsLoadBalancerTest<
          utility::HwMplsV6EcmpDataPlaneTestUtil,
          LabelForwardingAction::LabelForwardingType::SWAP> {};

class AgentLoadBalancerTestV6InMplsSwapWide
    : public AgentMpls2MplsLoadBalancerTest<
          utility::HwMplsV6EcmpDataPlaneTestUtil,
          LabelForwardingAction::LabelForwardingType::SWAP,
          true> {};

class AgentLoadBalancerTestV4InMplsPhp
    : public AgentMpls2MplsLoadBalancerTest<
          utility::HwMplsV4EcmpDataPlaneTestUtil,
          LabelForwardingAction::LabelForwardingType::PHP> {};

class AgentLoadBalancerTestV4InMplsPhpWide
    : public AgentMpls2MplsLoadBalancerTest<
          utility::HwMplsV4EcmpDataPlaneTestUtil,
          LabelForwardingAction::LabelForwardingType::PHP,
          true> {};

class AgentLoadBalancerTestV6InMplsPhp
    : public AgentMpls2MplsLoadBalancerTest<
          utility::HwMplsV6EcmpDataPlaneTestUtil,
          LabelForwardingAction::LabelForwardingType::PHP> {};

class AgentLoadBalancerTestV6InMplsPhpWide
    : public AgentMpls2MplsLoadBalancerTest<
          utility::HwMplsV6EcmpDataPlaneTestUtil,
          LabelForwardingAction::LabelForwardingType::PHP,
          true> {};

class AgentLoadBalancerTestV4Udf
    : public AgentLoadBalancerTestUdf<
          utility::HwIpV4RoCEEcmpDataPlaneTestUtil> {};

class AgentLoadBalancerTestV6Udf
    : public AgentLoadBalancerTestUdf<
          utility::HwIpV6RoCEEcmpDataPlaneTestUtil> {};

class AgentLoadBalancerTestV6UdfNegative
    : public AgentLoadBalancerTestUdf<
          utility::HwIpV6RoCEEcmpDestPortDataPlaneTestUtil> {};

class AgentLoadBalancerTestV6ArsSpray
    : public AgentLoadBalancerTestSpray<
          utility::HwIpV6RoCEEcmpDataPlaneTestUtil,
          true> {};

class AgentLoadBalancerTestV6EcmpSpray
    : public AgentLoadBalancerTestSpray<
          utility::HwIpV6RoCEEcmpDataPlaneTestUtil,
          false> {};

// SRv6 ECMP load balancing
class AgentSrv6EcmpLoadBalancerTest : public AgentLoadBalancerTest<
                                          utility::HwSrv6EcmpDataPlaneTestUtil,
                                          false> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::SRV6_ENCAP, ProductionFeature::ECMP_LOAD_BALANCER};
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return utility::srv6EcmpInitialConfig(ensemble);
  }

  std::unique_ptr<utility::HwSrv6EcmpDataPlaneTestUtil> getECMPHelper()
      override {
    if (!getEnsemble()) {
      return nullptr;
    }
    return std::make_unique<utility::HwSrv6EcmpDataPlaneTestUtil>(
        getEnsemble(), RouterID(0));
  }
};

// SRv6 next hops programmed with unequal weights, forming a UCMP group
class AgentSrv6UcmpLoadBalancerTest : public AgentSrv6EcmpLoadBalancerTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::SRV6_ENCAP,
        ProductionFeature::ECMP_LOAD_BALANCER,
        ProductionFeature::UCMP};
  }
};

// SRv6 ECMP load balancing driven purely by the IPv6 flow label (fixed 5-tuple)
class AgentSrv6FlowLabelEcmpLoadBalancerTest
    : public AgentLoadBalancerTest<
          utility::HwSrv6FlowLabelEcmpDataPlaneTestUtil,
          false> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::SRV6_ENCAP, ProductionFeature::ECMP_LOAD_BALANCER};
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return utility::srv6EcmpInitialConfig(ensemble);
  }

  std::unique_ptr<utility::HwSrv6FlowLabelEcmpDataPlaneTestUtil> getECMPHelper()
      override {
    if (!getEnsemble()) {
      return nullptr;
    }
    return std::make_unique<utility::HwSrv6FlowLabelEcmpDataPlaneTestUtil>(
        getEnsemble(), RouterID(0));
  }
};

RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_CPU(AgentLoadBalancerTestV4)
RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_CPU(AgentLoadBalancerTestV6)
RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_CPU(AgentLoadBalancerTestV4ToMpls)
RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_CPU(AgentLoadBalancerTestV6ToMpls)

RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_CPU(AgentLoadBalancerTestV4)
RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_CPU(AgentLoadBalancerTestV6)
RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_CPU(AgentLoadBalancerTestV4ToMpls)
RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_CPU(AgentLoadBalancerTestV6ToMpls)

RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_CPU(AgentLoadBalancerTestV4Wide)
RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_CPU(AgentLoadBalancerTestV6Wide)
RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_CPU(AgentLoadBalancerTestV4ToMplsWide)
RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_CPU(AgentLoadBalancerTestV6ToMplsWide)

RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV4)
RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV6)
RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV4ToMpls)
RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV6ToMpls)

RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV4)
RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV6)
RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV4ToMpls)
RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV6ToMpls)

RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV4Wide)
RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV6Wide)
RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV4ToMplsWide)
RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV6ToMplsWide)

RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV4InMplsSwap)
RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV6InMplsSwap)
RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV4InMplsPhp)
RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV6InMplsPhp)

RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV4InMplsSwap)
RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV6InMplsSwap)
RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV4InMplsPhp)
RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_FRONT_PANEL(AgentLoadBalancerTestV6InMplsPhp)

RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV4InMplsSwapWide)
RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV6InMplsSwapWide)
RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV4InMplsPhpWide)
RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV6InMplsPhpWide)

// Verify UDF hashing for v4 and v6
RUN_HW_LOAD_BALANCER_TEST_CPU(AgentLoadBalancerTestV4Udf, Ecmp, FullUdf)
RUN_HW_LOAD_BALANCER_TEST_CPU(AgentLoadBalancerTestV6Udf, Ecmp, FullUdf)
// Verify UDF field is not considered with regular ECMP
RUN_HW_LOAD_BALANCER_NEGATIVE_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV6Udf,
    Ecmp,
    Full)
// Verify UDF is a miss for traffic not matching UDP dst port
RUN_HW_LOAD_BALANCER_NEGATIVE_TEST_FRONT_PANEL(
    AgentLoadBalancerTestV6UdfNegative,
    Ecmp,
    FullUdf)

RUN_HW_LOAD_BALANCER_TEST_SPRAY(AgentLoadBalancerTestV6ArsSpray, Ecmp, Full)
RUN_HW_LOAD_BALANCER_TEST_SPRAY(AgentLoadBalancerTestV6EcmpSpray, Ecmp, Full)

// IPv6 flow label ECMP load balancing (plain IPv6, not tunneled)
class AgentV6FlowLabelEcmpLoadBalancerTest
    : public AgentLoadBalancerTest<
          utility::HwIpV6FlowLabelEcmpDataPlaneTestUtil,
          false> {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::ECMP_LOAD_BALANCER};
  }

  std::unique_ptr<utility::HwIpV6FlowLabelEcmpDataPlaneTestUtil> getECMPHelper()
      override {
    if (!this->getEnsemble()) {
      return nullptr;
    }
    return std::make_unique<utility::HwIpV6FlowLabelEcmpDataPlaneTestUtil>(
        this->getEnsemble(), RouterID(0));
  }
};

RUN_HW_LOAD_BALANCER_TEST_CPU(
    AgentV6FlowLabelEcmpLoadBalancerTest,
    Ecmp,
    FullWithFlowLabel)

RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(
    AgentV6FlowLabelEcmpLoadBalancerTest,
    Ecmp,
    FullWithFlowLabel)

// Verify that enabling IPv6 flow label hashing via config change
// (ApplyThriftConfig path) takes effect at runtime.
// Start with standard full hash (no flow label) where flow-label-only
// traffic is NOT load balanced, then apply config with flow label and
// verify traffic IS load balanced.
TEST_F(
    AgentV6FlowLabelEcmpLoadBalancerTest,
    EcmpFlowLabelHashEnabledViaConfigChange) {
  if (skipTest()) {
#if defined(GTEST_SKIP)
    GTEST_SKIP();
#endif
    return;
  }
  constexpr unsigned int kEcmpWidth = 8;
  auto* helper = getEcmpSetupHelper();
  auto setup = [=, this]() {
    auto fullHashNoFlowLabel = utility::getEcmpFullHashConfig({getHwAsic()});
    helper->programRoutesAndLoadBalancer(kEcmpWidth, {}, fullHashNoFlowLabel);
  };
  auto verify = [=, this]() {
    auto fullHashNoFlowLabel = utility::getEcmpFullHashConfig({getHwAsic()});
    auto fullHashWithFlowLabel =
        utility::getEcmpFullWithFlowLabelHashConfig({getHwAsic()});

    // With standard full hash, flow-label-only traffic should NOT
    // be load balanced (all packets hash to the same port)
    helper->pumpTrafficPortAndVerifyLoadBalanced(
        kEcmpWidth, false, {}, 25, false /* loadBalanceExpected */);

    // Enable flow label via config change (ApplyThriftConfig path)
    helper->programLoadBalancer(fullHashWithFlowLabel);

    // Same traffic should now be load balanced
    helper->pumpTrafficPortAndVerifyLoadBalanced(
        kEcmpWidth, false, {}, 25, true /* loadBalanceExpected */);

    // Reset to original hash so verify is idempotent across warmboot
    helper->programLoadBalancer(fullHashNoFlowLabel);
    // With standard full hash, flow-label-only traffic should NOT
    // be load balanced (all packets hash to the same port)
    helper->pumpTrafficPortAndVerifyLoadBalanced(
        kEcmpWidth, false, {}, 25, false /* loadBalanceExpected */);
  };
  runTestAcrossWarmBoots(setup, verify, []() {}, []() {});
}

RUN_HW_LOAD_BALANCER_TEST_CPU(
    AgentSrv6EcmpLoadBalancerTest,
    Ecmp,
    FullWithFlowLabel)

RUN_HW_LOAD_BALANCER_TEST_CPU(
    AgentSrv6UcmpLoadBalancerTest,
    Ucmp,
    FullWithFlowLabel)

RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(
    AgentSrv6UcmpLoadBalancerTest,
    Ucmp,
    FullWithFlowLabel)

RUN_HW_LOAD_BALANCER_TEST_CPU(
    AgentSrv6FlowLabelEcmpLoadBalancerTest,
    Ecmp,
    FullWithFlowLabel)

namespace {
constexpr int kPolarizationEcmpWidth = 8;
constexpr int kPolarizationNumAggPorts = 4;
constexpr int kPolarizationAggPortWidth = 3;
// Acceptable (highest-lowest)/lowest per-port deviation (%). At ~120
// packets/port (3K captured / 12 trunk ports), random sample noise alone can
// produce 30-50% deviation; real polarization produces 200%+ (some ports get
// zero traffic). 60% catches gross polarization without false-positives.
constexpr uint8_t kPolarizationMaxDeviationPct = 60;
// Minimum captured packets required before the polarization assertions are
// statistically meaningful. Below this threshold the test SKIPs (rather than
// emitting a false-positive failure).
constexpr size_t kMinCapturedPackets = 1000;
// Total V6 packet count to pump. With kInterPktSleep pacing this stays
// under 100 pps to avoid overflowing the SwSwitch packet observer rx queue.
constexpr int kPumpPacketCount = 6000;
// 12ms ≈ 83 pps per family.
constexpr auto kInterPktSleep = std::chrono::microseconds(12000);
// Allowed fraction of captured packets that may fail to replay (non-IP /
// missing UDP payload). 10% guards against a regression that silently shrinks
// the replay corpus.
constexpr double kMaxReplayDropRatio = 0.10;
const folly::MacAddress kPolarizationSeedMac1("fe:bd:67:0e:09:db");
const folly::MacAddress kPolarizationSeedMac2("9a:5d:82:09:3a:d9");

using PolarizationTestTypes =
    ::testing::Types<facebook::fboss::PortID, facebook::fboss::AggregatePortID>;
} // namespace

// Verifies that two consecutive hash stages with different seeds do not
// polarize. Uses capture+replay: hash A is applied, traffic is pumped, the
// half of egress ports trapped to CPU is captured. Hash B is then applied
// and the captured packets are replayed; egress should remain balanced.
//
// Typed by `TestType`: PortID runs over plain ECMP across physical ports,
// AggregatePortID runs over ECMP across trunks (each trunk = 3 member ports).
//
// Note: this test does not use HwLoadBalancerTestRunner (the CRTP runner used
// by the rest of this file). The runner's pump-and-verify-balance pattern
// can't express the capture+replay shape needed to detect polarization, so we
// drive setup/verify lambdas directly.
template <typename TestType>
class AgentHashPolarizationTest : public AgentHwTest {
 protected:
  static constexpr auto isTrunk = std::is_same_v<TestType, AggregatePortID>;

  // AggregatePortID instantiation needs 4*3 trunk fanout + 1 injection = 13
  // INTERFACE_PORTs. PortID instantiation only needs 9 but 13 is harmless.
  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return 13;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (isTrunk) {
      return {
          ProductionFeature::ECMP_LOAD_BALANCER,
          ProductionFeature::LAG,
          ProductionFeature::LAG_LOAD_BALANCER};
    } else {
      return {ProductionFeature::ECMP_LOAD_BALANCER};
    }
  }

  // Number of physical interface ports the test's fanout consumes
  // (kEcmpWidth for plain ECMP, numAggPorts*aggPortWidth for trunks).
  // The injection port lives at index fanoutSize().
  static constexpr int fanoutSize() {
    if constexpr (isTrunk) {
      return kPolarizationNumAggPorts * kPolarizationAggPortWidth;
    } else {
      return kPolarizationEcmpWidth;
    }
  }

  // Physical ports whose loopback ingress is trapped to CPU. For trunks this
  // is the first half of the trunk fanout (= first kNumAggPorts/2 trunks);
  // for plain ECMP it is the first half of the ECMP ports. Takes the
  // ensemble explicitly because this is invoked from initialConfig() before
  // AgentHwTest's ensemble member is bound.
  std::set<PortID> capturePorts(const AgentEnsemble& ensemble) const {
    auto ports = ensemble.masterLogicalPortIds();
    auto end = ports.begin() + fanoutSize() / 2;
    return {ports.begin(), end};
  }

  // Egress ports the load-balanced check measures. Same span as fanoutSize.
  std::vector<PortID> egressPorts() const {
    auto ports = masterLogicalInterfacePortIds();
    return {ports.begin(), ports.begin() + fanoutSize()};
  }

  // Inject from the first port past the fanout, so its egress stats never
  // overlap the measured egress ports.
  PortID injectionPort() const {
    return masterLogicalInterfacePortIds()[fanoutSize()];
  }

  // Emits GTEST_SKIP when the platform lacks enough master interface ports
  // (need fanoutSize + 1 for injection) or hash customization support.
  // Must be called from the TEST_F body — GTEST_SKIP cannot return from a
  // non-void helper.
  void skipIfUnsupported() {
    auto numPorts = masterLogicalInterfacePortIds().size();
    if (static_cast<int>(numPorts) < fanoutSize() + 1) {
      GTEST_SKIP() << "Not enough interface ports: have " << numPorts
                   << ", need " << fanoutSize() + 1;
    }
    if (!isSupportedOnAllAsics(HwAsic::Feature::HASH_FIELDS_CUSTOMIZATION)) {
      GTEST_SKIP() << "ASIC doesn't support hash customization";
    }
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = utility::onePortPerInterfaceConfig(
        ensemble.getSw(), ensemble.masterLogicalPortIds());
    auto asic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
    // Configure a permissive CPU queue (no per-queue rate cap) so trapped
    // packets reach the snooper without being dropped at the COPP queue.
    utility::setDefaultCpuTrafficPolicyConfig(
        cfg, ensemble.getL3Asics(), ensemble.isSai());
    utility::addCpuQueueConfig(
        cfg, ensemble.getL3Asics(), ensemble.isSai(), false /*setQueueRate*/);
    utility::addTrapPacketAcl(asic, &cfg, capturePorts(ensemble));
    return cfg;
  }

  std::vector<cfg::LoadBalancer> makeHashes(folly::MacAddress seedMac) const {
    auto hashes = utility::getEcmpFullTrunkHalfHashConfig(
        getAgentEnsemble()->getL3Asics());
    bool isSai = getAgentEnsemble()->isSai();
    hashes[0].seed() = utility::generateDeterministicSeed(
        cfg::LoadBalancerID::ECMP, seedMac, isSai);
    hashes[1].seed() = utility::generateDeterministicSeed(
        cfg::LoadBalancerID::AGGREGATE_PORT, seedMac, isSai);
    return hashes;
  }

  bool egressBalanced(
      const std::map<PortID, HwPortStats>& before,
      const std::map<PortID, HwPortStats>& after) const {
    auto [highest, lowest] =
        utility::getHighestAndLowestBytesIncrement<PortID, HwPortStats>(
            before, after);
    return utility::isDeviationWithinThreshold(
        lowest, highest, kPolarizationMaxDeviationPct, /*noTrafficOk=*/false);
  }

  void applyHash(const std::vector<cfg::LoadBalancer>& hashes) {
    applyNewState(
        [&](const std::shared_ptr<SwitchState>& in) {
          return utility::addLoadBalancers(
              getAgentEnsemble(), in, hashes, scopeResolver());
        },
        "set polarization hash");
  }

  template <typename PayloadT>
  std::unique_ptr<TxPacket> makeReplayPacket(
      const utility::AllocatePktFunc& allocFn,
      std::optional<VlanID> vlan,
      folly::MacAddress srcMac,
      folly::MacAddress dstMac,
      const PayloadT& payload) const {
    auto udp = payload.udpPayload();
    if (!udp.has_value()) {
      return nullptr;
    }
    return utility::makeUDPTxPacket(
        allocFn,
        vlan,
        srcMac,
        dstMac,
        payload.header().srcAddr,
        payload.header().dstAddr,
        udp->header().srcPort,
        udp->header().dstPort);
  }

  // Send numPackets IPv6 UDP packets in a sqrtN×sqrtN srcIP×dstIP grid out
  // injectionPort, with interPktDelay between each send. Mirrors
  // utility::pumpTraffic but throttled to keep the SwSwitch packet rx
  // queue from overflowing when many copies are trapped to CPU.
  void pumpTrafficPaced(
      int numPackets,
      std::chrono::microseconds interPktDelay) {
    auto allocFn = utility::getAllocatePktFn(getAgentEnsemble());
    auto sendFn = utility::getSendPktFunc(getAgentEnsemble());
    auto vlan = getVlanIDForTx();
    auto dstMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    folly::MacAddress srcMac =
        utility::MacAddressGenerator().get(dstMac.u64HBO() + 1);
    auto sqrtN = static_cast<int>(std::sqrt(numPackets));
    for (int i = 0; i < sqrtN; ++i) {
      auto srcIp = folly::IPAddress(
          fmt::format("1001::{}:{}", (i + 1) / 256, (i + 1) % 256));
      for (int j = 0; j < sqrtN; ++j) {
        auto dstIp = folly::IPAddress(
            fmt::format("2001::{}:{}", (j + 1) / 256, (j + 1) % 256));
        auto pkt = utility::makeUDPTxPacket(
            allocFn, vlan, srcMac, dstMac, srcIp, dstIp, 10000 + i, 20000 + j);
        sendFn(std::move(pkt), PortDescriptor(injectionPort()), std::nullopt);
        /* sleep override */
        std::this_thread::sleep_for(interPktDelay);
      }
    }
  }

  std::vector<utility::EthFrame> runFirstHash(
      const std::vector<cfg::LoadBalancer>& hashes) {
    auto ports = egressPorts();
    auto preStats = getLatestPortStats(ports);
    applyHash(hashes);

    utility::SwSwitchPacketSnooper snooper(getSw(), "polarization-snooper");
    snooper.ignoreUnclaimedRxPkts();

    pumpTrafficPaced(kPumpPacketCount, kInterPktSleep);

    // Snooper has been receiving packets in parallel during the paced
    // send. Drain until the queue stays idle for a streak of empty waits;
    // bound by an absolute wall-clock deadline so a stalled HW path can't
    // hang the test forever. Note: do NOT bound by iteration count —
    // each waitForPacket pops at most one packet, so an iter cap silently
    // truncates the capture.
    std::vector<utility::EthFrame> captured;
    constexpr int kMaxIdleStreak = 3;
    constexpr auto kDrainDeadline = std::chrono::seconds(60);
    auto drainStart = std::chrono::steady_clock::now();
    int idleStreak = 0;
    while (idleStreak < kMaxIdleStreak &&
           std::chrono::steady_clock::now() - drainStart < kDrainDeadline) {
      auto pktBuf = snooper.waitForPacket(1);
      if (!pktBuf.has_value()) {
        ++idleStreak;
        continue;
      }
      idleStreak = 0;
      folly::io::Cursor cursor{pktBuf->get()};
      captured.emplace_back(cursor);
    }
    XLOG(DBG2) << "Captured " << captured.size() << " packets under first hash";
    if (captured.size() < kMinCapturedPackets) {
      // Caller will GTEST_SKIP based on returned size — keep this method
      // non-void by skipping the load-balanced assertion early.
      return captured;
    }

    WITH_RETRIES({
      auto postStats = getLatestPortStats(ports);
      EXPECT_EVENTUALLY_TRUE(egressBalanced(preStats, postStats));
    });
    return captured;
  }

  void runSecondHash(
      const std::vector<utility::EthFrame>& replay,
      const std::vector<cfg::LoadBalancer>& hashes) {
    auto ports = egressPorts();
    auto preStats = getLatestPortStats(ports);
    applyHash(hashes);

    auto vlan = getVlanIDForTx();
    auto mac = getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto allocFn = utility::getAllocatePktFn(getAgentEnsemble());
    auto sendFn = utility::getSendPktFunc(getAgentEnsemble());
    size_t replayed = 0;
    size_t dropped = 0;
    for (const auto& frame : replay) {
      auto srcMac = frame.header().srcAddr;
      auto etherType = frame.header().etherType;
      std::unique_ptr<TxPacket> pkt;
      if (etherType == static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6)) {
        const auto& payload = frame.v6PayLoad();
        if (payload.has_value()) {
          pkt = makeReplayPacket(allocFn, vlan, srcMac, mac, *payload);
        }
      }
      if (!pkt) {
        ++dropped;
        XLOG_EVERY_N(WARN, 100)
            << "Skipping replay of non-UDPv6 frame, etherType=0x" << std::hex
            << etherType;
        continue;
      }
      sendFn(std::move(pkt), PortDescriptor(injectionPort()), std::nullopt);
      ++replayed;
    }
    XLOG(DBG2) << "Replayed " << replayed << " packets, dropped " << dropped;
    EXPECT_LE(dropped, replay.size() * kMaxReplayDropRatio)
        << "Too many captured frames were skipped during replay";

    WITH_RETRIES({
      auto postStats = getLatestPortStats(ports);
      EXPECT_EVENTUALLY_TRUE(egressBalanced(preStats, postStats));
    });
  }

  template <typename AddrT>
  void programRoutesForNhops(
      const boost::container::flat_set<PortDescriptor>& nhops) {
    utility::EcmpSetupTargetedPorts<AddrT> ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    applyNewState([&](const std::shared_ptr<SwitchState>& in) {
      return ecmpHelper.resolveNextHops(in, nhops);
    });
    auto routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(&routeUpdater, nhops);
  }

  // For trunks: configure aggregate ports + program routes via AggregatePortID
  // next-hops. For plain ECMP: program routes directly across physical ports.
  void setupRoutes() {
    boost::container::flat_set<PortDescriptor> nhops;
    if constexpr (isTrunk) {
      configureAggregatePorts();
      for (int i = 0; i < kPolarizationNumAggPorts; ++i) {
        nhops.insert(PortDescriptor(AggregatePortID(i + 1)));
      }
    } else {
      auto interfacePorts = masterLogicalInterfacePortIds();
      for (int i = 0; i < kPolarizationEcmpWidth; ++i) {
        nhops.insert(PortDescriptor(interfacePorts[i]));
      }
    }
    programRoutesForNhops<folly::IPAddressV6>(nhops);
  }

  void configureAggregatePorts() {
    auto& ensemble = *getAgentEnsemble();
    auto interfacePorts = masterLogicalInterfacePortIds();
    std::vector<utility::AggregatePortInfo> aggPorts;
    for (int i = 0; i < kPolarizationNumAggPorts; ++i) {
      std::vector<PortID> members;
      for (int j = 0; j < kPolarizationAggPortWidth; ++j) {
        members.push_back(interfacePorts[i * kPolarizationAggPortWidth + j]);
      }
      aggPorts.push_back({AggregatePortID(i + 1), members});
    }
    auto cfg = utility::oneAggregatePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        aggPorts,
        true /*interfaceHasSubnet*/);
    auto asic = checkSameAndGetAsicForTesting(ensemble.getL3Asics());
    utility::setDefaultCpuTrafficPolicyConfig(
        cfg, ensemble.getL3Asics(), ensemble.isSai());
    utility::addCpuQueueConfig(
        cfg, ensemble.getL3Asics(), ensemble.isSai(), false /*setQueueRate*/);
    utility::addTrapPacketAcl(asic, &cfg, capturePorts(ensemble));
    applyNewConfig(cfg);
    applyNewState(
        [](const std::shared_ptr<SwitchState>& state) {
          return utility::enableTrunkPorts(state);
        },
        "enable trunk ports");
  }

  void runPolarizationTest() {
    skipIfUnsupported();
    if (::testing::Test::IsSkipped()) {
      return;
    }
    auto setup = [this]() { setupRoutes(); };
    auto verify = [this]() {
      auto firstHashes = makeHashes(kPolarizationSeedMac1);
      auto secondHashes = makeHashes(kPolarizationSeedMac2);
      auto captured = runFirstHash(firstHashes);
      if (captured.size() < kMinCapturedPackets) {
        // Severe CPU rate-limiting (e.g., default COPP on some XGS
        // platforms) can shrink the trapped sample below what's needed to
        // test polarization meaningfully. Skip rather than emit a false
        // positive.
        GTEST_SKIP() << "Captured " << captured.size()
                     << " packets — too few to test polarization (min "
                     << kMinCapturedPackets
                     << "); platform CPU rx may be rate-limited";
      }
      runSecondHash(captured, secondHashes);
    };
    verifyAcrossWarmBoots(setup, verify);
  }
};

TYPED_TEST_SUITE(AgentHashPolarizationTest, PolarizationTestTypes);

TYPED_TEST(AgentHashPolarizationTest, verifyNoPolarization) {
  this->runPolarizationTest();
}

} // namespace facebook::fboss
