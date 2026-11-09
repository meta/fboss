// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <folly/Random.h>
#include <folly/String.h>
#include <folly/Subprocess.h>
#include <gtest/gtest.h>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/LldpManager.h"
#include "fboss/agent/SwAgentInitializer.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/test/link_tests/AgentEnsembleLinkTest.h"
#include "fboss/agent/test/link_tests/LinkTestUtils.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/lib/CommonUtils.h"
#include "fboss/lib/thrift_service_client/ThriftServiceClient.h"

#include "fboss/agent/platforms/common/PlatformMapping.h"

DECLARE_int32(update_watermark_stats_interval_s);
DECLARE_int32(update_cable_length_stats_s);
DECLARE_bool(disable_icmp_error_response);

using namespace ::testing;
using namespace facebook::fboss;

namespace {
constexpr auto kMaxEcmpWidthPerSwitchId = 512;
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

// For some reason this flag always evaluates as false if we hardcode the string
// So return it from a function instead
const std::string qsfpUtilPrefix() {
  std::string extraFlags;
  if (FLAGS_multi_npu_platform_mapping) {
    extraFlags += " --multi-npu-platform-mapping ";
  }
  if (FLAGS_janga_test) {
    extraFlags += " --janga-test ";
  }
  if (FLAGS_montblanc_odd_ports_8x100G) {
    extraFlags += " --montblanc-odd-ports-8x100G ";
  }
  return "wedge_qsfp_util " + extraFlags;
}

const std::vector<std::string> l1LinkTestNames = {"qsfpWarmbootIsHitLess"};

const std::vector<std::string> l2LinkTestNames = {
    "warmbootIsHitLess",
    "ptpEnableIsHitless"};
} // namespace

class AgentEnsembleLinkSanityTestDataPlaneFlood : public AgentEnsembleLinkTest {
 private:
  cfg::SwitchConfig initialConfig(const AgentEnsemble& ensemble) override {
    XLOG(DBG2)
        << "setup up initial config for sw ttl0 to create dataplane flood";
    setupTtl0ForwardingEnable();
    return AgentEnsembleLinkTest::initialConfig(ensemble);
  }

  void setCmdLineFlagOverrides() const override {
    AgentEnsembleLinkTest::setCmdLineFlagOverrides();
    // Default ecmp_width (64) is narrower than the cabled port count per NPU,
    // which would cap the programmed group below what verify() expects.
    FLAGS_ecmp_width = kMaxEcmpWidthPerSwitchId;
    FLAGS_enable_lldp = false;
    FLAGS_tun_intf = false;
    FLAGS_update_watermark_stats_interval_s = 0;
    FLAGS_update_cable_length_stats_s = 0;
    FLAGS_disable_neighbor_updates = true;
    FLAGS_disable_icmp_error_response = true;
  }

  std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const override {
    const std::string testName =
        testing::UnitTest::GetInstance()->current_test_info()->name();

    std::vector<link_test_production_features::LinkTestProductionFeature>
        features;
    if (std::find(l1LinkTestNames.begin(), l1LinkTestNames.end(), testName) !=
        l1LinkTestNames.end()) {
      features.push_back(
          link_test_production_features::LinkTestProductionFeature::
              L1_LINK_TEST);
    } else if (
        std::find(l2LinkTestNames.begin(), l2LinkTestNames.end(), testName) !=
        l2LinkTestNames.end()) {
      features.push_back(
          link_test_production_features::LinkTestProductionFeature::
              L2_LINK_TEST);
    } else {
      throw std::runtime_error(
          "Test type (L1/L2) not specified for this test case");
    }
    if (testName == "ptpEnableIsHitless") {
      features.push_back(
          link_test_production_features::LinkTestProductionFeature::PTP);
    }
    return features;
  }
};

// Tests that the link comes up after a flap on the ASIC
TEST_F(AgentEnsembleLinkTest, asicLinkFlap) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::LINK_BRINGUP});
  auto verify = [this]() {
    auto ports = getCabledPorts();
    addTestedPorts(ports);
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

TEST_F(AgentEnsembleLinkTest, getTransceivers) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::LINK_BRINGUP});
  auto verify = [this]() {
    WITH_RETRIES({
      auto ports = getCabledTransceiverPorts();
      addTestedPorts(ports);
      for (const auto& port : ports) {
        auto transceiverId =
            getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port);
        auto transceiverSpec = utility::getTransceiverSpec(getSw(), port);
        EXPECT_EVENTUALLY_TRUE(transceiverSpec) << "TcvrId " << transceiverId;
      }
    })

    WITH_RETRIES({
      auto ports = getCabledTransceiverPorts();
      for (const auto& port : ports) {
        auto transceiverIndx0 = getSw()->getTransceiverIdxThrift(port);
        auto transceiverIndx1 = getSw()->getTransceiverIdxThrift(port);
        EXPECT_EVENTUALLY_TRUE(isEqual(transceiverIndx0, transceiverIndx1));
      }
    })
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentEnsembleLinkTest, trafficRxTx) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::
           TRAFFIC_FORWARDING});
  addTestedPorts(getCabledPorts());
  auto verify = [this]() {
    WITH_RETRIES({
      getSw()->getLldpMgr()->sendLldpOnAllPorts();
      EXPECT_EVENTUALLY_TRUE(checkReachabilityOnAllCabledPorts());
    });
  };

  verifyAcrossWarmBoots([]() {}, verify);
}

TEST_F(AgentEnsembleLinkSanityTestDataPlaneFlood, warmbootIsHitLess) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::WARMBOOT});
  addTestedPorts(getCabledPorts());
  // Create a L3 data plane flood and then assert that none of the
  // traffic bearing ports loss traffic.
  // TODO: Assert that all (non downlink) cabled ports get traffic.
  const auto switchIdUnderTest = SwitchID(FLAGS_switch_id_for_testing);
  verifyAcrossWarmBoots(
      [this, switchIdUnderTest]() {
        createL3DataplaneFlood(
            getSingleVlanOrRoutedCabledPorts(switchIdUnderTest));
      },
      [this, switchIdUnderTest]() {
        // Assert no traffic loss and no ecmp shrink. If ports flap
        // these conditions will not be true
        assertNoInDiscards();
        auto ecmpSizeInSw =
            getSingleVlanOrRoutedCabledPorts(switchIdUnderTest).size();
        auto client =
            getAgentEnsemble()->getHwAgentTestClient(switchIdUnderTest);
        facebook::fboss::utility::CIDRNetwork cidr;
        cidr.IPAddress() = "::";
        cidr.mask() = 0;
        EXPECT_EQ(
            client->sync_getHwEcmpSize(cidr, 0, ecmpSizeInSw), ecmpSizeInSw);

        // Assert all cabled transceivers have ACTIVE state
        EXPECT_NO_THROW(waitForAllCabledPorts(true));
        EXPECT_NO_THROW(
            utility::waitForAllTransceiverStates(true, getCabledTranceivers()));
        EXPECT_NO_THROW(waitForPortStateMachineState(true));
      });
}

TEST_F(AgentEnsembleLinkSanityTestDataPlaneFlood, qsfpWarmbootIsHitLess) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::WARMBOOT});
  addTestedPorts(getCabledPorts());
  // Create a L3 data plane flood and then warmboot qsfp_service. Then assert
  // that none of the traffic bearing ports loss traffic.
  const auto switchIdUnderTest = SwitchID(FLAGS_switch_id_for_testing);
  verifyAcrossWarmBoots(
      [this, switchIdUnderTest]() {
        createL3DataplaneFlood(
            getSingleVlanOrRoutedCabledPorts(switchIdUnderTest));
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
      [this, switchIdUnderTest]() {
        // Assert no traffic loss and no ecmp shrink. If ports flap
        // these conditions will not be true
        assertNoInDiscards();
        auto ecmpSizeInSw =
            getSingleVlanOrRoutedCabledPorts(switchIdUnderTest).size();
        auto client =
            getAgentEnsemble()->getHwAgentTestClient(switchIdUnderTest);
        facebook::fboss::utility::CIDRNetwork cidr;
        cidr.IPAddress() = "::";
        cidr.mask() = 0;
        EXPECT_EQ(
            client->sync_getHwEcmpSize(cidr, 0, ecmpSizeInSw), ecmpSizeInSw);
        // Assert all cabled transceivers have ACTIVE state
        EXPECT_NO_THROW(waitForAllCabledPorts(true));
        EXPECT_NO_THROW(
            utility::waitForAllTransceiverStates(true, getCabledTranceivers()));
        EXPECT_NO_THROW(waitForPortStateMachineState(true));
      });
}

TEST_F(AgentEnsembleLinkSanityTestDataPlaneFlood, ptpEnableIsHitless) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::PTP});
  addTestedPorts(getCabledPorts());
  // disable PTP as by default we'll  have it enabled now
  getSw()->updateStateBlocking("ptp disable", [](auto state) {
    auto newState = state->clone();
    auto switchSettings = utility::getFirstNodeIf(newState->getSwitchSettings())
                              ->modify(&newState);
    switchSettings->setPtpTcEnable(false);
    return newState;
  });

  createL3DataplaneFlood();
  assertNoInDiscards();

  getSw()->updateStateBlocking("ptp enable", [](auto state) {
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
  auto client = getAgentEnsemble()->getHwAgentTestClient(SwitchID(0));
  facebook::fboss::utility::CIDRNetwork cidr;
  cidr.IPAddress() = "::";
  cidr.mask() = 0;
  EXPECT_EQ(client->sync_getHwEcmpSize(cidr, 0, ecmpSizeInSw), ecmpSizeInSw);
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
TEST_F(AgentEnsembleLinkTest, opticsTxDisableRandomPorts) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::
           TRANSCEIVER_TX_DISABLE});
  auto [opticalPorts, opticalPortNames] =
      getOpticalAndActiveCabledPortsAndNames();
  addTestedPorts(opticalPorts);
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

  // Ports sharing media lanes with a disabled port (e.g. ZR 3x200G) lose TX
  // too, so they and their peers are also expected Down
  std::vector<int32_t> transceiverIds;
  for (const auto& port : opticalPorts) {
    transceiverIds.push_back(
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port));
  }
  auto transceiverInfos = utility::waitForTransceiverInfo(transceiverIds);
  auto getMediaLanes = [&](PortID port) -> std::set<int32_t> {
    auto tcvrId =
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port);
    auto tcvrInfo = transceiverInfos.find(tcvrId);
    if (tcvrInfo == transceiverInfos.end()) {
      return {};
    }
    const auto& portNameToMediaLanes =
        *tcvrInfo->second.tcvrState()->portNameToMediaLanes();
    auto lanes = portNameToMediaLanes.find(getPortName(port));
    if (lanes == portNameToMediaLanes.end()) {
      return {};
    }
    return {lanes->second.begin(), lanes->second.end()};
  };
  for (const auto& disabledPort : disabledPorts) {
    auto disabledTcvrId =
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(disabledPort);
    auto disabledLanes = getMediaLanes(disabledPort);
    for (const auto& port : opticalPorts) {
      if (port == disabledPort ||
          getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port) !=
              disabledTcvrId) {
        continue;
      }
      auto lanes = getMediaLanes(port);
      if (std::none_of(lanes.begin(), lanes.end(), [&](int32_t lane) {
            return disabledLanes.contains(lane);
          })) {
        continue;
      }
      XLOG(DBG2) << fmt::format(
          "opticsTxDisableRandomPorts: {:s} shares media lanes with disabled port {:s}",
          getPortName(port),
          getPortName(disabledPort));
      expectedDownPorts.push_back(port);
      if (auto peerPort = getPeerPortID(port, connectedPairPortIds)) {
        expectedDownPorts.push_back(*peerPort);
      }
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
      qsfpUtilPrefix() + disabledPortNames + " --tx_disable";
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
      qsfpUtilPrefix() + disabledPortNames + " --tx_enable";
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

TEST_F(AgentEnsembleLinkTest, opticsTxDisableEnable) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::
           TRANSCEIVER_TX_DISABLE});
  auto [opticalPorts, opticalPortNames] =
      getOpticalAndActiveCabledPortsAndNames();
  addTestedPorts(opticalPorts);
  EXPECT_FALSE(opticalPorts.empty())
      << "opticsTxDisableEnable: Did not detect any optical transceivers";

  if (!opticalPorts.empty()) {
    opticalPortNames = qsfpUtilPrefix() + opticalPortNames;
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

// Disable the transceiver Rx output (host side) through the qsfp_service
// setInterfaceTxRx thrift API and verify the links go down, then re-enable and
// verify the links come back up
TEST_F(AgentEnsembleLinkTest, opticsRxOutputDisableEnable) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::
           TRANSCEIVER_RX_OUTPUT_DISABLE});

  // 1. Find the optical ports whose transceiver supports Rx output control
  auto [opticalPorts, opticalPortNames] =
      getOpticalAndActiveCabledPortsAndNames();
  std::vector<int32_t> tcvrIds;
  for (const auto& port : opticalPorts) {
    tcvrIds.push_back(
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port));
  }
  auto tcvrInfos = utility::waitForTransceiverInfo(tcvrIds);

  std::vector<PortID> rxCtrlPorts;
  std::vector<std::string> rxCtrlPortNames;
  for (const auto& port : opticalPorts) {
    auto tcvrId =
        getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port);
    auto tcvrInfo = tcvrInfos.find(tcvrId);
    if (tcvrInfo == tcvrInfos.end()) {
      continue;
    }
    const auto& tcvrState = *tcvrInfo->second.tcvrState();
    if (utility::isRxOutputDisableUnsupported(tcvrState)) {
      XLOG(DBG2) << "opticsRxOutputDisableEnable: Skipping "
                 << getPortName(port)
                 << ", transceiver Rx output disable is unsupported";
      continue;
    }
    const auto& diagCapability = tcvrState.diagCapability().as_const();
    if (apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
            diagCapability) &&
        *diagCapability->rxOutputControl()) {
      rxCtrlPorts.push_back(port);
      rxCtrlPortNames.push_back(getPortName(port));
    }
  }
  addTestedPorts(rxCtrlPorts);
  ASSERT_FALSE(rxCtrlPorts.empty())
      << "opticsRxOutputDisableEnable: No transceivers support Rx output control";
  XLOG(DBG2) << "opticsRxOutputDisableEnable: Testing ports "
             << folly::join(" ", rxCtrlPortNames);

  // The transceiver Rx output is the host facing (system side) transmitter
  auto setRxOutput = [&](bool enable) {
    std::vector<phy::TxRxEnableRequest> requests;
    for (const auto& portName : rxCtrlPortNames) {
      phy::TxRxEnableRequest request;
      request.portName() = portName;
      request.component() = phy::PortComponent::TRANSCEIVER_SYSTEM;
      request.direction() = phy::Direction::TRANSMIT;
      request.enable() = enable;
      requests.push_back(request);
    }
    std::vector<phy::TxRxEnableResponse> responses;
    auto qsfpServiceClient = utils::createQsfpServiceClient();
    qsfpServiceClient->sync_setInterfaceTxRx(responses, requests);
    EXPECT_EQ(responses.size(), requests.size());
    for (const auto& response : responses) {
      EXPECT_TRUE(*response.success())
          << "opticsRxOutputDisableEnable: Failed to "
          << (enable ? "enable" : "disable") << " Rx output on "
          << *response.portName();
    }
  };

  // 2. Disable Rx output and 3. confirm the links go down within 30s
  setRxOutput(false);
  XLOG(DBG2)
      << "opticsRxOutputDisableEnable: Rx disabled. Awaiting links to go down";
  EXPECT_NO_THROW(waitForLinkStatus(rxCtrlPorts, false, 30, 1s));

  // 4. Enable Rx output and 5. confirm the links come back up within 2min
  setRxOutput(true);
  XLOG(DBG2)
      << "opticsRxOutputDisableEnable: Rx enabled. Awaiting links to go up";
  EXPECT_NO_THROW(waitForLinkStatus(rxCtrlPorts, true, 120, 1s));
  XLOG(DBG2) << "opticsRxOutputDisableEnable: links are up";
}

// Tests that when link goes down then remediation is triggered
TEST_F(AgentEnsembleLinkTest, testOpticsRemediation) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::REMEDIATION});
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
    addTestedPorts(disabledPorts);

    const std::string txDisableCmd =
        qsfpUtilPrefix() + disabledPortNames + " --tx-disable";
    // @lint-ignore CLANGTIDY
    folly::Subprocess(txDisableCmd).waitChecked();

    for (const auto& port : disabledPorts) {
      auto tcvrId =
          getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port);
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
            getSw()->getPlatformMapping()->getTransceiverIdFromSwPort(port);
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

TEST_F(AgentEnsembleLinkTest, qsfpColdbootAfterAgentUp) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::LINK_BRINGUP});
  addTestedPorts(getCabledPorts());
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

TEST_F(AgentEnsembleLinkTest, fabricLinkHealth) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::FABRIC_LINK});
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

  auto fabricPorts = getCabledFabricPorts();
  addTestedPorts(fabricPorts);

  setForceTrafficOverFabric(true);

  utility::EcmpSetupTargetedPorts6 ecmpHelper(
      getSw()->getState(), getSw()->needL2EntryForNeighbor());
  const auto kDstPortDesc = ecmpHelper.ecmpPortDescriptorAt(0);
  const auto kSrcPortDesc = ecmpHelper.ecmpPortDescriptorAt(1);
  const auto kSrcPort = kSrcPortDesc.phyPortID();
  const auto kSrcPortName = getPortName(kSrcPort);

  auto beforeNifStats = getPortStats({kSrcPort})[kSrcPort];
  auto beforeFabricStats = getPortStats(fabricPorts);

  utility::pumpTraffic(
      utility::getAllocatePktFn(getSw()),
      utility::getSendPktFunc(getSw()),
      getSw()->getLocalMac(scope({kDstPortDesc})), /* dstMac */
      {ecmpHelper.ip(kSrcPortDesc)}, /* srcIps */
      {ecmpHelper.ip(kDstPortDesc)}, /* dstIps */
      8000, /* l4 srcPort */
      8001, /* l4 dstPort */
      1, /* streams */
      std::nullopt, /* vlan */
      kSrcPort, /* frontPanelPortToLoopTraffic */
      255, /* hopLimit */
      getSw()->getLocalMac(scope({kSrcPortDesc})), /* srcMac */
      10000 /* numPkts */);

  WITH_RETRIES_N_TIMED(5, std::chrono::seconds(60), {
    auto afterNifStats = getPortStats({kSrcPort})[kSrcPort];
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
