// Copyright 2004-present Facebook. All Rights Reserved.

#include <array>
#include <type_traits>

#include <folly/MacAddress.h>
#include <folly/container/F14Map.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include "fboss/agent/hw/sai/api/SaiObjectApi.h"
#include "fboss/agent/hw/sai/api/TamApi.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

using namespace facebook::fboss;

class TamApiTest : public ::testing::Test {
 public:
  void SetUp() override {
    fs = FakeSai::getInstance();
    sai_api_initialize(0, nullptr);
    tamApi = std::make_unique<TamApi>();
  }

 protected:
  static constexpr auto switchId = 0;
  std::shared_ptr<FakeSai> fs;
  std::unique_ptr<TamApi> tamApi;
};

TEST_F(TamApiTest, TamCollectorV4) {
  folly::IPAddress srcIp{"1.1.1.1"};
  folly::IPAddress dstIp{"2.2.2.2"};
  sai_object_id_t transportSaiObjectId = 100;

  SaiTamCollectorTraits::CreateAttributes collectorAttr;
  std::get<SaiTamCollectorTraits::Attributes::SrcIp>(collectorAttr) = srcIp;
  std::get<SaiTamCollectorTraits::Attributes::DstIp>(collectorAttr) = dstIp;
  std::get<std::optional<SaiTamCollectorTraits::Attributes::TruncateSize>>(
      collectorAttr) = 128;
  std::get<SaiTamCollectorTraits::Attributes::Transport>(collectorAttr) =
      transportSaiObjectId;
  std::get<std::optional<SaiTamCollectorTraits::Attributes::DscpValue>>(
      collectorAttr) = 0;

  auto collectorSaiId =
      tamApi->create<SaiTamCollectorTraits>(collectorAttr, switchId);
  EXPECT_EQ(
      tamApi->getAttribute(
          collectorSaiId, SaiTamCollectorTraits::CreateAttributes{}),
      collectorAttr);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  auto unsupported = std::tuple{
      std::optional<SaiTamCollectorTraits::Attributes::SrcMacAddress>{},
      std::optional<SaiTamCollectorTraits::Attributes::DstMacAddress>{},
      std::optional<SaiTamCollectorTraits::Attributes::Destination>{}};
  EXPECT_EQ(tamApi->getAttribute(collectorSaiId, unsupported), unsupported);
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  auto localhost =
      std::optional<SaiTamCollectorTraits::Attributes::Localhost>{};
  EXPECT_FALSE(tamApi->getAttribute(collectorSaiId, localhost).has_value());
#endif
  tamApi->remove(collectorSaiId);
  EXPECT_THROW(
      tamApi->getAttribute(
          collectorSaiId, SaiTamCollectorTraits::CreateAttributes{}),
      std::exception);
}

TEST_F(TamApiTest, TamCollectorV6) {
  folly::IPAddress srcIp{"2401::1"};
  folly::IPAddress dstIp{"2401::2"};
  sai_object_id_t transportSaiObjectId = 100;

  SaiTamCollectorTraits::CreateAttributes collectorAttr;
  std::get<SaiTamCollectorTraits::Attributes::SrcIp>(collectorAttr) = srcIp;
  std::get<SaiTamCollectorTraits::Attributes::DstIp>(collectorAttr) = dstIp;
  std::get<std::optional<SaiTamCollectorTraits::Attributes::TruncateSize>>(
      collectorAttr) = 128;
  std::get<SaiTamCollectorTraits::Attributes::Transport>(collectorAttr) =
      transportSaiObjectId;
  std::get<std::optional<SaiTamCollectorTraits::Attributes::DscpValue>>(
      collectorAttr) = 0;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::get<std::optional<SaiTamCollectorTraits::Attributes::SrcMacAddress>>(
      collectorAttr) = folly::MacAddress{"00:00:00:00:00:01"};
  std::get<std::optional<SaiTamCollectorTraits::Attributes::DstMacAddress>>(
      collectorAttr) = folly::MacAddress{"00:00:00:00:00:02"};
  std::get<std::optional<SaiTamCollectorTraits::Attributes::Destination>>(
      collectorAttr) = sai_object_id_t{42};

  const auto verifyMacLookup = [](const auto& attribute) {
    using Attribute = std::decay_t<decltype(attribute)>;
    folly::F14FastMap<std::optional<Attribute>, int> entries;
    entries.emplace(attribute, 1);
    const auto found = entries.find(Attribute{attribute.value()});
    ASSERT_NE(found, entries.end());
    EXPECT_EQ(found->second, 1);
  };
  verifyMacLookup(
      std::get<std::optional<SaiTamCollectorTraits::Attributes::SrcMacAddress>>(
          collectorAttr)
          .value());
  verifyMacLookup(
      std::get<std::optional<SaiTamCollectorTraits::Attributes::DstMacAddress>>(
          collectorAttr)
          .value());
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  std::get<std::optional<SaiTamCollectorTraits::Attributes::Localhost>>(
      collectorAttr) = false;
#endif

  auto collectorSaiId =
      tamApi->create<SaiTamCollectorTraits>(collectorAttr, switchId);
  EXPECT_EQ(
      tamApi->getAttribute(
          collectorSaiId, SaiTamCollectorTraits::CreateAttributes{}),
      collectorAttr);
  tamApi->remove(collectorSaiId);
  EXPECT_THROW(
      tamApi->getAttribute(
          collectorSaiId, SaiTamCollectorTraits::CreateAttributes{}),
      std::exception);
}

TEST_F(TamApiTest, TamTransport) {
  SaiTamTransportTraits::CreateAttributes transportAttr;
  std::get<SaiTamTransportTraits::Attributes::Type>(transportAttr) =
      SAI_TAM_TRANSPORT_TYPE_UDP;
  std::get<std::optional<SaiTamTransportTraits::Attributes::SrcPort>>(
      transportAttr) = 10001;
  std::get<std::optional<SaiTamTransportTraits::Attributes::DstPort>>(
      transportAttr) = 10002;
  std::get<std::optional<SaiTamTransportTraits::Attributes::Mtu>>(
      transportAttr) = 1500;
  std::get<std::optional<SaiTamTransportTraits::Attributes::SrcMacAddress>>(
      transportAttr) = folly::MacAddress("00:00:00:00:00:01");
  std::get<std::optional<SaiTamTransportTraits::Attributes::DstMacAddress>>(
      transportAttr) = folly::MacAddress("00:00:00:00:00:02");

  auto transportSaiId =
      tamApi->create<SaiTamTransportTraits>(transportAttr, switchId);
  EXPECT_EQ(
      tamApi->getAttribute(
          transportSaiId, SaiTamTransportTraits::CreateAttributes{}),
      transportAttr);
  tamApi->remove(transportSaiId);
  EXPECT_THROW(
      tamApi->getAttribute(
          transportSaiId, SaiTamTransportTraits::CreateAttributes{}),
      std::exception);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
TEST_F(TamApiTest, TamGreTransport) {
  SaiTamTransportTraits::CreateAttributes attributes;
  std::get<SaiTamTransportTraits::Attributes::Type>(attributes) =
      SAI_TAM_TRANSPORT_TYPE_GRE;
  std::get<std::optional<SaiTamTransportTraits::Attributes::GreProtocolType>>(
      attributes) = sai_uint16_t{0x6558};
  const auto transport =
      tamApi->create<SaiTamTransportTraits>(attributes, switchId);
  EXPECT_EQ(
      tamApi->getAttribute(
          transport, SaiTamTransportTraits::CreateAttributes{}),
      attributes);
  tamApi->setAttribute(
      transport, SaiTamTransportTraits::Attributes::GreProtocolType{0x0800});
  EXPECT_EQ(
      tamApi->getAttribute(
          transport, SaiTamTransportTraits::Attributes::GreProtocolType{}),
      0x0800);
  tamApi->remove(transport);
}
#endif

TEST_F(TamApiTest, TamReport) {
  SaiTamReportTraits::CreateAttributes reportAttr;
  std::get<SaiTamReportTraits::Attributes::Type>(reportAttr) =
      SAI_TAM_REPORT_TYPE_VENDOR_EXTN;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::get<std::optional<SaiTamReportTraits::Attributes::SampleRate>>(
      reportAttr) = sai_uint32_t{1000};
  std::get<std::optional<SaiTamReportTraits::Attributes::MaxReportRate>>(
      reportAttr) = sai_uint64_t{500};
  std::get<std::optional<SaiTamReportTraits::Attributes::MaxReportBurst>>(
      reportAttr) = sai_uint64_t{100};
#endif
  auto reportSaiId = tamApi->create<SaiTamReportTraits>(reportAttr, switchId);
  EXPECT_EQ(
      tamApi->getAttribute(reportSaiId, SaiTamReportTraits::CreateAttributes{}),
      reportAttr);
  tamApi->remove(reportSaiId);
  EXPECT_THROW(
      tamApi->getAttribute(reportSaiId, SaiTamReportTraits::CreateAttributes{}),
      std::exception);
}

TEST_F(TamApiTest, TamEventAction) {
  SaiTamEventActionTraits::CreateAttributes eventActionAttr;
  std::get<SaiTamEventActionTraits::Attributes::ReportType>(eventActionAttr) =
      1; // object id of report
  auto eventActionSaiId =
      tamApi->create<SaiTamEventActionTraits>(eventActionAttr, switchId);
  EXPECT_EQ(
      tamApi->getAttribute(
          eventActionSaiId, SaiTamEventActionTraits::CreateAttributes{}),
      eventActionAttr);
  tamApi->remove(eventActionSaiId);
  EXPECT_THROW(
      tamApi->getAttribute(
          eventActionSaiId, SaiTamEventActionTraits::CreateAttributes{}),
      std::exception);
}

TEST_F(TamApiTest, TamEvent) {
  std::vector<sai_object_id_t> eventActions{100}; // object id of event action
  std::vector<sai_object_id_t> eventCollectors{SAI_NULL_OBJECT_ID};
  std::vector<sai_int32_t> eventTypes{1, 2, 3, 4}; // parity error e.g.

  sai_int32_t deviceId = 0;
  sai_int32_t eventId = 1;
  std::vector<sai_object_id_t> extensionsCollectorList{10};
  std::vector<sai_int32_t> packetDropTypeMmu = {3, 4};
  std::vector<sai_int32_t> packetDropTypeIngress = {5, 6};
  sai_object_id_t agingGroup = 20;
  sai_object_id_t ingressSamplepacketEnable = 30;

  SaiTamEventTraits::CreateAttributes eventAttr;
  std::get<SaiTamEventTraits::Attributes::Type>(eventAttr) =
      SAI_TAM_EVENT_TYPE_PACKET_DROP; // type of Event
  std::get<SaiTamEventTraits::Attributes::ActionList>(eventAttr) = eventActions;
  std::get<SaiTamEventTraits::Attributes::CollectorList>(eventAttr) =
      eventCollectors;
  std::get<std::optional<SaiTamEventTraits::Attributes::SwitchEventType>>(
      eventAttr) = eventTypes;
  std::get<std::optional<SaiTamEventTraits::Attributes::DeviceId>>(eventAttr) =
      deviceId;
  std::get<std::optional<SaiTamEventTraits::Attributes::SwitchEventId>>(
      eventAttr) = eventId;
  std::get<
      std::optional<SaiTamEventTraits::Attributes::ExtensionsCollectorList>>(
      eventAttr) = extensionsCollectorList;
  std::get<std::optional<SaiTamEventTraits::Attributes::PacketDropTypeMmu>>(
      eventAttr) = packetDropTypeMmu;
  std::get<std::optional<SaiTamEventTraits::Attributes::PacketDropTypeIngress>>(
      eventAttr) = packetDropTypeIngress;
  std::get<std::optional<SaiTamEventTraits::Attributes::AgingGroup>>(
      eventAttr) = agingGroup;
  std::get<
      std::optional<SaiTamEventTraits::Attributes::IngressSamplepacketEnable>>(
      eventAttr) = ingressSamplepacketEnable;
  sai_object_id_t threshold = 40;
  std::get<std::optional<SaiTamEventTraits::Attributes::Threshold>>(eventAttr) =
      threshold;

  auto eventSaiId = tamApi->create<SaiTamEventTraits>(eventAttr, switchId);
  EXPECT_EQ(
      tamApi->getAttribute(eventSaiId, SaiTamEventTraits::CreateAttributes{}),
      eventAttr);
  tamApi->remove(eventSaiId);
  EXPECT_THROW(
      tamApi->getAttribute(eventSaiId, SaiTamEventTraits::CreateAttributes{}),
      std::exception);
}

TEST_F(TamApiTest, Tam) {
  std::vector<sai_object_id_t> tamEvents{100}; // object id of event
  std::vector<sai_int32_t> tamBindpoints{
      SAI_TAM_BIND_POINT_TYPE_SWITCH}; // bind point of tam

  SaiTamTraits::CreateAttributes tamAttr{tamEvents, tamBindpoints};

  auto tamSaiId = tamApi->create<SaiTamTraits>(tamAttr, switchId);
  EXPECT_EQ(
      tamApi->getAttribute(tamSaiId, SaiTamTraits::CreateAttributes{}),
      tamAttr);
  tamApi->remove(tamSaiId);
  EXPECT_THROW(
      tamApi->getAttribute(tamSaiId, SaiTamEventTraits::CreateAttributes{}),
      std::exception);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 1)
TEST_F(TamApiTest, CsigIntApiOperations) {
  sai_tam_api_t* api = nullptr;
  ASSERT_EQ(
      sai_api_query(SAI_API_TAM, reinterpret_cast<void**>(&api)),
      SAI_STATUS_SUCCESS);
  ASSERT_NE(api->create_tam_int, nullptr);
  ASSERT_NE(api->remove_tam_int, nullptr);
  ASSERT_NE(api->set_tam_int_attribute, nullptr);
  ASSERT_NE(api->get_tam_int_attribute, nullptr);

  std::array<sai_attribute_t, 8> attrs{};
  attrs[0].id = SAI_TAM_INT_ATTR_TYPE;
  attrs[0].value.s32 = SAI_TAM_INT_TYPE_CSIG_COMPACT;
  attrs[1].id = SAI_TAM_INT_ATTR_DEVICE_ID;
  attrs[1].value.u32 = 0;
  attrs[2].id = SAI_TAM_INT_ATTR_INLINE;
  attrs[2].value.booldata = true;
  attrs[3].id = SAI_TAM_INT_ATTR_INT_PRESENCE_TYPE;
  attrs[3].value.s32 = SAI_TAM_INT_PRESENCE_TYPE_ETH_TYPE;
  attrs[4].id = SAI_TAM_INT_ATTR_ETH_TYPE_CODE_POINT;
  attrs[4].value.u16 = 0x9900;
  attrs[5].id = SAI_TAM_INT_ATTR_CSIG_SIGNAL_TYPE;
  attrs[5].value.s32 = SAI_CSIG_SIGNAL_TYPE_ABW;
  attrs[6].id = SAI_TAM_INT_ATTR_QUANT_BANDS;
  attrs[6].value.u32 = 2;
  std::array<sai_u64_range_t, 2> ranges{
      sai_u64_range_t{0, 1ULL << 32},
      sai_u64_range_t{(1ULL << 32) + 1, 1ULL << 40}};
  attrs[7].id = SAI_TAM_INT_ATTR_QUANT_BAND_UINT64_RANGE_LIST;
  attrs[7].value.u64rangelist = {ranges.size(), ranges.data()};
  sai_object_id_t id{};
  ASSERT_EQ(
      api->create_tam_int(&id, switchId, attrs.size(), attrs.data()),
      SAI_STATUS_SUCCESS);
  sai_attribute_t read{};
  read.id = SAI_TAM_INT_ATTR_QUANT_BAND_UINT64_RANGE_LIST;
  EXPECT_EQ(
      api->get_tam_int_attribute(id, 1, &read), SAI_STATUS_BUFFER_OVERFLOW);
  ASSERT_EQ(read.value.u64rangelist.count, 2);
  std::array<sai_u64_range_t, 2> actual{};
  read.value.u64rangelist.list = actual.data();
  ASSERT_EQ(api->get_tam_int_attribute(id, 1, &read), SAI_STATUS_SUCCESS);
  EXPECT_EQ(actual[0].max, 1ULL << 32);
  EXPECT_EQ(actual[1].min, (1ULL << 32) + 1);
  EXPECT_EQ(actual[1].max, 1ULL << 40);
  sai_attribute_t interval{};
  interval.id = SAI_TAM_INT_ATTR_BW_TIME_INTERVAL;
  interval.value.u32 = 256;
  ASSERT_EQ(api->set_tam_int_attribute(id, &interval), SAI_STATUS_SUCCESS);
  interval.value.u32 = 0;
  ASSERT_EQ(api->get_tam_int_attribute(id, 1, &interval), SAI_STATUS_SUCCESS);
  EXPECT_EQ(interval.value.u32, 256);
  EXPECT_EQ(api->remove_tam_int(id), SAI_STATUS_SUCCESS);
}

TEST_F(TamApiTest, CsigCompactQuantization) {
  using A = SaiTamIntTraits::Attributes;
  const std::vector<sai_u64_range_t> ranges{
      {0, 1ULL << 32}, {(1ULL << 32) + 1, 1ULL << 40}};
  SaiTamIntTraits::CreateAttributes attrs{
      SAI_TAM_INT_TYPE_CSIG_COMPACT,
      0,
      SAI_TAM_INT_PRESENCE_TYPE_ETH_TYPE,
      true,
      0x9900,
      SAI_CSIG_SIGNAL_TYPE_ABW,
      256,
      2,
      std::vector<sai_uint16_t>{7, 3},
      ranges,
      std::nullopt,
      std::nullopt,
      false};
  const auto id = tamApi->create<SaiTamIntTraits>(attrs, switchId);
  EXPECT_EQ(tamApi->getAttribute(id, A::QuantBandUint64RangeList{}), ranges);
  EXPECT_EQ(
      tamApi->getAttribute(id, A::QuantBandId{}),
      (std::vector<sai_uint16_t>{7, 3}));
  EXPECT_EQ(tamApi->getAttribute(id, A::QuantBands{}), 2);
  EXPECT_EQ(tamApi->getAttribute(id, A::BwTimeInterval{}), 256);

  A::QuantBandUint64RangeList original{ranges};
  auto copy = original;
  ASSERT_NE(
      original.saiAttr()->value.u64rangelist.list,
      copy.saiAttr()->value.u64rangelist.list);
  const std::vector<sai_u64_range_t> updated{{1ULL << 33, 1ULL << 34}};
  tamApi->setAttribute(id, A::QuantBandUint64RangeList{updated});
  EXPECT_EQ(tamApi->getAttribute(id, A::QuantBandUint64RangeList{}), updated);
  EXPECT_EQ(copy.value(), ranges);
  tamApi->remove(id);
}

TEST_F(TamApiTest, CsigWideQuantization) {
  using A = SaiTamIntTraits::Attributes;
  SaiTamIntTraits::CreateAttributes attrs{
      SAI_TAM_INT_TYPE_CSIG_WIDE,
      0,
      SAI_TAM_INT_PRESENCE_TYPE_ETH_TYPE,
      true,
      0x9901,
      SAI_CSIG_SIGNAL_TYPE_DELAY,
      std::nullopt,
      1048576,
      std::nullopt,
      std::nullopt,
      1024,
      32,
      true};
  const auto id = tamApi->create<SaiTamIntTraits>(attrs, switchId);
  EXPECT_EQ(tamApi->getAttribute(id, A::QuantBands{}), 1048576);
  EXPECT_EQ(tamApi->getAttribute(id, A::WideQuantBaseValue{}), 1024);
  EXPECT_EQ(tamApi->getAttribute(id, A::WideQuantStepValue{}), 32);
  EXPECT_TRUE(tamApi->getAttribute(id, A::CsigDBit{}));
  std::optional<A::BwTimeInterval> interval;
  EXPECT_FALSE(tamApi->getAttribute(id, interval).has_value());
  tamApi->setAttribute(id, A::WideQuantStepValue{64});
  EXPECT_EQ(tamApi->getAttribute(id, A::WideQuantStepValue{}), 64);
  EXPECT_THROW(
      tamApi->setAttribute(id, A::EthTypeCodePoint{0x9902}), SaiApiError);
  tamApi->remove(id);
}

#endif
