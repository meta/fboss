// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/hw/sai/fake/FakeManager.h"

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class FakeSaiTam {
 public:
  FakeSaiTam(
      const std::vector<sai_object_id_t>& events,
      const std::vector<sai_int32_t>& bindpoints)
      : events_(events), bindpoints_(bindpoints) {}

  sai_object_id_t id;
  std::vector<sai_object_id_t> events_;
  std::vector<sai_int32_t> bindpoints_;
};

class FakeSaiTamEvent {
 public:
  FakeSaiTamEvent(
      sai_int32_t eventType,
      const std::vector<sai_object_id_t>& actions,
      const std::vector<sai_object_id_t>& collectors,
      const std::vector<sai_int32_t>& switchEvents,
      sai_int32_t deviceId,
      sai_int32_t eventId,
      std::vector<sai_object_id_t> extensionsCollectorList,
      std::vector<sai_int32_t> packetDropTypeMmu,
      std::vector<sai_int32_t> packetDropTypeIngress,
      sai_object_id_t agingGroup,
      sai_object_id_t ingressSamplepacketEnable,
      sai_object_id_t threshold = 0)
      : eventType_(eventType),
        actions_(actions),
        collectors_(collectors),
        switchEvents_(switchEvents),
        deviceId_(deviceId),
        eventId_(eventId),
        extensionsCollectorList_(std::move(extensionsCollectorList)),
        packetDropTypeMmu_(std::move(packetDropTypeMmu)),
        packetDropTypeIngress_(std::move(packetDropTypeIngress)),
        agingGroup_(agingGroup),
        ingressSamplepacketEnable_(ingressSamplepacketEnable),
        threshold_(threshold) {}
  sai_object_id_t id;
  sai_int32_t eventType_;
  std::vector<sai_object_id_t> actions_;
  std::vector<sai_object_id_t> collectors_;
  std::vector<sai_int32_t> switchEvents_;
  sai_int32_t deviceId_;
  sai_int32_t eventId_;
  std::vector<sai_object_id_t> extensionsCollectorList_;
  std::vector<sai_int32_t> packetDropTypeMmu_;
  std::vector<sai_int32_t> packetDropTypeIngress_;
  sai_object_id_t agingGroup_;
  sai_object_id_t ingressSamplepacketEnable_;
  sai_object_id_t threshold_;
};

class FakeSaiTamEventAction {
 public:
  explicit FakeSaiTamEventAction(sai_object_id_t report) : report_(report) {}
  sai_object_id_t id;
  sai_object_id_t report_;
};

class FakeSaiTamReport {
 public:
  explicit FakeSaiTamReport(sai_int32_t type) : type_(type) {}
  sai_object_id_t id;
  sai_int32_t type_;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::optional<sai_uint32_t> sampleRate_;
  std::optional<sai_uint64_t> maxReportRate_;
  std::optional<sai_uint64_t> maxReportBurst_;
#endif
};

class FakeSaiTamEventThreshold {
 public:
  FakeSaiTamEventThreshold(
      std::optional<sai_int32_t> unit,
      std::optional<sai_uint32_t> rate)
      : unit_(unit), rate_(rate) {}
  sai_object_id_t id{};
  std::optional<sai_int32_t> unit_;
  std::optional<sai_uint32_t> rate_;
};

class FakeSaiTamTransport {
 public:
  FakeSaiTamTransport(
      sai_int32_t transportType,
      std::optional<sai_uint32_t> srcPort,
      std::optional<sai_uint32_t> dstPort,
      std::optional<sai_uint32_t> mtu,
      std::optional<folly::MacAddress> srcMac,
      std::optional<folly::MacAddress> dstMac)
      : transportType_(transportType),
        srcPort_(srcPort),
        dstPort_(dstPort),
        mtu_(mtu),
        srcMac_(srcMac),
        dstMac_(dstMac) {}
  sai_object_id_t id;
  sai_int32_t transportType_;
  std::optional<sai_uint32_t> srcPort_;
  std::optional<sai_uint32_t> dstPort_;
  std::optional<sai_uint32_t> mtu_;
  std::optional<folly::MacAddress> srcMac_;
  std::optional<folly::MacAddress> dstMac_;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::optional<sai_uint16_t> greProtocolType_;
#endif
};

class FakeSaiTamCollector {
 public:
  FakeSaiTamCollector(
      const folly::IPAddress& srcIp,
      const folly::IPAddress& dstIp,
      std::optional<sai_uint16_t> truncateSize,
      sai_object_id_t transport,
      std::optional<sai_uint8_t> dscp)
      : srcIp_(srcIp),
        dstIp_(dstIp),
        truncateSize_(truncateSize),
        transport_(transport),
        dscp_(dscp) {}
  sai_object_id_t id;
  folly::IPAddress srcIp_;
  folly::IPAddress dstIp_;
  std::optional<sai_uint16_t> truncateSize_;
  sai_object_id_t transport_;
  std::optional<sai_uint8_t> dscp_;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::optional<folly::MacAddress> srcMac_;
  std::optional<folly::MacAddress> dstMac_;
  std::optional<sai_object_id_t> destination_;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  std::optional<bool> localhost_;
#endif
};

using FakeTamManager = FakeManager<sai_object_id_t, FakeSaiTam>;
using FakeTamEventManager = FakeManager<sai_object_id_t, FakeSaiTamEvent>;
using FakeTamEventActionManager =
    FakeManager<sai_object_id_t, FakeSaiTamEventAction>;
using FakeTamReportManager = FakeManager<sai_object_id_t, FakeSaiTamReport>;
using FakeTamEventThresholdManager =
    FakeManager<sai_object_id_t, FakeSaiTamEventThreshold>;
using FakeTamTransportManager =
    FakeManager<sai_object_id_t, FakeSaiTamTransport>;
using FakeTamCollectorManager =
    FakeManager<sai_object_id_t, FakeSaiTamCollector>;

void populate_tam_api(sai_tam_api_t** tam_api);

} // namespace facebook::fboss
