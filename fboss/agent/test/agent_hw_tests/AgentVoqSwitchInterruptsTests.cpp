// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
#include <fmt/format.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string_view>

#include <folly/FileUtil.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/lib/CommonUtils.h"

#include "folly/testing/TestUtil.h"

namespace facebook::fboss {

namespace {
constexpr auto kInterruptMaskedEventCintStr = R"(
      cint_reset();
      bcm_switch_event_control_t type;
      type.event_id = 2128; // JR3_INT_MACT_LARGE_EM_LARGE_EM_EVENT_FIFO_HIGH_THRESHOLD_REACHED
      type.index = 0;
      type.action = bcmSwitchEventMask;
      bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, type, 0);
    )";
} // namespace

class AgentVoqSwitchInterruptTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::VOQ_DNX_INTERRUPTS};
  }

 protected:
  void runCint(const std::string& cintStr) {
    folly::test::TemporaryFile file;
    XLOG(INFO) << " Cint file " << file.path().c_str();
    folly::writeFull(file.fd(), cintStr.c_str(), cintStr.size());
    auto cmd = fmt::format("cint {}\n", file.path().c_str());
    runCmd(cmd);
  }
  void runCmd(const std::string& cmd) {
    for (auto voqSwitchId : getSw()->getSwitchInfoTable().getSwitchIdsOfType(
             cfg::SwitchType::VOQ)) {
      std::string out;
      // Start with a blank command first and then run the actual command
      // This is because, sometimes first diag command doesn't work in some SDK
      // versions and in FBOSS OSS
      getAgentEnsemble()->runDiagCommand("\n", out, voqSwitchId);
      getAgentEnsemble()->runDiagCommand(cmd, out, voqSwitchId);
    }
  }
  std::unordered_set<uint16_t> voqIndices() const {
    return getSw()->getSwitchInfoTable().getSwitchIndicesOfType(
        cfg::SwitchType::VOQ);
  }
  std::map<uint16_t, HwAsicErrors> getVoqAsicErrors() {
    auto voqIdxs = voqIndices();
    std::map<uint16_t, HwAsicErrors> asicErrors;
    WITH_RETRIES({
      asicErrors.clear();
      for (const auto& [switchIdx, switchStats] :
           getSw()->getHwSwitchStatsExpensive()) {
        if (voqIdxs.contains(switchIdx)) {
          asicErrors.insert({switchIdx, *switchStats.hwAsicErrors()});
        }
      }
      EXPECT_EVENTUALLY_EQ(asicErrors.size(), voqIdxs.size());
    });
    if (asicErrors.size() != voqIdxs.size()) {
      throw FbossError("Could not get voq asic error stats");
    }

    return asicErrors;
  }
};

class AgentVoqSwitchVendorSwitchInterruptTest
    : public AgentVoqSwitchInterruptTest {
 public:
  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_ignore_asic_hard_reset_notification = true;
  }
};

class AgentVoqSwitchSdkDumpRateLimitTest : public AgentVoqSwitchInterruptTest {
 public:
  void setCmdLineFlagOverrides() const override {
    AgentVoqSwitchInterruptTest::setCmdLineFlagOverrides();
    // The rate limiter has nothing to suppress unless the SDK is dumping.
    FLAGS_skip_sdk_reg_dump = false;
    FLAGS_sdk_dump_rate_limit_window_ms = windowSecs() * 1000;
    // Keep the dump off /var/facebook so the test can read it back. The temp
    // dir, not a fixed /tmp, since the hw agent may run with its own /tmp.
    FLAGS_sdk_reg_dump_path_prefix =
        (std::filesystem::temp_directory_path() / "sdk_reg_dump_rate_limit")
            .string();
  }

 protected:
  // The rate limit window is expressed in stats cycles rather than seconds, so
  // it is always a whole number of cycles whatever update_stats_interval_s is
  // set to and a window always closes on a cycle boundary.
  static constexpr int kWindowCycles = 10;
  // The SDK prefixes every dump it writes with this header. Matching the field
  // that follows BEGIN as well keeps a payload that happens to contain the word
  // from being counted as a write.
  static constexpr std::string_view kSdkRegDumpWriteMarker =
      "\nBEGIN\nEvent Type:";

  static int sampleSecs() {
    return std::max(1, FLAGS_update_stats_interval_s);
  }

  static int windowSecs() {
    return kWindowCycles * sampleSecs();
  }

  // Mirrors the path SaiSwitch::setSdkRegDumpEnabledLocked() programs, which
  // names the file after the switch index.
  std::string sdkRegDumpPath(uint16_t switchIdx) const {
    return fmt::format("{}_{}.log", FLAGS_sdk_reg_dump_path_prefix, switchIdx);
  }

  // The SDK writes a header delimited block per dump it actually writes, so
  // counting them counts the writes the rate limiter let through. Summed over
  // the same switches getSdkDumpSuppressedCount() covers, since each one writes
  // its own file.
  int countSdkRegDumpWrites() const {
    int writes = 0;
    for (auto switchIdx : voqIndices()) {
      std::string contents;
      if (!folly::readFile(sdkRegDumpPath(switchIdx).c_str(), contents)) {
        continue;
      }
      for (size_t pos = contents.find(kSdkRegDumpWriteMarker);
           pos != std::string::npos;
           pos = contents.find(
               kSdkRegDumpWriteMarker, pos + kSdkRegDumpWriteMarker.size())) {
        writes++;
      }
    }
    return writes;
  }

  // Sum over the same switches countSdkRegDumpWrites() covers.
  int64_t getSdkDumpSuppressedCount() {
    auto voqIdxs = voqIndices();
    int64_t total = 0;
    for (const auto& [switchIdx, switchStats] :
         getSw()->getHwSwitchStatsExpensive()) {
      if (!voqIdxs.contains(switchIdx)) {
        continue;
      }
      auto suppressed =
          switchStats.fb303GlobalStats()->sdk_dump_suppressed_count();
      if (suppressed.has_value()) {
        total += *suppressed;
        XLOG(DBG2) << "Switch index: " << switchIdx
                   << " SDK dumps suppressed: " << *suppressed;
      }
    }
    return total;
  }
};

TEST_F(AgentVoqSwitchInterruptTest, ireError) {
  auto verify = [=, this]() {
    constexpr auto kIreErrorIncjectorCintStr = R"(
  cint_reset();
  bcm_switch_event_control_t event_ctrl;
  event_ctrl.event_id = 2064;
  event_ctrl.index = 0; /* core ID */
  event_ctrl.action = bcmSwitchEventForce;
  print bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 0);
  print bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 1);
  )";
    runCint(kIreErrorIncjectorCintStr);
    WITH_RETRIES({
      auto asicErrors = getVoqAsicErrors();
      for (const auto& [idx, asicError] : asicErrors) {
        auto ireErrors = asicError.ingressReceiveEditorErrors().value_or(0);
        XLOG(INFO) << "Switch index: " << idx << " IRE Errors: " << ireErrors;
        EXPECT_EVENTUALLY_GT(ireErrors, 0);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchInterruptTest, itppError) {
  auto verify = [=, this]() {
    std::string out;
    runCmd("s itpp_interrupt_mask_register 0x3f\n");
    runCmd("s itpp_interrupt_register_test 0x0\n");
    runCmd("s itpp_interrupt_register_test 0x2\n");
    runCmd("s itppd_interrupt_mask_register 0x3f\n");
    runCmd("s itppd_interrupt_register_test 0x0\n");
    runCmd("s itppd_interrupt_register_test 0x2\n");
    WITH_RETRIES({
      auto asicErrors = getVoqAsicErrors();
      for (const auto& [idx, asicError] : asicErrors) {
        auto itppErrors = asicError.ingressTransmitPipelineErrors().value_or(0);
        XLOG(INFO) << "Switch index: " << idx << " ITPP Errors: " << itppErrors;
        EXPECT_EVENTUALLY_GT(itppErrors, 0);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchInterruptTest, epniError) {
  auto verify = [=, this]() {
    constexpr auto kEpniErrorIncjectorCintStr = R"(
  cint_reset();
  bcm_switch_event_control_t event_ctrl;
  event_ctrl.event_id = 717;  // JR3_INT_EPNI_FIFO_OVERFLOW_INT
  event_ctrl.index = 0; /* core ID */
  event_ctrl.action = bcmSwitchEventForce;
  print bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 0);
  print bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 1);
  )";
    runCint(kEpniErrorIncjectorCintStr);
    WITH_RETRIES({
      auto asicErrors = getVoqAsicErrors();
      for (const auto& [idx, asicError] : asicErrors) {
        auto epniErrors =
            asicError.egressPacketNetworkInterfaceErrors().value_or(0);
        XLOG(INFO) << "Switch index: " << idx << " EPNI Errors: " << epniErrors;
        EXPECT_EVENTUALLY_GT(epniErrors, 0);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchInterruptTest, alignerError) {
  auto verify = [=, this]() {
    constexpr auto kAlignerErrorIncjectorCintStr = R"(
  cint_reset();
  bcm_switch_event_control_t event_ctrl;
  event_ctrl.event_id = 8;  // JR3_INT_ALIGNER_PKT_SIZE_EOP_MISMATCH_INT
  event_ctrl.index = 0; /* core ID */
  event_ctrl.action = bcmSwitchEventForce;
  print bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 0);
  print bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 1);
  )";
    runCint(kAlignerErrorIncjectorCintStr);
    WITH_RETRIES({
      auto asicErrors = getVoqAsicErrors();
      for (const auto& [idx, asicError] : asicErrors) {
        auto alignerErrors = asicError.alignerErrors().value_or(0);
        XLOG(INFO) << "Switch index: " << idx
                   << " Aligner Errors: " << alignerErrors;
        EXPECT_EVENTUALLY_GT(alignerErrors, 0);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchInterruptTest, fqpError) {
  auto verify = [=, this]() {
    constexpr auto kFqpErrorIncjectorCintStr = R"(
  cint_reset();
  bcm_switch_event_control_t event_ctrl;
  event_ctrl.event_id = 1294;  // JR3_INT_FQP_ECC_ECC_1B_ERR_INT
  event_ctrl.index = 0; /* core ID */
  event_ctrl.action = bcmSwitchEventForce;
  print bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 0);
  print bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 1);
  )";
    runCint(kFqpErrorIncjectorCintStr);
    WITH_RETRIES({
      auto asicErrors = getVoqAsicErrors();
      for (const auto& [idx, asicError] : asicErrors) {
        auto fqpErrors = asicError.forwardingQueueProcessorErrors().value_or(0);
        XLOG(INFO) << "Switch index: " << idx << " FQP Errors: " << fqpErrors;
        EXPECT_EVENTUALLY_GT(fqpErrors, 0);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchInterruptTest, allReassemblyContextsTakenError) {
  auto verify = [=, this]() {
    std::string out;
    runCmd(
        "modreg RQP_PKT_REAS_INTERRUPT_REGISTER_TEST PKT_REAS_INTERRUPT_REGISTER_TEST=0x8000\n");
    runCmd(
        "modreg RQP_PKT_REAS_INTERRUPT_REGISTER_TEST PKT_REAS_INTERRUPT_REGISTER_TEST=0x10000\n");
    WITH_RETRIES({
      auto asicErrors = getVoqAsicErrors();
      for (const auto& [idx, asicError] : asicErrors) {
        auto allReassemblyContextsTaken =
            asicError.allReassemblyContextsTaken().value_or(0);
        XLOG(INFO) << " Switch index: " << idx
                   << " All Reassemble contexts taken: "
                   << allReassemblyContextsTaken;
        EXPECT_EVENTUALLY_GT(allReassemblyContextsTaken, 0);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchInterruptTest, validateInterruptMaskedEventCallback) {
  auto verify = [=, this]() {
    runCint(kInterruptMaskedEventCintStr);
    WITH_RETRIES({
      for (const auto& [switchIdx, switchStats] :
           getSw()->getHwSwitchStatsExpensive()) {
        auto intrMaskedEvents =
            switchStats.fb303GlobalStats()->interrupt_masked_events();
        ASSERT_EVENTUALLY_TRUE(intrMaskedEvents.has_value());
        XLOG(INFO) << "Switch index: " << switchIdx
                   << " Interrupt masked events: " << *intrMaskedEvents;
        EXPECT_EVENTUALLY_GT(*intrMaskedEvents, 0);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchSdkDumpRateLimitTest, verifySdkDumpRateLimit) {
  auto verify = [=, this]() {
    auto baselineSuppressed = getSdkDumpSuppressedCount();
    auto baselineWrites = countSdkRegDumpWrites();

    // A masked interrupt is one of the NORMAL priority events the SDK writes a
    // register/state dump for. Once masked it fires back to back at a high
    // rate - that flood is what S673847 was about - so the SDK sees far more
    // events than the injections below, and the suppressed count climbs by
    // thousands a second rather than tracking the injection rate.
    //
    // One sample per stats cycle, so the agent has refreshed the counter
    // between samples. The budget covers a little over two windows, which is
    // headroom - the loop stops as soon as the throttling shows.
    const int kNumSamples = kWindowCycles * 2 + 5;
    // Exactly one dump is let through per window, so two writes already show
    // the throttling and the loop stops there - this is checking that dumps are
    // throttled, not counting them precisely, so there is nothing to gain from
    // waiting on further windows. The cap covers every window the run could
    // span, plus a boundary landing either side of it.
    const int kMinWrites = 2;
    const int kMaxWrites = kNumSamples / kWindowCycles + 2;
    int events = 0;
    WITH_RETRIES_N_TIMED(kNumSamples, std::chrono::seconds(sampleSecs()), {
      runCint(kInterruptMaskedEventCintStr);
      events++;
      // Gating the loop on the write count means it always covers at least the
      // windows that count needs.
      EXPECT_EVENTUALLY_GE(
          countSdkRegDumpWrites() - baselineWrites, kMinWrites);
    });

    auto writes = countSdkRegDumpWrites() - baselineWrites;
    // The agent accumulates sdk_dump_suppressed_count every stats cycle, so a
    // single read here already reflects everything suppressed during the loop.
    auto suppressed = getSdkDumpSuppressedCount() - baselineSuppressed;
    XLOG(INFO) << "Injected " << events << " masked interrupts with a "
               << windowSecs() << "s dump rate limit window, SDK wrote "
               << writes << " dumps under " << FLAGS_sdk_reg_dump_path_prefix
               << " and suppressed " << suppressed;
    // The loop only stops once two dumps have been written, one window apart,
    // so by here the SDK has written once per window. All that is left to rule
    // out is it writing once per event.
    EXPECT_LE(writes, kMaxWrites);
    // The masked interrupt fires far faster than the rate it is injected at, so
    // the limiter holds back orders of magnitude more dumps than the handful it
    // lets through - in practice thousands. Assert only that it is a large
    // multiple of the injections, well below what is actually observed, since
    // the exact rate is a property of the ASIC and not something to pin down.
    EXPECT_GT(suppressed, 10 * events);
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchInterruptTest, softResetErrors) {
  auto verify = [=, this]() {
    std::string out;
    runCmd("der soft nofabric=1\n");
    runCmd("quit\n");
    WITH_RETRIES({
      auto asicErrors = getVoqAsicErrors();
      for (const auto& [idx, asicError] : asicErrors) {
        auto asicSoftResetErrors = asicError.asicSoftResetErrors().value_or(0);
        XLOG(INFO) << " Switch index: " << idx
                   << " Asic soft reset errors: " << asicSoftResetErrors;
        EXPECT_EVENTUALLY_GT(asicSoftResetErrors, 0);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchVendorSwitchInterruptTest, blockLevelErrorInterruptsTest) {
  auto verify = [=, this]() {
    constexpr auto kTriggerBlockWiseInterruptsCintStr = R"(
  cint_reset();
  int count = 10;
  int event_ids[count] = {2321, 521, 792, 728, 1087, 935, 2659, 1079, 2230, 2438};
  int i;
  for (i=0; i < count; i++) {
    bcm_switch_event_control_t event_ctrl;
    event_ctrl.event_id = event_ids[i];
    event_ctrl.index = 0; /* core id */
    event_ctrl.action = bcmSwitchEventForce;
    bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 1);
  }
  sal_usleep(100000);
  for (i=0; i < count; i++) {
    bcm_switch_event_control_t event_ctrl;
    event_ctrl.event_id = event_ids[i];
    event_ctrl.index = 0; /* core id */
    event_ctrl.action = bcmSwitchEventForce;
    bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 0);
  }
)";
    WITH_RETRIES({
      runCint(kTriggerBlockWiseInterruptsCintStr);
      auto asicErrors = getVoqAsicErrors();
      for (const auto& [idx, asicError] : asicErrors) {
        XLOG(INFO) << "Errors for switch id " << idx;
        auto dramErrors = asicError.dramErrors().value_or(0);
        XLOG(INFO) << "DRAM errors: " << dramErrors;
        EXPECT_EVENTUALLY_GT(dramErrors, 0);
        auto egressTmErrors = asicError.egressTmErrors().value_or(0);
        XLOG(INFO) << "Egress TM errors: " << egressTmErrors;
        EXPECT_EVENTUALLY_GT(egressTmErrors, 0);
        auto fabricLinkErrors = asicError.fabricLinkErrors().value_or(0);
        XLOG(INFO) << "Fabric Link errors: " << fabricLinkErrors;
        EXPECT_EVENTUALLY_GT(fabricLinkErrors, 0);
        auto fabricRxErrors = asicError.fabricRxErrors().value_or(0);
        XLOG(INFO) << "Fabric RX errors: " << fabricRxErrors;
        EXPECT_EVENTUALLY_GT(fabricRxErrors, 0);
        auto fabricTxErrors = asicError.fabricTxErrors().value_or(0);
        XLOG(INFO) << "Fabric TX errors: " << fabricTxErrors;
        EXPECT_EVENTUALLY_GT(fabricTxErrors, 0);
        auto ingressPpErrors = asicError.ingressPpErrors().value_or(0);
        XLOG(INFO) << "Ingress PP errors: " << ingressPpErrors;
        EXPECT_EVENTUALLY_GT(ingressPpErrors, 0);
        auto egressPpErrors = asicError.egressPpErrors().value_or(0);
        XLOG(INFO) << "Egress PP errors: " << egressPpErrors;
        EXPECT_EVENTUALLY_GT(egressPpErrors, 0);
        auto counterAndMeterErrors =
            asicError.counterAndMeterErrors().value_or(0);
        XLOG(INFO) << "Counter and Meter errors: " << counterAndMeterErrors;
        EXPECT_EVENTUALLY_GT(counterAndMeterErrors, 0);
        auto fabricTopologyErrors =
            asicError.fabricTopologyErrors().value_or(0);
        XLOG(INFO) << "Fabric Topology errors: " << fabricTopologyErrors;
        EXPECT_EVENTUALLY_GT(fabricTopologyErrors, 0);
        auto networkInterfaceErrors =
            asicError.networkInterfaceErrors().value_or(0);
        XLOG(INFO) << "Network Interface errors: " << networkInterfaceErrors;
        EXPECT_EVENTUALLY_GT(networkInterfaceErrors, 0);
      }
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentVoqSwitchVendorSwitchInterruptTest, verifyhardResetCallback) {
  auto verify = [=, this]() {
    constexpr auto kTriggerHardResetCintStr = R"(
  cint_reset();
  bcm_switch_event_control_t event_ctrl;
  event_ctrl.event_id = 1952;
  event_ctrl.index = 0; /* core id */
  event_ctrl.action = bcmSwitchEventForce;

  bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 1);
  sal_usleep(100000);
  bcm_switch_event_control_set(0, BCM_SWITCH_EVENT_DEVICE_INTERRUPT, event_ctrl, 0);
)";

    bool hardResetNotifSeen = false;
    runCint(kTriggerHardResetCintStr);

    WITH_RETRIES_N_TIMED(10, std::chrono::milliseconds(1000), {
      auto asicErrors = getVoqAsicErrors();
      for (const auto& [idx, asicError] : asicErrors) {
        auto ingressTmErrors = asicError.ingressTmErrors().value_or(0);
        XLOG(INFO) << "Ingress TM errors: " << ingressTmErrors;
        EXPECT_EVENTUALLY_GT(ingressTmErrors, 0);
      }

      // Make sure that hard reset notification callback is working as expected!
      // Hard reset notification callback should result from the interrupt ID
      // 1952 used in this test.
      for (const auto& [switchId, asic] : getAsics()) {
        auto switchIndex =
            getSw()->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
        int hardResetNotificationReceived =
            *getSw()
                 ->getHwSwitchStatsExpensive()[switchIndex]
                 .hardResetStats()
                 ->hard_reset_notification_received();
        if (hardResetNotificationReceived) {
          hardResetNotifSeen = true;
        }
      }
      EXPECT_EVENTUALLY_TRUE(hardResetNotifSeen);
    });
  };
  verifyAcrossWarmBoots([]() {}, verify);
}

} // namespace facebook::fboss
