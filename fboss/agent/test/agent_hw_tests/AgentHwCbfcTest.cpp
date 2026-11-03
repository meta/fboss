/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/if/gen-cpp2/AgentHwTestCtrl.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/ResourceLibUtil.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PacketTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"

#include <fmt/core.h>
#include <folly/logging/xlog.h>

namespace facebook::fboss {

class AgentHwCbfcTest : public AgentHwTest {
 public:
  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    return {ProductionFeature::CREDIT_BASED_FLOW_CONTROL};
  }

 protected:
  static constexpr auto kCbfcConfigName = "cbfc_default";
  // The two lossless classes SUSWs run today: pg2 rdma, pg6 monitoring.
  static constexpr int16_t kRdmaVc = 2;
  static constexpr int16_t kMonitoringVc = 6;
  // 36 and 12 cells. brcm-sai reads reserved credits as bytes (/56), so 36
  // alone programs 0 cells. Back to cells once CS00012481990 fixes the unit.
  static constexpr int64_t kRdmaReservedCredits = 36 * 56;
  static constexpr int64_t kMonitoringReservedCredits = 12 * 56;
  // 8192 cells, in bytes for the same reason. 8192 alone programs 146 cells.
  static constexpr int64_t kSenderCreditLimit = 8192 * 56;
  // The TU1 receiver's values, so a TU1-to-TU1 link counts credits alike.
  static constexpr int16_t kSenderCreditSize = 56;
  static constexpr int16_t kSenderPacketOverhead = 16;
  inline static const std::map<int16_t, int16_t> kVcMap{
      {kRdmaVc, kRdmaVc},
      {kMonitoringVc, kMonitoringVc}};

  utility::PortVcInfo getPortVcInfo(PortID portId) {
    auto switchId =
        getAgentEnsemble()->scopeResolver().scope(portId).switchId();
    auto client = getAgentEnsemble()->getHwAgentTestClient(switchId);
    utility::PortVcInfo vcInfo;
    client->sync_getPortVcInfo(vcInfo, portId);
    return vcInfo;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentHwTest::initialConfig(ensemble);
    if (ensemble.getSw()->getHwAsicTable()->isFeatureSupportedOnAllAsic(
            HwAsic::Feature::CBFC)) {
      addCbfcConfig(cfg, ensemble);
    }
    return cfg;
  }

  void addCbfcConfig(cfg::SwitchConfig& cfg, const AgentEnsemble& ensemble)
      const {
    cfg::PortVcConfig rdma;
    rdma.id() = kRdmaVc;
    rdma.name() = "rdma";
    rdma.senderEnable() = true;
    rdma.receiverEnable() = true;
    rdma.reservedCreditSize() = kRdmaReservedCredits;

    cfg::PortVcConfig monitoring;
    monitoring.id() = kMonitoringVc;
    monitoring.name() = "monitoring";
    monitoring.senderEnable() = true;
    monitoring.receiverEnable() = true;
    monitoring.reservedCreditSize() = kMonitoringReservedCredits;

    cfg::CbfcConfig cbfc;
    cbfc.virtualChannels() = {rdma, monitoring};
    cbfc.senderCreditLimit() = kSenderCreditLimit;
    cbfc.senderCreditSize() = kSenderCreditSize;
    cbfc.senderPacketOverhead() = kSenderPacketOverhead;
    cfg.cbfcConfigs() = {{kCbfcConfigName, cbfc}};

    // Traffic class and queue N map to VC N, as on SUSWs.
    utility::addOlympicQosMaps(cfg, ensemble.getL3Asics());
    for (auto& policy : *cfg.qosPolicies()) {
      auto& qosMap = policy.qosMap().ensure();
      qosMap.trafficClassToVcId() = kVcMap;
      qosMap.queueToVcId() = kVcMap;
    }

    for (const auto& portId : ensemble.masterLogicalInterfacePortIds()) {
      auto portCfg = utility::findCfgPort(cfg, portId);
      portCfg->cbfcConfigName() = kCbfcConfigName;
    }
  }

  void verifyCbfcProgrammed();
  void sendPacket(uint8_t dscp);
};

// Routed once out of the port the default route points to, through the queue
// its DSCP maps to. On a CBFC port that send needs credit from the peer.
void AgentHwCbfcTest::sendPacket(uint8_t dscp) {
  auto intfMac =
      getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
  auto srcMac = utility::MacAddressGenerator().get(intfMac.u64HBO() + 1);
  auto txPacket = utility::makeUDPTxPacket(
      getSw(),
      getVlanIDForTx(),
      srcMac,
      intfMac,
      folly::IPAddressV6(kTestSrcIpV6),
      folly::IPAddressV6(kTestDstIpV6),
      kTestSrcPort,
      kTestDstPort,
      dscp << 2, // the low two bits are ECN
      255);
  sendPacketSwitchedAsync(std::move(txPacket));
}

void AgentHwCbfcTest::verifyCbfcProgrammed() {
  auto setup = []() {};
  auto verify = [&]() {
    auto state = getProgrammedState();
    auto portStats = getLatestPortStats(masterLogicalInterfacePortIds());
    for (const auto& portId : masterLogicalInterfacePortIds()) {
      auto port = state->getPorts()->getNodeIf(portId);
      ASSERT_NE(port, nullptr);
      ASSERT_TRUE(port->getCbfcConfigName().has_value());
      EXPECT_EQ(*port->getCbfcConfigName(), kCbfcConfigName);
      EXPECT_EQ(port->getCbfcSenderCreditLimit(), kSenderCreditLimit);

      // Read the virtual channels back from hardware. This confirms the SAI
      // objects still exist, are bound to the port, and carry the last-applied
      // values after the warm boot -- not just that the SwitchState intent
      // survived.
      auto vcInfo = getPortVcInfo(portId);

      // Logged first, before any assertion that could abort the loop. These
      // are read-only and derived by hardware from the MMU carving; capturing
      // them is the main value of a run. Unset means the SDK refused the read,
      // which is a result rather than a failure.
      auto show = [](const auto& opt) {
        return opt.has_value() ? std::to_string(*opt)
                               : std::string("<not readable>");
      };
      XLOG(DBG2) << "Port " << portId << " CBFC native: creditSize="
                 << show(vcInfo.receiverNativeCreditSize()) << " pktOverhead="
                 << show(vcInfo.receiverNativePacketOverhead())
                 << " totalCredits="
                 << show(vcInfo.receiverNativeTotalCredits());
      XLOG(DBG2) << "Port " << portId << " CBFC sender: creditSize="
                 << show(vcInfo.senderCreditSize())
                 << " pktOverhead=" << show(vcInfo.senderPacketOverhead());
      auto showMap = [](const auto& opt) {
        if (!opt.has_value()) {
          return std::string("<not readable>");
        }
        std::string out;
        for (const auto& [from, to] : *opt) {
          out += fmt::format("{}->{} ", from, to);
        }
        return out.empty() ? std::string("<none>") : out;
      };
      XLOG(DBG2) << "Port " << portId << " CBFC qos maps: tcToVc="
                 << showMap(vcInfo.trafficClassToVcId())
                 << " queueToVc=" << showMap(vcInfo.queueToVcId());
      for (const auto& vc : *vcInfo.virtualChannels()) {
        XLOG(DBG2) << "Port " << portId << " VC "
                   << static_cast<int>(*vc.index()) << " nativeCreditLimit="
                   << show(vc.receiverNativeCreditLimit());
      }

      // From the SDK sender table. 32 / 0 means the sender was left at the
      // values brcm-sai writes for fields it was never given.
      EXPECT_EQ(vcInfo.senderCreditSize(), kSenderCreditSize);
      EXPECT_EQ(vcInfo.senderPacketOverhead(), kSenderPacketOverhead);
      EXPECT_EQ(vcInfo.trafficClassToVcId(), kVcMap);
      EXPECT_EQ(vcInfo.queueToVcId(), kVcMap);

      ASSERT_EQ(vcInfo.virtualChannels()->size(), 2);

      std::map<int, utility::VcInfo> byIndex;
      for (const auto& vc : *vcInfo.virtualChannels()) {
        byIndex[*vc.index()] = vc;
      }
      ASSERT_TRUE(byIndex.contains(kRdmaVc));
      ASSERT_TRUE(byIndex.contains(kMonitoringVc));

      for (const auto& [index, vc] : byIndex) {
        EXPECT_NE(*vc.vcId(), 0);
        EXPECT_TRUE(*vc.senderEnable());
        EXPECT_TRUE(*vc.receiverEnable());
        EXPECT_TRUE(
            apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
                vc.reservedCreditSize()));
        EXPECT_NE(*vc.creditProfileId(), 0);
      }
      EXPECT_EQ(*byIndex[kRdmaVc].reservedCreditSize(), kRdmaReservedCredits);
      EXPECT_EQ(
          *byIndex[kMonitoringVc].reservedCreditSize(),
          kMonitoringReservedCredits);

      // Distinct reservations must not collapse onto one profile object.
      EXPECT_NE(
          *byIndex[kRdmaVc].creditProfileId(),
          *byIndex[kMonitoringVc].creditProfileId());

      // Counters read 0 without induced traffic, so presence is the assertion.
      // Everything is logged before the first EXPECT so a run still yields
      // readings even when one fails.
      const auto& stats = portStats.at(portId);
      XLOG(DBG2) << "Port " << portId << " CBFC port counters:"
                 << " ccUpdateTx=" << show(stats.cbfcCcUpdateTx_())
                 << " cfUpdateTx=" << show(stats.cbfcCfUpdateTx_())
                 << " cfUpdateRx=" << show(stats.cbfcCfUpdateRx_());
      auto showVcMap = [&](folly::StringPiece name, const auto& vcMap) {
        for (const auto& [vcIndex, value] : vcMap) {
          XLOG(DBG2) << "Port " << portId << " VC " << vcIndex << " " << name
                     << "=" << value;
        }
        if (vcMap.empty()) {
          XLOG(DBG2) << "Port " << portId << " " << name << " <empty>";
        }
      };
      showVcMap("senderCreditsConsumed", *stats.cbfcVcSenderCreditsConsumed_());
      showVcMap("senderCreditsFreed", *stats.cbfcVcSenderCreditsFreed_());
      showVcMap(
          "receiverCreditsConsumed", *stats.cbfcVcReceiverCreditsConsumed_());
      showVcMap("receiverCreditsFreed", *stats.cbfcVcReceiverCreditsFreed_());

      EXPECT_TRUE(stats.cbfcCcUpdateTx_().has_value());
      EXPECT_TRUE(stats.cbfcCfUpdateTx_().has_value());
      EXPECT_TRUE(stats.cbfcCfUpdateRx_().has_value());
      // Only the receiver-consumed map is asserted. The other three virtual
      // channel counters are Tomahawk Ultra 1 B0 stepping only and are not
      // requested (see SaiVirtualChannelTraits::cbfcVcStats), so their maps
      // stay empty on A0 -- the logging above is how we find out if that
      // changes.
      EXPECT_TRUE(stats.cbfcVcReceiverCreditsConsumed_()->contains(kRdmaVc));
      EXPECT_TRUE(
          stats.cbfcVcReceiverCreditsConsumed_()->contains(kMonitoringVc));
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

TEST_F(AgentHwCbfcTest, verifyCbfcConfig) {
  verifyCbfcProgrammed();
}

// A MAC loopback does not return CF_Update to the port (RX_CBFC_CF_OS stays 0)
// and TU1 rejects PHY loopback, so traffic tests run with no loopback. The CBFC
// port needs a cable or an optic that loops it back to itself.
class AgentHwCbfcTrafficTest : public AgentHwCbfcTest {
 protected:
  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto cfg = AgentHwCbfcTest::initialConfig(ensemble);
    for (const auto& portId : ensemble.masterLogicalInterfacePortIds()) {
      utility::findCfgPort(cfg, portId)->loopbackMode() =
          cfg::PortLoopbackMode::NONE;
    }
    return cfg;
  }
};

// Traffic on both lossless VCs leaves a CBFC port. Queue N maps to VC N.
TEST_F(AgentHwCbfcTrafficTest, verifyLosslessVcTraffic) {
  auto setup = [this]() {
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    resolveNeighborAndProgramRoutes(ecmpHelper, 1);
  };
  auto verify = [this]() {
    utility::EcmpSetupAnyNPorts6 ecmpHelper(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
    auto portId = ecmpHelper.ecmpPortDescriptorAt(0).phyPortID();
    const auto& queueToDscp = utility::kOlympicQueueToDscp();
    std::map<int, std::vector<uint8_t>> vcQueueToDscp{
        {kRdmaVc, {queueToDscp.at(kRdmaVc).front()}},
        {kMonitoringVc, {queueToDscp.at(kMonitoringVc).front()}}};
    utility::sendPktAndVerifyQueueHit(
        vcQueueToDscp,
        getSw(),
        [this](int dscp) { sendPacket(dscp); },
        portId,
        std::nullopt);
  };
  verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
