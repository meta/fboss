// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/EcmpDataPlaneTestUtil.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/UdfTestUtils.h"

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/facilities/expand.hpp>
#include <boost/preprocessor/stringize.hpp>

#include <gtest/gtest.h>

#define TRAFFIC_NAME(TRAFFIC_TYPE) BOOST_PP_CAT(TRAFFIC_TYPE, Traffic)
#define HASH_NAME(HASH_TYPE) BOOST_PP_CAT(HASH_TYPE, Hash)
#define LB_NAME(MULTIPATH_TYPE) BOOST_PP_CAT(MULTIPATH_TYPE, LoadBalance)

#define TEST_NAME(MULTIPATH_TYPE, HASH_TYPE, TRAFFIC_TYPE) \
  BOOST_PP_CAT(                                            \
      LB_NAME(MULTIPATH_TYPE),                             \
      BOOST_PP_CAT(HASH_NAME(HASH_TYPE), TRAFFIC_NAME(TRAFFIC_TYPE)))

#define ECMP_SHRINK_EXPAND_TEST_NAME(HASH_TYPE) \
  BOOST_PP_CAT(                                 \
      ECMPShrinkExpandLoadBalance,              \
      BOOST_PP_CAT(HASH_NAME(HASH_TYPE), TRAFFIC_NAME(Cpu)))

/*
 * Run a particular combination of HwLoadBalancer test
 * For ASICs that don't support customization of hash fields, full hash
 * and half hash tests are identical. So skip half hash tests and just
 * run full hash tests
 * */
#define RUN_HW_LOAD_BALANCER_TEST(                                           \
    TEST_FIXTURE, MULTIPATH_TYPE, HASH_TYPE, TRAFFIC_TYPE)                   \
  TEST_F(TEST_FIXTURE, TEST_NAME(MULTIPATH_TYPE, HASH_TYPE, TRAFFIC_TYPE)) { \
    if (BOOST_PP_STRINGIZE(MULTIPATH_TYPE) == std::string{"WideUcmp"} &&                    \
            !isFeatureSupported(HwAsic::Feature::WIDE_ECMP)) {               \
      return;                                                                \
    }                                                                        \
    static bool kLoopThroughFrontPanelPort =                                 \
        (BOOST_PP_STRINGIZE(TRAFFIC_TYPE) != std::string{"Cpu"});            \
    runLoadBalanceTest(                                                      \
        8,                                                                   \
        facebook::fboss::utility::getEcmp##HASH_TYPE##HashConfig(            \
            {getHwAsic()}),                                                  \
        facebook::fboss::utility::kHwTest##MULTIPATH_TYPE##Weights(),        \
        kLoopThroughFrontPanelPort,                                          \
        true);                                                               \
  }

#define RUN_HW_LOAD_BALANCER_NEGATIVE_TEST(                                  \
    TEST_FIXTURE, MULTIPATH_TYPE, HASH_TYPE, TRAFFIC_TYPE)                   \
  TEST_F(TEST_FIXTURE, TEST_NAME(MULTIPATH_TYPE, HASH_TYPE, TRAFFIC_TYPE)) { \
    if (BOOST_PP_STRINGIZE(MULTIPATH_TYPE) == std::string{"WideUcmp"} &&                    \
            !isFeatureSupported(HwAsic::Feature::WIDE_ECMP)) {               \
      return;                                                                \
    }                                                                        \
    static bool kLoopThroughFrontPanelPort =                                 \
        (BOOST_PP_STRINGIZE(TRAFFIC_TYPE) != std::string{"Cpu"});            \
    runLoadBalanceTest(                                                      \
        8,                                                                   \
        facebook::fboss::utility::getEcmp##HASH_TYPE##HashConfig(            \
            {getHwAsic()}),                                                  \
        facebook::fboss::utility::kHwTest##MULTIPATH_TYPE##Weights(),        \
        kLoopThroughFrontPanelPort,                                          \
        false);                                                              \
  }

#define RUN_SHRINK_EXPAND_HW_LOAD_BALANCER_TEST(TEST_FIXTURE, HASH_TYPE) \
  TEST_F(TEST_FIXTURE, ECMP_SHRINK_EXPAND_TEST_NAME(HASH_TYPE)) {        \
    runEcmpShrinkExpandLoadBalanceTest(                                  \
        8,                                                               \
        facebook::fboss::utility::getEcmp##HASH_TYPE##HashConfig(        \
            {getHwAsic()}));                                             \
  }

#define RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, MULTIPATH_TYPE, HASH_TYPE) \
  RUN_HW_LOAD_BALANCER_TEST(TEST_FIXTURE, MULTIPATH_TYPE, HASH_TYPE, Cpu)

#define RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL( \
    TEST_FIXTURE, MULTIPATH_TYPE, HASH_TYPE)   \
  RUN_HW_LOAD_BALANCER_TEST(TEST_FIXTURE, MULTIPATH_TYPE, HASH_TYPE, FrontPanel)

#define RUN_HW_LOAD_BALANCER_NEGATIVE_TEST_FRONT_PANEL( \
    TEST_FIXTURE, MULTIPATH_TYPE, HASH_TYPE)            \
  RUN_HW_LOAD_BALANCER_NEGATIVE_TEST(                   \
      TEST_FIXTURE, MULTIPATH_TYPE, HASH_TYPE, FrontPanel)

#define RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_CPU(TEST_FIXTURE) \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, Ecmp, Full)    \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, Ecmp, Half)

#define RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_CPU(TEST_FIXTURE) \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, Ucmp, Full)    \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, Ucmp, Half)

#define RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_CPU(TEST_FIXTURE) \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, WideUcmp, Full)     \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, WideUcmp, Half)

#define RUN_ALL_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE)       \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, Ecmp, Full)     \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, Ecmp, Half)     \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, Ucmp, Full)     \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, Ucmp, Half)     \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, WideUcmp, Full) \
  RUN_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE, WideUcmp, Half)

#define RUN_ALL_HW_LOAD_BALANCER_ECMP_TEST_FRONT_PANEL(TEST_FIXTURE) \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, Ecmp, Full)    \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, Ecmp, Half)

#define RUN_ALL_HW_LOAD_BALANCER_UCMP_TEST_FRONT_PANEL(TEST_FIXTURE) \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, Ucmp, Full)    \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, Ucmp, Half)

#define RUN_ALL_HW_LOAD_BALANCER_WIDE_UCMP_TEST_FRONT_PANEL(TEST_FIXTURE) \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, WideUcmp, Full)     \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, WideUcmp, Half)

#define RUN_ALL_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE)       \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, Ecmp, Full)     \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, Ecmp, Half)     \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, Ucmp, Full)     \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, Ucmp, Half)     \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, WideUcmp, Full) \
  RUN_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE, WideUcmp, Half)

#define RUN_SHRINK_EXPAND_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE) \
  RUN_SHRINK_EXPAND_HW_LOAD_BALANCER_TEST(TEST_FIXTURE, Full)     \
  RUN_SHRINK_EXPAND_HW_LOAD_BALANCER_TEST(TEST_FIXTURE, Half)

#define RUN_ALL_HW_LOAD_BALANCER_TEST(TEST_FIXTURE)       \
  RUN_ALL_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE)         \
  RUN_ALL_HW_LOAD_BALANCER_TEST_FRONT_PANEL(TEST_FIXTURE) \
  RUN_SHRINK_EXPAND_HW_LOAD_BALANCER_TEST_CPU(TEST_FIXTURE)

// 6 (not 8) to allow for the members to stay within same core
#define RUN_HW_LOAD_BALANCER_TEST_SPRAY(                              \
    TEST_FIXTURE, MULTIPATH_TYPE, HASH_TYPE)                          \
  TEST_F(TEST_FIXTURE, TEST_NAME(MULTIPATH_TYPE, HASH_TYPE, Cpu)) {   \
    runLoadBalanceTest(                                               \
        6,                                                            \
        facebook::fboss::utility::getEcmp##HASH_TYPE##HashConfig(     \
            {getHwAsic()}),                                           \
        facebook::fboss::utility::kHwTest##MULTIPATH_TYPE##Weights(), \
        false,                                                        \
        true);                                                        \
  }

namespace facebook::fboss::utility {

template <typename EcmpTestHelperT, bool kFlowLetSwitching = false>
class HwLoadBalancerTestRunner {
 public:
  using SETUP_FN = std::function<void()>;
  using VERIFY_FN = std::function<void()>;
  using SETUP_POSTWB_FN = std::function<void()>;
  using VERIFY_POSTWB_FN = std::function<void()>;

  virtual ~HwLoadBalancerTestRunner() = default;

 protected:
  virtual void runTestAcrossWarmBoots(
      SETUP_FN setup,
      VERIFY_FN verify,
      SETUP_POSTWB_FN setupPostWarmboot,
      VERIFY_POSTWB_FN verifyPostWarmboot) = 0;

  virtual std::unique_ptr<EcmpTestHelperT> getECMPHelper() = 0;

  virtual TestEnsembleIf* getEnsemble() = 0;
  virtual const TestEnsembleIf* getEnsemble() const = 0;
  std::vector<PortID> getMasterLogicalPortIds() const {
    return getEnsemble()->masterLogicalPortIds();
  }

  void setEcmpHelper() {
    helper_ = getECMPHelper();
  }

  virtual bool skipTest() const {
    return false;
  }

  // Called before traffic is looped through frontPanelPort.
  virtual void learnFrontPanelSrcMac(PortID /* frontPanelPort */) {}

  bool isFeatureSupported(HwAsic::Feature feature) const {
    return getEnsemble()->getHwAsicTable()->isFeatureSupportedOnAnyAsic(
        feature);
  }

  const HwAsic* getHwAsic() const {
    auto switchIds = getEnsemble()->getHwAsicTable()->getSwitchIDs();
    CHECK_GE(switchIds.size(), 1);
    return getEnsemble()->getHwAsicTable()->getHwAsic(*switchIds.begin());
  }

  void runLoadBalanceTest(
      unsigned int ecmpWidth,
      const cfg::LoadBalancer& loadBalancer,
      const std::vector<NextHopWeight>& weights,
      bool loopThroughFrontPanel = false,
      bool loadBalanceExpected = true,
      uint8_t deviation = 25) {
    if (skipTest()) {
#if defined(GTEST_SKIP)
      GTEST_SKIP();
#endif
      return;
    }
    auto setup = [=, this]() {
      helper_->programRoutesAndLoadBalancer(ecmpWidth, weights, loadBalancer);
    };
    auto verify = [=, this]() {
      if (loopThroughFrontPanel) {
        learnFrontPanelSrcMac(helper_->ecmpSetupHelper()
                                  ->ecmpPortDescriptorAt(ecmpWidth)
                                  .phyPortID());
      }
      helper_->pumpTrafficPortAndVerifyLoadBalanced(
          ecmpWidth,
          loopThroughFrontPanel,
          weights,
          deviation,
          loadBalanceExpected);
    };

    runTestAcrossWarmBoots(setup, verify, []() {}, []() {});
  }

  void runEcmpShrinkExpandLoadBalanceTest(
      unsigned int ecmpWidth,
      const cfg::LoadBalancer& loadBalancer,
      uint8_t deviation = 25) {
    if (skipTest()) {
#if defined(GTEST_SKIP)
      GTEST_SKIP();
#endif
      return;
    }
    unsigned int minLinksLoadbalanceTest = 1;
    auto setup = [=, this]() {
      helper_->programRoutesAndLoadBalancer(
          ecmpWidth, {} /*weights*/, loadBalancer);
    };
    auto verify = [=, this]() {
      unsigned int width = ecmpWidth;

      while (width > minLinksLoadbalanceTest) {
        width--;
        helper_->shrinkECMP(width);

        helper_->pumpTrafficPortAndVerifyLoadBalanced(
            width,
            false, /*loopThroughFrontPanel*/
            {}, /*weights*/
            deviation,
            true);
        // Now that we are done checking, unresolve the shrunk next hop
        helper_->unresolveNextHop(width);
      }

      while (width < ecmpWidth) {
        helper_->expandECMP(width);
        helper_->pumpTrafficPortAndVerifyLoadBalanced(
            width,
            false, /*loopThroughFrontPanel*/
            {}, /*weights*/
            deviation,
            true);
        width++;
      }
    };
    setup();
    verify();
  }

  EcmpTestHelperT* getEcmpSetupHelper() const {
    return helper_.get();
  }

 private:
  std::unique_ptr<EcmpTestHelperT> helper_;
};
} // namespace facebook::fboss::utility
