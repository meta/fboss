/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/IPAddressV6.h>
#include <folly/logging/xlog.h>
#include "fboss/agent/hw/bcm/BcmAclTable.h"
#include "fboss/agent/hw/bcm/BcmLogBuffer.h"
#include "fboss/agent/hw/bcm/BcmQcmCollector.h"
#include "fboss/agent/hw/bcm/BcmQcmManager.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/tests/BcmLinkStateDependentTests.h"
#include "fboss/agent/hw/bcm/tests/BcmSwitchEnsemble.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/hw/test/HwTestAclUtils.h"
#include "fboss/agent/hw/test/HwTestPacketUtils.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"

namespace {
constexpr int kLoopCount = 10;
constexpr int kPktTxCount = 10;
constexpr int kAclPktTxCount = 100;
const folly::IPAddressV6 kIPv6Route = folly::IPAddressV6("2401::201:ab00");
const folly::IPAddress kIPv6FlowDstAddress =
    folly::IPAddressV6("2401::201:ab01");

const std::array<folly::IPAddress, 4> kIPv6FlowSrcAddress{
    folly::IPAddressV6("1::9"),
    folly::IPAddressV6("1::10"),
    folly::IPAddressV6("1::11"),
    folly::IPAddressV6("1::12"),
};

const folly::IPAddress kIPv4FlowSrcAddress = folly::IPAddressV4("10.0.0.1");

const std::string kCollectorSrcIpv4 = "10.0.0.2/32";
const std::string kCollectorDstIpv4 = "11.0.0.2/32";

const std::string kCollectorSrcIpv6 = "2401::301:ab01/128";
const std::string kCollectorDstIpv6 = "2401::401:ab01/128";
int kCollectorUDPSrcPort = 20000;
const int kMaskV6 = 120;

const std::string kCollectorAclCounter = "collectorAclCounter";
const std::string kCollectorAcl = "testCollectorAcl";
} // namespace

namespace facebook::fboss {

class BcmQcmDataTest : public BcmLinkStateDependentTests {
 protected:
  void SetUp() override {
    FLAGS_load_qcm_fw = true;
    // program ifp entries with the statistics enabled
    // for testing
    FLAGS_enable_qcm_ifp_statistics = true;
    // allow enough init gpoort count to squeeze in 2 ports for monitoring
    // but not more, this is to test various corner scenarios when
    // we run out of QCM port monitoring capacity
    // gport available count has to be set before init but asic has not
    // initialized yet. 4 indicates the number of WRR Queue IDs.
    // Assert once the asic is initialized to ensure the number
    // of gport matches WRR queue count * 2.
    FLAGS_init_gport_available_count = 4 * 2;
    BcmLinkStateDependentTests::SetUp();
    const auto& queueIds = utility::kOlympicWRRQueueIds();
    CHECK_EQ(FLAGS_init_gport_available_count, queueIds.size() * 2);
    ecmpHelper6_ = std::make_unique<utility::EcmpSetupTargetedPorts6>(
        getProgrammedState(),
        getHwSwitch()->needL2EntryForNeighbor(),
        RouterID(0));
  }

  cfg::SwitchConfig initialConfig() const override {
    std::vector<PortID> ports = {
        masterLogicalPortIds()[0],
        masterLogicalPortIds()[1],
    };
    auto config = utility::onePortPerInterfaceConfig(
        getHwSwitch(),
        std::move(ports),
        getAsic()->desiredLoopbackModes(),
        true);
    return config;
  }

  cfg::QcmConfig getQcmConfig(bool isIpv6 = false) {
    auto qcmCfg = cfg::QcmConfig();
    // some dummy address
    if (isIpv6) {
      *qcmCfg.collectorDstIp() = kCollectorDstIpv6;
      *qcmCfg.collectorSrcIp() = kCollectorSrcIpv6;
    } else {
      *qcmCfg.collectorDstIp() = kCollectorDstIpv4;
      *qcmCfg.collectorSrcIp() = kCollectorSrcIpv4;
    }
    // only configured ports from beginning, need the cos queues
    const auto& portList = {
        masterLogicalPortIds()[0], masterLogicalPortIds()[1]};
    for (const auto& port : portList) {
      for (const auto& queueId : utility::kOlympicWRRQueueIds()) {
        qcmCfg.port2QosQueueIds()[port].push_back(queueId);
      }
    }

    *qcmCfg.collectorSrcPort() = kCollectorUDPSrcPort;
    *qcmCfg.agingIntervalInMsecs() = 1000;
    *qcmCfg.numFlowSamplesPerView() = 1;
    return qcmCfg;
  }

  void setupHelperInPddcMode(bool bestEffortScan = false) {
    auto newCfg{initialConfig()};
    auto qcmCfg = getQcmConfig();
    newCfg.switchSettings()->qcmEnable() = true;
    newCfg.qcmConfig() = qcmCfg;
    // pddc mode is defined by flowlimit being zero
    newCfg.qcmConfig()->flowLimit() = 0;
    if (bestEffortScan) {
      // in this mode, QCM try to scan at best effort
      newCfg.qcmConfig()->scanIntervalInUsecs() = 0;
    }
    applyNewConfig(newCfg);
    cfg_ = newCfg;

    addRoute(kIPv6Route, kMaskV6, PortDescriptor(masterLogicalPortIds()[0]));
  }

  void setupHelper(bool setupPolicer = false) {
    auto newCfg{initialConfig()};
    auto qcmCfg = getQcmConfig();
    if (setupPolicer) {
      qcmCfg.ppsToQcm() = 0;
    }
    *newCfg.switchSettings()->qcmEnable() = true;
    newCfg.qcmConfig() = qcmCfg;
    applyNewConfig(newCfg);
    cfg_ = newCfg;

    addRoute(kIPv6Route, kMaskV6, PortDescriptor(masterLogicalPortIds()[0]));
  }

  void setupHelperWithPorts(
      const std::vector<int32_t>& monitorPortList,
      bool monitorQcmCfgPortOnly = false) {
    auto newCfg{initialConfig()};
    auto qcmCfg = getQcmConfig();
    Port2QosQueueIdMap map = {};

    for (const auto& port : monitorPortList) {
      qcmCfg.monitorQcmPortList()->emplace_back(port);
      for (const auto& queueId : utility::kOlympicWRRQueueIds()) {
        qcmCfg.port2QosQueueIds()[port].push_back(queueId);
      }
    }
    qcmCfg.monitorQcmCfgPortsOnly() = monitorQcmCfgPortOnly;
    newCfg.qcmConfig() = qcmCfg;
    *newCfg.switchSettings()->qcmEnable() = true;

    applyNewConfig(newCfg);
  }

  void setupQcmOnly(const bool qcmEnable) {
    *cfg_.switchSettings()->qcmEnable() = qcmEnable;
    applyNewConfig(cfg_);
  }

  std::tuple<int, int> createQcmFlows(int numFlows, int pktCount) {
    int port1Counter = 0, port0Counter = 0;
    if (numFlows > kIPv6FlowSrcAddress.size()) {
      XLOG(ERR) << "Flow count requested : " << numFlows
                << " more than pool of addresses. Count= "
                << kIPv6FlowSrcAddress.size();
      return {0, 0};
    }
    // send flows
    for (int i = 0; i < pktCount; i++) {
      for (int j = 0; j < numFlows; j++) {
        sendL3Packet(
            kIPv6FlowDstAddress,
            masterLogicalPortIds()[1],
            kIPv6FlowSrcAddress[j]);
      }
    }
    port1Counter = getHwSwitch()->getBcmQcmMgr()->getIfpHitStatCounter(
        masterLogicalPortIds()[1]);
    port0Counter = getHwSwitch()->getBcmQcmMgr()->getIfpHitStatCounter(
        masterLogicalPortIds()[0]);
    return {port0Counter, port1Counter};
  }

  void setUpHelperWithAcls(bool isIpv6) {
    auto newCfg{initialConfig()};
    *newCfg.switchSettings()->qcmEnable() = true;

    auto qcmCfg = getQcmConfig(isIpv6);
    // add an acl
    auto* acl = utility::addAcl_DEPRECATED(&newCfg, kCollectorAcl);
    acl->dstMac() = BcmQcmCollector::getCollectorDstMac().toString();
    if (!isIpv6) {
      acl->dstIp() = kCollectorDstIpv4;
      acl->srcIp() = kCollectorSrcIpv4;
    } else {
      acl->dstIp() = kCollectorDstIpv6;
      acl->srcIp() = kCollectorSrcIpv6;
    }
    acl->l4DstPort() = *qcmCfg.collectorDstPort(); // pick the default
    utility::addAclStat(&newCfg, kCollectorAcl, kCollectorAclCounter);

    newCfg.qcmConfig() = qcmCfg;
    applyNewConfig(newCfg);
    addRoute(kIPv6Route, kMaskV6, PortDescriptor(masterLogicalPortIds()[0]));
  }

  HwSwitchEnsemble::Features featuresDesired() const override {
    return {HwSwitchEnsemble::LINKSCAN, HwSwitchEnsemble::PACKET_RX};
  }

  void addRoute(folly::IPAddress prefix, uint8_t mask, PortDescriptor port) {
    auto state = ecmpHelper6_->resolveNextHops(
        getProgrammedState(),
        {
            port,
        });

    // current support is for v6 flows only.
    // We could revisit to add support for v4 at a later time
    if (prefix.isV6()) {
      ecmpHelper6_->programRoutes(
          getRouteUpdater(), {port}, {RoutePrefixV6{prefix.asV6(), mask}});
    }
  }

  void sendL3Packet(
      folly::IPAddress dstIp,
      PortID from,
      const std::optional<folly::IPAddress>& srcIp = std::nullopt) {
    // TODO: Remove the dependency on VLAN below
    auto vlan = getHwSwitchEnsemble()->getVlanIDForTx();
    if (!vlan) {
      throw FbossError("VLAN id unavailable for test");
    }
    auto vlanId = *vlan;
    // construct eth hdr
    const auto intfMac = utility::getInterfaceMac(getProgrammedState(), vlanId);

    EthHdr::VlanTags_t vlans{
        VlanTag(vlanId, static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN))};

    EthHdr eth{intfMac, intfMac, std::move(vlans), 0x86DD};
    // construct l3 hdr
    bool isV6 = dstIp.isV6();
    const auto dummySrcIp = isV6 ? kIPv6FlowSrcAddress[0] : kIPv4FlowSrcAddress;
    auto kDefaultPayload = std::vector<uint8_t>(256, 0xff);
    auto pkt = utility::makeTCPTxPacket(
        getHwSwitch(),
        vlanId,
        intfMac,
        intfMac,
        srcIp ? *srcIp : dummySrcIp,
        dstIp,
        60000,
        60001,
        0);

    // send pkt on src port, let it loop back in switch and be l3 switched
    getHwSwitch()->sendPacketOutOfPortSync(std::move(pkt), from);
  }

  std::unique_ptr<utility::EcmpSetupTargetedPorts6> ecmpHelper6_;
  cfg::SwitchConfig cfg_;
};

TEST_F(BcmQcmDataTest, VerifyMonitorQcmCfgPortOnly) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    // This test only applies to ceratin ASIC e.g. TH
    // and to specific sdk versions
    GTEST_SKIP();
  }
  auto setup = [&]() {
    // empty monitorPortList
    setupHelperWithPorts({}, true);
  };
  auto verify = [&]() {
    Port2QosQueueIdMap portMap;
    // none of the initial ports  should be configured, as only
    // monitorQcmCfgPorts are allowed to be monitored and they are set to empty
    // here
    EXPECT_EQ(
        getHwSwitch()->getBcmQcmMgr()->getAvailableGPorts(),
        FLAGS_init_gport_available_count);
    createQcmFlows(1, kPktTxCount);
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getLearnedFlowCount(), 0);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Intent of this test is to add thrift configuration for some
// ports and ensure that they are programmed and get priority
// over the other cfged ports
// In this case masterLogicalPortIds(2),masterLogicalPortIds(3)
// should get prioritized over masterLogicalPortIds(0),
// masterLogicalPortIds(1) for programming
TEST_F(BcmQcmDataTest, VerifyPortMonitoringPriorityPortsConfigured) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    // This test only applies to ceratin ASIC e.g. TH
    // and to specific sdk versions
    GTEST_SKIP();
  }

  auto setup = [&]() {
    setupHelperWithPorts(
        {masterLogicalPortIds()[2], masterLogicalPortIds()[3]});
  };
  auto verify = [&]() {
    Port2QosQueueIdMap portMap;
    const auto& queueIds = utility::kOlympicWRRQueueIds();
    const std::set<int> queueIdSet(queueIds.begin(), queueIds.end());
    portMap[masterLogicalPortIds()[0]] = queueIdSet;
    portMap[masterLogicalPortIds()[1]] = queueIdSet;
    // Expect that we are not able to update QCM monitored ports here with
    // these 2 ports {masterLogicalPortIds()[0], masterLogicalPortIds()[1]}
    // this is because FLAGS_init_gport_available_count is configured to fit
    // only 2 ports
    EXPECT_FALSE(
        getHwSwitch()->getBcmQcmMgr()->updateQcmMonitoredPortsIfNeeded(
            portMap));
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getAvailableGPorts(), 0);

    // provide space for 2 more ports
    getHwSwitch()->getBcmQcmMgr()->setAvailableGPorts(queueIdSet.size() * 2);

    portMap.clear();
    portMap[masterLogicalPortIds()[2]] = queueIdSet;
    portMap[masterLogicalPortIds()[3]] = queueIdSet;
    // these 2 ports should already be programmed during setup above
    // note: they get priroity over the other cfged ports such as logical port
    // 0, 1
    EXPECT_FALSE(
        getHwSwitch()->getBcmQcmMgr()->updateQcmMonitoredPortsIfNeeded(
            portMap));
    EXPECT_EQ(
        getHwSwitch()->getBcmQcmMgr()->getAvailableGPorts(),
        queueIdSet.size() * 2);

    portMap.clear();
    portMap[masterLogicalPortIds()[0]] = queueIdSet;
    portMap[masterLogicalPortIds()[1]] = queueIdSet;
    EXPECT_TRUE(
        getHwSwitch()->getBcmQcmMgr()->updateQcmMonitoredPortsIfNeeded(
            portMap));
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getAvailableGPorts(), 0);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Intent of this test is to ensure that
// port monitoring works and  obeyes constraint of available gports.
TEST_F(BcmQcmDataTest, VerifyPortMonitoringNoneConfigured) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    // This test only applies to ceratin ASIC e.g. TH
    // and to specific sdk versions
    GTEST_SKIP();
  }

  auto setup = [&]() { setupHelper(); };

  auto verify = [&]() {
    Port2QosQueueIdMap portMap;
    const auto& queueIds = utility::kOlympicWRRQueueIds();
    const std::set<int> queueIdSet(queueIds.begin(), queueIds.end());

    getHwSwitch()->getBcmQcmMgr()->setAvailableGPorts(queueIdSet.size() - 1);
    portMap[masterLogicalPortIds()[2]] = queueIdSet;
    // monitoring for this port should fail, as gports available are less than
    // queueIds
    EXPECT_FALSE(
        getHwSwitch()->getBcmQcmMgr()->updateQcmMonitoredPortsIfNeeded(
            portMap));

    getHwSwitch()->getBcmQcmMgr()->setAvailableGPorts(queueIdSet.size());
    // monitoring for the port suceeds now, that gports are available
    EXPECT_TRUE(
        getHwSwitch()->getBcmQcmMgr()->updateQcmMonitoredPortsIfNeeded(
            portMap));

    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getAvailableGPorts(), 0);
    portMap.clear();
    portMap[masterLogicalPortIds()[3]] = queueIdSet;
    // since available gports is 0, it should fail
    EXPECT_FALSE(
        getHwSwitch()->getBcmQcmMgr()->updateQcmMonitoredPortsIfNeeded(
            portMap));

    getHwSwitch()->getBcmQcmMgr()->setAvailableGPorts(queueIdSet.size());
    EXPECT_TRUE(
        getHwSwitch()->getBcmQcmMgr()->updateQcmMonitoredPortsIfNeeded(
            portMap));
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getAvailableGPorts(), 0);

    getHwSwitch()->getBcmQcmMgr()->setAvailableGPorts(2 * queueIdSet.size());
    portMap.clear();
    portMap[masterLogicalPortIds()[2]] = queueIdSet;
    portMap[masterLogicalPortIds()[3]] = queueIdSet;
    // since these ports are already programmed, this update should fail
    // even though gports are available
    EXPECT_FALSE(
        getHwSwitch()->getBcmQcmMgr()->updateQcmMonitoredPortsIfNeeded(
            portMap));
    EXPECT_EQ(
        getHwSwitch()->getBcmQcmMgr()->getAvailableGPorts(),
        2 * queueIdSet.size());
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(BcmQcmDataTest, VerifyQcmFirmwareInit) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    // This test only applies to ceratin ASIC e.g. TH
    // and to specific sdk versions
    GTEST_SKIP();
  }

  // intent of this test is to load the QcmFirmware during
  // initialization (using flags above in SetUp) and ensure it goes through fine
  // Verify if the status of firmware is good
  auto setup = [=, this]() { setupHelper(); };

  auto verify = [this]() {
    BcmLogBuffer logBuffer;
    getHwSwitch()->printDiagCmd("mcsstatus Quick=true");
    std::string mcsStatusStr = logBuffer.getBuffer()->to<std::string>();
    XLOG(DBG2) << "MCStatus:" << mcsStatusStr;
    EXPECT_TRUE(mcsStatusStr.find("uC 0 status: OK") != std::string::npos);
  };
  verifyAcrossWarmBoots(setup, verify);
}

// Intent of test is to attach policer and verify
// that it works as expected. We attach a polcier with allowed rate of 0 kbps
// We expect only 1 pkt to be sent to R5 (since first pkt always goes through)
// and rest all pkts should be marked as red and not sent to the R5 cpu
TEST_F(BcmQcmDataTest, FlowLearningPolicer) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    GTEST_SKIP();
  }
  auto setup = [&]() { setupHelper(true); };

  auto verify = [&]() {
    // send 2 flows
    auto [nonFlowLearnPortCounter, flowLearnPortCounter] =
        createQcmFlows(2, kPktTxCount);
    std::ignore = nonFlowLearnPortCounter;
    // First pkt from first flow results in flow learn,
    // other 9 pkts of first flow doesn't hit the acl as a result
    // (would have been dropped anyways because of policer!)
    // Second flow, drops all pkts because of policer and
    // hence hit the acl 10 times
    EXPECT_EQ(flowLearnPortCounter, kPktTxCount + 1);
    // expect only 1 flow to be learned
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getLearnedFlowCount(), 1);
    // all pkts except the first one should be marked as RED
    EXPECT_EQ(
        getHwSwitch()->getBcmQcmMgr()->getIfpRedPktStatCounter(
            masterLogicalPortIds()[0]),
        2 * kPktTxCount - 1);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Intent of this test is to setup QCM tables
// send a flow and verify if the flow learning happens
// Only first pkt should hit the ifp entry, all subsequent pkts should
// not hit the ifp entry
// Since its 1 flow, we should only learn 1 flow
TEST_F(BcmQcmDataTest, FlowLearning) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    GTEST_SKIP();
  }
  auto setup = [&]() { setupHelper(); };

  auto verify = [&]() {
    // send 1 flow only
    auto [nonFlowLearnPortCounter, flowLearnPortCounter] =
        createQcmFlows(1, kPktTxCount);
    EXPECT_EQ(flowLearnPortCounter, 1);
    EXPECT_EQ(nonFlowLearnPortCounter, kPktTxCount);
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getLearnedFlowCount(), 1);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Intent of this test is to setup QCM tables
// send multiple flows and check that learning happens
// for multiple flows
TEST_F(BcmQcmDataTest, MultiFlows) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    GTEST_SKIP();
  }
  auto setup = [&]() { setupHelper(); };

  auto verify = [&]() {
    // send 2 flows
    auto [nonFlowLearnPortCounter, flowLearnPortCounter] =
        createQcmFlows(2, kPktTxCount);
    EXPECT_EQ(nonFlowLearnPortCounter, 2 * kPktTxCount);
    EXPECT_EQ(flowLearnPortCounter, 2);
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getLearnedFlowCount(), 2);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Intent of this test is to setup QCM tables
// send multiple flows, but restrict flow limit to 1
// Ensure that only 1 flow is learned
// also ifp entry should hit multiple times
TEST_F(BcmQcmDataTest, RestrictFlowLearning) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    GTEST_SKIP();
  }
  auto setup = [&]() { setupHelper(); };

  auto verify = [&]() {
    // force the flow learning to 1 only
    getHwSwitch()->getBcmQcmMgr()->flowLimitSet(1);
    // send 4 flows
    auto [nonFlowLearnPortCounter, flowLearnPortCounter] =
        createQcmFlows(4, kPktTxCount);

    // we learn 4 flows after first hit, but 3 hits after that
    // so its 9x3 + 4
    EXPECT_EQ(flowLearnPortCounter, 31);
    EXPECT_EQ(nonFlowLearnPortCounter, 4 * kPktTxCount);
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getLearnedFlowCount(), 1);
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Intent of this test is to enable QCM, do flow learning
// Stop qcm and ensure that flow tracker is disabled,
// Enable QCM again and ensure flow learning happens as desired
TEST_F(BcmQcmDataTest, VerifyQcmStartStop) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    GTEST_SKIP();
  }
  auto setup = [=, this]() { setupHelper(true /* enable policer */); };
  auto setupQcm = [=, this](const bool qcmEnable) { setupQcmOnly(qcmEnable); };
  auto verify = [&]() {
    // send 1 flow
    auto [nonFlowLearnPortCounter, flowLearnPortCounter] =
        createQcmFlows(1, kPktTxCount);
    std::ignore = nonFlowLearnPortCounter;
    EXPECT_EQ(flowLearnPortCounter, 1);
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getLearnedFlowCount(), 1);
    EXPECT_NE(0, getHwSwitch()->getBcmQcmMgr()->getPolicerId());
  };
  auto verifyStop = [&]() {
    EXPECT_TRUE(getHwSwitch()->getBcmQcmMgr()->isFlowTrackerDisabled());
    EXPECT_EQ(0, getHwSwitch()->getBcmQcmMgr()->getPolicerId());
  };

  // run with qcm enabled, one time setup
  setup();
  verify();

  for (int i = 0; i < kLoopCount; ++i) {
    // run setup with qcm disabled
    bool qcmEnable = false;
    setupQcm(qcmEnable);
    verifyStop();

    // enable qcm again
    qcmEnable = true;
    setupQcm(qcmEnable);
    verify();
  }
}

// Intent of this test is to test Qcm Start/Stop using PDDC mode
// In pddc mode, learned flow count = 0
// Stop qcm and ensure that flow tracker is disabled,
// Enable QCM again.
TEST_F(BcmQcmDataTest, VerifyQcmStartStopPddcMode) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    GTEST_SKIP();
  }
  auto setup = [=, this]() { setupHelperInPddcMode(); };
  auto setupQcm = [=, this](const bool qcmEnable) { setupQcmOnly(qcmEnable); };
  auto verify = [&]() {
    // send 1 flow
    createQcmFlows(1, kPktTxCount);
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getLearnedFlowCount(), 0);
    EXPECT_FALSE(getHwSwitch()->getBcmQcmMgr()->isFlowTrackerDisabled());
  };
  auto verifyStop = [&]() {
    EXPECT_TRUE(getHwSwitch()->getBcmQcmMgr()->isFlowTrackerDisabled());
  };

  // run with qcm enabled
  setup();
  verify();

  for (int i = 0; i < kLoopCount; ++i) {
    // run setup with qcm disabled
    bool qcmEnable = false;
    setupQcm(qcmEnable);
    verifyStop();

    // enable qcm again
    qcmEnable = true;
    setupQcm(qcmEnable);
    verify();
  }
}

// Intent of this test is to test Qcm in PDDC mode
// with scan interval  = 0
// In pddc mode, learned flow count = 0
TEST_F(BcmQcmDataTest, VerifyQcmPddcModeScanIntervalZero) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    GTEST_SKIP();
  }
  auto setup = [=, this]() {
    setupHelperInPddcMode(true /* bestEffortScan */);
  };
  auto verify = [&]() {
    // send 1 flow
    createQcmFlows(1, kPktTxCount);
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getLearnedFlowCount(), 0);
    EXPECT_FALSE(getHwSwitch()->getBcmQcmMgr()->isFlowTrackerDisabled());
    EXPECT_GT(getHwSwitch()->getBcmQcmMgr()->readScanIntervalInUsecs(), 0);
  };
  verifyAcrossWarmBoots(setup, verify);
}

class BcmQcmDataCollectorParamTest : public BcmQcmDataTest,
                                     public testing::WithParamInterface<bool> {
};

TEST_P(BcmQcmDataCollectorParamTest, VerifyFlowCollector) {
  if (!BcmQcmManager::isQcmSupported(getHwSwitch())) {
    GTEST_SKIP();
  }
  bool isIpv6 = GetParam();
  auto setup = [this, isIpv6]() { setUpHelperWithAcls(isIpv6); };
  auto verify = [&]() {
    auto [nonFlowLearnPortCounter, flowLearnPortCounter] =
        createQcmFlows(1, kAclPktTxCount);
    std::ignore = nonFlowLearnPortCounter;
    EXPECT_EQ(flowLearnPortCounter, 1);
    EXPECT_EQ(getHwSwitch()->getBcmQcmMgr()->getLearnedFlowCount(), 1);
    auto aclCounter = utility::getAclInOutPackets(
        getHwSwitch(),
        getProgrammedState(),
        kCollectorAcl,
        kCollectorAclCounter);
    EXPECT_GT(aclCounter, 0);
  };
  verifyAcrossWarmBoots(setup, verify);
}

INSTANTIATE_TEST_CASE_P(
    BcmQcmDataCollectorParamTestInstantitation,
    BcmQcmDataCollectorParamTest,
    ::testing::Values(false, true));
} // namespace facebook::fboss
