// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "folly/ScopeGuard.h"
#include "folly/container/F14Map.h"
#include "folly/executors/FunctionScheduler.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "thrift/lib/cpp2/test/Matcher.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/HwSwitchThriftClientTable.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types.h"
#include "fboss/agent/if/gen-cpp2/FbossHwCtrlAsyncClient.h"
#include "fboss/agent/if/gen-cpp2/highfreq_types.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/AgentTestAddressConstants.h"
#include "fboss/agent/test/utils/ConfigUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/test/utils/OlympicTestUtils.h"
#include "fboss/agent/test/utils/PfcTestUtils.h"
#include "fboss/agent/test/utils/QosTestUtils.h"
#include "fboss/agent/types.h"
#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss {

using testing::_;
using testing::Contains;
using testing::Eq;
using testing::Gt;
using testing::Pair;

using apache::thrift::test::ThriftField;
namespace field = apache::thrift::ident;

class AgentHighFrequencyStatsTest : public AgentHwTest {
 public:
  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_enable_high_frequency_stats_polling = true;
  }

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
    return {
        ProductionFeature::PFC,
        ProductionFeature::HIGH_FREQUENCY_CPU_POLLING_STATS};
  }

 protected:
  // Returns a HighFrequencyStatsCollectionConfig with include device watermark
  // set to false, a filter config, and a wait duration of 20ms for a collection
  // duration of 200ms.
  HighFrequencyStatsCollectionConfig getCollectionConfig() {
    HighFrequencyStatsCollectionConfig config{};
    // Poll every 20ms for 200 ms. Note that the current minimum polling
    // interval is 20ms.
    config.schedulerConfig()->statsWaitDurationInMicroseconds() = 20000;
    config.schedulerConfig()->statsCollectionDurationInMicroseconds() = 200000;
    config.statsConfig()->portStatsConfig()->filterConfig().ensure();
    config.statsConfig()->includeDeviceWatermark() = false;
    return config;
  }

  // Returns a HfPortStatsCollectionConfig with the no stats set.
  HfPortStatsCollectionConfig getPortStatsCollectionConfig() {
    HfPortStatsCollectionConfig config{};
    config.includePfcRx() = false;
    config.includePfcTx() = false;
    config.includePgWatermark() = false;
    config.includeQueueWatermark() = false;
    return config;
  }

  // Returns a HighFrequencyStatsCollectionConfig with device watermark set to
  // false and assigns portStatsConfig for the portNames in filterConfig.
  HighFrequencyStatsCollectionConfig getCollectionConfigWithPortFilter(
      std::span<const std::string> portNames,
      const HfPortStatsCollectionConfig& portStatsConfig) {
    HighFrequencyStatsCollectionConfig config{getCollectionConfig()};
    for (const std::string& portName : portNames) {
      config.statsConfig()
          ->portStatsConfig()
          ->filterConfig()
          .value()[portName] = portStatsConfig;
    }
    return config;
  }

  void sendPfcFrame(const std::vector<PortID>& portIds, uint8_t classVector) {
    for (const PortID& portId : portIds) {
      getSw()->sendPacketOutOfPortAsync(
          utility::makePfcFramePacket(*getAgentEnsemble(), classVector),
          portId);
    }
  }

  const folly::IPAddressV6 kDestIp() const {
    static const folly::IPAddressV6 destIp{"2620:0:1cfe:face:b00c::3"};
    return destIp;
  }

  // Sends UDP packets to the destination IP. The UDP packets are sent on ECN1
  // queue (queue 2) as the queue watermarks for production are measured for
  // queue 2.
  void sendUdpPkts(
      const folly::IPAddressV6& dstIp,
      int cnt = 100,
      int payloadSize = 6000) {
    folly::MacAddress intfMac =
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState());
    const int queueId{
        utility::getOlympicQueueId(utility::OlympicQueueType::ECN1)};
    const std::map<int, std::vector<uint8_t>> kOlympicQueueToDscp{
        utility::kOlympicQueueToDscp()};
    auto dscpsIt = kOlympicQueueToDscp.find(queueId);
    ASSERT_NE(dscpsIt, kOlympicQueueToDscp.end());
    for (int i = 0; i < cnt; i++) {
      sendPacketSwitchedAsync(
          utility::makeUDPTxPacket(
              getSw(),
              getVlanIDForTx() /*vlan*/,
              utility::MacAddressGenerator().get(
                  intfMac.u64HBO() + 1) /*srcMac*/,
              intfMac /*dstMac*/,
              folly::IPAddressV6(kTestSrcIpV6) /*srcIp*/,
              dstIp,
              kTestSrcPort /*srcPort*/,
              kTestDstPort /*dstPort*/,
              static_cast<uint8_t>(dscpsIt->second.at(0) << 2) /*dscp*/,
              255 /*hopLimit*/,
              std::vector<uint8_t>(payloadSize, 0xff) /*payload*/));
    }
  }

  void setupEcmpTraffic(
      const PortID& portId,
      const folly::IPAddressV6& addr,
      const folly::MacAddress& nextHopMac,
      bool disableTtlDecrement = false) {
    utility::EcmpSetupTargetedPorts6 ecmpHelper{
        getProgrammedState(), getSw()->needL2EntryForNeighbor(), nextHopMac};
    const PortDescriptor port{portId};
    applyNewState([&](const std::shared_ptr<SwitchState>& state) {
      return ecmpHelper.resolveNextHops(state, {port});
    });
    SwSwitchRouteUpdateWrapper routeUpdater = getSw()->getRouteUpdater();
    ecmpHelper.programRoutes(
        &routeUpdater,
        {port},
        {RoutePrefixV6(addr, 128)},
        {},
        std::nullopt,
        disableTtlDecrement ? std::make_optional(true) : std::nullopt);
    if (disableTtlDecrement) {
      utility::disablePortTTLDecrementIfSupported(
          getAgentEnsemble(), ecmpHelper.getRouterId(), ecmpHelper.nhop(port));
    }
  }

  // Configures the switch with olympic queue config and qos maps.
  void configureOlympicSwitchConfig() {
    AgentEnsemble& ensemble = *getAgentEnsemble();
    cfg::SwitchConfig config = ensemble.getCurrentConfig();
    utility::addOlympicQueueConfig(&config, ensemble.getL3Asics());
    utility::addOlympicQosMaps(config, ensemble.getL3Asics());
    utility::setTTLZeroCpuConfig(ensemble.getL3Asics(), config);
    applyNewConfig(config);
  }

  void configureSwitchConfigAndOneLoopbackPort(const PortID& portId) {
    configureOlympicSwitchConfig();
    setupEcmpTraffic(
        portId,
        kDestIp(),
        getMacForFirstInterfaceWithPortsForTesting(getProgrammedState()),
        true /*disableTtlDecrement*/);
  }
};

// Test the PFC counters for high frequency stats collection. Verify that the
// PFC counters are zero at cold boot. Set up PFC on two ports (logical
// interface ports 0 and 1). Set up a thrift client to call the high frequency
// stats collection API on the hw agent. Send PFC frames on both ports and run
// the high frequency stats collection job simultaneously. Call the get high
// frequency stats API using a port filter config for the ports with PFC. Verify
// that multiple time series stats are returned. Verify that the PFC counters
// are non-zero for all the ports and all the lossless PG IDs.
TEST_F(AgentHighFrequencyStatsTest, PfcTest) {
  std::vector<PortID> portIds{
      masterLogicalInterfacePortIds()[0], masterLogicalInterfacePortIds()[1]};
  std::vector<int> losslessPgIds{2};
  std::vector<int> lossyPgIds{0};

  auto setup = [&]() {
    cfg::SwitchConfig cfg = getAgentEnsemble()->getCurrentConfig();
    utility::setupPfcBuffers(
        getAgentEnsemble(), cfg, portIds, losslessPgIds, lossyPgIds);
    applyNewConfig(cfg);

    for (const auto& [portId, stats] : getLatestPortStats(portIds)) {
      for (const auto& [qos, value] : stats.inPfc_().value()) {
        EXPECT_EQ(value, 0);
      }
    }
  };

  auto verify = [&]() {
    // Collect PFC counters before test
    const std::map<PortID, HwPortStats> initialStats{
        getLatestPortStats(portIds)};

    apache::thrift::Client<FbossHwCtrl>* client =
        getSw()->getHwSwitchThriftClientTable()->getClient(SwitchID(0));

    const std::shared_ptr<SwitchState> state = getProgrammedState();
    ASSERT_TRUE(state);

    // Maintain a map of port ID to port name
    std::map<PortID, std::string> port2Name{};
    std::vector<std::string> portNames{};
    for (const PortID& portId : portIds) {
      const std::shared_ptr<Port> port = state->getPort(portId);
      ASSERT_TRUE(port) << "No port found for portId: " << portId;
      port2Name[portId] = port->getName();
      portNames.push_back(port->getName());
    }

    HfPortStatsCollectionConfig portStatsConfig{getPortStatsCollectionConfig()};
    portStatsConfig.includePfcRx() = true;
    portStatsConfig.includePfcTx() = true;
    HighFrequencyStatsCollectionConfig config{
        getCollectionConfigWithPortFilter(portNames, portStatsConfig)};

    // Send PFC frames on all ports every 2.5ms to guarantee about 8 PFC frames
    // per collection interval.
    folly::FunctionScheduler sendPfcScheduler;
    sendPfcScheduler.addFunction(
        [&]() { sendPfcFrame(portIds, 0xFF); },
        std::chrono::microseconds(2500),
        "sendPfc");
    sendPfcScheduler.setSteady(true);

    // Start the PFC frame sending thread and the high frequency stats
    // collection job simultaneously.
    sendPfcScheduler.start();
    client->sync_startHighFrequencyStatsCollection(config);

    // Wait for 200ms to ensure that the high frequency stats collection job
    // is complete and stop both the threads.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    client->sync_stopHighFrequencyStatsCollection();
    sendPfcScheduler.shutdown();

    // Get the high frequency stats.
    GetHighFrequencyStatsOptions options{};
    options.limit() = 1024;
    std::vector<HwHighFrequencyStats> timeseriesStats{};
    client->sync_getHighFrequencyStats(timeseriesStats, options);

    ASSERT_GE(timeseriesStats.size(), 1);

    // Verify that the PFC counters are non-zero for all the ports and all the
    // lossless PG IDs.
    for (const auto& [portId, portName] : port2Name) {
      auto it = timeseriesStats.back().portStats()->find(portName);
      ASSERT_NE(it, timeseriesStats.back().portStats()->end())
          << "No port stats found for port: " << portName;
      const std::map<int16_t, HwHighFrequencyPfcStats>& pfcStats{
          it->second.pfcStats().value()};
      ASSERT_GE(pfcStats.size(), losslessPgIds.size());
      for (int pgId : losslessPgIds) {
        auto pfcIt = pfcStats.find(pgId);
        ASSERT_NE(pfcIt, pfcStats.end());
        EXPECT_GE(pfcIt->second.inPfc().value(), 1);
      }
    }
  };
  verifyAcrossWarmBoots(setup, verify);
}

// Test device watermarks for high frequency stats collection. Set up ECMP
// traffic loop on a port. Set up a thrift client to call the high frequency
// stats collection API on the hw agent. Send traffic on the loopback port and
// run the high frequency stats collection job simultaneously. Call the get high
// frequency stats API. Verify that the device watermarks are non-zero.
TEST_F(AgentHighFrequencyStatsTest, DeviceWatermarkTest) {
  PortID loopbackPort{masterLogicalInterfacePortIds()[0]};

  auto setup = [&]() { configureSwitchConfigAndOneLoopbackPort(loopbackPort); };

  auto verify = [&]() {
    apache::thrift::Client<FbossHwCtrl>* client =
        getSw()->getHwSwitchThriftClientTable()->getClient(SwitchID(0));

    HighFrequencyStatsCollectionConfig config{getCollectionConfig()};
    config.statsConfig()->includeDeviceWatermark() = true;
    client->sync_startHighFrequencyStatsCollection(config);
    SCOPE_EXIT {
      client->sync_stopHighFrequencyStatsCollection();
    };

    // Send traffic
    sendUdpPkts(kDestIp());
    SCOPE_EXIT {
      bringDownPort(loopbackPort);
      bringUpPort(loopbackPort);
    };

    GetHighFrequencyStatsOptions options{};
    options.limit() = 1024;
    std::vector<HwHighFrequencyStats> statsList;
    // Poll the stats until at least 2 entries are present. At a 20ms polling
    // interval and a 200ms collection duration, we should have near 10 entries,
    // probably closer to 8-9 to account for the time to actually poll the
    // watermark stats.
    WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(20), {
      client->sync_getHighFrequencyStats(statsList, options);
      EXPECT_EVENTUALLY_GE(statsList.size(), 2);
    });
    EXPECT_THAT(
        statsList,
        Contains(
            ThriftField<field::itmPoolSharedWatermarkBytes>(
                Contains(Pair(_, Gt(0))))));
  };

  verifyAcrossWarmBoots(setup, verify);
}

// Test queue watermarks for high frequency stats collection. Set up ECMP
// traffic loop on a port. Set up a thrift client to call the high frequency
// stats collection API on the hw agent. Send traffic on the loopback port and
// run the high frequency stats collection job simultaneously. Call the get high
// frequency stats API using a port filter config for the port. Verify that the
// queue watermarks are non-zero.
TEST_F(AgentHighFrequencyStatsTest, QueueWatermarkTest) {
  PortID loopbackPort{masterLogicalInterfacePortIds()[0]};

  auto setup = [&]() { configureSwitchConfigAndOneLoopbackPort(loopbackPort); };

  auto verify = [&]() {
    apache::thrift::Client<FbossHwCtrl>* client =
        getSw()->getHwSwitchThriftClientTable()->getClient(SwitchID(0));

    const std::shared_ptr<Port> port =
        getProgrammedState()->getPort(loopbackPort);
    ASSERT_TRUE(port) << "No port found for portId: " << loopbackPort;
    std::vector<std::string> portNames{port->getName()};

    HfPortStatsCollectionConfig portStatsConfig{getPortStatsCollectionConfig()};
    portStatsConfig.includeQueueWatermark() = true;
    HighFrequencyStatsCollectionConfig config{
        getCollectionConfigWithPortFilter(portNames, portStatsConfig)};

    client->sync_startHighFrequencyStatsCollection(config);
    SCOPE_EXIT {
      client->sync_stopHighFrequencyStatsCollection();
    };

    // Send traffic
    sendUdpPkts(kDestIp());
    SCOPE_EXIT {
      bringDownPort(loopbackPort);
      bringUpPort(loopbackPort);
    };

    GetHighFrequencyStatsOptions options{};
    options.limit() = 1024;
    std::vector<HwHighFrequencyStats> statsList{};
    // Poll the stats until at least 2 entries are present. At a 20ms polling
    // interval and a 200ms collection duration, we should have near 10 entries,
    // probably closer to 8-9 to account for the time to actually poll the
    // watermark stats.
    WITH_RETRIES_N_TIMED(20, std::chrono::milliseconds(20), {
      client->sync_getHighFrequencyStats(statsList, options);
      EXPECT_EVENTUALLY_GE(statsList.size(), 2);
    });
    EXPECT_THAT(
        statsList,
        Contains(
            ThriftField<field::portStats>(Contains(Pair(
                Eq(port->getName()),
                ThriftField<field::queueWatermarkBytes>(Contains(Pair(
                    Eq(utility::getOlympicQueueId(
                        utility::OlympicQueueType::ECN1)),
                    Gt(0)))))))));
  };

  verifyAcrossWarmBoots(setup, verify);
}

// TODO(T233514960): Add test to verify polling regularly with increasing
// timestamps
// TODO(T233514960): Add test to verify polling polling with an accurate
// frequency
// TODO(T233514960): Add test to verify polling stops automatically after the
// stats collection duration
// TODO(T233514960): Add test to verify stop API ends stats polling
// TODO(T233514960): Add test to verify get stats collection with timestamp
// filters works as expected

} // namespace facebook::fboss
