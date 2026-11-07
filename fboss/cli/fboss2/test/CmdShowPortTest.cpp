// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/IPAddressV4.h>
#include <folly/synchronization/Baton.h>

#include <thread>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"

#include <thrift/lib/cpp2/reflection/testing.h>
#include "fboss/cli/fboss2/commands/show/port/CmdShowPort.h"
#include "fboss/cli/fboss2/commands/show/port/CmdShowPortQueue.h"
#include "fboss/cli/fboss2/commands/show/port/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#ifndef IS_OSS
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
using namespace facebook::neteng::fboss::bgp::thrift;
#endif

using namespace ::testing;

namespace facebook::fboss {

std::map<int64_t, cfg::SwitchInfo> createSwitchIdToSwitchInfo() {
  std::map<int64_t, cfg::SwitchInfo> switchIdToSwitchInfo;

  cfg::SwitchInfo switchInfo1;
  switchInfo1.switchType() = cfg::SwitchType::FABRIC;
  switchInfo1.asicType() = cfg::AsicType::ASIC_TYPE_TRIDENT2;
  switchInfo1.switchIndex() = 1;
  switchIdToSwitchInfo.insert({1, switchInfo1});

  return switchIdToSwitchInfo;
}

std::map<std::string, FabricEndpoint> createFabricConnectivity() {
  return {};
}

/*
 * Set up port test data
 */
std::map<int32_t, PortInfoThrift> createPortEntries() {
  std::map<int32_t, PortInfoThrift> portMap;

  PortInfoThrift portEntry1;
  portEntry1.portId() = 1;
  portEntry1.name() = "eth1/5/1";
  portEntry1.adminState() = PortAdminState::ENABLED;
  portEntry1.operState() = PortOperState::DOWN;
  portEntry1.speedMbps() = 100000;
  portEntry1.profileID() = "PROFILE_100G_4_NRZ_CL91_COPPER";
  portEntry1.hwLogicalPortId() = 1;
  TransceiverIdxThrift tcvr1;
  tcvr1.transceiverId() = 0;
  portEntry1.transceiverIdx() = tcvr1;
  PfcConfig pfcCfg;
  pfcCfg.tx() = true;
  pfcCfg.rx() = true;
  pfcCfg.watchdog() = true;
  portEntry1.pfc() = pfcCfg;
  portEntry1.isDrained() = false;
  portEntry1.userMetaData() = cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED;
  portEntry1.ingressAclTableName() = "AccessPolicyBlockTable";
  portEntry1.coreId() = 1;
  portEntry1.virtualDeviceId() = 1;

  PortInfoThrift portEntry2;
  portEntry2.portId() = 2;
  portEntry2.name() = "eth1/5/2";
  portEntry2.adminState() = PortAdminState::DISABLED;
  portEntry2.operState() = PortOperState::DOWN;
  portEntry2.speedMbps() = 25000;
  portEntry2.profileID() = "PROFILE_25G_1_NRZ_CL74_COPPER";
  portEntry2.hwLogicalPortId() = 2;
  TransceiverIdxThrift tcvr2;
  tcvr2.transceiverId() = 1;
  portEntry2.transceiverIdx() = tcvr2;
  portEntry2.rxPause() = true;
  portEntry2.isDrained() = false;
  portEntry2.userMetaData() = cfg::AclLookupClassPort::CLASS_PORT_UNCONSTRAINED;
  portEntry2.coreId() = 2;
  portEntry2.virtualDeviceId() = 2;

  PortInfoThrift portEntry3;
  portEntry3.portId() = 3;
  portEntry3.name() = "eth1/5/3";
  portEntry3.adminState() = PortAdminState::ENABLED;
  portEntry3.operState() = PortOperState::UP;
  portEntry3.speedMbps() = 100000;
  portEntry3.profileID() = "PROFILE_100G_4_NRZ_CL91_COPPER";
  portEntry3.hwLogicalPortId() = 3;
  TransceiverIdxThrift tcvr3;
  tcvr3.transceiverId() = 2;
  portEntry3.transceiverIdx() = tcvr3;
  portEntry3.isDrained() = false;
  portEntry3.coreId() = 3;
  portEntry3.virtualDeviceId() = 3;

  PortInfoThrift portEntry4;
  portEntry4.portId() = 8;
  portEntry4.name() = "fab402/9/1";
  portEntry4.adminState() = PortAdminState::ENABLED;
  portEntry4.operState() = PortOperState::UP;
  portEntry4.speedMbps() = 100000;
  portEntry4.profileID() = "PROFILE_100G_4_NRZ_NOFEC_COPPER";
  portEntry4.hwLogicalPortId() = 8;
  TransceiverIdxThrift tcvr4;
  tcvr4.transceiverId() = 3;
  portEntry4.transceiverIdx() = tcvr4;
#ifdef IS_OSS
  portEntry4.isDrained() = false;
#else
  portEntry4.isDrained() = true;
#endif

  PortInfoThrift portEntry5;
  portEntry5.portId() = 7;
  portEntry5.name() = "eth1/10/2";
  portEntry5.adminState() = PortAdminState::ENABLED;
  portEntry5.operState() = PortOperState::UP;
  portEntry5.speedMbps() = 100000;
  portEntry5.profileID() = "PROFILE_100G_4_NRZ_CL91_OPTICAL";
  portEntry5.hwLogicalPortId() = 7;
  TransceiverIdxThrift tcvr5;
  tcvr5.transceiverId() = 4;
  portEntry5.transceiverIdx() = tcvr5;
  portEntry5.isDrained() = false;
  portEntry5.coreId() = 5;
  portEntry5.virtualDeviceId() = 5;

  PortInfoThrift portEntry6;
  portEntry6.portId() = 9;
  portEntry6.name() = "eth1/4/1";
  portEntry6.adminState() = PortAdminState::ENABLED;
  portEntry6.operState() = PortOperState::UP;
  portEntry6.speedMbps() = 100000;
  portEntry6.profileID() = "PROFILE_100G_4_NRZ_CL91_OPTICAL";
  portEntry6.hwLogicalPortId() = 9;
  TransceiverIdxThrift tcvr6;
  tcvr6.transceiverId() = 5;
  portEntry6.transceiverIdx() = tcvr6;
  portEntry6.isDrained() = false;
  portEntry6.coreId() = 6;
  portEntry6.virtualDeviceId() = 6;

  portMap[folly::copy(portEntry1.portId().value())] = portEntry1;
  portMap[folly::copy(portEntry2.portId().value())] = portEntry2;
  portMap[folly::copy(portEntry3.portId().value())] = portEntry3;
  portMap[folly::copy(portEntry4.portId().value())] = portEntry4;
  portMap[folly::copy(portEntry5.portId().value())] = portEntry5;
  portMap[folly::copy(portEntry6.portId().value())] = portEntry6;
  return portMap;
}

std::map<int32_t, PortInfoThrift> createInvalidPortEntries() {
  std::map<int32_t, PortInfoThrift> portMap;

  // port name format <module name><module number>/<port number>/<subport
  // number>
  // missing <module number>

  PortInfoThrift portEntry1, portEntry2;
  portEntry1.portId() = 1;
  portEntry1.name() = "eth/5/1";

  portEntry2.portId() = 2;
  portEntry2.name() = "eth/5/1";

  portMap[folly::copy(portEntry1.portId().value())] = portEntry1;
  portMap[folly::copy(portEntry2.portId().value())] = portEntry2;
  return portMap;
}

/*
 * Set up transceiver test data
 */
std::map<int, TransceiverInfo> createTransceiverEntries() {
  std::map<int, TransceiverInfo> transceiverMap;

  TransceiverInfo transceiverEntry1;
  transceiverEntry1.tcvrState()->present() = true;

  TransceiverInfo transceiverEntry2;
  transceiverEntry2.tcvrState()->present() = true;

  TransceiverInfo transceiverEntry3;
  transceiverEntry3.tcvrState()->present() = false;

  TransceiverInfo transceiverEntry4;
  transceiverEntry4.tcvrState()->present() = false;

  TransceiverInfo transceiverEntry5;
  transceiverEntry5.tcvrState()->present() = true;

  TransceiverInfo transceiverEntry6;
  transceiverEntry6.tcvrState()->present() = true;

  transceiverMap[0] = transceiverEntry1;
  transceiverMap[1] = transceiverEntry2;
  transceiverMap[2] = transceiverEntry3;
  transceiverMap[3] = transceiverEntry4;
  transceiverMap[4] = transceiverEntry5;
  transceiverMap[5] = transceiverEntry6;
  return transceiverMap;
}

cli::ShowPortModel createPortModel() {
  cli::ShowPortModel model;

  cli::PortEntry entry1, entry2, entry3, entry4, entry5, entry6;
  entry1.id() = 1;
  entry1.hwLogicalPortId() = 1;
  entry1.name() = "eth1/5/1";
  entry1.adminState() = "Enabled";
  entry1.linkState() = "Down";
  entry1.activeState() = "--";
  entry1.speed() = "100G";
  entry1.profileId() = "PROFILE_100G_4_NRZ_CL91_COPPER";
  entry1.tcvrID() = 0;
  entry1.tcvrPresent() = "Present";
  entry1.numUnicastQueues() = 0;
  // when pfc exists, pause shouldn't
  entry1.pfc() = "TX RX WD";
#ifdef IS_OSS
  entry1.isDrained() = "No";
#else
  entry1.isDrained() = "Yes";
#endif
  entry1.activeErrors() = "--";
  entry1.peerSwitchDrained() = "--";
  entry1.peerPortDrainedOrDown() = "--";
  entry1.coreId() = "1";
  entry1.virtualDeviceId() = "1";
  entry1.cableLengthMeters() = "--";
  entry1.userMetaData() = "Restricted";
  entry1.ingressAclTable() = "AccessPolicyBlockTable";

  entry2.id() = 2;
  entry2.hwLogicalPortId() = 2;
  entry2.name() = "eth1/5/2";
  entry2.adminState() = "Disabled";
  entry2.linkState() = "Down";
  entry2.activeState() = "--";
  entry2.speed() = "25G";
  entry2.profileId() = "PROFILE_25G_1_NRZ_CL74_COPPER";
  entry2.tcvrID() = 1;
  entry2.tcvrPresent() = "Present";
  entry2.numUnicastQueues() = 0;
  entry2.pause() = "RX";
  entry2.isDrained() = "No";
  entry2.activeErrors() = "--";
  entry2.peerSwitchDrained() = "--";
  entry2.peerPortDrainedOrDown() = "--";
  entry2.coreId() = "2";
  entry2.virtualDeviceId() = "2";
  entry2.cableLengthMeters() = "--";
  entry2.userMetaData() = "Unconstrained";
  entry2.ingressAclTable() = "--";

  entry3.id() = 3;
  entry3.hwLogicalPortId() = 3;
  entry3.name() = "eth1/5/3";
  entry3.adminState() = "Enabled";
  entry3.linkState() = "Up";
  entry3.activeState() = "--";
  entry3.speed() = "100G";
  entry3.profileId() = "PROFILE_100G_4_NRZ_CL91_COPPER";
  entry3.tcvrID() = 2;
  entry3.tcvrPresent() = "Absent";
  entry3.numUnicastQueues() = 0;
  entry3.pause() = "";
  entry3.isDrained() = "No";
  entry3.activeErrors() = "--";
  entry3.peerSwitchDrained() = "--";
  entry3.peerPortDrainedOrDown() = "--";
  entry3.coreId() = "3";
  entry3.virtualDeviceId() = "3";
  entry3.cableLengthMeters() = "--";
  entry3.userMetaData() = "--";
  entry3.ingressAclTable() = "--";

  entry4.id() = 8;
  entry4.hwLogicalPortId() = 8;
  entry4.name() = "fab402/9/1";
  entry4.adminState() = "Enabled";
  entry4.linkState() = "Up";
  entry4.activeState() = "--";
  entry4.speed() = "100G";
  entry4.profileId() = "PROFILE_100G_4_NRZ_NOFEC_COPPER";
  entry4.tcvrID() = 3;
  entry4.tcvrPresent() = "Absent";
  entry4.numUnicastQueues() = 0;
  entry4.pause() = "";
#ifdef IS_OSS
  entry4.isDrained() = "No";
#else
  entry4.isDrained() = "Yes";
#endif
  entry4.activeErrors() = "--";
  entry4.peerSwitchDrained() = "--";
  entry4.peerPortDrainedOrDown() = "--";
  entry4.coreId() = "--";
  entry4.virtualDeviceId() = "--";
  entry4.cableLengthMeters() = "--";
  entry4.userMetaData() = "--";
  entry4.ingressAclTable() = "--";

  entry5.id() = 7;
  entry5.hwLogicalPortId() = 7;
  entry5.name() = "eth1/10/2";
  entry5.adminState() = "Enabled";
  entry5.linkState() = "Up";
  entry5.activeState() = "--";
  entry5.speed() = "100G";
  entry5.profileId() = "PROFILE_100G_4_NRZ_CL91_OPTICAL";
  entry5.tcvrID() = 4;
  entry5.tcvrPresent() = "Present";
  entry5.numUnicastQueues() = 0;
  entry5.pause() = "";
  entry5.isDrained() = "No";
  entry5.activeErrors() = "--";
  entry5.peerSwitchDrained() = "--";
  entry5.peerPortDrainedOrDown() = "--";
  entry5.coreId() = "5";
  entry5.virtualDeviceId() = "5";
  entry5.cableLengthMeters() = "--";
  entry5.userMetaData() = "--";
  entry5.ingressAclTable() = "--";

  entry6.id() = 9;
  entry6.hwLogicalPortId() = 9;
  entry6.name() = "eth1/4/1";
  entry6.adminState() = "Enabled";
  entry6.linkState() = "Up";
  entry6.activeState() = "--";
  entry6.speed() = "100G";
  entry6.profileId() = "PROFILE_100G_4_NRZ_CL91_OPTICAL";
  entry6.tcvrID() = 5;
  entry6.tcvrPresent() = "Present";
  entry6.numUnicastQueues() = 0;
  entry6.pause() = "";
#ifdef IS_OSS
  entry6.isDrained() = "No";
#else
  entry6.isDrained() = "Yes";
#endif
  entry6.activeErrors() = "--";
  entry6.peerSwitchDrained() = "--";
  entry6.peerPortDrainedOrDown() = "--";
  entry6.coreId() = "6";
  entry6.virtualDeviceId() = "6";
  entry6.cableLengthMeters() = "--";
  entry6.userMetaData() = "--";
  entry6.ingressAclTable() = "--";

  // sorted by name
  model.portEntries() = {entry6, entry1, entry2, entry3, entry5, entry4};
  return model;
}

std::unordered_map<std::string, PeerPortInfo> createPeerPortInfo() {
  return {};
}

std::unordered_map<std::string, cfg::SwitchDrainState> createPeerDrainStates() {
  return {};
}
std::unordered_map<std::string, bool> createPeerPortStates() {
  return {};
}

std::vector<std::string> createDrainedInterfaces() {
#ifdef IS_OSS
  return {};
#else
  return {"eth1/4/1", "eth1/5/1"};
#endif
}

std::string createMockedBgpConfig() {
#ifdef IS_OSS
  return "";
#else
  std::string bgpConfigStr;
  bgp::thrift::BgpConfig bgpConfig;
  std::vector<std::string> drainedInterfaces = createDrainedInterfaces();
  bgpConfig.drained_interfaces() = std::move(drainedInterfaces);
  apache::thrift::SimpleJSONSerializer::serialize(bgpConfig, &bgpConfigStr);
  return bgpConfigStr;
#endif
}

class CmdShowPortTestFixture : public CmdHandlerTestBase {
 public:
  CmdShowPortTraits::ObjectArgType queriedEntries;
  std::map<int64_t, cfg::SwitchInfo> mockSwitchIdToSwitchInfo;
  std::map<int32_t, facebook::fboss::PortInfoThrift> mockPortEntries;
  std::map<std::string, FabricEndpoint> mockFabricConnectivity;
  std::map<int32_t, facebook::fboss::TransceiverInfo> mockTransceiverEntries;
  std::unordered_map<std::string, Endpoint> mockPortToPeer;
  std::map<std::string, facebook::fboss::HwPortStats> mockPortStats;
  std::unordered_map<std::string, cfg::SwitchDrainState> mockPeerDrainStates;
  std::unordered_map<std::string, PeerPortInfo> mockPeerPortInfo;
  std::unordered_map<std::string, bool> mockPeerPortStates;
  cli::ShowPortModel normalizedModel;
  std::vector<std::string> mockDrainedInterfaces;
  std::string mockBgpRunningConfig;
  MultiSwitchRunState mockMultiSwitchRunState;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    mockSwitchIdToSwitchInfo = createSwitchIdToSwitchInfo();
    mockPortEntries = createPortEntries();
    mockFabricConnectivity = createFabricConnectivity();
    mockTransceiverEntries = createTransceiverEntries();
    normalizedModel = createPortModel();
    mockPeerDrainStates = createPeerDrainStates();
    mockPeerPortInfo = createPeerPortInfo();
    mockPeerPortStates = createPeerPortStates();
    mockDrainedInterfaces = createDrainedInterfaces();
    mockBgpRunningConfig = createMockedBgpConfig();
    mockMultiSwitchRunState.hwIndexToRunState()->insert(
        {100, SwitchRunState::CONFIGURED});
  }
};

TEST(
    CmdShowPortEventBaseTest,
    PlaintextClients_ConcurrentSyncCallsFromDifferentThreads_Succeed) {
  auto firstMockAgent = std::make_shared<MockFbossCtrlAgent>();
  auto secondMockAgent = std::make_shared<MockFbossCtrlAgent>();
  folly::Baton<> firstRequestStarted;
  folly::Baton<> unblockFirstRequest;
  EXPECT_CALL(*firstMockAgent, getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& /* entries */) {
        firstRequestStarted.post();
        unblockFirstRequest.wait();
      }));
  EXPECT_CALL(*secondMockAgent, getAllPortInfo(_)).Times(1);

  apache::thrift::ScopedServerInterfaceThread firstServer(
      firstMockAgent,
      "::1",
      0,
      CmdHandlerTestBase::createFastMockServerConfig());
  apache::thrift::ScopedServerInterfaceThread secondServer(
      secondMockAgent,
      "::1",
      0,
      CmdHandlerTestBase::createFastMockServerConfig());
  const HostInfo hostInfo(
      "test.host", "test-oob.host", folly::IPAddressV6("::1"));

  auto firstClient =
      utils::createPlaintextClient<apache::thrift::Client<FbossCtrl>>(
          hostInfo, firstServer.getAddress().getPort());
  auto secondClient =
      utils::createPlaintextClient<apache::thrift::Client<FbossCtrl>>(
          hostInfo, secondServer.getAddress().getPort());

  std::thread firstWorker([&] {
    std::map<int32_t, PortInfoThrift> entries;
    firstClient->sync_getAllPortInfo(entries);
  });
  firstRequestStarted.wait();

  std::thread secondWorker([&] {
    std::map<int32_t, PortInfoThrift> entries;
    secondClient->sync_getAllPortInfo(entries);
  });
  secondWorker.join();

  unblockFirstRequest.post();
  firstWorker.join();
}

TEST(CmdShowPortEventBaseTest, PlaintextClient_FutureRpc_ReturnsResponse) {
  auto mockAgent = std::make_shared<MockFbossCtrlAgent>();
  const std::map<int32_t, PortInfoThrift> expectedEntries{
      {1, PortInfoThrift{}}};
  EXPECT_CALL(*mockAgent, getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& entries) { entries = expectedEntries; }));

  apache::thrift::ScopedServerInterfaceThread server(
      mockAgent, "::1", 0, CmdHandlerTestBase::createFastMockServerConfig());
  const HostInfo hostInfo(
      "test.host", "test-oob.host", folly::IPAddressV6("::1"));
  auto client = utils::createPlaintextClient<apache::thrift::Client<FbossCtrl>>(
      hostInfo, server.getAddress().getPort());

  EXPECT_EQ(client->future_getAllPortInfo().get(), expectedEntries);
}

TEST_F(CmdShowPortTestFixture, sortByName) {
  auto model = CmdShowPort().createModel(
      mockPortEntries,
      mockTransceiverEntries,
      queriedEntries,
      mockPortStats,
      mockPortToPeer,
      mockPeerDrainStates,
      mockPeerPortInfo,
      mockDrainedInterfaces);

  EXPECT_THRIFT_EQ(normalizedModel, model);
}

TEST_F(CmdShowPortTestFixture, cbfc) {
  auto portEntries = mockPortEntries;
  CbfcInfo cbfc;
  cbfc.configName() = "cbfc";
  cbfc.virtualChannels() = {2, 6};
  cbfc.senderCreditSize() = 56;
  cbfc.senderPacketOverhead() = 16;
  cbfc.senderCreditLimit() = 8192;
  portEntries[3].cbfc() = cbfc;

  HwPortStats stats;
  stats.cbfcCfUpdateTx_() = 10;
  stats.cbfcCfUpdateRx_() = 20;
  stats.cbfcCcUpdateTx_() = 30;
  std::map<std::string, HwPortStats> portStats{{"eth1/5/3", stats}};

  auto model = CmdShowPort().createModel(
      portEntries,
      mockTransceiverEntries,
      queriedEntries,
      portStats,
      mockPortToPeer,
      mockPeerDrainStates,
      mockPeerPortInfo,
      mockDrainedInterfaces);

  for (const auto& entry : *model.portEntries()) {
    if (*entry.name() != "eth1/5/3") {
      EXPECT_FALSE(entry.cbfc().has_value());
      continue;
    }
    EXPECT_EQ(entry.cbfc(), "cbfc, VC 2 6, sender 56 B / 16 B, limit 8192");
    EXPECT_EQ(entry.hwPortStats()->cbfcCfUpdateTx(), 10);
    EXPECT_EQ(entry.hwPortStats()->cbfcCfUpdateRx(), 20);
    EXPECT_EQ(entry.hwPortStats()->cbfcCcUpdateTx(), 30);
  }
}

TEST_F(CmdShowPortTestFixture, cumulativeAndWindowedStats) {
  PortQueueThrift namedQueue;
  namedQueue.id() = 2;
  namedQueue.name() = "silver";
  PortQueueThrift unnamedQueue;
  unnamedQueue.id() = 4;
  mockPortEntries.at(1).portQueues() = {namedQueue, unnamedQueue};

  HwPortStats hwPortStats;
  hwPortStats.inUnicastPkts_() = 10001;
  hwPortStats.inMulticastPkts_() = 10002;
  hwPortStats.inBroadcastPkts_() = 10003;
  hwPortStats.outUnicastPkts_() = 20001;
  hwPortStats.outMulticastPkts_() = 20002;
  hwPortStats.outBroadcastPkts_() = 20003;
  hwPortStats.outErrors_() = 42;
  hwPortStats.inIpv4HdrErrors_() = 43;
  hwPortStats.inIpv6HdrErrors_() = 44;
  hwPortStats.queueOutBytes_() = {{2, 1234}, {4, 0}};
  hwPortStats.queueOutDiscardBytes_() = {{2, 0}, {4, 0}};
  hwPortStats.queueOutDiscardPackets_() = {{2, 55}, {4, 0}};
  mockPortStats["eth1/5/1"] = hwPortStats;
  const std::map<std::string, int64_t> counters = {
      {"eth1/5/1.in_bytes.sum.60", 15000},
      {"eth1/5/1.in_bytes.sum.600", 150000},
      {"eth1/5/1.in_bytes.sum.3600", 900000},
      {"eth1/5/1.out_bytes.sum.60", 7500},
      {"eth1/5/1.out_bytes.sum.600", 75000},
      {"eth1/5/1.out_bytes.sum.3600", 450000},
      {"eth1/5/1.in_unicast_pkts.sum.60", 109},
      {"eth1/5/1.in_unicast_pkts.sum.600", 1100},
      {"eth1/5/1.in_unicast_pkts.sum.3600", 6647},
      {"eth1/5/1.in_errors.sum.60", 1},
      {"eth1/5/1.in_errors.sum.600", 2},
      {"eth1/5/1.in_errors.sum.3600", 3},
      {"eth1/5/1.queue2.silver.out_bytes.sum.60", 7500},
      {"eth1/5/1.queue2.silver.out_bytes.sum.600", 75000},
      {"eth1/5/1.queue2.silver.out_bytes.sum.3600", 450000},
      {"eth1/5/1.queue2.silver.out_congestion_discards.sum.60", 120},
      {"eth1/5/1.queue2.silver.out_congestion_discards.sum.600", 600},
      {"eth1/5/1.queue2.silver.out_congestion_discards.sum.3600", 3600},
      {"eth1/5/1.queue4.queue4.out_bytes.sum.60", 1000},
      {"eth1/5/1.queue4.queue4.out_congestion_discards.sum.60", 10},
      {"eth1/5/2.in_bytes.sum.60", 9999},
  };

  CmdShowPortTraits::ObjectArgType queriedPorts(
      std::vector<std::string>{"eth1/5/1"});
  auto model = CmdShowPort().createModel(
      mockPortEntries,
      mockTransceiverEntries,
      queriedPorts,
      mockPortStats,
      mockPortToPeer,
      mockPeerDrainStates,
      mockPeerPortInfo,
      mockDrainedInterfaces,
      counters);

  ASSERT_EQ(model.portEntries()->size(), 1);
  const auto& stats = model.portEntries()->front().hwPortStats().value();
  EXPECT_EQ(stats.inMulticastPkts(), 10002);
  EXPECT_EQ(stats.inBroadcastPkts(), 10003);
  EXPECT_EQ(stats.outUnicastPkts(), 20001);
  EXPECT_EQ(stats.outMulticastPkts(), 20002);
  EXPECT_EQ(stats.outBroadcastPkts(), 20003);
  EXPECT_EQ(stats.outErrorPkts(), 42);
  EXPECT_EQ(stats.inIpv4HeaderErrorPkts(), 43);
  EXPECT_EQ(stats.inIpv6HeaderErrorPkts(), 44);
  EXPECT_EQ(stats.queueOutDiscardPackets()->at(2), 55);

  const auto& windowed = stats.windowedStats().value();
  EXPECT_EQ(windowed.ingressBitsPerSecond()->oneMinute(), 2000);
  EXPECT_EQ(windowed.ingressBitsPerSecond()->tenMinutes(), 2000);
  EXPECT_EQ(windowed.ingressBitsPerSecond()->oneHour(), 2000);
  EXPECT_EQ(windowed.egressBitsPerSecond()->oneMinute(), 1000);
  EXPECT_EQ(windowed.inUnicastPkts()->oneMinute(), 109);
  EXPECT_EQ(windowed.inUnicastPkts()->tenMinutes(), 1100);
  EXPECT_EQ(windowed.inUnicastPkts()->oneHour(), 6647);
  EXPECT_EQ(windowed.inErrorPkts()->oneMinute(), 1);
  EXPECT_EQ(windowed.inErrorPkts()->tenMinutes(), 2);
  EXPECT_EQ(windowed.inErrorPkts()->oneHour(), 3);

  const auto& namedRates = stats.queueOutBitsPerSecond()->at(2);
  EXPECT_EQ(namedRates.oneMinute(), 1000);
  EXPECT_EQ(namedRates.tenMinutes(), 1000);
  EXPECT_EQ(namedRates.oneHour(), 1000);
  const auto& discardRates =
      stats.queueOutCongestionDiscardPacketsPerSecond()->at(2);
  EXPECT_EQ(discardRates.oneMinute(), 2);
  EXPECT_EQ(discardRates.tenMinutes(), 1);
  EXPECT_EQ(discardRates.oneHour(), 1);

  EXPECT_FALSE(stats.queueOutBitsPerSecond()->contains(4));
  EXPECT_FALSE(stats.queueOutCongestionDiscardPacketsPerSecond()->contains(4));
}

TEST_F(CmdShowPortTestFixture, invalidPortName) {
  // Interface names are free-form -- SVIs are named arbitrary things and the
  // name may be unset -- so a name that doesn't follow the
  // moduleNum/port/subport pattern must not abort the command. Such entries
  // are ordered by their numeric port ID instead.
  auto invalidPortEntries = createInvalidPortEntries();

  cli::ShowPortModel model;
  ASSERT_NO_THROW(
      model = CmdShowPort().createModel(
          invalidPortEntries,
          mockTransceiverEntries,
          queriedEntries,
          mockPortStats,
          mockPortToPeer,
          mockPeerDrainStates,
          mockPeerPortInfo,
          mockDrainedInterfaces));

  auto portEntries = model.portEntries().value();
  ASSERT_EQ(portEntries.size(), 2);
  EXPECT_EQ(folly::copy(portEntries[0].id().value()), 1);
  EXPECT_EQ(folly::copy(portEntries[1].id().value()), 2);
}

TEST_F(CmdShowPortTestFixture, queryClient) {
  setupMockedAgentServer();
  setupMockedBgpServer();
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& entries) { entries = mockPortEntries; }));
  EXPECT_CALL(getMockAgent(), getSwitchIdToSwitchInfo(_))
      .WillOnce(
          Invoke([&](auto& entries) { entries = mockSwitchIdToSwitchInfo; }));
  EXPECT_CALL(getQsfpService(), getTransceiverInfo(_, _))
      .WillOnce(Invoke(
          [&](auto& entries, auto) { entries = mockTransceiverEntries; }));

#ifndef IS_OSS
  EXPECT_CALL(getBgpService(), getRunningConfig(_))
      .WillOnce(Invoke(
          [&](auto& bgpConfigStr) { bgpConfigStr = mockBgpRunningConfig; }));
#endif
  EXPECT_CALL(getMockAgent(), getMultiSwitchRunState(_))
      .WillOnce(
          Invoke([&](auto& entries) { entries = mockMultiSwitchRunState; }));

  auto cmd = CmdShowPort();
  CmdShowPortTraits::ObjectArgType queriedEntries;
  auto model = cmd.queryClient(localhost(), queriedEntries);

  EXPECT_THRIFT_EQ(normalizedModel, model);
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowPortTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowPortTraits::description().empty());
  EXPECT_FALSE(CmdShowPort::sampleModel().portEntries()->empty());
}

TEST_F(CmdShowPortTestFixture, wikiDocHooksQueue) {
  EXPECT_FALSE(CmdShowPortQueueTraits::description().empty());
  EXPECT_FALSE(CmdShowPortQueue::sampleModel().empty());
}

} // namespace facebook::fboss
