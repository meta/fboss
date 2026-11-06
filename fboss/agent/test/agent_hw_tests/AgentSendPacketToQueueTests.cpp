/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/IPAddress.h>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/AsicUtils.h"
#include "fboss/agent/test/utils/TrafficPolicyTestUtils.h"
#include "fboss/agent/test/utils/VoqTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace {
constexpr uint8_t kDefaultQueue = 0;
constexpr uint8_t kTestingQueue = 7;
constexpr uint32_t kDscp = 0x24;
constexpr uint8_t kChenabTxQueue = 7;
} // namespace

namespace facebook::fboss {

class AgentSendPacketToQueueTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::L3_FORWARDING};
  }

  // OUT_OF_PORT send on J3 EDSW dual-stage 3Q+2Q needs the high-ID
  // system/recycle ports in the configured port set. Opt out of the default
  // interface-port cap so the initial config keeps the full port set.
  std::optional<size_t> maxRequiredInterfacePorts() const override {
    return std::nullopt;
  }

 protected:
  void checkSendPacket(std::optional<uint8_t> ucQueue, bool isOutOfPort);
};

void AgentSendPacketToQueueTest::checkSendPacket(
    std::optional<uint8_t> ucQueue,
    bool isOutOfPort) {
  auto setup = [=, this]() {
    if (!isOutOfPort) {
      // need to set up ecmp for switching
      auto kEcmpWidthForTest = 1;
      utility::EcmpSetupAnyNPorts6 ecmpHelper6{
          getProgrammedState(), getSw()->needL2EntryForNeighbor()};
      resolveNeighborAndProgramRoutes(ecmpHelper6, kEcmpWidthForTest);
    }
  };

  auto verify = [=, this]() {
    utility::EcmpSetupAnyNPorts6 ecmpHelper6{
        getProgrammedState(), getSw()->needL2EntryForNeighbor()};
    auto port = ecmpHelper6.nhop(0).portDesc.phyPortID();
    uint8_t queueID = ucQueue ? *ucQueue : kDefaultQueue;

    // for chenab, when a packet is injected by CPU into port  with pipeline
    // bypass  the queue used for tx is not 'deffault queue' but special
    // internal queue  is used (queue 16) this queue is accounted against queue
    // id 7
    auto sw = getAgentEnsemble()->getSw();
    auto asic = utility::getAsic(*sw, port);
    if (asic->getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB &&
        isOutOfPort) {
      queueID = kChenabTxQueue;
    } else if (
        ucQueue && asic->isSupported(HwAsic::Feature::VOQ) &&
        isDualStage3Q2QQos()) {
      // Dual-stage 3Q+2Q EDSW programs only queues 0/1/2; queue 7 is unmapped
      // and the packet processor rejects packets targeted at it
      // (bcmCosqDropReasonPpErrorReject). Remap to the equivalent VOQ.
      queueID = utility::getTrafficClassToVoqId(asic, queueID);
    }

    auto beforeOutPkts =
        folly::copy(getLatestPortStats(port).queueOutPackets_().value())
            .at(queueID);
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    // packet format shouldn't be matter in this test
    auto pkt = utility::makeUDPTxPacket(
        getSw(),
        vlanId,
        intfMac,
        intfMac,
        folly::IPAddressV6(kTestSrcIpV6),
        folly::IPAddressV6(kTestDstIpV6),
        kTestSrcPort,
        kTestDstPort);

    if (isOutOfPort) {
      if (ucQueue) {
        getAgentEnsemble()->ensureSendPacketOutOfPort(
            std::move(pkt), port, queueID);
      } else {
        getAgentEnsemble()->ensureSendPacketOutOfPort(std::move(pkt), port);
      }
    } else {
      getAgentEnsemble()->ensureSendPacketSwitched(std::move(pkt));
    }

    WITH_RETRIES({
      auto afterOutPkts =
          folly::copy(getLatestPortStats(port).queueOutPackets_().value())
              .at(queueID);

      /*
       * Once the packet egresses out of the asic, the packet will be looped
       * back with dmac as neighbor mac. This will certainly fail the my mac
       * check. Some asic vendors drop the packet right away in the pipeline
       * whereas some drop later in the pipeline after MMU once the packet is
       * queueed. This will cause the queue counters to increment more than
       * once. Always check if atleast 1 packet is received.
       */
      EXPECT_EVENTUALLY_GE(afterOutPkts - beforeOutPkts, 1);
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentSendPacketToQueueTest, SendPacketOutOfPortToUCQueue) {
  checkSendPacket(kTestingQueue, true);
}

TEST_F(AgentSendPacketToQueueTest, SendPacketOutOfPortToDefaultUCQueue) {
  checkSendPacket(std::nullopt, true);
}

TEST_F(AgentSendPacketToQueueTest, SendPacketSwitchedToDefaultUCQueue) {
  checkSendPacket(std::nullopt, false);
}

class AgentSendPacketToMulticastQueueTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::L3_FORWARDING, ProductionFeature::MULTICAST_QUEUE};
  }
};

TEST_F(AgentSendPacketToMulticastQueueTest, SendPacketOutOfPortToMCQueue) {
  auto ensemble = getAgentEnsemble();
  auto l3Asics = ensemble->getSw()->getHwAsicTable()->getL3Asics();
  auto asic = checkSameAndGetAsicForTesting(l3Asics);
  auto masterLogicalPortIds = ensemble->masterLogicalPortIds();
  auto port = masterLogicalPortIds[0];

  auto setup = [=, this]() {
    // put all ports in the same vlan
    auto newCfg = utility::oneL3IntfNPortConfig(
        ensemble->getSw()->getPlatformMapping(),
        asic,
        masterLogicalPortIds,
        ensemble->getSw()->getPlatformSupportsAddRemovePort(),
        asic->desiredLoopbackModes());
    applyNewConfig(newCfg);
  };

  auto verifyFlooding = [=, this]() {
    auto beforeOutPkts = *getLatestPortStats(port).outMulticastPkts_();
    WITH_RETRIES({
      auto afterOutPkts = *getLatestPortStats(port).outMulticastPkts_();
      XLOG(DBG2) << "afterOutPkts " << afterOutPkts << ", beforeOutPkts "
                 << beforeOutPkts;
      EXPECT_EVENTUALLY_GT(afterOutPkts - beforeOutPkts, 10000);
    });
  };

  auto verify = [=, this]() {
    if (getSw()->getBootType() == BootType::WARM_BOOT) {
      return;
    }
    auto vlanId = getVlanIDForTx();
    auto intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    auto randomMac = folly::MacAddress("01:02:03:04:05:06");
    // send packets with random dst mac to flood the vlan
    for (int i = 0; i < 100; i++) {
      auto pkt = utility::makeUDPTxPacket(
          getSw(),
          vlanId,
          intfMac,
          randomMac,
          folly::IPAddressV6(kTestSrcIpV6),
          folly::IPAddressV6(kTestDstIpV6),
          kTestSrcPort,
          kTestDstPort,
          kDscp << 2);
      ensemble->sendPacketAsync(
          std::move(pkt),
          PortDescriptor(masterLogicalPortIds[0]),
          std::nullopt);
    }

    XLOG(DBG2) << "Verify multicast traffic is flooding";
    verifyFlooding();

    XLOG(DBG2)
        << "Add ACL to send packet to queue 7. This should only affect unicast traffic but not multicast traffic here";
    // In S422033, multicast traffic was also send to multicast queue 7, which
    // does not exist on TH4 and corrupted the asic. This caused parity error
    // later. All packets are also dropped and no traffic flooding any more.
    auto newCfg = ensemble->getCurrentConfig();
    utility::addDscpAclToCfg(asic, &newCfg, "acl1", kDscp);
    utility::addQueueMatcher(&newCfg, "acl1", kTestingQueue, ensemble->isSai());
    applyNewConfig(newCfg);

    XLOG(DBG2) << "Wait 10 seconds and verify multicast traffic still flooding";
    sleep(10);
    verifyFlooding();
  };

  auto verifyPostWb = [&]() {
    XLOG(DBG2) << "Verify multicast traffic still flooding after warmboot";
    verifyFlooding();
  };
  verifyAcrossWarmBoots(setup, verify, []() {}, verifyPostWb);
}

} // namespace facebook::fboss
