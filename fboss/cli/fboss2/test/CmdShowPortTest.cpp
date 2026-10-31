// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/IPAddressV4.h>
#include <folly/synchronization/Baton.h>

#include <algorithm>
#include <thread>

#include <fmt/core.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/cli/fboss2/CmdGlobalOptions.h"
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

TEST_F(CmdShowPortTestFixture, invalidPortName) {
  auto invalidPortEntries = createInvalidPortEntries();

  try {
    CmdShowPort().createModel(
        invalidPortEntries,
        mockTransceiverEntries,
        queriedEntries,
        mockPortStats,
        mockPortToPeer,
        mockPeerDrainStates,
        mockPeerPortInfo,
        mockDrainedInterfaces);
    FAIL();
  } catch (const std::invalid_argument& expected) {
    ASSERT_STREQ(
        "Invalid port name: eth/5/1\n"
        "Port name must match 'moduleNum/port/subport' pattern",
        expected.what());
  }
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

namespace {
// Sets the global --detail option by parsing it through CLI11, the same path
// the fboss2 binary uses.
void setDetailFlag(bool detail) {
  CLI::App app;
  CmdGlobalOptions::getInstance()->init(app);
  app.parse(std::vector<std::string>{detail ? "--detail" : "--detail=false"});
}

ActiveQueueManagement makeLinearAqm(
    QueueCongestionBehavior behavior,
    int32_t minLength,
    int32_t maxLength,
    int32_t probability) {
  ActiveQueueManagement aqm;
  aqm.behavior() = behavior;
  LinearQueueCongestionDetection linear;
  linear.minimumLength() = minLength;
  linear.maximumLength() = maxLength;
  linear.probability() = probability;
  aqm.detection()->linear() = linear;
  return aqm;
}

PortQueueThrift makeWrrQueue(int32_t id, int32_t weight) {
  PortQueueThrift queue;
  queue.id() = id;
  queue.name() = fmt::format("q{}", id);
  queue.mode() = "WEIGHTED_ROUND_ROBIN";
  queue.weight() = weight;
  queue.reservedBytes() = 0;
  queue.scalingFactor() = "TWO";
  return queue;
}

// Two ports sharing a queue profile where only q3 carries AQM, and one
// port with a queue that has both ECN and WRED entries.
std::map<int32_t, PortInfoThrift> createPortQueueEntries() {
  std::vector<PortQueueThrift> queues = {
      makeWrrQueue(0, 1),
      makeWrrQueue(1, 2),
      makeWrrQueue(2, 4),
      makeWrrQueue(3, 8),
  };
  queues[3].aqms() = {
      makeLinearAqm(QueueCongestionBehavior::ECN, 1000000, 5000000, 100)};

  std::map<int32_t, PortInfoThrift> entries;
  for (const auto& [portId, name] :
       std::vector<std::pair<int32_t, std::string>>{
           {1, "eth1/1/1"}, {5, "eth1/2/1"}}) {
    PortInfoThrift port;
    port.portId() = portId;
    port.name() = name;
    port.portQueues() = queues;
    entries[portId] = port;
  }

  PortInfoThrift mixed;
  mixed.portId() = 9;
  mixed.name() = "eth1/3/1";
  auto both = makeWrrQueue(0, 1);
  both.aqms() = {
      makeLinearAqm(QueueCongestionBehavior::ECN, 100000, 200000, 100),
      makeLinearAqm(QueueCongestionBehavior::EARLY_DROP, 300000, 400000, 50)};
  mixed.portQueues() = {both};
  entries[9] = mixed;
  return entries;
}
} // namespace

TEST_F(CmdShowPortTestFixture, queueQueryClientFiltersPorts) {
  setupMockedAgentServer();
  auto entries = createPortQueueEntries();
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& out) { out = entries; }));

  utils::PortList queried(std::vector<std::string>{"eth1/2/1"});
  auto result = CmdShowPortQueue().queryClient(localhost(), queried);

  ASSERT_EQ(result.size(), 1);
  ASSERT_EQ(result.count(5), 1);
  const auto& queues = *result.at(5).portQueues();
  ASSERT_EQ(queues.size(), 4);
  EXPECT_FALSE(queues[0].aqms().has_value());
  ASSERT_TRUE(queues[3].aqms().has_value());
  EXPECT_EQ(*queues[3].aqms(), *entries.at(5).portQueues()[3].aqms());
}

TEST_F(CmdShowPortTestFixture, queueQueryClientReturnsAllPortsWhenUnfiltered) {
  setupMockedAgentServer();
  auto entries = createPortQueueEntries();
  EXPECT_CALL(getMockAgent(), getAllPortInfo(_))
      .WillOnce(Invoke([&](auto& out) { out = entries; }));

  utils::PortList queried(std::vector<std::string>{});
  auto result = CmdShowPortQueue().queryClient(localhost(), queried);

  EXPECT_EQ(result, entries);
}

TEST_F(CmdShowPortTestFixture, queueOutputOmitsAqmWithoutDetail) {
  setDetailFlag(false);
  std::stringstream ss;
  CmdShowPortQueue().printOutput(createPortQueueEntries(), ss);

  std::string output = ss.str();
  EXPECT_THAT(output, HasSubstr("eth1/1/1\n"));
  EXPECT_THAT(
      output,
      HasSubstr(
          "3      q3                  WEIGHTED_ROUND_ROBIN     "
          "8         0              TWO            \n"));
  EXPECT_THAT(output, Not(HasSubstr("AQM")));
  EXPECT_THAT(output, Not(HasSubstr("ECN")));
}

TEST_F(CmdShowPortTestFixture, queueDetailShowsAqmPerQueue) {
  setDetailFlag(true);
  std::stringstream ss;
  CmdShowPortQueue().printOutput(createPortQueueEntries(), ss);
  setDetailFlag(false);

  std::string output = ss.str();
  // Queues without AQM say so explicitly, so an operator can tell a
  // missing ECN config apart from one that applied.
  EXPECT_THAT(
      output,
      HasSubstr(
          "0      q0                  WEIGHTED_ROUND_ROBIN     "
          "1         0              TWO            \n"
          "       AQM: none\n"
          "1      q1                  WEIGHTED_ROUND_ROBIN     "
          "2         0              TWO            \n"
          "       AQM: none\n"));
  EXPECT_THAT(
      output,
      HasSubstr(
          "3      q3                  WEIGHTED_ROUND_ROBIN     "
          "8         0              TWO            \n"
          "       AQM: ECN  minLength=1000000  maxLength=5000000  "
          "probability=100%\n"));
  // A queue carrying both ECN and WRED lists one line per entry.
  EXPECT_THAT(
      output,
      HasSubstr(
          "eth1/3/1\n"
          "==========\n"));
  EXPECT_THAT(
      output,
      HasSubstr(
          "       AQM: ECN  minLength=100000  maxLength=200000  "
          "probability=100%\n"
          "       AQM: EARLY_DROP  minLength=300000  maxLength=400000  "
          "probability=50%\n"));
  // The same profile is printed under both ports that use it.
  EXPECT_EQ(
      std::count(output.begin(), output.end(), '%'),
      4 /* 2 ports x q3 ECN + eth1/3/1 ECN + EARLY_DROP */);
}

TEST_F(CmdShowPortTestFixture, queueDetailHandlesUnknownDetection) {
  auto entries = createPortQueueEntries();
  ActiveQueueManagement noDetection;
  noDetection.behavior() = QueueCongestionBehavior::EARLY_DROP;
  entries.at(9).portQueues()[0].aqms() = {noDetection};

  setDetailFlag(true);
  std::stringstream ss;
  CmdShowPortQueue().printOutput(entries, ss);
  setDetailFlag(false);

  EXPECT_THAT(
      ss.str(), HasSubstr("       AQM: EARLY_DROP  detection=unknown\n"));
}

TEST_F(CmdShowPortTestFixture, queueSampleModelCarriesAqm) {
  auto model = CmdShowPortQueue::sampleModel();
  bool sawAqm = false;
  for (const auto& [portId, port] : model) {
    for (const auto& queue : *port.portQueues()) {
      sawAqm |= queue.aqms().has_value() && !queue.aqms()->empty();
    }
  }
  EXPECT_TRUE(sawAqm);
}

} // namespace facebook::fboss
