// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/lib/CommonUtils.h"

#include <fb303/ServiceData.h>
#include <folly/logging/xlog.h>

#include <chrono>
#include <thread>

namespace facebook::fboss {

constexpr double kTolerance = 0.10;
constexpr int32_t kHoldoffLongMs = 15000;
constexpr auto kFlapWithinWindow = std::chrono::milliseconds(1000);
// Number of extra flaps to inject inside an active down-holdoff window to force
// the SDK debounce to retrigger.
constexpr int kNumRetriggers = 3;
// Settle time between flaps so the SDK observes each link edge as a distinct
// notification. Without it, wait-free flaps can outpace the SDK's link
// notification delivery on slower SDKs and drop an edge, making the retrigger
// count non-deterministic.
constexpr int32_t kFlapSettleMs = 1000;
constexpr auto kDownRetriggerCounter = "link_down_debounce_retrigger";
constexpr auto kUpRetriggerCounter = "link_up_debounce_retrigger";

class AgentHwLinkDebounceTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::PORT_DEBOUNCE};
  }

 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    return utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
  }

  PortID portForTest() const {
    return getAgentEnsemble()->masterLogicalInterfacePortIds(
        getCurrentSwitchIdForTesting())[0];
  }

  // Apply a config that overrides the debounce values for the test port.
  // Either argument may be std::nullopt to leave the SDK default in place.
  void applyDebounceConfig(
      std::optional<int32_t> upMs,
      std::optional<int32_t> downMs) {
    auto config = initialConfig(*getAgentEnsemble());
    auto portCfg = utility::findCfgPort(config, portForTest());
    if (upMs.has_value()) {
      portCfg->portUpHoldoffTimeMs() = *upMs;
    } else {
      portCfg->portUpHoldoffTimeMs().reset();
    }
    if (downMs.has_value()) {
      portCfg->portDownHoldoffTimeMs() = *downMs;
    } else {
      portCfg->portDownHoldoffTimeMs().reset();
    }
    applyNewConfig(config);
  }

  // Time how long it takes for the test port to reach `up` after triggering
  // a flap via bringUpPort/bringDownPort. Both calls block until the
  // corresponding state change is observed, so the elapsed wall-clock time
  // reflects the SDK debounce hold timer + baseline FBOSS overhead.
  std::chrono::milliseconds measureBringUpDownLatency(bool up) {
    auto port = portForTest();
    auto start = std::chrono::steady_clock::now();
    if (up) {
      bringUpPort(port);
    } else {
      bringDownPort(port);
    }
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  }

  // bringUp/DownPort go through linkStateToggler which waits for the port
  // status to be updated; this helper flips loopback mode without waiting so
  // we can flap inside the SDK debounce window.
  void togglePortNoWait(PortID port, bool toUp) {
    auto asicTable = getAgentEnsemble()->getHwAsicTable();
    auto switchId = scopeResolver().scope(port).switchId();
    auto asic = asicTable->getHwAsic(switchId);
    auto desiredMode = toUp
        ? asic->getDesiredLoopbackMode(
              getProgrammedState()->getPorts()->getNodeIf(port)->getPortType())
        : cfg::PortLoopbackMode::NONE;
    applyNewState([port, desiredMode](const std::shared_ptr<SwitchState>& in) {
      auto newState = in->clone();
      auto newPort = newState->getPorts()->getNodeIf(port)->modify(&newState);
      newPort->setLoopbackMode(desiredMode);
      return newState;
    });
  }

  int64_t getPortFb303Counter(PortID port, const std::string& key) const {
    auto name = getProgrammedState()->getPorts()->getNodeIf(port)->getName();
    return fb303::fbData->getCounterIfExists(name + "." + key + ".sum")
        .value_or(0);
  }

  int64_t getLinkStateFlapCount(PortID port) const {
    return getPortFb303Counter(port, "link_state.flap");
  }

  int64_t getLinkFaultCount(PortID port) const {
    return getPortFb303Counter(port, "link_fault");
  }

  int64_t getDebounceRetriggerCount(PortID port, bool up) {
    auto stats = getLatestPortStats(port);
    return (up ? stats.linkUpDebounceRetriggerCount_()
               : stats.linkDownDebounceRetriggerCount_())
        .value_or(0);
  }

  void settleBetweenFlaps() const {
    // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep
    std::this_thread::sleep_for(std::chrono::milliseconds(kFlapSettleMs));
  }

  void verifyRetriggerCount(bool upDebounce) {
    auto port = portForTest();
    applyDebounceConfig(kHoldoffLongMs, kHoldoffLongMs);
    if (upDebounce) {
      bringDownPort(port);
      ASSERT_FALSE(getProgrammedState()->getPorts()->getNodeIf(port)->isUp());
    } else {
      ASSERT_TRUE(getProgrammedState()->getPorts()->getNodeIf(port)->isUp());
    }

    auto retriggerCount = [&]() {
      return getDebounceRetriggerCount(port, upDebounce);
    };
    // The SDK counts survive warm boot, so baselining before the first read
    // that follows the debounce config charges them to this run.
    WITH_RETRIES({
      auto stats = getLatestPortStats(port);
      EXPECT_EVENTUALLY_TRUE(
          stats.linkDownDebounceRetriggerCount_().has_value() &&
          stats.linkUpDebounceRetriggerCount_().has_value());
    });
    auto before = retriggerCount();
    auto flapsBefore = getLinkStateFlapCount(port);
    auto faultBefore = getLinkFaultCount(port);
    auto downRetriggersBefore = getDebounceRetriggerCount(port, false);
    auto upRetriggersBefore = getDebounceRetriggerCount(port, true);
    auto swDownRetriggersBefore =
        getPortFb303Counter(port, kDownRetriggerCounter);
    auto swUpRetriggersBefore = getPortFb303Counter(port, kUpRetriggerCounter);

    // Arm the debounce in the held-off direction, then flap back and forth,
    // settling between flaps so the SDK registers each edge (deterministic).
    togglePortNoWait(port, upDebounce);
    settleBetweenFlaps();
    for (int i = 0; i < kNumRetriggers; ++i) {
      togglePortNoWait(port, !upDebounce);
      settleBetweenFlaps();
      togglePortNoWait(port, upDebounce);
      settleBetweenFlaps();
    }

    WITH_RETRIES({
      auto after = retriggerCount();
      XLOG(INFO) << "Port link" << (upDebounce ? "Up" : "Down")
                 << "DebounceRetriggerCount before/after: " << before << "/"
                 << after;
      EXPECT_EVENTUALLY_EQ(after - before, kNumRetriggers);
    });

    // link_fault counts link downs and link down debounce retriggers. Link up
    // retriggers are re-asserted link ups, not faults, so they must never be
    // added here. Reported flaps alternate from the oper state asserted above,
    // so half of them are downs, rounding up only if the port started up.
    // Compared as deltas since the retrigger counts are not reset by the test.
    WITH_RETRIES({
      auto flapsDelta = getLinkStateFlapCount(port) - flapsBefore;
      auto downRetriggersDelta =
          getDebounceRetriggerCount(port, false) - downRetriggersBefore;
      auto faultDelta = getLinkFaultCount(port) - faultBefore;
      auto downFlaps = upDebounce ? flapsDelta / 2 : (flapsDelta + 1) / 2;
      XLOG(INFO) << "Port link_fault delta " << faultDelta << " vs link downs "
                 << downFlaps << " (of " << flapsDelta
                 << " flaps) + link down retriggers " << downRetriggersDelta;
      EXPECT_EVENTUALLY_EQ(faultDelta, downFlaps + downRetriggersDelta);
    });

    // Both directions are also tracked on their own, so the SwSwitch counters
    // must mirror the hardware counts even though only the down one is a fault.
    WITH_RETRIES({
      auto swDown = getPortFb303Counter(port, kDownRetriggerCounter) -
          swDownRetriggersBefore;
      auto swUp =
          getPortFb303Counter(port, kUpRetriggerCounter) - swUpRetriggersBefore;
      XLOG(INFO) << "Port debounce retrigger deltas, sw down/up " << swDown
                 << "/" << swUp;
      EXPECT_EVENTUALLY_EQ(
          swDown,
          getDebounceRetriggerCount(port, false) - downRetriggersBefore);
      EXPECT_EVENTUALLY_EQ(
          swUp, getDebounceRetriggerCount(port, true) - upRetriggersBefore);
    });

    applyDebounceConfig(std::nullopt, std::nullopt);
    bringUpPort(port);
  }
};

TEST_F(AgentHwLinkDebounceTest, DebounceTimerWithinTolerance) {
  auto port = portForTest();
  auto setup = [&]() { bringUpPort(port); };
  auto verify = [&]() {
    // First measure the no-debounce baseline so we can subtract it from each
    // holdoff measurement and isolate the SDK's hold-timer contribution.
    applyDebounceConfig(std::nullopt, std::nullopt);
    auto baselineDownMs = measureBringUpDownLatency(false);
    auto baselineUpMs = measureBringUpDownLatency(true);
    XLOG(INFO) << "Baseline (no debounce) down=" << baselineDownMs.count()
               << "ms up=" << baselineUpMs.count() << "ms";

    auto verifyHoldoff = [&](int32_t holdoffMs) {
      applyDebounceConfig(holdoffMs, holdoffMs);
      auto downMs = measureBringUpDownLatency(false);
      auto upMs = measureBringUpDownLatency(true);
      XLOG(INFO) << "Debounce(" << holdoffMs << "ms) down=" << downMs.count()
                 << "ms up=" << upMs.count() << "ms";

      auto downHoldoff = (downMs - baselineDownMs).count();
      auto upHoldoff = (upMs - baselineUpMs).count();
      auto loBound = static_cast<int64_t>(holdoffMs * (1 - kTolerance));
      auto hiBound = static_cast<int64_t>(holdoffMs * (1 + kTolerance));

      EXPECT_GE(downHoldoff, loBound)
          << "down holdoff " << downHoldoff << "ms below " << loBound
          << "ms (configured " << holdoffMs << "ms)";
      EXPECT_LE(downHoldoff, hiBound)
          << "down holdoff " << downHoldoff << "ms above " << hiBound
          << "ms (configured " << holdoffMs << "ms)";
      EXPECT_GE(upHoldoff, loBound)
          << "up holdoff " << upHoldoff << "ms below " << loBound
          << "ms (configured " << holdoffMs << "ms)";
      EXPECT_LE(upHoldoff, hiBound)
          << "up holdoff " << upHoldoff << "ms above " << hiBound
          << "ms (configured " << holdoffMs << "ms)";
    };

    verifyHoldoff(kHoldoffLongMs);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentHwLinkDebounceTest, FlapWithinDebounceWindow) {
  auto port = portForTest();

  auto verifyNoEventInWindow = [&](bool portStartsAsUp, int32_t holdoffMs) {
    auto preFlap = getLinkStateFlapCount(port);
    bool needsTransition =
        getProgrammedState()->getPorts()->getNodeIf(port)->isUp() !=
        portStartsAsUp;

    if (portStartsAsUp) {
      bringUpPort(port);
    } else {
      bringDownPort(port);
    }
    EXPECT_EQ(
        getProgrammedState()->getPorts()->getNodeIf(port)->isUp(),
        portStartsAsUp);

    // Wait for fb303 publish to catch up to the expected flap count from the
    // initial transition. bringUp/DownPort produces one flap event per call.
    auto baselineFlaps = preFlap + (needsTransition ? 1 : 0);
    WITH_RETRIES(
        { EXPECT_EVENTUALLY_EQ(getLinkStateFlapCount(port), baselineFlaps); });

    auto noFlapsSinceBaseline = [&]() -> bool {
      return getLinkStateFlapCount(port) == baselineFlaps;
    };

    togglePortNoWait(port, !portStartsAsUp);
    CHECK_HOLDS_FOR_DURATION(kFlapWithinWindow, noFlapsSinceBaseline);
    togglePortNoWait(port, portStartsAsUp);

    auto verifyWindow =
        std::chrono::milliseconds(holdoffMs) - kFlapWithinWindow;
    CHECK_HOLDS_FOR_DURATION(verifyWindow, noFlapsSinceBaseline);
    EXPECT_EQ(
        getProgrammedState()->getPorts()->getNodeIf(port)->isUp(),
        portStartsAsUp)
        << "Port should still be in initial state after suppressed flap";
  };

  applyDebounceConfig(0, kHoldoffLongMs);
  verifyNoEventInWindow(true /* portStartsAsUp */, kHoldoffLongMs);

  applyDebounceConfig(kHoldoffLongMs, 0);
  verifyNoEventInWindow(false /* portStartsAsUp */, kHoldoffLongMs);
}

TEST_F(AgentHwLinkDebounceTest, PacketDropDuringDownHoldoff) {
  auto port = portForTest();
  auto setup = [&]() { bringUpPort(port); };
  auto verify = [&]() {
    applyDebounceConfig(std::nullopt, kHoldoffLongMs);

    ASSERT_TRUE(getProgrammedState()->getPorts()->getNodeIf(port)->isUp());
    auto baselineFlaps = getLinkStateFlapCount(port);
    auto statsBefore = getLatestPortStats(port);

    auto holdoffStart = std::chrono::steady_clock::now();
    togglePortNoWait(port, false /* toUp */);

    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
    auto pkt = utility::makeUDPTxPacket(
        getSw(),
        std::nullopt /* vlan */,
        srcMac,
        intfMac,
        folly::IPAddress("1001::1"),
        folly::IPAddress("2001::1"),
        4242,
        4242);
    getSw()->sendPacketOutOfPortAsync(std::move(pkt), port);

    auto elapsed = std::chrono::steady_clock::now() - holdoffStart;
    auto verifyWindow = std::chrono::milliseconds(kHoldoffLongMs) - elapsed;
    CHECK_HOLDS_FOR_DURATION(verifyWindow, [&]() -> bool {
      return getProgrammedState()->getPorts()->getNodeIf(port)->isUp() &&
          getLinkStateFlapCount(port) == baselineFlaps;
    });

    WITH_RETRIES({
      EXPECT_EVENTUALLY_FALSE(
          getProgrammedState()->getPorts()->getNodeIf(port)->isUp());
    });

    auto inBefore = *statsBefore.inDiscards_();
    WITH_RETRIES({
      auto statsAfter = getLatestPortStats(port);
      auto inAfter = *statsAfter.inDiscards_();
      XLOG(INFO) << "Port inDiscards before/after: " << inBefore << "/"
                 << inAfter;
      EXPECT_EVENTUALLY_EQ(inAfter, inBefore + 1);
    });

    togglePortNoWait(port, true /* toUp */);
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentHwLinkDebounceTest, LinkDownDebounceRetriggerCount) {
  auto port = portForTest();
  auto setup = [&]() { bringUpPort(port); };
  auto verify = [&]() { verifyRetriggerCount(false /* upDebounce */); };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentHwLinkDebounceTest, LinkUpDebounceRetriggerCount) {
  auto port = portForTest();
  auto setup = [&]() { bringUpPort(port); };
  auto verify = [&]() { verifyRetriggerCount(true /* upDebounce */); };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
