// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <unordered_set>

#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/hw/mock/MockPlatformMapping.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

using namespace ::testing;

namespace facebook::fboss {

// see TestUtils > testConfigAImpl()
const std::string kRecyclePortName = "rcy1/1/1";
constexpr int kRecyclePortId = 1;
constexpr int kGlobalScopedRecyclePortId = 2;
constexpr int kNifPortId = 5;

class MirrorOnDropReportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_ = createMockPlatform(cfg::SwitchType::VOQ, kVoqSwitchIdBegin);
    state_ = std::make_shared<SwitchState>();
    addSwitchInfo(
        state_,
        cfg::SwitchType::VOQ,
        kVoqSwitchIdBegin /* switchId */,
        cfg::AsicType::ASIC_TYPE_JERICHO3);
    config_ =
        testConfigA(cfg::SwitchType::VOQ, cfg::AsicType::ASIC_TYPE_JERICHO3);

    // Add a globally scoped recycle port
    auto platformPort =
        mockPlatformMapping_.getPlatformPort(kGlobalScopedRecyclePortId);
    platformPort.mapping()->scope() = cfg::Scope::GLOBAL;
    mockPlatformMapping_.setPlatformPort(
        kGlobalScopedRecyclePortId, platformPort);

    cfg::Port recyclePort;
    recyclePort.logicalID() = kGlobalScopedRecyclePortId;
    recyclePort.name() = "rcy1/1/2";
    recyclePort.speed() = cfg::PortSpeed::XG;
    recyclePort.profileID() =
        cfg::PortProfileID::PROFILE_10G_1_NRZ_NOFEC_COPPER;
    recyclePort.portType() = cfg::PortType::RECYCLE_PORT;
    recyclePort.scope() = cfg::Scope::GLOBAL;
    config_.ports()->push_back(recyclePort);
  }

  std::shared_ptr<SwitchState> state_;
  std::shared_ptr<Platform> platform_;
  cfg::SwitchConfig config_;
  MockPlatformMapping mockPlatformMapping_;
};

class MirrorOnDropReportChenabTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_ = createMockPlatform(cfg::SwitchType::NPU, 0);
    state_ = std::make_shared<SwitchState>();
    addSwitchInfo(
        state_, cfg::SwitchType::NPU, 0, cfg::AsicType::ASIC_TYPE_CHENAB);
    config_ = testConfigA(cfg::SwitchType::NPU);
    // testConfigA's NPU branch uses ASIC_TYPE_MOCK.
    config_.switchSettings()->switchIdToSwitchInfo() = {
        {0,
         createSwitchInfo(
             cfg::SwitchType::NPU, cfg::AsicType::ASIC_TYPE_CHENAB)}};
  }

  std::shared_ptr<SwitchState> state_;
  std::shared_ptr<Platform> platform_;
  cfg::SwitchConfig config_;
  MockPlatformMapping mockPlatformMapping_;
};

cfg::MirrorOnDropEventConfig makeEventConfig(
    std::optional<cfg::MirrorOnDropAgingGroup> group,
    const std::vector<cfg::MirrorOnDropReasonAggregation>& reasonAggs) {
  cfg::MirrorOnDropEventConfig eventCfg;
  if (group.has_value()) {
    eventCfg.agingGroup() = group.value();
  }
  eventCfg.dropReasonAggregations() = reasonAggs;
  return eventCfg;
}

cfg::MirrorOnDropReport makeReportCfg(
    std::variant<int, cfg::MirrorDestination, std::monostate> port,
    const std::string& ip) {
  cfg::MirrorOnDropReport report;
  report.name() = "mod-1";
  if (std::holds_alternative<int>(port)) {
    report.mirrorPortId() = std::get<int>(port);
  } else if (std::holds_alternative<cfg::MirrorDestination>(port)) {
    report.mirrorPort() = std::get<cfg::MirrorDestination>(port);
  } // else leave it unset and let agent auto-detect
  report.localSrcPort() = 10000;
  report.collectorIp() = ip;
  report.collectorPort() = 20000;
  report.mtu() = 1500;
  report.truncateSize() = 128;
  report.dscp() = 0;
  report.modEventToConfigMap()[0] = makeEventConfig(
      std::nullopt,
      {cfg::MirrorOnDropReasonAggregation::INGRESS_PACKET_PROCESSING_DISCARDS});
  report.modEventToConfigMap()[1] = makeEventConfig(
      cfg::MirrorOnDropAgingGroup::PORT,
      {
          cfg::MirrorOnDropReasonAggregation::
              INGRESS_SOURCE_CONGESTION_DISCARDS,
          cfg::MirrorOnDropReasonAggregation::
              INGRESS_DESTINATION_CONGESTION_DISCARDS,
      });
  report.agingGroupAgingIntervalUsecs()[cfg::MirrorOnDropAgingGroup::PORT] =
      100;
  return report;
}

// mirrorPortId is unset on ASICs with no recirculation port. Pin that it stays
// unset through a thrift round trip, since warmboot reads it back.
TEST(MirrorOnDropReportOptionalPortTest, UnsetMirrorPortIdSurvivesRoundTrip) {
  auto makeReport = [](const std::optional<PortID>& mirrorPortId) {
    const std::string name{"mod-1"};
    return std::make_shared<MirrorOnDropReport>(
        name,
        mirrorPortId,
        folly::IPAddress("2401::1"),
        static_cast<int16_t>(12345),
        folly::IPAddress("2401::2"),
        static_cast<int16_t>(6343),
        static_cast<int16_t>(1500),
        static_cast<int16_t>(128),
        static_cast<uint8_t>(0),
        std::string("00:00:00:00:00:01"),
        std::string("00:00:00:00:00:02"),
        std::map<int8_t, cfg::MirrorOnDropEventConfig>{},
        std::map<cfg::MirrorOnDropAgingGroup, int32_t>{});
  };
  auto roundTrip = [](const std::shared_ptr<MirrorOnDropReport>& report) {
    return std::make_shared<MirrorOnDropReport>(report->toThrift());
  };

  auto unset = makeReport(std::nullopt);
  EXPECT_EQ(unset->getMirrorPortId(), std::nullopt);
  EXPECT_EQ(roundTrip(unset)->getMirrorPortId(), std::nullopt);

  auto set = makeReport(PortID(kRecyclePortId));
  EXPECT_EQ(set->getMirrorPortId(), PortID(kRecyclePortId));
  EXPECT_EQ(roundTrip(set)->getMirrorPortId(), PortID(kRecyclePortId));
}

TEST_F(MirrorOnDropReportTest, CreateReportV4) {
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(kRecyclePortId, "1.2.3.4"));

  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  auto report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  EXPECT_NE(report, nullptr);
  EXPECT_EQ(report->getID(), "mod-1");
  EXPECT_TRUE(report->getLocalSrcIp().isV4());
  EXPECT_FALSE(report->getSwitchMac().empty());
  EXPECT_FALSE(report->getFirstInterfaceMac().empty());
  EXPECT_EQ(report->getModEventToConfigMap().size(), 2);
  EXPECT_EQ(
      report->getModEventToConfigMap()[0].agingGroup().value_or(
          cfg::MirrorOnDropAgingGroup::GLOBAL),
      cfg::MirrorOnDropAgingGroup::GLOBAL);
  EXPECT_EQ(
      report->getModEventToConfigMap()[1].agingGroup().value_or(
          cfg::MirrorOnDropAgingGroup::GLOBAL),
      cfg::MirrorOnDropAgingGroup::PORT);
  EXPECT_EQ(
      report->getAgingGroupAgingIntervalUsecs()
          [cfg::MirrorOnDropAgingGroup::PORT],
      100);
}

TEST_F(MirrorOnDropReportTest, CreateReportV6) {
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(kRecyclePortId, "2401::1"));

  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  auto report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  EXPECT_NE(report, nullptr);
  EXPECT_EQ(report->getID(), "mod-1");
  EXPECT_TRUE(report->getLocalSrcIp().isV6());
  EXPECT_FALSE(report->getSwitchMac().empty());
  EXPECT_FALSE(report->getFirstInterfaceMac().empty());
  EXPECT_EQ(report->getModEventToConfigMap().size(), 2);
  EXPECT_EQ(
      report->getModEventToConfigMap()[0].agingGroup().value_or(
          cfg::MirrorOnDropAgingGroup::GLOBAL),
      cfg::MirrorOnDropAgingGroup::GLOBAL);
  EXPECT_EQ(
      report->getModEventToConfigMap()[1].agingGroup().value_or(
          cfg::MirrorOnDropAgingGroup::GLOBAL),
      cfg::MirrorOnDropAgingGroup::PORT);
  EXPECT_EQ(
      report->getAgingGroupAgingIntervalUsecs()
          [cfg::MirrorOnDropAgingGroup::PORT],
      100);
}

TEST_F(MirrorOnDropReportTest, RateThresholdsUnsetByDefault) {
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(kRecyclePortId, "2401::1"));

  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  auto report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  ASSERT_NE(report, nullptr);
  EXPECT_FALSE(report->getDropPacketRateThreshold().has_value());
  EXPECT_FALSE(report->getAggregateDropPacketRateThreshold().has_value());
}

TEST_F(MirrorOnDropReportTest, RateThresholds) {
  auto reportCfg = makeReportCfg(kRecyclePortId, "2401::1");
  reportCfg.dropPacketRateThreshold() = 2000;
  reportCfg.aggregateDropPacketRateThreshold() = 500;
  config_.mirrorOnDropReports()->push_back(reportCfg);

  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  auto report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  ASSERT_NE(report, nullptr);
  EXPECT_EQ(report->getDropPacketRateThreshold(), 2000);
  EXPECT_EQ(report->getAggregateDropPacketRateThreshold(), 500);

  // Changing only the aggregate threshold must produce a new report.
  config_.mirrorOnDropReports()->at(0).aggregateDropPacketRateThreshold() =
      1000;
  auto newState = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);
  ASSERT_NE(newState, nullptr);
  auto newReport = newState->getMirrorOnDropReports()->getNodeIf("mod-1");
  ASSERT_NE(newReport, nullptr);
  EXPECT_EQ(newReport->getDropPacketRateThreshold(), 2000);
  EXPECT_EQ(newReport->getAggregateDropPacketRateThreshold(), 1000);

  // Removing the aggregate threshold from config must clear it in state.
  config_.mirrorOnDropReports()
      ->at(0)
      .aggregateDropPacketRateThreshold()
      .reset();
  auto clearedState = publishAndApplyConfig(
      newState, &config_, platform_.get(), nullptr, &mockPlatformMapping_);
  ASSERT_NE(clearedState, nullptr);
  auto clearedReport =
      clearedState->getMirrorOnDropReports()->getNodeIf("mod-1");
  ASSERT_NE(clearedReport, nullptr);
  EXPECT_EQ(clearedReport->getDropPacketRateThreshold(), 2000);
  EXPECT_FALSE(
      clearedReport->getAggregateDropPacketRateThreshold().has_value());
}

TEST_F(MirrorOnDropReportTest, CreateReportByMirrorDestinationLogicalId) {
  cfg::MirrorDestination destination;
  destination.egressPort().ensure().set_logicalID(kRecyclePortId);
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(destination, "2401::1"));

  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  auto report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  EXPECT_NE(report, nullptr);
}

TEST_F(MirrorOnDropReportTest, CreateReportByMirrorDestinationName) {
  cfg::MirrorDestination destination;
  destination.egressPort().ensure().set_name(kRecyclePortName);
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(destination, "2401::1"));

  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  auto report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  EXPECT_NE(report, nullptr);
}

TEST_F(MirrorOnDropReportTest, CreateReportByInvalidMirrorDestination) {
  cfg::MirrorDestination destination;
  // Not specifying anything in destination
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(destination, "2401::1"));

  EXPECT_THROW(
      publishAndApplyConfig(
          state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_),
      FbossError);
}

TEST_F(MirrorOnDropReportTest, CreateReportByAutoDetect) {
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(std::monostate{}, "2401::1"));

  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  auto report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  EXPECT_NE(report, nullptr);
  EXPECT_EQ(report->getMirrorPortId(), PortID(kRecyclePortId));
}

TEST_F(MirrorOnDropReportTest, CreateReportOnNifPort) {
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(kNifPortId, "2401::1"));
  EXPECT_THROW(
      publishAndApplyConfig(
          state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_),
      FbossError);
}

// XGS has no mirror port, so every NPU gets the report.
TEST(MirrorOnDropReportXgsTest, ReportReachesEveryNpu) {
  auto platform = createMockPlatform();
  auto config = testConfigA();
  // One VLAN interface per NPU: Vlan1 is ports 1-11, Vlan55 is 12-20.
  config.switchSettings()->switchIdToSwitchInfo() = {
      {0,
       createSwitchInfo(
           cfg::SwitchType::NPU, cfg::AsicType::ASIC_TYPE_MOCK, 0, 11, 0)},
      {1,
       createSwitchInfo(
           cfg::SwitchType::NPU, cfg::AsicType::ASIC_TYPE_MOCK, 12, 99, 1)}};
  auto reportCfg = makeReportCfg(std::monostate{}, "2401::1");
  reportCfg.mirrorPort().ensure().tunnel().ensure().srcIp() = "2401::2";
  config.mirrorOnDropReports()->push_back(reportCfg);

  auto state = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &config, platform.get());
  ASSERT_NE(state, nullptr);

  for (const auto& switchId : {SwitchID(0), SwitchID(1)}) {
    auto reports = state->getMirrorOnDropReports()->getMapNodeIf(
        HwSwitchMatcher(std::unordered_set<SwitchID>{switchId}));
    ASSERT_NE(reports, nullptr) << "switch " << switchId;
    EXPECT_NE(reports->getNodeIf("mod-1"), nullptr) << "switch " << switchId;
  }
}

TEST_F(MirrorOnDropReportTest, CreateReportOnGlobalScopedRecyclePort) {
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(kGlobalScopedRecyclePortId, "2401::1"));
  EXPECT_THROW(
      publishAndApplyConfig(
          state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_),
      FbossError);
}

TEST_F(MirrorOnDropReportTest, PreserveResolutionOnIdenticalConfig) {
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(kRecyclePortId, "2401::1"));
  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  const folly::MacAddress collectorMac{"00:00:00:00:00:01"};
  cfg::PortDescriptor egressPort;
  egressPort.portId() = kRecyclePortId;
  egressPort.portType() = cfg::PortDescriptorType::Physical;
  auto report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  report->setIsResolved(true);
  report->setResolvedCollectorMac(collectorMac);
  report->setResolvedEgressPort(egressPort);

  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  EXPECT_TRUE(report->isResolved());
  EXPECT_EQ(report->getResolvedCollectorMac(), collectorMac);
  EXPECT_EQ(report->getResolvedEgressPort(), egressPort);
}

TEST_F(MirrorOnDropReportTest, ClearResolutionWhenCollectorChanges) {
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(kRecyclePortId, "2401::1"));
  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  cfg::PortDescriptor egressPort;
  egressPort.portId() = kRecyclePortId;
  egressPort.portType() = cfg::PortDescriptorType::Physical;
  auto report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  report->setIsResolved(true);
  report->setResolvedCollectorMac(folly::MacAddress{"00:00:00:00:00:01"});
  report->setResolvedEgressPort(egressPort);
  config_.mirrorOnDropReports()->at(0).collectorIp() = "2401::2";

  state_ = publishAndApplyConfig(
      state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);

  report = state_->getMirrorOnDropReports()->getNodeIf("mod-1");
  EXPECT_FALSE(report->isResolved());
  EXPECT_FALSE(report->getResolvedCollectorMac().has_value());
  EXPECT_FALSE(report->getResolvedEgressPort().has_value());
}

TEST_F(MirrorOnDropReportChenabTest, RejectsEmptySrcIpSentinel) {
  cfg::MirrorTunnel tunnel;
  tunnel.srcIp() = "";
  cfg::MirrorDestination destination;
  destination.tunnel() = tunnel;
  config_.mirrorOnDropReports()->push_back(
      makeReportCfg(destination, "1.2.3.4"));

  try {
    publishAndApplyConfig(
        state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);
    FAIL() << "Expected unresolved srcIp sentinel to be rejected";
  } catch (const FbossError& error) {
    const std::string message{error.what()};
    EXPECT_NE(message.find("mod-1"), std::string::npos);
    EXPECT_NE(message.find("mirrorPort.tunnel.srcIp"), std::string::npos);
  }
}

TEST_F(MirrorOnDropReportChenabTest, RejectsNegativeTruncateSize) {
  auto report = makeReportCfg(std::monostate{}, "1.2.3.4");
  report.truncateSize() = -1;
  config_.mirrorOnDropReports()->push_back(report);

  try {
    publishAndApplyConfig(
        state_, &config_, platform_.get(), nullptr, &mockPlatformMapping_);
    FAIL() << "Expected negative truncateSize to be rejected";
  } catch (const FbossError& error) {
    const std::string message{error.what()};
    EXPECT_NE(
        message.find("truncateSize must be non-negative"), std::string::npos);
  }
}

} // namespace facebook::fboss
