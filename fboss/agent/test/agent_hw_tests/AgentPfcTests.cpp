// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/PacketSnooper.h"
#include "fboss/agent/test/utils/PfcTestUtils.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

class AgentPfcTest : public AgentHwTest {
 public:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /*interfaceHasSubnet*/);
    return config;
  }

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::PFC};
  }

 protected:
  void sendPfcFrame(const std::vector<PortID>& portIds, uint8_t classVector) {
    for (const PortID& portId : portIds) {
      getSw()->sendPacketOutOfPortAsync(
          utility::makePfcFramePacket(*getAgentEnsemble(), classVector),
          portId);
    }
  }
};

TEST_F(AgentPfcTest, verifyPfcCounters) {
  std::vector<PortID> portIds = {
      masterLogicalPortIds(
          {cfg::PortType::INTERFACE_PORT, cfg::PortType::HYPER_PORT})[0],
      masterLogicalPortIds(
          {cfg::PortType::INTERFACE_PORT, cfg::PortType::HYPER_PORT})[1]};
  std::vector<int> losslessPgIds = {2};
  std::vector<int> lossyPgIds = {0};

  auto setup = [&]() {
    auto cfg = getAgentEnsemble()->getCurrentConfig();
    utility::setupPfcBuffers(
        getAgentEnsemble(), cfg, portIds, losslessPgIds, lossyPgIds);
    applyNewConfig(cfg);

    for (auto portId : portIds) {
      auto inPfc = folly::copy(getLatestPortStats(portId).inPfc_().value());
      for (auto [qos, value] : inPfc) {
        EXPECT_EQ(value, 0);
      }
    }
  };

  auto verify = [&]() {
    std::map<PortID, int64_t> inPauseBefore;
    std::map<PortID, std::map<int, int64_t>> inPfcBefore;
    for (auto portId : portIds) {
      auto stats = getLatestPortStats(portId);
      inPauseBefore[portId] = *stats.inPause_();
      auto inPfc = folly::copy(*stats.inPfc_());
      for (int pgId : losslessPgIds) {
        inPfcBefore[portId][pgId] = inPfc[pgId];
      }
    }

    sendPfcFrame(portIds, 0xFF);

    WITH_RETRIES({
      for (auto portId : portIds) {
        // We map pgIds to PFC priorities 1:1, so check for the same IDs.
        auto stats = getLatestPortStats(portId);
        auto inPfc = stats.inPfc_().value();
        ASSERT_EVENTUALLY_GE(inPfc.size(), losslessPgIds.size());
        for (int pgId : losslessPgIds) {
          EXPECT_EVENTUALLY_EQ(inPfc[pgId], inPfcBefore[portId][pgId] + 1);
        }
        EXPECT_EVENTUALLY_EQ(*stats.inPause_(), inPauseBefore[portId]);
      }
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

class AgentPfcWatchdogGranularityTest : public AgentPfcTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::PFC, ProductionFeature::PFC_WATCHDOG_GRANULARITY};
  }
};

TEST_F(AgentPfcWatchdogGranularityTest, verifyPfcWatchdogTimerGranularity) {
  auto portId = masterLogicalPortIds(
      {cfg::PortType::INTERFACE_PORT, cfg::PortType::HYPER_PORT})[0];

  auto setup = [&]() {};
  auto verify = [&]() {
    // Populate PFC and watchdog configs.
    auto cfg = getAgentEnsemble()->getCurrentConfig();
    utility::setupPfcBuffers(getAgentEnsemble(), cfg, {portId}, {2}, {0});
    auto portCfg = utility::findCfgPort(cfg, portId);
    cfg::PfcWatchdog pfcWatchdog;
    pfcWatchdog.recoveryAction() = cfg::PfcWatchdogRecoveryAction::NO_DROP;
    pfcWatchdog.recoveryTimeMsecs() = 1000;
    portCfg->pfc().ensure().watchdog() = pfcWatchdog;

    // Try different combinations of granularity and detection time.
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    // TH3 does not support granularity of 1ms
    if (asic->getAsicType() != cfg::AsicType::ASIC_TYPE_TOMAHAWK3) {
      cfg.switchSettings()->pfcWatchdogTimerGranularityMsec() = 1;
      portCfg->pfc().ensure().watchdog().ensure().detectionTimeMsecs() = 5;
      applyNewConfig(cfg);
    }

    cfg.switchSettings()->pfcWatchdogTimerGranularityMsec() = 10;
    portCfg->pfc().ensure().watchdog().ensure().detectionTimeMsecs() = 150;
    applyNewConfig(cfg);

    cfg.switchSettings()->pfcWatchdogTimerGranularityMsec() = 100;
    portCfg->pfc().ensure().watchdog().ensure().detectionTimeMsecs() = 200;
    applyNewConfig(cfg);
  };

  verifyAcrossWarmBoots(setup, verify);
}

class AgentPfcCaptureTest : public AgentPfcTest {
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {
        ProductionFeature::PFC,
        ProductionFeature::PFC_CAPTURE,
    };
  }
};

TEST_F(AgentPfcCaptureTest, verifyPfcLoopback) {
  std::vector<PortID> portIds = {masterLogicalPortIds(
      {cfg::PortType::INTERFACE_PORT, cfg::PortType::HYPER_PORT})[0]};
  std::vector<int> losslessPgIds = {2};
  std::vector<int> lossyPgIds = {0};

  auto setup = [&]() {
    auto cfg = getAgentEnsemble()->getCurrentConfig();
    utility::setupPfcBuffers(
        getAgentEnsemble(), cfg, portIds, losslessPgIds, lossyPgIds);
    utility::addPuntPfcPacketAcl(
        cfg, utility::getCoppMidPriQueueId(getAgentEnsemble()->getL3Asics()));
    applyNewConfig(cfg);

    for (const auto& switchId : getSw()->getHwAsicTable()->getSwitchIDs()) {
      std::string out;
      // Start with a blank command first. This is because, sometimes first
      // diag command doesn't work in some SDK versions and in FBOSS OSS
      getAgentEnsemble()->runDiagCommand("\n", out, switchId);
      // The registers that disable reserved-MAC filtering and flow-control
      // packet dropping (so a PFC frame can loop back) live in different
      // hardware blocks on Q4D than on the other (XGS / legacy DNX) ASICs, so
      // use the Q4D-specific register names there.
      auto asicType =
          getSw()->getHwAsicTable()->getHwAsicIf(switchId)->getAsicType();
      std::string diagCmds = (asicType == cfg::AsicType::ASIC_TYPE_QUMRAN4D)
          ? "m PT100_DC3PORT_DC3MAC_RSV_MASK MASK=0\n"
            "m NCURX_RX_MLF_DROP_FC_PKTS RX_DROP_FC_PKTS=0\n"
            "g PT100_DC3PORT_DC3MAC_RSV_MASK\n"
            "g NCURX_RX_MLF_DROP_FC_PKTS\n"
          : "m DC3MAC_RSV_MASK MASK=0\n"
            "m NBU_RX_MLF_DROP_FC_PKTS RX_DROP_FC_PKTS=0\n"
            "g DC3MAC_RSV_MASK\n"
            "g NBU_RX_MLF_DROP_FC_PKTS\n";
      getAgentEnsemble()->runDiagCommand(diagCmds, out, switchId);
      XLOG(DBG3) << "==== Diag command output ====\n"
                 << out << "\n==== End output ====";
      getAgentEnsemble()->runDiagCommand("quit\n", out, switchId);
    }
  };

  auto verify = [&]() {
    utility::SwSwitchPacketSnooper snooper(getSw(), "snooper");
    sendPfcFrame(portIds, 0xFF);

    WITH_RETRIES({
      auto frameRx = snooper.waitForPacket(1);
      ASSERT_EVENTUALLY_TRUE(frameRx.has_value());

      // Received packet should be unmodified.
      folly::io::Cursor cursor(frameRx->get());
      EthHdr ethHdr(cursor); // Consume ethernet header
      EXPECT_EQ(cursor.readBE<uint16_t>(), 0x0101); // PFC opcode
      EXPECT_EQ(cursor.readBE<uint16_t>(), 0x00FF); // PFC class vector
      for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(cursor.readBE<uint16_t>(), 0x00F0); // PFC quanta
      }
    });
  };

  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
