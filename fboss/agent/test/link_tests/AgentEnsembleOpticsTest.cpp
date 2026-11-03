// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fmt/format.h>
#include <folly/Conv.h>
#include <algorithm>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/test/link_tests/AgentEnsembleLinkTest.h"
#include "fboss/agent/test/link_tests/LinkTestUtils.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"

using namespace ::testing;
using namespace facebook::fboss;

namespace {

struct OpticsThresholdRange {
  double minThreshold;
  double maxThreshold;
};

struct OpticsSidePerformanceMonitoringThresholds {
  OpticsThresholdRange pam4eSnr;
  OpticsThresholdRange pam4Ltp;
  OpticsThresholdRange preFecBer;
  OpticsThresholdRange fecTailMax;
  // 2x400G-FR4 optics operating in 100G-CWDM4 mode run NRZ on their lanes and
  // don't report a meaningful PAM4 SNR, so a relaxed SNR minimum is used for
  // them instead of pam4eSnr.
  OpticsThresholdRange cwdm4Snr;
};

struct OpticsPerformanceMonitoringThresholds {
  OpticsSidePerformanceMonitoringThresholds mediaThresholds;
  OpticsSidePerformanceMonitoringThresholds hostThresholds;
};

// CMIS optics thresholds
struct OpticsPerformanceMonitoringThresholds kCmisOpticsThresholds = {
    .mediaThresholds =
        {
            .pam4eSnr = {FLAGS_link_stress_test ? 20.0 : 19.0, 49.0},
            .pam4Ltp = {33.0, 99.0},
            .preFecBer = {0, FLAGS_link_stress_test ? 5.0e-7 : 2.4e-5},
            .fecTailMax = {0, FLAGS_link_stress_test ? 11.0 : 14.0},
            .cwdm4Snr = {0.0, 49.0},
        },
    .hostThresholds =
        {
            .pam4eSnr = {FLAGS_link_stress_test ? 20.0 : 19.0, 49.0},
            .pam4Ltp = {33.0, 99.0},
            .preFecBer = {0, FLAGS_link_stress_test ? 5.0e-7 : 2.4e-5},
            .fecTailMax = {0, FLAGS_link_stress_test ? 11.0 : 14.0},
            .cwdm4Snr = {0.0, 49.0},
        },
};

void validateVdm(
    const std::map<int, TransceiverInfo>& transceiverInfos,
    const std::vector<int>& tcvrsToTest) {
  auto validatePerfMon =
      [](const std::string& portName,
         phy::Side side,
         const VdmPerfMonitorPortSideStats& vdmPerfMon,
         OpticsSidePerformanceMonitoringThresholds thresholds,
         MediaInterfaceCode moduleMediaInterface,
         MediaInterfaceCode portMediaInterface) {
        auto& preFecBer = vdmPerfMon.datapathBER().value();
        // Fec tail is not implemented on all modules that support VDM. FEC tail
        // is available starting CMIS 5.0 (2x400G-[D|F]R4)
        auto fecTailMax = vdmPerfMon.fecTailMax().value_or({});
        auto& laneSnr = vdmPerfMon.laneSNR().value();

        // 2x400G-FR4 (and FR4-Lite) modules configured in 100G-CWDM4 mode run
        // NRZ and don't report a meaningful PAM4 SNR, so validate against the
        // relaxed CWDM4 SNR minimum instead of the PAM4 one.
        auto snrMinThreshold =
            ((moduleMediaInterface == MediaInterfaceCode::FR4_2x400G ||
              moduleMediaInterface == MediaInterfaceCode::FR4_LITE_2x400G) &&
             portMediaInterface == MediaInterfaceCode::CWDM4_100G)
            ? thresholds.cwdm4Snr.minThreshold
            : thresholds.pam4eSnr.minThreshold;

        XLOG(DBG2) << "Validating VDM performance monitoring for " << portName
                   << ", side: " << apache::thrift::util::enumNameSafe(side)
                   << ", moduleMediaInterface: "
                   << apache::thrift::util::enumNameSafe(moduleMediaInterface)
                   << ", portMediaInterface: "
                   << apache::thrift::util::enumNameSafe(portMediaInterface)
                   << ", snrMinThreshold: " << snrMinThreshold;
        EXPECT_LE(preFecBer.max().value(), thresholds.preFecBer.maxThreshold)
            << fmt::format(
                   "PreFecBer Max for {} is {}",
                   portName,
                   folly::copy(preFecBer.max().value()));
        EXPECT_LE(fecTailMax, thresholds.fecTailMax.maxThreshold)
            << fmt::format("FecTail Max for {} is {}", portName, fecTailMax);
        for (auto& [lane, snr] : laneSnr) {
          EXPECT_GE(snr, snrMinThreshold) << fmt::format(
              "SNR for lane {} on {} is {}", lane, portName, snr);
        }
      };

  for (const auto& tcvrId : tcvrsToTest) {
    auto txInfoItr = transceiverInfos.find(tcvrId);
    ASSERT_TRUE(txInfoItr != transceiverInfos.end());
    auto& tcvrState = *txInfoItr->second.tcvrState();
    auto vdmPerfMonitorStats =
        txInfoItr->second.tcvrStats()->vdmPerfMonitorStats();
    ASSERT_TRUE(vdmPerfMonitorStats.has_value());
    auto& mediaStats = vdmPerfMonitorStats->mediaPortVdmStats().value();
    auto& hostStats = vdmPerfMonitorStats->hostPortVdmStats().value();

    auto moduleMediaInterface =
        tcvrState.moduleMediaInterface().value_or(MediaInterfaceCode::UNKNOWN);

    // Map each port to its current per-lane media interface code so we can tell
    // when a flexible module (e.g. 2x400G-FR4) is operating in a different mode
    // (e.g. 100G-CWDM4).
    std::map<int, MediaInterfaceCode> laneToMediaInterface;
    if (auto settings = tcvrState.settings()) {
      for (const auto& mediaIntf : settings->mediaInterface().value_or({})) {
        laneToMediaInterface[*mediaIntf.lane()] = *mediaIntf.code();
      }
    }
    std::map<std::string, MediaInterfaceCode> portToMediaInterface;
    for (const auto& [portName, lanes] :
         tcvrState.portNameToMediaLanes().value()) {
      if (!lanes.empty()) {
        auto laneItr = laneToMediaInterface.find(lanes.front());
        if (laneItr != laneToMediaInterface.end()) {
          portToMediaInterface[portName] = laneItr->second;
        }
      }
    }

    auto validateSideStats =
        [&, validatePerfMon](
            const std::map<std::string, VdmPerfMonitorPortSideStats>& sideStat,
            phy::Side side,
            OpticsSidePerformanceMonitoringThresholds threshold) {
          for (auto& [portName, vdmPerfMon] : sideStat) {
            auto portMediaInterface = MediaInterfaceCode::UNKNOWN;
            auto portIntfItr = portToMediaInterface.find(portName);
            if (portIntfItr != portToMediaInterface.end()) {
              portMediaInterface = portIntfItr->second;
            }
            validatePerfMon(
                portName,
                side,
                vdmPerfMon,
                threshold,
                moduleMediaInterface,
                portMediaInterface);
          }
        };
    validateSideStats(
        mediaStats, phy::Side::LINE, kCmisOpticsThresholds.mediaThresholds);
    validateSideStats(
        hostStats, phy::Side::SYSTEM, kCmisOpticsThresholds.hostThresholds);
  }
}

} // namespace

class AgentEnsembleOpticsTest : public AgentEnsembleLinkTest {
 private:
  std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const override {
    return {
        link_test_production_features::LinkTestProductionFeature::L1_LINK_TEST,
        link_test_production_features::LinkTestProductionFeature::
            ACTIVE_TRANSCEIVER};
  }

 public:
  std::set<std::pair<PortID, PortID>> getConnectedOpticalPortPairs() const {
    // TransceiverFeature::NONE will get us all optical pairs.
    return getConnectedOpticalAndActivePortPairWithFeature(
        TransceiverFeature::NONE,
        phy::Side::LINE /* side doesn't matter when feature is None */,
        true /* skipLoopback */);
  }
};

TEST_F(AgentEnsembleOpticsTest, verifyTxRxLatches) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::
           TRANSCEIVER_TX_RX_LATCHES});
  /*
   * 1. Filter out ports with optics
   * 2. Set ASIC port status to false on A side
   * 3. Expect TX_LOS, TX_LOL on A side, RX_LOL on Z side
   * 4. Set ASIC port status to true on A side
   * 5. Expect TX_LOS, TX_LOL, RX_LOL to be cleared on A and Z sides
   * 6. Repeat steps 2-5 by flipping A and Z sides
   * Note: LPO Transceivers don't have a DSP and there is no detection
   *       for LOL, so only bypass that.
   */
  auto opticalPortPairs = getConnectedOpticalPortPairs();
  for (const auto& [p1, p2] : opticalPortPairs) {
    addTestedPort(p1);
    addTestedPort(p2);
  }
  EXPECT_FALSE(opticalPortPairs.empty())
      << "Did not detect any optical transceivers";

  std::set<int32_t> allTcvrIds;
  // Gather list of all transceiverIDs so that we get their corresponding
  // transceiverInfo in one shot
  for (auto [portID1, portID2] : opticalPortPairs) {
    allTcvrIds.insert(int32_t(
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(portID1)));
    allTcvrIds.insert(int32_t(
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(portID2)));
  }

  auto allTcvrInfos = utility::waitForTransceiverInfo(
      std::vector<int32_t>(allTcvrIds.begin(), allTcvrIds.end()),
      /*includeLpo*/ true);

  // Cache the host and media lanes for each port because once the ports are
  // disabled, transceiver state machine moves to discovered state and we would
  // lose this information in transceiverInfo
  std::map<std::string, std::vector<int>> cachedHostLanes;
  std::map<std::string, std::vector<int>> cachedMediaLanes;
  for (auto& tcvrInfo : allTcvrInfos) {
    auto& tcvrState = *tcvrInfo.second.tcvrState();
    cachedHostLanes.insert(
        tcvrState.portNameToHostLanes().value().begin(),
        tcvrState.portNameToHostLanes().value().end());
    cachedMediaLanes.insert(
        tcvrState.portNameToMediaLanes().value().begin(),
        tcvrState.portNameToMediaLanes().value().end());
  }

  // Pause remediation because we don't want transceivers to remediate while
  // they are down and then interfere with latches
  auto qsfpServiceClient = utils::createQsfpServiceClient();
  qsfpServiceClient->sync_pauseRemediation(24 * 60 * 60 /* 24hrs */, {});

  auto verifyLatches = [this, cachedHostLanes, cachedMediaLanes](
                           std::unordered_map<int32_t, std::string>&
                               transceiverIds,
                           bool txLatch,
                           bool rxLatch) {
    std::vector<int32_t> onlyTcvrIds;
    for (const auto& tcvrId : transceiverIds) {
      onlyTcvrIds.push_back(int32_t(tcvrId.first));
    }
    WITH_RETRIES_N_TIMED(10, std::chrono::seconds(10), {
      auto transceiverInfos =
          utility::waitForTransceiverInfo(onlyTcvrIds, /*includeLpo*/ true);
      for (const auto& tcvrId : onlyTcvrIds) {
        auto& portName = transceiverIds[TransceiverID(tcvrId)];
        auto tcvrInfoInfoItr = transceiverInfos.find(tcvrId);
        ASSERT_EVENTUALLY_TRUE(tcvrInfoInfoItr != transceiverInfos.end());

        auto& tcvrState = *tcvrInfoInfoItr->second.tcvrState();
        auto mediaInterface = tcvrState.moduleMediaInterface().value_or({});
        ASSERT_EVENTUALLY_TRUE(
            cachedHostLanes.find(portName) != cachedHostLanes.end())
            << fmt::format("Port {} not found in cachedHostLanes", portName);
        ASSERT_EVENTUALLY_TRUE(
            cachedMediaLanes.find(portName) != cachedMediaLanes.end())
            << fmt::format("Port {} not found in cachedMediaLanes", portName);
        auto& hostLanes = cachedHostLanes.at(portName);
        auto& mediaLanes = cachedMediaLanes.at(portName);
        auto& hostLaneSignals = *tcvrState.hostLaneSignals();
        auto& mediaLaneSignals = *tcvrState.mediaLaneSignals();

        ASSERT_EVENTUALLY_GT(hostLanes.size(), 0);
        ASSERT_EVENTUALLY_GT(mediaLanes.size(), 0);
        ASSERT_EVENTUALLY_GT(hostLaneSignals.size(), 0);
        ASSERT_EVENTUALLY_GT(mediaLaneSignals.size(), 0);

        // LPO does not support TxLol
        bool isLpo = *tcvrState.moduleTechnology() == ModuleTechnology::LPO;

        for (const auto& signal : hostLaneSignals) {
          if (std::find(
                  hostLanes.begin(), hostLanes.end(), signal.lane().value()) !=
              hostLanes.end()) {
            if (!isLpo) {
              ASSERT_EVENTUALLY_TRUE(signal.txLol().has_value());
            }
            ASSERT_EVENTUALLY_TRUE(signal.txLos().has_value());
            // TX_LOL is not reliable right now on certain 100G
            // CWDM4 optics like AOI and Miniphoton. So skip checking it
            // on these optics for now
            if (mediaInterface != MediaInterfaceCode::CWDM4_100G &&
                isLpo == false) {
              EXPECT_EVENTUALLY_EQ(signal.txLol().value(), txLatch)
                  << portName << ", lane: " << signal.lane().value();
            }
            // We see TX_LOS set only on the first lane of FR1_100G. This is
            // a bug but we can't get the vendor to fix it now. Therefore,
            // handle it separately in the test
            if (mediaInterface != MediaInterfaceCode::FR1_100G ||
                signal.lane().value() == 0) {
              EXPECT_EVENTUALLY_EQ(signal.txLos().value(), txLatch)
                  << portName << ", lane: " << signal.lane().value();
            }
          }
        }

        for (const auto& signal : mediaLaneSignals) {
          if (std::find(
                  mediaLanes.begin(),
                  mediaLanes.end(),
                  signal.lane().value()) != mediaLanes.end()) {
            if (isLpo) {
              // LPO Supports RX LOS only.
              ASSERT_EVENTUALLY_TRUE(signal.rxLos().has_value());
              EXPECT_EVENTUALLY_EQ(signal.rxLos().value(), rxLatch)
                  << portName << ", lane: " << signal.lane().value();
            } else {
              // Unfortunately, can't rely on rxLos as it doesn't always get
              // set. Some optics don't squelch their line side when the
              // system side is down.
              ASSERT_EVENTUALLY_TRUE(signal.rxLol().has_value());

              // RX_LOL is not reliable right now on certain 100G
              // CWDM4 optics like AOI and Miniphoton. So skip checking it
              // on these optics for now
              // RX_LOL is not raised for 800G ZR optics as squelching is
              // disable by default
              if (mediaInterface != MediaInterfaceCode::CWDM4_100G &&
                  mediaInterface != MediaInterfaceCode::ZR_800G) {
                EXPECT_EVENTUALLY_EQ(signal.rxLol().value(), rxLatch)
                    << portName << ", lane: " << signal.lane().value();
              }
            }
          }
        }
      }
    });
  };

  auto verify = [this, opticalPortPairs, verifyLatches](bool disableAPort) {
    std::unordered_map<int32_t, std::string> portsWithTxDown;
    std::unordered_map<int32_t, std::string> portsWithRxDown;
    for (const auto& port : opticalPortPairs) {
      auto tcvrIdA =
          getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port.first);
      auto portNameA = getPortName(port.first);
      auto tcvrIdZ = getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(
          port.second);
      auto portNameZ = getPortName(port.second);
      if (disableAPort) {
        XLOG(INFO) << "Disabling port " << portNameA << ", tcvrId " << tcvrIdA;
        setPortStatus(port.first, false);
        portsWithTxDown.insert({tcvrIdA, portNameA});
        portsWithRxDown.insert({tcvrIdZ, portNameZ});
      } else {
        XLOG(INFO) << "Disabling port " << portNameZ << ", tcvrId " << tcvrIdZ;
        setPortStatus(port.second, false);
        portsWithTxDown.insert({tcvrIdZ, portNameZ});
        portsWithRxDown.insert({tcvrIdA, portNameA});
      }
    }
    EXPECT_NO_THROW(verifyLatches(
        portsWithTxDown, true /* txLatch */, false /* rxLatch */));
    EXPECT_NO_THROW(verifyLatches(
        portsWithRxDown, false /* txLatch */, true /* rxLatch */));

    // Set the port status on all cabled ports to true. The link should come
    // back up
    for (const auto& port : opticalPortPairs) {
      setPortStatus(port.first, true);
      setPortStatus(port.second, true);
    }
    EXPECT_NO_THROW(waitForAllCabledPorts(true));
    EXPECT_NO_THROW(verifyLatches(
        portsWithTxDown, false /* txLatch */, false /* rxLatch */));
    EXPECT_NO_THROW(verifyLatches(
        portsWithRxDown, false /* txLatch */, false /* rxLatch */));
  };

  XLOG(INFO) << "Testing TX_LOS, TX_LOL on A side, RX_LOS, RX_LOL on Z side";
  verify(true /* disableAPort */);

  XLOG(INFO) << "Testing TX_LOS, TX_LOL on Z side, RX_LOS, RX_LOL on A side";
  verify(false /* disableAPort */);
}

/*
 * verifyNoModeMismatchOrNegativeThermalMargin
 *
 * On a healthy, correctly configured link, a module built to the Meta FW spec
 * should report that its operation mode matches the host and line signal rates
 * and that its DSP and laser are running within their rated temperatures.
 * Asserting a mode mismatch requires deliberately driving the host PHY at a
 * different rate, so any mismatch seen here is a real misconfiguration.
 * Steps:
 * 1. Find optical ports whose module advertises the custom features
 *    (Page 01h Byte 191), and mark only those as the ports under test
 * 2. Wait for a TransceiverInfo refresh that started after the test began --
 *    the Byte 67 flags are latched and read-to-clear, so a flag set during
 *    link bringup would otherwise still be present in the cached snapshot
 * 3. Expect no latched flag set, no lane reporting a mode mismatch, and both
 *    thermal margins positive
 */
TEST_F(AgentEnsembleOpticsTest, verifyNoModeMismatchOrNegativeThermalMargin) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::
           TRANSCEIVER_MODE_MISMATCH_THERMAL_MARGIN});

  auto opticalPortPairs = getConnectedOpticalPortPairs();
  EXPECT_FALSE(opticalPortPairs.empty())
      << "Did not detect any optical transceivers";

  std::map<int32_t, std::vector<PortID>> tcvrIdToPorts;
  for (const auto& [portID1, portID2] : opticalPortPairs) {
    for (const auto& portID : {portID1, portID2}) {
      auto tcvrId = int32_t(
          getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(portID));
      tcvrIdToPorts[tcvrId].push_back(portID);
    }
  }
  std::vector<int32_t> allTcvrIds;
  allTcvrIds.reserve(tcvrIdToPorts.size());
  for (const auto& [tcvrId, _] : tcvrIdToPorts) {
    allTcvrIds.push_back(tcvrId);
  }

  // 1. Keep only the transceivers that advertise at least one of the custom
  // features. The registers are CMIS Custom space and mean nothing otherwise.
  // Only their ports count as tested -- the rest of the optical ports are
  // untouched by this test.
  auto transceiverInfos =
      utility::waitForTransceiverInfo(allTcvrIds, /*includeLpo*/ false);
  std::vector<int32_t> capableTcvrIds;
  for (const auto& [tcvrId, info] : transceiverInfos) {
    const auto& diagCapability = *info.tcvrState()->diagCapability();
    if (*diagCapability.modeMismatchFlag() || *diagCapability.dspTempMargin() ||
        *diagCapability.laserTempMargin()) {
      capableTcvrIds.push_back(tcvrId);
      addTestedPorts(tcvrIdToPorts.at(tcvrId));
    }
  }
  ASSERT_FALSE(capableTcvrIds.empty())
      << "No optics advertise mode mismatch or thermal margin support";

  // 2. The Byte 67 flags are read-to-clear, so require a snapshot qsfp_service
  // collected after this point rather than trusting the cached one, which may
  // still carry a flag latched while the link was coming up.
  std::map<int32_t, TransceiverInfo> freshInfos;
  ASSERT_TRUE(
      utility::waitForFreshTransceiverInfo(
          capableTcvrIds, transceiverInfos, freshInfos, /*includeLpo*/ false))
      << "qsfp_service did not refresh all transceivers under test";

  // 3. Verify nothing is mismatched and nothing is over temperature.
  for (const auto& tcvrId : capableTcvrIds) {
    auto freshInfoItr = freshInfos.find(tcvrId);
    ASSERT_TRUE(freshInfoItr != freshInfos.end());
    const auto& tcvrState = *freshInfoItr->second.tcvrState();
    const auto& tcvrStats = *freshInfoItr->second.tcvrStats();
    const auto& tcvrName = *tcvrState.tcvrName();
    const auto& diagCapability = *tcvrState.diagCapability();
    ASSERT_TRUE(tcvrState.status().has_value()) << tcvrName;
    const auto& status = *tcvrState.status();

    if (*diagCapability.modeMismatchFlag()) {
      EXPECT_EQ(status.modeMismatchFlag(), false)
          << tcvrName << ": module reports its operation mode does not match "
          << "the host or line signal rate";
      for (const auto& signal : tcvrState.hostLaneSignals().value_or({})) {
        EXPECT_EQ(signal.modeMismatch(), false)
            << tcvrName << ": host lane " << *signal.lane() << " mismatched";
      }
      for (const auto& signal : tcvrState.mediaLaneSignals().value_or({})) {
        EXPECT_EQ(signal.modeMismatch(), false)
            << tcvrName << ": media lane " << *signal.lane() << " mismatched";
      }
    }

    // A margin is the headroom to the component's rated limit, so a value at
    // or below zero means it is running at or past that limit.
    if (*diagCapability.dspTempMargin()) {
      ASSERT_TRUE(tcvrStats.dspTempMargin().has_value()) << tcvrName;
      EXPECT_EQ(status.dspTempNegativeMarginFlag(), false) << tcvrName;
      EXPECT_GT(*tcvrStats.dspTempMargin(), 0)
          << tcvrName << ": DSP is over its rated temperature";
    }
    if (*diagCapability.laserTempMargin()) {
      ASSERT_TRUE(tcvrStats.laserTempMargin().has_value()) << tcvrName;
      EXPECT_EQ(status.laserTempNegativeMarginFlag(), false) << tcvrName;
      EXPECT_GT(*tcvrStats.laserTempMargin(), 0)
          << tcvrName << ": laser is over its rated temperature";
    }
  }
}

/*
 * opticsVdmPerformanceMonitoring
 *
 * Check VDM parameters are within the threshold for VDM supported optics
 * Steps:
 * 1. Find the list of optical ports with VDM supported optics
 * 2. Wait for a VDM interval that started after the test began
 * 3. Wait for a fresh TransceiverInfo update from qsfp_service and validate the
 *    VDM Performance Monitoring parameters within the thresholds defined in
 *    spec, repeating for every iteration
 * Note: Bypass LPO Transceivers for this test since the LPO
 *       transceivers don't have a DSP and there are no VDM stats.
 */
TEST_F(AgentEnsembleLinkTest, opticsVdmPerformanceMonitoring) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::VDM});

  // 1. Find the list of optical ports with VDM supported optics
  auto connectedPairPortIds = getConnectedOpticalAndActivePortPairWithFeature(
      TransceiverFeature::VDM, phy::Side::LINE);
  CHECK(!connectedPairPortIds.empty())
      << "opticsVdmPerformanceMonitoring: No optics capable of this test";

  std::vector<PortID> allTestPorts;
  for (auto portPair : connectedPairPortIds) {
    allTestPorts.push_back(portPair.first);
    allTestPorts.push_back(portPair.second);
  }
  addTestedPorts(allTestPorts);

  std::unordered_set<int32_t> transceiverIdSet;
  for (const auto& port : allTestPorts) {
    auto tcvrId =
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port);
    transceiverIdSet.insert(tcvrId);
  }
  std::vector<int32_t> transceiverIds(
      transceiverIdSet.begin(), transceiverIdSet.end());
  auto transceiverInfos =
      utility::waitForTransceiverInfo(transceiverIds, /*includeLpo*/ false);

  std::time_t startTime = std::time(nullptr);
  // 2. Wait for TWO VDM interval boundaries after the test began before
  // validating, to skip the link-up interval entirely. intervalStartTime is
  // stamped at the freeze and labels the *next* interval, but right after a
  // freeze the reporting registers still hold the *previous* interval's data --
  // so a single boundary past startTime can still surface link-up noise. Once a
  // second boundary (F2 > F1 > startTime) is observed, both the just-frozen
  // ([F1,F2]) and the live-running ([F2,now]) VDM data are guaranteed to start
  // after link up.
  std::time_t firstIntervalStart = 0;
  WITH_RETRIES_N_TIMED(40, std::chrono::seconds(5), {
    transceiverInfos =
        utility::waitForTransceiverInfo(transceiverIds, /*includeLpo*/ false);
    auto vdmStat =
        transceiverInfos.begin()->second.tcvrStats()->vdmPerfMonitorStats();
    ASSERT_EVENTUALLY_TRUE(vdmStat.has_value());
    auto intervalStart = vdmStat->intervalStartTime().value();
    ASSERT_EVENTUALLY_GT(intervalStart, startTime);
    // Record the first interval boundary after startTime, then require a
    // second, later one so even a just-frozen snapshot reflects a post-link-up
    // interval.
    if (firstIntervalStart == 0 && intervalStart > startTime) {
      firstIntervalStart = intervalStart;
    }
    ASSERT_EVENTUALLY_GT(intervalStart, firstIntervalStart);
    ASSERT_EVENTUALLY_GT(vdmStat->statsCollectionTme().value(), intervalStart);
  });

  // Track the worst (highest) datapath pre-FEC BER and max FEC tail seen per
  // port for each transceiver side across all iterations. These are surfaced as
  // per-port metadata so the optics link health observed during the test is
  // recorded in Scuba.
  std::map<PortID, double> mediaWorstPreFecBer, hostWorstPreFecBer;
  std::map<PortID, int16_t> mediaMaxFecTail, hostMaxFecTail;
  auto accumulateVdm = [&](const std::map<int, TransceiverInfo>& infos) {
    auto accumulateSide =
        [&](const std::map<std::string, VdmPerfMonitorPortSideStats>& sideStats,
            std::map<PortID, double>& worstPreFecBer,
            std::map<PortID, int16_t>& maxFecTail) {
          for (const auto& [portName, vdmPerfMon] : sideStats) {
            auto portId = getPortID(portName);
            worstPreFecBer[portId] = std::max(
                worstPreFecBer[portId],
                vdmPerfMon.datapathBER()->max().value());
            if (vdmPerfMon.fecTailMax().has_value()) {
              maxFecTail[portId] =
                  std::max(maxFecTail[portId], vdmPerfMon.fecTailMax().value());
            }
          }
        };
    for (const auto& tcvrId : transceiverIds) {
      auto txInfoItr = infos.find(tcvrId);
      if (txInfoItr == infos.end()) {
        continue;
      }
      auto vdmPerfMonitorStats =
          txInfoItr->second.tcvrStats()->vdmPerfMonitorStats();
      if (!vdmPerfMonitorStats.has_value()) {
        continue;
      }
      accumulateSide(
          vdmPerfMonitorStats->mediaPortVdmStats().value(),
          mediaWorstPreFecBer,
          mediaMaxFecTail);
      accumulateSide(
          vdmPerfMonitorStats->hostPortVdmStats().value(),
          hostWorstPreFecBer,
          hostMaxFecTail);
    }
  };

  // 3. validate the VDM Performance Monitoring parameters within the threshold
  int testIterations = FLAGS_link_stress_test ? 60 : 1;
  do {
    std::map<int32_t, TransceiverInfo> freshInfos;
    if (!utility::waitForFreshTransceiverInfo(
            transceiverIds,
            transceiverInfos,
            freshInfos,
            /*includeLpo*/ false)) {
      ADD_FAILURE() << "qsfp_service published no fresh transceiverInfo";
      break;
    }
    transceiverInfos = std::move(freshInfos);
    validateVdm(transceiverInfos, transceiverIds);
    accumulateVdm(transceiverInfos);
  } while (testIterations-- && !::testing::Test::HasFailure());

  for (const auto& [port, ber] : mediaWorstPreFecBer) {
    addTestMetadata(
        port, "tcvr_media_worst_pre_fec_ber", folly::to<std::string>(ber));
  }
  for (const auto& [port, ber] : hostWorstPreFecBer) {
    addTestMetadata(
        port, "tcvr_host_worst_pre_fec_ber", folly::to<std::string>(ber));
  }
  for (const auto& [port, fecTail] : mediaMaxFecTail) {
    addTestMetadata(
        port, "tcvr_media_max_fec_tail", folly::to<std::string>(fecTail));
  }
  for (const auto& [port, fecTail] : hostMaxFecTail) {
    addTestMetadata(
        port, "tcvr_host_max_fec_tail", folly::to<std::string>(fecTail));
  }
}
