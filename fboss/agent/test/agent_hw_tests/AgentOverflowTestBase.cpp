// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/agent_hw_tests/AgentOverflowTestBase.h"

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/test/ProdConfigFactory.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestEcmpConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/EcmpDataPlaneTestUtil.h"
#include "fboss/agent/test/utils/InvariantTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"

namespace {
constexpr auto kTxRxThresholdMs = 10000;
} // namespace

namespace facebook::fboss {

void AgentOverflowTestBase::SetUp() {
  AgentHwTest::SetUp();
  if (!IsSkipped()) {
    auto ecmpHelper = std::make_unique<utility::HwIpV6EcmpDataPlaneTestUtil>(
        getAgentEnsemble(), RouterID(0));

    std::vector<PortDescriptor> ecmpPortDesc;
    for (const auto& port : getUplinkPorts()) {
      ecmpPortDesc.emplace_back(port);
    }
    ecmpHelper->programRoutesVecHelper(
        ecmpPortDesc, std::vector<NextHopWeight>(kWideEcmpWidth, 1));
  }
}

cfg::SwitchConfig AgentOverflowTestBase::initialConfig(
    const AgentEnsemble& ensemble) const {
  // Since we run inavriant tests based on switch role, we just pick the
  // most common switch role (i.e. rsw) for convenience of testing.
  // Not intended to extend coverage for every platform
  return utility::createProdRswConfig(
      ensemble.getL3Asics(),
      ensemble.getSw()->getPlatformType(),
      ensemble.getSw()->getPlatformMapping(),
      ensemble.getSw()->getPlatformSupportsAddRemovePort(),
      ensemble.masterLogicalInterfacePortIds(),
      ensemble.isSai());
}

std::vector<ProductionFeature>
AgentOverflowTestBase::getProductionFeaturesVerified() const {
  return {
      ProductionFeature::COPP,
      ProductionFeature::ECMP_LOAD_BALANCER,
      ProductionFeature::L3_QOS};
}

void AgentOverflowTestBase::startPacketTxRxVerify() {
  CHECK(!packetRxVerifyRunning_);
  packetRxVerifyRunning_ = true;
  auto vlanId = getVlanIDForTx();
  auto intfMac =
      getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  auto dstIp = folly::IPAddress::createNetwork(
                   getSw()->getConfig().interfaces()[0].ipAddresses()[0])
                   .first;
  auto sendBgpPktToMe = [=, this]() {
    utility::sendTcpPkts(
        getSw(),
        1 /*numPktsToSend*/,
        vlanId,
        intfMac,
        dstIp,
        utility::kNonSpecialPort1,
        utility::kBgpPort,
        // tests using invariants need to ensure
        // that they don't step over the invariant tests
        // hence explicitly pick a port to send traffic from
        // else can result in test failures
        getDownlinkPort());
  };
  packetRxVerifyThread_ =
      std::make_unique<std::thread>([this, sendBgpPktToMe]() {
        utility::SwSwitchPacketSnooper snooper(
            getSw(), "AgentOverflowTestBase snooper");
        initThread("PacketRxTxVerify");
        while (packetRxVerifyRunning_) {
          auto startTime(std::chrono::steady_clock::now());
          sendBgpPktToMe();
          snooper.waitForPacket(5);
          std::chrono::duration<double, std::milli> durationMillseconds =
              std::chrono::steady_clock::now() - startTime;
          if (durationMillseconds.count() > kTxRxThresholdMs) {
            throw FbossError(
                "Took more than : ", kTxRxThresholdMs, " for TX, RX cycle");
          }
        }
      });
}
void AgentOverflowTestBase::stopPacketTxRxVerify() {
  packetRxVerifyRunning_ = false;
  packetRxVerifyThread_->join();
  packetRxVerifyThread_.reset();
  // Wait slightly more than  kTxRxThresholdMs  so as to drain any inflight
  // traffic from packetTxRxThread send loop. Else the infligh traffic can
  // effect subsequent verifications by bumping up counters
  sleep(kTxRxThresholdMs / 1000 + 1);
}

PortID AgentOverflowTestBase::getDownlinkPort() {
  // pick the first downlink in the list
  return utility::getAllUplinkDownlinkPorts(
             getSw()->getPlatformType(),
             getSw()->getConfig(),
             kWideEcmpWidth,
             /* TODO: For RTSW invariant enable mmu lossless */
             false /* mmu_lossless*/)
      .second[0];
}

std::vector<PortID> AgentOverflowTestBase::getUplinkPorts() {
  // pick the first downlink in the list
  return utility::getAllUplinkDownlinkPorts(
             getSw()->getPlatformType(),
             getSw()->getConfig(),
             kWideEcmpWidth,
             /* TODO: For RTSW invariant enable mmu lossless */
             false /* mmu_lossless*/)
      .first;
}

void AgentOverflowTestBase::verifyInvariants() {
  utility::verifySafeDiagCmds(getAgentEnsemble());
  for (const auto& switchId : getSw()->getSwitchInfoTable().getL3SwitchIDs()) {
    utility::verifyCopp(getSw(), switchId, getDownlinkPort());
  }

  std::vector<PortDescriptor> ecmpPortDesc;
  for (const auto& port : getUplinkPorts()) {
    ecmpPortDesc.emplace_back(port);
  }
  utility::verifyLoadBalance(getSw(), kWideEcmpWidth, ecmpPortDesc);
  utility::verifyDscpToQueueMapping(getSw(), getUplinkPorts());
}
} // namespace facebook::fboss
