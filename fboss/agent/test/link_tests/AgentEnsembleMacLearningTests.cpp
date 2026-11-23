// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/link_tests/AgentEnsembleLinkTest.h"
#include "fboss/agent/test/utils/LoadBalancerTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"

using namespace ::testing;
using namespace facebook::fboss;

namespace {
constexpr int kMaxRetries{60};
constexpr int kL2AgeTimer{5};
constexpr int kL2LearnDelay{1};
} // unnamed namespace

class AgentEnsembleMacLearningTest : public AgentEnsembleLinkTest {
 private:
  cfg::SwitchConfig initialConfig(const AgentEnsemble& ensemble) override {
    XLOG(DBG2) << "setup up initial config for sw l2 learning mode, "
               << "since changing mode is not fully supported on SAI";
    overrideL2LearningConfig(true, kL2AgeTimer);
    return AgentEnsembleLinkTest::initialConfig(ensemble);
  }

  std::vector<link_test_production_features::LinkTestProductionFeature>
  getProductionFeatures() const override {
    return {
        link_test_production_features::LinkTestProductionFeature::L2_LINK_TEST,
        link_test_production_features::LinkTestProductionFeature::MAC_LEARNING};
  }

 public:
  void waitForL2EntryAgedOut(MacAddress macAddr) {
    WITH_RETRIES_N_TIMED(60, std::chrono::seconds(1), {
      std::vector<L2EntryThrift> l2Entries;
      facebook::fboss::ThriftHandler handler(getSw());
      handler.getL2Table(l2Entries);
      bool found = false;
      for (const auto& entry : l2Entries) {
        if (*entry.mac() == macAddr.toString()) {
          found = true;
          break;
        }
      }
      XLOG(DBG2) << "Stale L2 entry for " << macAddr
                 << " in HW: " << (found ? "present" : "absent");
      EXPECT_EVENTUALLY_FALSE(found);
    });
  }

  void updateL2Aging(int ageout) {
    getSw()->updateStateBlocking("update L2 aging", [ageout](auto state) {
      auto newState = state->clone();
      auto switchSettings =
          utility::getFirstNodeIf(newState->getSwitchSettings())
              ->modify(&newState);
      switchSettings->setL2AgeTimerSeconds(ageout);
      return newState;
    });
  }

  void txPacket(MacAddress macAddr, VlanID vlan, PortID port) {
    auto txPacket = utility::makeEthTxPacket(
        getSw(),
        vlan,
        macAddr,
        MacAddress::BROADCAST,
        ETHERTYPE::ETHERTYPE_LLDP);
    XLOG(DBG2) << "send packet with vlan " << vlan << " mac addr " << macAddr
               << " out of port " << port;
    getAgentEnsemble()->ensureSendPacketOutOfPort(std::move(txPacket), port);
  }

  void verifyL2EntryLearned(MacAddress macAddr, VlanID vlanId) {
    auto l2EntryLearned = [&](const std::shared_ptr<SwitchState>& state) {
      auto vlan = state->getVlans()->getNode(vlanId);
      auto* macTable = vlan->getMacTable().get();
      auto node = macTable->getMacIf(macAddr);
      XLOG(DBG2) << "found l2 entry node for " << macAddr << " and vlan "
                 << vlanId << ": " << (node != nullptr);
      return node != nullptr;
    };
    WITH_RETRIES({
      EXPECT_EVENTUALLY_TRUE(
          waitForSwitchStateCondition(l2EntryLearned, kMaxRetries));
    });
  }

  void verifyL2EntryValidated(PortID txPort, MacAddress srcMac) {
    // send packets whose src mac matches L2 entry and verify no drops,
    // if L2 entry is in pending state, these packets would be dropped
    // Only use ports on the same switch as txPort for ECMP group
    auto txPortSwitchId = getSw()->getScopeResolver()->scope(txPort).switchId();
    auto ecmpPorts = getSingleVlanOrRoutedCabledPorts(txPortSwitchId);
    XLOG(DBG2) << "Verify L2Entry ecmpPorts.size()=" << ecmpPorts.size()
               << " on switchId=" << txPortSwitchId << " (txPort=" << txPort
               << ", srcMac=" << srcMac << ")";

    if (ecmpPorts.empty()) {
      FAIL() << "verifyL2EntryValidated: No ECMP ports found on the same "
             << "switch as txPort " << txPort << " (switchId=" << txPortSwitchId
             << "). Cannot verify L2 entry.";
    }

    auto switchId = scope(ecmpPorts);
    utility::EcmpSetupTargetedPorts6 ecmp6(
        getSw()->getState(),
        getSw()->needL2EntryForNeighbor(),
        getSw()->getLocalMac(switchId),
        RouterID(0),
        false,
        {cfg::PortType::INTERFACE_PORT, cfg::PortType::MANAGEMENT_PORT});
    if (getSw()->getHwAsicTable()->isFeatureSupported(
            switchId, HwAsic::Feature::NEXTHOP_TTL_DECREMENT_DISABLE)) {
      programDefaultRouteWithDisableTTLDecrement(ecmpPorts, ecmp6);
    } else {
      programDefaultRoute(ecmpPorts, ecmp6);
      utility::disableTTLDecrements(getSw(), ecmpPorts);
    }
    // wait long enough for all L2 entries learned/validated, port stats
    // updated sleep override
    sleep(5);
    // get discards before pump traffic
    std::map<std::string, HwPortStats> portStats;
    getSw()->getAllHwPortStats(portStats);
    int maxDiscards = 0;
    for (auto [port, stats] : portStats) {
      auto inDiscards = *stats.inDiscards_();
      XLOG(DBG2) << "Port: " << port << " in discards: " << inDiscards
                 << " in bytes: " << *stats.inBytes_()
                 << " out bytes: " << *stats.outBytes_() << " at timestamp "
                 << *stats.timestamp_();
      if (inDiscards > maxDiscards) {
        maxDiscards = inDiscards;
      }
    }
    utility::pumpTraffic(
        true,
        utility::getAllocatePktFn(getSw()),
        utility::getSendPktFunc(getSw()),
        getSw()->getLocalMac(switchId),
        getSw()->getState()->getVlans()->getFirstVlanID(),
        txPort,
        255,
        10000,
        srcMac);
    // no additional discards after pump traffic
    assertNoInDiscards(maxDiscards);
  }
};

TEST_F(AgentEnsembleMacLearningTest, l2EntryFlap) {
  addVerifiedProductionFeatures(
      {link_test_production_features::LinkTestProductionFeature::MAC_LEARNING});
  auto verify = [this]() {
    auto macAddr = MacAddress("02:00:00:00:00:05");
    // After warm boot, the ASIC may still have a stale FDB entry for this
    // MAC from the cold boot run.
    // Wait for the stale entry to age out before proceeding.
    waitForL2EntryAgedOut(macAddr);
    auto connectedPair = *getConnectedPairs().begin();
    auto txPort = connectedPair.first;
    auto rxPort = connectedPair.second;
    addTestedPort(txPort);
    auto cfg = getSw()->getConfig();
    auto portCfg = std::find_if(
        cfg.ports()->begin(), cfg.ports()->end(), [&rxPort](auto& p) {
          return PortID(*p.logicalID()) == rxPort;
        });
    auto vlan = VlanID(*portCfg->ingressVlan());
    txPacket(macAddr, vlan, txPort);
    verifyL2EntryLearned(macAddr, vlan);

    getSw()->getUpdateEvb()->runInFbossEventBaseThread([]() {
      XLOG(DBG2) << "Pause state update evb thread";
      // sleep override
      sleep(kL2AgeTimer + kL2LearnDelay);
    });
    // sleep override
    sleep(kL2AgeTimer);
    // previous learnt L2 entry should aged out by now

    txPacket(macAddr, vlan, txPort);
    // disable L2 aging
    updateL2Aging(0);

    // wait some time for all queued L2 entry state updates processed
    // sleep override
    sleep(kL2LearnDelay);

    // Send one more packet, L2 entry should be learned again
    txPacket(macAddr, vlan, txPort);

    verifyL2EntryLearned(macAddr, vlan);
    std::vector<L2EntryThrift> l2Entries;
    facebook::fboss::ThriftHandler handler(getSw());
    handler.getL2Table(l2Entries);

    WITH_RETRIES({
      bool foundL2Entry = false;
      for (auto& l2Entry : l2Entries) {
        if (*l2Entry.mac() == macAddr.toString()) {
          XLOG(DBG2) << "L2 entry state is "
                     << (*l2Entry.l2EntryType() ==
                                 L2EntryType::L2_ENTRY_TYPE_PENDING
                             ? "pending"
                             : "validated");
          foundL2Entry = true;
          EXPECT_EVENTUALLY_TRUE(
              *l2Entry.l2EntryType() == L2EntryType::L2_ENTRY_TYPE_VALIDATED);
        }
      }
      EXPECT_EVENTUALLY_TRUE(foundL2Entry);
    });
    verifyL2EntryValidated(txPort, macAddr);
  };
  verifyAcrossWarmBoots([]() {}, verify);
}
