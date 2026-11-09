// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/Random.h>
#include <folly/Subprocess.h>
#include <gtest/gtest.h>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/PlatformPort.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/hw/test/HwTestEcmpUtils.h"
#include "fboss/agent/hw/test/HwTestFabricUtils.h"
#include "fboss/agent/hw/test/LoadBalancerUtils.h"
#include "fboss/agent/test/link_tests/LinkTest.h"
#include "fboss/agent/test/link_tests/LinkTestUtils.h"
#include "fboss/lib/CommonUtils.h"

using namespace ::testing;
using namespace facebook::fboss;

namespace {
bool isEqual(
    const TransceiverIdxThrift& left,
    const TransceiverIdxThrift& right) {
  auto getChannelId = [](const TransceiverIdxThrift& idx) {
    if (!idx.channelId().has_value()) {
      return std::optional<int32_t>();
    }
    return std::make_optional(static_cast<int>(*idx.channelId()));
  };

  auto getChannels = [](const TransceiverIdxThrift& idx) {
    std::set<int32_t> channels;
    if (!idx.channels().has_value()) {
      return channels;
    }
    for (auto channel : *idx.channels()) {
      channels.insert(channel);
    }
    return channels;
  };

  return *left.transceiverId() == *right.transceiverId() &&
      getChannelId(left) == getChannelId(right) &&
      getChannels(left) == getChannels(right);
}
const std::vector<std::string> l1LinkTestNames = {"qsfpWarmbootIsHitLess"};

const std::vector<std::string> l2LinkTestNames = {
    "warmbootIsHitLess",
    "ptpEnableIsHitless"};
} // namespace

class LinkSanityTestDataPlaneFlood : public LinkTest {
 private:
  void setupConfigFlag() override {
    XLOG(DBG2)
        << "setup up initial config for sw ttl0 to create dataplane flood";
    setupTtl0ForwardingEnable();
  }

  std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const override {
    const std::string testName =
        testing::UnitTest::GetInstance()->current_test_info()->name();

    if (std::find(l1LinkTestNames.begin(), l1LinkTestNames.end(), testName) !=
        l1LinkTestNames.end()) {
      return {link_test_production_features::LinkTestProductionFeature::
                  L1_LINK_TEST};
    } else if (
        std::find(l2LinkTestNames.begin(), l2LinkTestNames.end(), testName) !=
        l2LinkTestNames.end()) {
      return {link_test_production_features::LinkTestProductionFeature::
                  L2_LINK_TEST};
    } else {
      throw std::runtime_error(
          "Test type (L1/L2) not specified for this test case");
    }
  }
};

// Tests that the link comes up after a flap on the ASIC
TEST_F(LinkTest, asicLinkFlap) {
  auto verify = [this]() {
    auto ports = getCabledPorts();
    int numIterations = FLAGS_link_stress_test ? 25 : 1;
    for (int iteration = 1; iteration <= numIterations; iteration++) {
      XLOG(INFO) << "Starting iteration# " << iteration;
      // Set the port status on all cabled ports to false. The link should go
      // down
      for (const auto& port : ports) {
        setPortStatus(port, false);
      }
      ASSERT_NO_THROW(waitForAllCabledPorts(false));
      ASSERT_NO_THROW(
          utility::waitForAllTransceiverStates(
              false, getCabledTranceivers(), 60, 5s));
      EXPECT_NO_THROW(waitForPortStateMachineState(false, 60, 5s));

      // Set the port status on all cabled ports to true. The link should come
      // back up
      for (const auto& port : ports) {
        setPortStatus(port, true);
      }
      ASSERT_NO_THROW(waitForAllCabledPorts(true));
      ASSERT_NO_THROW(
          utility::waitForAllTransceiverStates(
              true, getCabledTranceivers(), 60, 5s));
      EXPECT_NO_THROW(waitForPortStateMachineState(true, 60, 5s));
      ASSERT_NO_THROW(checkAgentMemoryInBounds());
    }
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(LinkTest, getTransceivers) {
  auto verify = [this]() {
    WITH_RETRIES({
      auto ports = getCabledTransceiverPorts();
      for (const auto& port : ports) {
        auto transceiverId =
            platform()->getPlatformPort(port)->getTransceiverID().value();
        auto transceiverSpec = utility::getTransceiverSpec(sw(), port);
        EXPECT_EVENTUALLY_TRUE(transceiverSpec) << "TcvrId " << transceiverId;
      }
    })

    WITH_RETRIES({
      auto ports = getCabledTransceiverPorts();
      for (const auto& port : ports) {
        auto profileID =
            sw()->getState()->getPorts()->getNode(port)->getProfileID();
        auto transceiverIndx0 = platform()->getPortMapping(port, profileID);
        auto transceiverIndx1 = sw()->getTransceiverIdxThrift(port);
        EXPECT_EVENTUALLY_TRUE(isEqual(transceiverIndx0, transceiverIndx1));
      }
    })
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(LinkTest, trafficRxTx) {
  auto verify = [this]() {
    WITH_RETRIES({
      sw()->getLldpMgr()->sendLldpOnAllPorts();
      EXPECT_EVENTUALLY_TRUE(checkReachabilityOnAllCabledPorts());
    });
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(LinkSanityTestDataPlaneFlood, warmbootIsHitLess) {
  // Create a L3 data plane flood and then assert that none of the
  // traffic bearing ports loss traffic.
  // TODO: Assert that all (non downlink) cabled ports get traffic.
  verifyAcrossWarmBoots(
      [this]() { createL3DataplaneFlood(); },
      [this]() {
        // Assert no traffic loss and no ecmp shrink. If ports flap
        // these conditions will not be true
        assertNoInDiscards();
        auto ecmpSizeInSw = getSingleVlanOrRoutedCabledPorts().size();
        EXPECT_EQ(
            utility::getEcmpSizeInHw(
                platform()->getHwSwitch(),
                {folly::IPAddress("::"), 0},
                RouterID(0),
                ecmpSizeInSw),
            ecmpSizeInSw);
        // Assert all cabled transceivers have ACTIVE state
        EXPECT_NO_THROW(waitForAllCabledPorts(true));
        EXPECT_NO_THROW(
            utility::waitForAllTransceiverStates(true, getCabledTranceivers()));
        EXPECT_NO_THROW(waitForPortStateMachineState(true));
      });
}

TEST_F(LinkSanityTestDataPlaneFlood, qsfpWarmbootIsHitLess) {
  // Create a L3 data plane flood and then warmboot qsfp_service. Then assert
  // that none of the traffic bearing ports loss traffic.
  verifyAcrossWarmBoots(
      [this]() {
        createL3DataplaneFlood();
        utility::restartQsfpService(false /* coldboot */);
        // Wait for all transceivers to converge to Active state
        EXPECT_NO_THROW(
            utility::waitForAllTransceiverStates(
                true,
                getCabledTranceivers(),
                60 /* retries */,
                5s /* retry interval */));
        EXPECT_NO_THROW(waitForPortStateMachineState(
            true, 60 /* retries */, 5s /* retry interval */));
      },
      [this]() {
        // Assert no traffic loss and no ecmp shrink. If ports flap
        // these conditions will not be true
        assertNoInDiscards();
        auto ecmpSizeInSw = getSingleVlanOrRoutedCabledPorts().size();
        EXPECT_EQ(
            utility::getEcmpSizeInHw(
                platform()->getHwSwitch(),
                {folly::IPAddress("::"), 0},
                RouterID(0),
                ecmpSizeInSw),
            ecmpSizeInSw);
        // Assert all cabled transceivers have ACTIVE state
        EXPECT_NO_THROW(waitForAllCabledPorts(true));
        EXPECT_NO_THROW(
            utility::waitForAllTransceiverStates(true, getCabledTranceivers()));
        EXPECT_NO_THROW(waitForPortStateMachineState(true));
      });
}

TEST_F(LinkSanityTestDataPlaneFlood, ptpEnableIsHitless) {
  // disable PTP as by default we'll  have it enabled now
  sw()->updateStateBlocking("ptp disable", [](auto state) {
    auto newState = state->clone();
    auto switchSettings = utility::getFirstNodeIf(newState->getSwitchSettings())
                              ->modify(&newState);
    switchSettings->setPtpTcEnable(false);
    return newState;
  });

  createL3DataplaneFlood();
  assertNoInDiscards();

  sw()->updateStateBlocking("ptp enable", [](auto state) {
    auto newState = state->clone();
    auto switchSettings = utility::getFirstNodeIf(newState->getSwitchSettings())
                              ->modify(&newState);
    EXPECT_FALSE(switchSettings->isPtpTcEnable());
    switchSettings->setPtpTcEnable(true);
    return newState;
  });

  // Assert no traffic loss and no ecmp shrink. If ports flap
  // these conditions will not be true
  assertNoInDiscards();
  auto ecmpSizeInSw = getSingleVlanOrRoutedCabledPorts().size();
  EXPECT_EQ(
      utility::getEcmpSizeInHw(
          platform()->getHwSwitch(),
          {folly::IPAddress("::"), 0},
          RouterID(0),
          ecmpSizeInSw),
      ecmpSizeInSw);
}

/*
 * opticsTxDisableRandomPorts
 *
 * Test randomly selected ports for optics tx_disable and tx_enable.
 * Steps:
 * 1. Randomly select some of the optical ports
 * 2. Mark these ports and their peers for expected Down Ports
 * 3. Disable the randomly selected port from step 1
 * 4. Verify all Expected Down ports from step 2 go Down
 * 5. Verify all other ports (all optical ports - expected Down ports) keep
 *    remain Up
 * 6. Enable the randomly selected port from step 1
 * 7. Make sure all the ports come up again
 */
TEST_F(LinkTest, opticsTxDisableRandomPorts) {
  auto [opticalPorts, opticalPortNames] =
      getOpticalAndActiveCabledPortsAndNames();
  EXPECT_FALSE(opticalPorts.empty())
      << "opticsTxDisableRandomPorts: Did not detect any optical transceivers";

  auto connectedPairPortIds = getConnectedOpticalAndActivePortPairWithFeature(
      TransceiverFeature::TX_DISABLE, phy::Side::LINE);

  std::vector<PortID> disabledPorts; // List of PortID of disabled ports
  std::string disabledPortNames; // List of port Names of disabled ports
  std::vector<PortID> expectedDownPorts; // List of PortID of disabled ports
                                         // and their peers
  std::vector<PortID> expectedUpPorts; // opticalPorts - expectedDownPorts

  for (auto portPair : connectedPairPortIds) {
    auto port = portPair.first;
    auto peerPort = portPair.second;
    // Get port name
    auto name = getPortName(port);

    // 1. Randomly select if this portA needs to be disabled
    auto disableThisPort = folly::Random::randBool(0.5);
    if (disableThisPort) {
      // Add the port to disable port list
      disabledPorts.push_back(port);
      disabledPortNames += " " + name;

      // 2. Mark these ports and their peers for expected Down Ports
      // Add this port to expectedDownPorts list
      expectedDownPorts.push_back(port);

      // Add the peer port to expectedDownPorts list
      expectedDownPorts.push_back(peerPort);
    }
  }

  // Expected Up ports = All optical ports - expected down ports
  for (auto port : opticalPorts) {
    if (std::find(expectedDownPorts.begin(), expectedDownPorts.end(), port) ==
        expectedDownPorts.end()) {
      expectedUpPorts.push_back(port);
    }
  }

  // 0. Verify all the ports are Up first
  EXPECT_NO_THROW(waitForLinkStatus(opticalPorts, true));

  // 3. Disable the randomly selected port from step 1
  const std::string txDisableCmd =
      "wedge_qsfp_util " + disabledPortNames + " --tx_disable";
  XLOG(DBG2) << fmt::format(
      "opticsTxDisableRandomPorts: Disabling ports using command: {:s}",
      txDisableCmd);
  folly::Subprocess(txDisableCmd).waitChecked();
  XLOG(DBG2) << fmt::format(
      "opticsTxDisableRandomPorts: cmd {:s} finished. Awaiting links to go down...",
      txDisableCmd);

  // 4. Verify all Expected Down ports from step 2 go Down
  EXPECT_NO_THROW(waitForLinkStatus(expectedDownPorts, false));
  XLOG(DBG2) << fmt::format(
      "opticsTxDisableRandomPorts: link Tx disabled for {:s}",
      disabledPortNames);

  // 5. Verify thet expected Up ports (all optical ports - expected Down ports)
  // keep
  //   remain Up
  EXPECT_NO_THROW(waitForLinkStatus(expectedUpPorts, true));

  // 6. Enable the randomly selected port from step 1
  const std::string txEnableCmd =
      "wedge_qsfp_util " + disabledPortNames + " --tx_enable";
  XLOG(DBG2) << fmt::format(
      "opticsTxDisableRandomPorts: Enabling ports using command: {:s}",
      txEnableCmd);
  folly::Subprocess(txEnableCmd).waitChecked();
  XLOG(DBG2) << fmt::format(
      "opticsTxDisableRandomPorts: cmd {:s} finished. Awaiting links to go up...",
      txEnableCmd);

  // 7. Make sure all the ports are Up again
  EXPECT_NO_THROW(waitForLinkStatus(opticalPorts, true));
  XLOG(DBG2) << fmt::format(
      "opticsTxDisableRandomPorts: link Tx enabled for {:s}",
      disabledPortNames);
}

TEST_F(LinkTest, opticsTxDisableEnable) {
  auto [opticalPorts, opticalPortNames] =
      getOpticalAndActiveCabledPortsAndNames();
  EXPECT_FALSE(opticalPorts.empty())
      << "opticsTxDisableEnable: Did not detect any optical transceivers";

  if (!opticalPorts.empty()) {
    opticalPortNames = "wedge_qsfp_util " + opticalPortNames;
    const std::string txDisableCmd = opticalPortNames + "--tx-disable";

    XLOG(DBG2) << "opticsTxDisableEnable: About to execute cmd: "
               << txDisableCmd;
    // TODO(ccpowers): Doesn't seem like there's a way for us to make this
    // command a literal, since it depends on the cabling. We may want to just
    // make a qsfp-service API for this rather than using a shell cmd
    // @lint-ignore CLANGTIDY
    folly::Subprocess(txDisableCmd).waitChecked();
    XLOG(DBG2) << fmt::format(
        "opticsTxDisableEnable: cmd {:s} finished. Awaiting links to go down...",
        txDisableCmd);
    EXPECT_NO_THROW(waitForLinkStatus(opticalPorts, false));
    XLOG(DBG2) << "opticsTxDisableEnable: link Tx disabled";

    const std::string txEnableCmd = opticalPortNames + "--tx-enable";
    XLOG(DBG2) << "opticsTxDisableEnable: About to execute cmd: "
               << txEnableCmd;
    // @lint-ignore CLANGTIDY
    folly::Subprocess(txEnableCmd).waitChecked();
    XLOG(DBG2) << fmt::format(
        "opticsTxDisableEnable: cmd {:s} finished. Awaiting links to go up...",
        txEnableCmd);
    EXPECT_NO_THROW(waitForLinkStatus(opticalPorts, true));
    XLOG(DBG2) << "opticsTxDisableEnable: links are up";
  }
}

// Tests that when link goes down then remediation is triggered
TEST_F(LinkTest, testOpticsRemediation) {
  auto verify = [this]() {
    std::vector<int32_t> transceiverIds;
    // Bring down the link on all the optical cabled ports having tx_disable
    // feature supported. The link should go down and the remediation should
    // get triggered bringing it up
    auto connectedPairPortIds = getConnectedOpticalAndActivePortPairWithFeature(
        TransceiverFeature::TX_DISABLE, phy::Side::LINE);

    EXPECT_GT(connectedPairPortIds.size(), 0);

    std::vector<PortID> disabledPorts; // List of PortID of disabled ports
    std::string disabledPortNames; // List of port Names of disabled ports

    for (auto portPair : connectedPairPortIds) {
      auto port = portPair.first;
      auto peerPort = portPair.second;
      // Get port names
      auto portName = getPortName(port);
      auto peerPortName = getPortName(peerPort);

      disabledPorts.push_back(port);
      disabledPorts.push_back(peerPort);
      disabledPortNames += " " + portName;
      disabledPortNames += " " + peerPortName;
    }

    const std::string txDisableCmd =
        "wedge_qsfp_util " + disabledPortNames + " --tx-disable";
    // @lint-ignore CLANGTIDY
    folly::Subprocess(txDisableCmd).waitChecked();

    for (const auto& port : disabledPorts) {
      auto tcvrId =
          platform()->getPlatformPort(port)->getTransceiverID().value();
      transceiverIds.push_back(tcvrId);
    }
    XLOG(DBG2) << "Wait for all ports to be down " << disabledPortNames;
    EXPECT_NO_THROW(waitForLinkStatus(disabledPorts, false));

    // Check the module's remediation counter has increased.
    // Exclude the Miniphoton ports
    XLOG(DBG2) << "Check for Transceiver Info from qsfp_service now";

    // If the remediation counter has incremented for at least one of the
    // disabled ports then pass the test
    WITH_RETRIES_N_TIMED(5, std::chrono::seconds(60), {
      auto transceiverInfos = utility::waitForTransceiverInfo(transceiverIds);
      int numPortsRemediated = 0;
      for (const auto& port : disabledPorts) {
        auto tcvrId =
            platform()->getPlatformPort(port)->getTransceiverID().value();
        auto txInfoItr = transceiverInfos.find(tcvrId);
        if ((txInfoItr != transceiverInfos.end()) &&
            (apache::thrift::
                 is_non_optional_field_set_manually_or_by_serializer(
                     txInfoItr->second.tcvrState()) &&
             txInfoItr->second.tcvrState()->identifier().has_value() &&
             txInfoItr->second.tcvrState()->identifier().value() !=
                 TransceiverModuleIdentifier::MINIPHOTON_OBO) &&
            (apache::thrift::
                 is_non_optional_field_set_manually_or_by_serializer(
                     txInfoItr->second.tcvrStats()) &&
             txInfoItr->second.tcvrStats()->remediationCounter().has_value())) {
          XLOG(DBG2)
              << "Tcvr Id " << tcvrId << " remediation counter "
              << txInfoItr->second.tcvrStats()->remediationCounter().value();
          if (txInfoItr->second.tcvrStats()->remediationCounter().value() > 0) {
            numPortsRemediated++;
          }
        }
      }
      EXPECT_EVENTUALLY_GT(numPortsRemediated, 0);
    });

    XLOG(DBG2) << "Wait for all ports to come up " << disabledPortNames;
    EXPECT_NO_THROW(waitForLinkStatus(disabledPorts, true, 60, 5s));
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(LinkTest, qsfpColdbootAfterAgentUp) {
  // Verifies that a qsfp cold boot after agent is up can still bringup the
  // links and there is no dependency on which service starts first
  verifyAcrossWarmBoots(
      []() {},
      [this]() {
        utility::restartQsfpService(true /* coldboot */);
        /* sleep override */
        sleep(5);
        // Assert all cabled ports are up and transceivers have ACTIVE state
        EXPECT_NO_THROW(waitForAllCabledPorts(true, 60, 5s));
        EXPECT_NO_THROW(
            utility::waitForAllTransceiverStates(
                true, getCabledTranceivers(), 60, 5s));
        EXPECT_NO_THROW(waitForPortStateMachineState(true, 60, 5s));
      });
}

TEST_F(LinkTest, fabricLinkHealth) {
  //  Test for verifying fabric link health
  //   1. Enable traffic spray over fabric
  //   2. Generate traffic on the CPU and pump them out a front panel port. The
  //   traffic should loop back in because of snake configuration and then spray
  //   over the fabric.
  //   3. Verify out counters on both NIF and Fabric Ports
  //   4. Verify no in discards/in errors on NIF or Ports
  //   5. Verify FEC UCW doesn't increment on both NIF and Fabric ports
  //   6. TODO: Figure out other counters (pre-FEC BER, reassembly, packet
  //   integrity etc) to verify and add those checks

  auto fabricPorts = getPortName(getCabledFabricPorts());

  setForceTrafficOverFabric(platform()->getHwSwitch(), true);

  utility::EcmpSetupTargetedPorts6 ecmpHelper(
      sw()->getState(), sw()->needL2EntryForNeighbor());
  const auto kDstPortDesc = ecmpHelper.ecmpPortDescriptorAt(0);
  const auto kSrcPortDesc = ecmpHelper.ecmpPortDescriptorAt(1);
  const auto kSrcPort = kSrcPortDesc.phyPortID();
  const auto kSrcPortName = getPortName(kSrcPort);

  auto beforeNifStats = getPortStats({kSrcPortName})[kSrcPortName];
  auto beforeFabricStats = getPortStats(fabricPorts);

  utility::pumpTraffic(
      utility::getAllocatePktFn(sw()),
      utility::getSendPktFunc(sw()),
      sw()->getLocalMac(scope({kDstPortDesc})), /* dstMac */
      {ecmpHelper.ip(kSrcPortDesc)}, /* srcIps */
      {ecmpHelper.ip(kDstPortDesc)}, /* dstIps */
      8000, /* l4 srcPort */
      8001, /* l4 dstPort */
      1, /* streams */
      std::nullopt, /* vlan */
      kSrcPort, /* frontPanelPortToLoopTraffic */
      255, /* hopLimit */
      sw()->getLocalMac(scope({kSrcPortDesc})), /* srcMac */
      10000 /* numPkts */);

  WITH_RETRIES_N_TIMED(5, std::chrono::seconds(60), {
    auto afterNifStats = getPortStats({kSrcPortName})[kSrcPortName];
    auto fabricPortStats = getPortStats(fabricPorts);
    XLOG(DBG2) << "Before pkts: "
               << folly::copy(beforeNifStats.outUnicastPkts_().value())
               << " After pkts: "
               << folly::copy(afterNifStats.outUnicastPkts_().value());
    EXPECT_EVENTUALLY_GE(
        afterNifStats.outUnicastPkts_().value(),
        beforeNifStats.outUnicastPkts_().value() + 10000);

    auto fabricBytes = 0;
    for (const auto& idAndStats : fabricPortStats) {
      fabricBytes += idAndStats.second.outBytes_().value();
    }
    XLOG(DBG2) << "NIF bytes: "
               << folly::copy(afterNifStats.outBytes_().value())
               << " Fabric bytes: " << fabricBytes;
    EXPECT_EVENTUALLY_GE(fabricBytes, afterNifStats.outBytes_().value());

    for (const auto& idAndStats : fabricPortStats) {
      EXPECT_EQ(
          idAndStats.second.inErrors_().value(),
          beforeFabricStats[idAndStats.first].inErrors_().value())
          << "InErrors incremented on " << idAndStats.first;
      EXPECT_EQ(
          beforeFabricStats[idAndStats.first].fecUncorrectableErrors().value(),
          idAndStats.second.fecUncorrectableErrors().value())
          << "FEC Uncorrectable erorrs incremented on " << idAndStats.first;
    }
    EXPECT_EQ(
        afterNifStats.inErrors_().value(), beforeNifStats.inErrors_().value())
        << "InErrors incremented on " << kSrcPortName;
    EXPECT_EQ(
        beforeNifStats.fecUncorrectableErrors().value(),
        afterNifStats.fecUncorrectableErrors().value())
        << "FEC Uncorrectable errors incremented on " << kSrcPortName;
  });
}
