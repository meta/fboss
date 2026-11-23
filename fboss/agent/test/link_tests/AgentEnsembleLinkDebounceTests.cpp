// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <folly/logging/xlog.h>

#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/link_tests/AgentEnsembleLinkTest.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/lib/CommonUtils.h"

using namespace ::testing;
using namespace facebook::fboss;

namespace {
constexpr int32_t kHoldoffMs = 500;
constexpr double kTolerance = 0.10;
constexpr int kNumDownSamples = 5;

constexpr int32_t kUpHoldoffMs = 500;
// Bounds on the measured up hold, as fractions of the configured hold.
constexpr double kUpLoBoundFraction = 0.5;
constexpr double kUpHiBoundFraction = 1.5;

constexpr int kNumUpSamples = 5;
// qsfp_service resets every transceiver to DISCOVERED once it polls a newer
// agent config, ~2s after the apply; after a cold boot it also re-inits the
// data path. A second reset landing mid re-init wedges the data path, so each
// config apply waits for the previous one to settle and then for its own.
constexpr auto kQsfpConfigPickup = std::chrono::seconds(10);

int64_t median(std::vector<int64_t> samples) {
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}
} // namespace

class AgentEnsembleLinkDebounceTest : public AgentEnsembleLinkTest {
  std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const override {
    return {
        link_test_production_features::LinkTestProductionFeature::L1_LINK_TEST,
        link_test_production_features::LinkTestProductionFeature::
            ACTIVE_TRANSCEIVER};
  }

 protected:
  // Pick a cabled interface-port pair. The first port is the one under test;
  // the second is its neighbor, whose transmitter is squelched to drive the
  // link of the first. Returns nullopt if the setup has no such pair.
  std::optional<std::pair<PortID, PortID>> getCabledInterfacePortPair() const {
    const auto state = getProgrammedState();
    for (const auto& pair : getConnectedPairs()) {
      if (state->getPorts()->getNodeIf(pair.first)->getPortType() ==
              cfg::PortType::INTERFACE_PORT &&
          state->getPorts()->getNodeIf(pair.second)->getPortType() ==
              cfg::PortType::INTERFACE_PORT) {
        return std::make_pair(pair.first, pair.second);
      }
    }
    return std::nullopt;
  }

  // Apply a config that overrides the link-down debounce hold timer for
  // `port`; std::nullopt leaves the SDK default (no debounce) in place.
  //
  // On TH5/TH6 hardware linkscan is set on the same port in the same config on
  // purpose; other SAIs take no linkscan mode and fail the apply. The BRCM SDK
  // rejects SAI_PORT_ATTR_LINK_DOWN_DEBOUNCE_TIMEOUT outright
  // unless the port is already in SAI_PORT_LINKSCAN_MODE_HW, and LinkScanMode
  // sits ahead of LinkDownDebouncePeriodMs in SaiPortTraits::CreateAttributes,
  // which is the order both the create list and checkAndSetAttribute follow.
  void applyDownHoldoff(PortID port, std::optional<int32_t> downMs) {
    auto config = getSw()->getConfig();
    auto portCfg = utility::findCfgPort(config, port);
    const auto switchId = getSw()->getScopeResolver()->scope(port).switchId();
    const auto* asic = getSw()->getHwAsicTable()->getHwAsic(switchId);
    if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK5 ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK6) {
      portCfg->linkScanMode() = cfg::LinkScanMode::HARDWARE;
    }
    if (downMs.has_value()) {
      portCfg->portDownHoldoffTimeMs() = *downMs;
    } else {
      portCfg->portDownHoldoffTimeMs().reset();
    }
    waitForTransceiversSettled();
    applyNewConfig(config);
    waitForTransceiversSettled();
  }

  // Where the ASIC's up hold is a single switch wide timer, the agent rejects
  // a config whose interface ports disagree on it, so every interface port on
  // `port`'s switch gets the same hold.
  void applyUpHoldoff(PortID port, std::optional<int32_t> upMs) {
    auto config = getSw()->getConfig();
    const auto* resolver = getSw()->getScopeResolver();
    const auto switchId = resolver->scope(port).switchId();
    const auto* asic = getSw()->getHwAsicTable()->getHwAsic(switchId);
    if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK5 ||
        asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK6) {
      utility::findCfgPort(config, port)->linkScanMode() =
          cfg::LinkScanMode::HARDWARE;
    }
    std::vector<PortID> holdPorts{port};
    if (asic->isSupported(HwAsic::Feature::SWITCH_WIDE_LINK_UP_DEBOUNCE)) {
      holdPorts.clear();
      for (const auto& portMap :
           std::as_const(*getProgrammedState()->getPorts())) {
        for (const auto& [_, swPort] : std::as_const(*portMap.second)) {
          if (swPort->getPortType() == cfg::PortType::INTERFACE_PORT &&
              resolver->scope(swPort).has(switchId)) {
            holdPorts.push_back(swPort->getID());
          }
        }
      }
    }
    for (const auto& holdPort : holdPorts) {
      auto portCfg = utility::findCfgPort(config, holdPort);
      if (upMs.has_value()) {
        portCfg->portUpHoldoffTimeMs() = *upMs;
      } else {
        portCfg->portUpHoldoffTimeMs().reset();
      }
    }
    waitForTransceiversSettled();
    applyNewConfig(config);
    waitForTransceiversSettled();
  }

  void waitForTransceiversSettled() const {
    // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep
    std::this_thread::sleep_for(kQsfpConfigPickup);
    utility::waitForAllTransceiverStates(true, getCabledTranceivers());
  }

  // Bring `port`'s link down/up by squelching its transceiver optical TX via
  // wedge_qsfp_util, rather than admin-disabling the port. A genuine optical
  // loss-of-signal makes the link partner observe a real link down, which
  // exercises the SDK link-down debounce hold timer; an admin disable instead
  // reprograms the observed port and puts tens of ms of SAI writes inside the
  // measurement window. squelch=true disables TX (link down), squelch=false
  // re-enables TX (link up).
  void setPortTxSquelched(PortID port, bool squelch) const {
    const auto cmd = std::string("wedge_qsfp_util ") + getPortName(port) +
        (squelch ? " --tx_disable" : " --tx_enable");
    XLOG(INFO) << "setPortTxSquelched: " << (squelch ? "disable" : "enable")
               << " TX on port=" << static_cast<int>(port) << " via: " << cmd;
    // runShellCommand quotes its argument as a single shell word.
    runCommand({"/bin/sh", "-c", cmd});
  }
};

// Verify the SDK link-down debounce (hold) timer using a real physical link
// flap. Configure a down-holdoff on portA -- which FBOSS programs as the
// per-port SAI_PORT_ATTR_LINK_DOWN_DEBOUNCE_TIMEOUT extension, wrapped as
// SaiPortTraits::Attributes::LinkDownDebouncePeriodMs -- then squelch the
// transmitter of its cabled neighbor portB so portA observes a genuine loss of
// signal. portA should report oper-down only after roughly the configured
// holdoff.
//
// The up direction is covered separately by LinkUpDebounceTimerWithinTolerance,
// which goes through a different SAI object: Broadcom has no per-port link-up
// debounce, only a switch-global one.
TEST_F(AgentEnsembleLinkDebounceTest, DebounceTimerWithinTolerance) {
  auto l3Asics = getSw()->getHwAsicTable()->getL3Asics();
  ASSERT_FALSE(l3Asics.empty());
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::
           TRANSCEIVER_TX_DISABLE});

  // Pick a cabled interface-port pair. portA is observed; its neighbor portB is
  // toggled to drive the link down/up.
  const auto portPair = getCabledInterfacePortPair();
  ASSERT_TRUE(portPair.has_value())
      << "No cabled interface-port pair available for debounce test";
  const auto portA = portPair->first;
  const auto portB = portPair->second;
  addTestedPorts({portA, portB});

  // Disable neighbor portB and time how long portA takes to report oper-down.
  // Poll portA's oper state tightly; waitForLinkStatus's 1s granularity is too
  // coarse for a 500ms +/- 10% holdoff. Leaves portB (and thus portA) down.
  auto measureDownLatency = [&]() {
    XLOG(INFO) << "measureDownLatency: portA=" << static_cast<int>(portA)
               << " portB=" << static_cast<int>(portB);
    EXPECT_NO_THROW(waitForLinkStatus(
        {portA, portB}, true, 60, std::chrono::milliseconds(1000)));
    const auto start = std::chrono::steady_clock::now();
    setPortTxSquelched(portB, true);
    constexpr auto kMaxWait = std::chrono::seconds(5);
    while (getProgrammedState()->getPorts()->getNodeIf(portA)->isUp()) {
      if (std::chrono::steady_clock::now() - start > kMaxWait) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
  };

  // Unsquelch neighbor portB and wait for portA to come back up, so the next
  // measurement starts from a known oper-up state. Not itself a measurement:
  // optical bring-up takes 1.5-3s and swings by hundreds of ms.
  auto restoreLink = [&]() {
    setPortTxSquelched(portB, false);
    EXPECT_NO_THROW(waitForLinkStatus(
        {portA, portB}, true, 60, std::chrono::milliseconds(1000)));
  };

  auto medianDownLatencyMs = [&](const std::string& label) {
    std::vector<int64_t> samples;
    for (int i = 0; i < kNumDownSamples; ++i) {
      samples.push_back(measureDownLatency().count());
      restoreLink();
      XLOG(INFO) << label << " down latency sample " << i << ": "
                 << samples.back() << "ms";
    }
    const auto result = median(samples);
    XLOG(INFO) << label << " down latency median: " << result << "ms";
    return result;
  };

  auto verify = [&]() {
    // Baseline (no debounce) isolates the fixed propagation/notification
    // latency so it can be subtracted from the debounced measurement.
    XLOG(INFO) << "Measuring baseline down latency";
    applyDownHoldoff(portA, std::nullopt);
    const auto baselineDownMs = medianDownLatencyMs("baseline");

    XLOG(INFO) << "Measuring debounced down latency";
    applyDownHoldoff(portA, kHoldoffMs);
    // Programming the debounce re-asserts HW linkscan on portA, which can
    // bounce a live link. Such a flap is invisible for up to the holdoff --
    // hiding it is precisely what the debounce does -- so the measurement
    // would otherwise start against a port that is already physically down.
    // Wait the hold out before timing anything.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(2 * kHoldoffMs + 1000));
    const auto downMs = medianDownLatencyMs("debounced");

    XLOG(INFO) << "Link down latency baseline=" << baselineDownMs
               << "ms debounce(" << kHoldoffMs << "ms)=" << downMs << "ms";

    const auto loBound = static_cast<int64_t>(kHoldoffMs * (1 - kTolerance));
    const auto hiBound = static_cast<int64_t>(kHoldoffMs * (1 + kTolerance));
    const auto downHoldoff = downMs - baselineDownMs;
    EXPECT_GE(downHoldoff, loBound)
        << "down holdoff " << downHoldoff << "ms below " << loBound
        << "ms (configured " << kHoldoffMs << "ms)";
    EXPECT_LE(downHoldoff, hiBound)
        << "down holdoff " << downHoldoff << "ms above " << hiBound
        << "ms (configured " << kHoldoffMs << "ms)";

    // Leave the port undebounced for whatever runs next.
    applyDownHoldoff(portA, std::nullopt);
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

// Verify the link up hold by timing portA's oper-up after unsquelching portB.
TEST_F(AgentEnsembleLinkDebounceTest, LinkUpDebounceTimerWithinTolerance) {
  auto l3Asics = getSw()->getHwAsicTable()->getL3Asics();
  ASSERT_FALSE(l3Asics.empty());
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::
           TRANSCEIVER_TX_DISABLE});

  // portA is observed; its cabled neighbor portB is toggled to drive the link.
  const auto portPair = getCabledInterfacePortPair();
  ASSERT_TRUE(portPair.has_value())
      << "No cabled interface-port pair available for debounce test";
  const auto portA = portPair->first;
  const auto portB = portPair->second;
  addTestedPorts({portA, portB});

  // Squelch neighbor portB and wait for portA to settle oper-down, so every
  // measurement starts from the same state. Only the up timer is configured
  // here, so this direction is never debounced and stays quick.
  auto settleLinkDown = [&]() {
    setPortTxSquelched(portB, true);
    EXPECT_NO_THROW(waitForLinkStatus(
        {portA, portB}, false, 60, std::chrono::milliseconds(1000)));
  };

  // Where the up hold is per port only portA carries it, so portB reports
  // oper-up when the link physically returns and portA's lag behind it within
  // the same flap is the hold, free of the seconds of optical bring-up jitter.
  // A switch wide hold delays both ends, so there only portA's own latency,
  // debounced against baseline, can show it.
  const bool switchWideUp =
      l3Asics[0]->isSupported(HwAsic::Feature::SWITCH_WIDE_LINK_UP_DEBOUNCE);

  struct UpLatency {
    int64_t portAMs;
    int64_t portBMs;
  };

  // Unsquelch neighbor portB and time how long each end takes to report
  // oper-up. Poll tightly; waitForLinkStatus's 1s granularity is coarser than
  // the holdoff being measured. Leaves both ports up.
  auto measureUpLatency = [&]() {
    const auto start = std::chrono::steady_clock::now();
    setPortTxSquelched(portB, false);
    // Generous: optical bring-up alone runs into seconds before any holdoff.
    constexpr auto kMaxWait = std::chrono::seconds(30);
    std::optional<int64_t> portAMs;
    std::optional<int64_t> portBMs;
    while (!portAMs.has_value() || !portBMs.has_value()) {
      const auto elapsedMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      const auto ports = getProgrammedState()->getPorts();
      if (!portAMs.has_value() && ports->getNodeIf(portA)->isUp()) {
        portAMs = elapsedMs;
      }
      if (!portBMs.has_value() && ports->getNodeIf(portB)->isUp()) {
        portBMs = elapsedMs;
      }
      if (std::chrono::steady_clock::now() - start > kMaxWait) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto maxWaitMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(kMaxWait).count();
    return UpLatency{portAMs.value_or(maxWaitMs), portBMs.value_or(maxWaitMs)};
  };

  // Medians of portA's latency and of portA's lag behind portB.
  auto medianUpLatencyMs = [&](const std::string& label) {
    std::vector<int64_t> portASamples;
    std::vector<int64_t> lagSamples;
    for (int i = 0; i < kNumUpSamples; ++i) {
      settleLinkDown();
      const auto sample = measureUpLatency();
      portASamples.push_back(sample.portAMs);
      lagSamples.push_back(sample.portAMs - sample.portBMs);
      XLOG(INFO) << label << " up latency sample " << i << ": portA "
                 << sample.portAMs << "ms portB " << sample.portBMs
                 << "ms portA-portB " << lagSamples.back() << "ms";
    }
    const auto result =
        std::make_pair(median(portASamples), median(lagSamples));
    XLOG(INFO) << label << " up latency median: portA " << result.first
               << "ms portA-portB " << result.second << "ms";
    return result;
  };

  auto verify = [&]() {
    // Baseline (no debounce) isolates the optical bring-up latency, and any
    // standing skew between the two ends, so it can be subtracted from the
    // debounced measurement.
    XLOG(INFO) << "Measuring baseline up latency";
    applyUpHoldoff(portA, std::nullopt);
    const auto [baselineUpMs, baselineLagMs] = medianUpLatencyMs("baseline");

    XLOG(INFO) << "Measuring debounced up latency";
    applyUpHoldoff(portA, kUpHoldoffMs);
    const auto [upMs, lagMs] = medianUpLatencyMs("debounced");

    XLOG(INFO) << "Link up latency baseline=" << baselineUpMs << "ms debounce("
               << kUpHoldoffMs << "ms)=" << upMs
               << "ms; portA-portB baseline=" << baselineLagMs
               << "ms debounce=" << lagMs << "ms";

    const auto upHoldoff =
        switchWideUp ? upMs - baselineUpMs : lagMs - baselineLagMs;
    XLOG(INFO) << "Measured up holdoff " << upHoldoff << "ms ("
               << (switchWideUp ? "portA latency" : "portA-portB lag")
               << ", configured " << kUpHoldoffMs << "ms)";
    const auto loBound =
        static_cast<int64_t>(kUpHoldoffMs * kUpLoBoundFraction);
    const auto hiBound =
        static_cast<int64_t>(kUpHoldoffMs * kUpHiBoundFraction);
    EXPECT_GE(upHoldoff, loBound)
        << "up holdoff " << upHoldoff << "ms below " << loBound
        << "ms (configured " << kUpHoldoffMs << "ms)";
    EXPECT_LE(upHoldoff, hiBound)
        << "up holdoff " << upHoldoff << "ms above " << hiBound
        << "ms (configured " << kUpHoldoffMs << "ms)";

    // Leave the switch undebounced for whatever runs next.
    applyUpHoldoff(portA, std::nullopt);
  };

  verifyAcrossWarmBoots([]() {}, verify);
}
