// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/fake/FakeSaiTam.h"

#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

extern "C" {
#include "fboss/agent/hw/sai/api/fake/saifakeextensions.h"
}

namespace {

using facebook::fboss::FakeSai;

sai_status_t create_tam(
    sai_object_id_t* id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  std::vector<sai_object_id_t> events;
  std::vector<sai_int32_t> bindpoints;
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_ATTR_EVENT_OBJECTS_LIST:
        std::copy(
            attr_list[i].value.objlist.list,
            attr_list[i].value.objlist.list + attr_list[i].value.objlist.count,
            std::back_inserter(events));
        break;

      case SAI_TAM_ATTR_TAM_BIND_POINT_TYPE_LIST:
        std::copy(
            attr_list[i].value.s32list.list,
            attr_list[i].value.s32list.list + attr_list[i].value.s32list.count,
            std::back_inserter(bindpoints));
        break;

      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  auto fs = FakeSai::getInstance();
  *id = fs->tamManager.create(events, bindpoints);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_tam(sai_object_id_t id) {
  auto fs = FakeSai::getInstance();
  fs->tamManager.remove(id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_tam_attribute(
    sai_object_id_t id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& tam = fs->tamManager.get(id);
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_ATTR_EVENT_OBJECTS_LIST:
        if (attr_list[i].value.objlist.count < tam.events_.size()) {
          attr_list[i].value.objlist.count = tam.events_.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (auto j = 0; j < attr_list[i].value.objlist.count; j++) {
          attr_list[i].value.objlist.list[j] = tam.events_[j];
        }
        break;

      case SAI_TAM_ATTR_TAM_BIND_POINT_TYPE_LIST:
        if (attr_list[i].value.s32list.count < tam.bindpoints_.size()) {
          attr_list[i].value.objlist.count = tam.bindpoints_.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (auto j = 0; j < attr_list[i].value.s32list.count; j++) {
          attr_list[i].value.s32list.list[j] = tam.bindpoints_[j];
        }
        break;

      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_tam_attribute(
    sai_object_id_t id,
    const sai_attribute_t* attr_list) {
  std::vector<sai_object_id_t> events;
  std::vector<sai_int32_t> bindpoints;
  switch (attr_list[0].id) {
    case SAI_TAM_ATTR_EVENT_OBJECTS_LIST:
      std::copy(
          attr_list[0].value.objlist.list,
          attr_list[0].value.objlist.list + attr_list[0].value.objlist.count,
          std::back_inserter(events));
      break;

    case SAI_TAM_ATTR_TAM_BIND_POINT_TYPE_LIST:
      std::copy(
          attr_list[0].value.s32list.list,
          attr_list[0].value.s32list.list + attr_list[0].value.s32list.count,
          std::back_inserter(bindpoints));
      break;
    default:
      return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
  }
  try {
    auto fs = FakeSai::getInstance();
    auto& tam = fs->tamManager.get(id);
    tam.events_ = events;
    tam.bindpoints_ = bindpoints;
  } catch (...) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_tam_event(
    sai_object_id_t* id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  sai_int32_t eventType{};
  std::vector<sai_object_id_t> actions{};
  std::vector<sai_object_id_t> collectors{};
  std::vector<sai_int32_t> switchEvents{};

  sai_int32_t deviceId{};
  sai_int32_t eventId{};
  std::vector<sai_object_id_t> extensionsCollectorList{};
  std::vector<sai_int32_t> packetDropTypeMmu{};
  std::vector<sai_int32_t> packetDropTypeIngress{};
  sai_object_id_t agingGroup{};
  sai_object_id_t ingressSamplepacketEnable{};
  sai_object_id_t threshold{};

  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_EVENT_ATTR_TYPE:
        eventType = attr_list[i].value.s32;
        break;

      case SAI_TAM_EVENT_ATTR_ACTION_LIST:
        std::copy(
            attr_list[i].value.objlist.list,
            attr_list[i].value.objlist.list + attr_list[i].value.objlist.count,
            std::back_inserter(actions));
        break;

      case SAI_TAM_EVENT_ATTR_COLLECTOR_LIST:
        std::copy(
            attr_list[i].value.objlist.list,
            attr_list[i].value.objlist.list + attr_list[i].value.objlist.count,
            std::back_inserter(collectors));
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_SWITCH_EVENT_TYPE:
        std::copy(
            attr_list[i].value.s32list.list,
            attr_list[i].value.s32list.list + attr_list[i].value.s32list.count,
            std::back_inserter(switchEvents));
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_DEVICE_ID:
        deviceId = attr_list[i].value.s32;
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_SWITCH_EVENT_ID:
        eventId = attr_list[i].value.s32;
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_EXTENSIONS_COLLECTOR_LIST:
        std::copy(
            attr_list[i].value.objlist.list,
            attr_list[i].value.objlist.list + attr_list[i].value.objlist.count,
            std::back_inserter(extensionsCollectorList));
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_PACKET_DROP_TYPE_MMU:
        std::copy(
            attr_list[i].value.s32list.list,
            attr_list[i].value.s32list.list + attr_list[i].value.s32list.count,
            std::back_inserter(packetDropTypeMmu));
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_PACKET_DROP_TYPE_INGRESS:
        std::copy(
            attr_list[i].value.s32list.list,
            attr_list[i].value.s32list.list + attr_list[i].value.s32list.count,
            std::back_inserter(packetDropTypeIngress));
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_AGING_GROUP:
        agingGroup = attr_list[i].value.oid;
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_INGRESS_SAMPLEPACKET_ENABLE:
        ingressSamplepacketEnable = attr_list[i].value.oid;
        break;

      case SAI_TAM_EVENT_ATTR_THRESHOLD:
        threshold = attr_list[i].value.oid;
        break;

      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  auto fs = FakeSai::getInstance();
  *id = fs->tamEventManager.create(
      eventType,
      actions,
      collectors,
      switchEvents,
      deviceId,
      eventId,
      extensionsCollectorList,
      packetDropTypeMmu,
      packetDropTypeIngress,
      agingGroup,
      ingressSamplepacketEnable,
      threshold);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_tam_event(sai_object_id_t id) {
  auto fs = FakeSai::getInstance();
  fs->tamEventManager.remove(id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_tam_event_attribute(
    sai_object_id_t id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& eventAction = fs->tamEventManager.get(id);
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_EVENT_ATTR_TYPE:
        attr_list[i].value.s32 = eventAction.eventType_;
        break;

      case SAI_TAM_EVENT_ATTR_ACTION_LIST:
        if (attr_list[i].value.objlist.count < eventAction.actions_.size()) {
          attr_list[i].value.objlist.count = eventAction.actions_.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (auto j = 0; j < attr_list[i].value.objlist.count; j++) {
          attr_list[i].value.objlist.list[j] = eventAction.actions_[j];
        }
        break;

      case SAI_TAM_EVENT_ATTR_COLLECTOR_LIST:
        if (attr_list[i].value.objlist.count < eventAction.collectors_.size()) {
          attr_list[i].value.objlist.count = eventAction.collectors_.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (auto j = 0; j < attr_list[i].value.objlist.count; j++) {
          attr_list[i].value.objlist.list[j] = eventAction.collectors_[j];
        }
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_SWITCH_EVENT_TYPE:
        if (attr_list[i].value.s32list.count <
            eventAction.switchEvents_.size()) {
          attr_list[i].value.s32list.count = eventAction.switchEvents_.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (auto j = 0; j < attr_list[i].value.s32list.count; j++) {
          attr_list[i].value.s32list.list[j] = eventAction.switchEvents_[j];
        }
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_DEVICE_ID:
        attr_list[i].value.s32 = eventAction.deviceId_;
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_SWITCH_EVENT_ID:
        attr_list[i].value.s32 = eventAction.eventId_;
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_EXTENSIONS_COLLECTOR_LIST:
        if (attr_list[i].value.objlist.count <
            eventAction.extensionsCollectorList_.size()) {
          attr_list[i].value.objlist.count =
              eventAction.extensionsCollectorList_.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (auto j = 0; j < attr_list[i].value.objlist.count; j++) {
          attr_list[i].value.objlist.list[j] =
              eventAction.extensionsCollectorList_[j];
        }
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_PACKET_DROP_TYPE_MMU:
        if (attr_list[i].value.s32list.count <
            eventAction.packetDropTypeMmu_.size()) {
          attr_list[i].value.s32list.count =
              static_cast<uint32_t>(eventAction.packetDropTypeMmu_.size());
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (auto j = 0; j < attr_list[i].value.s32list.count; j++) {
          attr_list[i].value.s32list.list[j] =
              eventAction.packetDropTypeMmu_[j];
        }
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_PACKET_DROP_TYPE_INGRESS:
        if (attr_list[i].value.s32list.count <
            eventAction.packetDropTypeIngress_.size()) {
          attr_list[i].value.s32list.count =
              static_cast<uint32_t>(eventAction.packetDropTypeIngress_.size());
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        for (auto j = 0; j < attr_list[i].value.s32list.count; j++) {
          attr_list[i].value.s32list.list[j] =
              eventAction.packetDropTypeIngress_[j];
        }
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_AGING_GROUP:
        attr_list[i].value.oid = eventAction.agingGroup_;
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_INGRESS_SAMPLEPACKET_ENABLE:
        attr_list[i].value.oid = eventAction.ingressSamplepacketEnable_;
        break;

      case SAI_TAM_EVENT_ATTR_THRESHOLD:
        attr_list[i].value.oid = eventAction.threshold_;
        break;

      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_tam_event_attribute(
    sai_object_id_t id,
    const sai_attribute_t* attr) {
  try {
    auto fs = FakeSai::getInstance();
    auto& tamEvent = fs->tamEventManager.get(id);
    switch (attr->id) {
      case SAI_TAM_EVENT_ATTR_TYPE:
        tamEvent.eventType_ = attr->value.s32;
        break;

      case SAI_TAM_EVENT_ATTR_ACTION_LIST:
        tamEvent.actions_.clear();
        std::copy(
            attr->value.objlist.list,
            attr->value.objlist.list + attr->value.objlist.count,
            std::back_inserter(tamEvent.actions_));
        break;

      case SAI_TAM_EVENT_ATTR_COLLECTOR_LIST:
        tamEvent.collectors_.clear();
        std::copy(
            attr->value.objlist.list,
            attr->value.objlist.list + attr->value.objlist.count,
            std::back_inserter(tamEvent.collectors_));
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_SWITCH_EVENT_TYPE:
        tamEvent.switchEvents_.clear();
        std::copy(
            attr->value.s32list.list,
            attr->value.s32list.list + attr->value.s32list.count,
            std::back_inserter(tamEvent.switchEvents_));
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_DEVICE_ID:
        tamEvent.deviceId_ = attr->value.s32;
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_SWITCH_EVENT_ID:
        tamEvent.eventId_ = attr->value.s32;
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_EXTENSIONS_COLLECTOR_LIST:
        tamEvent.extensionsCollectorList_.clear();
        std::copy(
            attr->value.objlist.list,
            attr->value.objlist.list + attr->value.objlist.count,
            std::back_inserter(tamEvent.extensionsCollectorList_));
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_PACKET_DROP_TYPE_MMU:
        tamEvent.packetDropTypeMmu_.clear();
        std::copy(
            attr->value.s32list.list,
            attr->value.s32list.list + attr->value.s32list.count,
            std::back_inserter(tamEvent.packetDropTypeMmu_));
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_PACKET_DROP_TYPE_INGRESS:
        tamEvent.packetDropTypeIngress_.clear();
        std::copy(
            attr->value.s32list.list,
            attr->value.s32list.list + attr->value.s32list.count,
            std::back_inserter(tamEvent.packetDropTypeIngress_));
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_AGING_GROUP:
        tamEvent.agingGroup_ = attr->value.oid;
        break;

      case SAI_TAM_EVENT_ATTR_FAKE_INGRESS_SAMPLEPACKET_ENABLE:
        tamEvent.ingressSamplepacketEnable_ = attr->value.oid;
        break;

      case SAI_TAM_EVENT_ATTR_THRESHOLD:
        tamEvent.threshold_ = attr->value.oid;
        break;

      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
    }
  } catch (...) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_tam_event_action(
    sai_object_id_t* id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  sai_object_id_t reportType{};
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_EVENT_ACTION_ATTR_REPORT_TYPE:
        reportType = attr_list[i].value.oid;
        break;
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  auto fs = FakeSai::getInstance();
  *id = fs->tamEventActionManager.create(reportType);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_tam_event_action(sai_object_id_t id) {
  auto fs = FakeSai::getInstance();
  fs->tamEventActionManager.remove(id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_tam_event_action_attribute(
    sai_object_id_t id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& eventAction = fs->tamEventActionManager.get(id);
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_EVENT_ACTION_ATTR_REPORT_TYPE:
        attr_list[i].value.oid = eventAction.report_;
        break;
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_tam_event_action_attribute(
    sai_object_id_t id,
    const sai_attribute_t* attr_list) {
  sai_object_id_t reportType{};
  switch (attr_list[0].id) {
    case SAI_TAM_EVENT_ACTION_ATTR_REPORT_TYPE:
      reportType = attr_list[0].value.oid;
      break;
    default:
      return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
  }
  try {
    auto fs = FakeSai::getInstance();
    auto& tamEventAction = fs->tamEventActionManager.get(id);
    tamEventAction.report_ = reportType;
  } catch (...) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_tam_report(
    sai_object_id_t* id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  sai_int32_t type{};
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::optional<sai_uint32_t> sampleRate;
  std::optional<sai_uint64_t> maxReportRate;
  std::optional<sai_uint64_t> maxReportBurst;
#endif
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_REPORT_ATTR_TYPE:
        type = attr_list[i].value.s32;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_TAM_REPORT_ATTR_SAMPLE_RATE:
        sampleRate = attr_list[i].value.u32;
        break;
      case SAI_TAM_REPORT_ATTR_MAX_REPORT_RATE:
        maxReportRate = attr_list[i].value.u64;
        break;
      case SAI_TAM_REPORT_ATTR_MAX_REPORT_BURST:
        maxReportBurst = attr_list[i].value.u64;
        break;
#endif
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  auto fs = FakeSai::getInstance();
  *id = fs->tamReportManager.create(type);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  auto& report = fs->tamReportManager.get(*id);
  report.sampleRate_ = sampleRate;
  report.maxReportRate_ = maxReportRate;
  report.maxReportBurst_ = maxReportBurst;
#endif
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_tam_report(sai_object_id_t id) {
  auto fs = FakeSai::getInstance();
  fs->tamReportManager.remove(id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_tam_report_attribute(
    sai_object_id_t id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  const auto& tamReport = fs->tamReportManager.get(id);
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_REPORT_ATTR_TYPE:
        attr_list[i].value.s32 = tamReport.type_;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_TAM_REPORT_ATTR_SAMPLE_RATE:
        attr_list[i].value.u32 = tamReport.sampleRate_.value_or(0);
        break;
      case SAI_TAM_REPORT_ATTR_MAX_REPORT_RATE:
        attr_list[i].value.u64 = tamReport.maxReportRate_.value_or(0);
        break;
      case SAI_TAM_REPORT_ATTR_MAX_REPORT_BURST:
        attr_list[i].value.u64 = tamReport.maxReportBurst_.value_or(0);
        break;
#endif
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_tam_report_attribute(
    sai_object_id_t id,
    const sai_attribute_t* attr_list) {
  try {
    auto fs = FakeSai::getInstance();
    auto& tamReport = fs->tamReportManager.get(id);
    switch (attr_list[0].id) {
      case SAI_TAM_REPORT_ATTR_TYPE:
        tamReport.type_ = attr_list[0].value.s32;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_TAM_REPORT_ATTR_SAMPLE_RATE:
        tamReport.sampleRate_ = attr_list[0].value.u32;
        break;
      case SAI_TAM_REPORT_ATTR_MAX_REPORT_RATE:
        tamReport.maxReportRate_ = attr_list[0].value.u64;
        break;
      case SAI_TAM_REPORT_ATTR_MAX_REPORT_BURST:
        tamReport.maxReportBurst_ = attr_list[0].value.u64;
        break;
#endif
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
    }
  } catch (...) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_tam_event_threshold(
    sai_object_id_t* id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  std::optional<sai_int32_t> unit;
  std::optional<sai_uint32_t> rate;
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_EVENT_THRESHOLD_ATTR_UNIT:
        unit = attr_list[i].value.s32;
        break;
      case SAI_TAM_EVENT_THRESHOLD_ATTR_RATE:
        rate = attr_list[i].value.u32;
        break;
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  auto fs = FakeSai::getInstance();
  *id = fs->tamEventThresholdManager.create(unit, rate);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_tam_event_threshold(sai_object_id_t id) {
  auto fs = FakeSai::getInstance();
  fs->tamEventThresholdManager.remove(id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_tam_event_threshold_attribute(
    sai_object_id_t id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  const auto& threshold = fs->tamEventThresholdManager.get(id);
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_EVENT_THRESHOLD_ATTR_UNIT:
        attr_list[i].value.s32 = threshold.unit_.value_or(0);
        break;
      case SAI_TAM_EVENT_THRESHOLD_ATTR_RATE:
        attr_list[i].value.u32 = threshold.rate_.value_or(0);
        break;
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_tam_event_threshold_attribute(
    sai_object_id_t id,
    const sai_attribute_t* attr) {
  try {
    auto fs = FakeSai::getInstance();
    auto& threshold = fs->tamEventThresholdManager.get(id);
    switch (attr->id) {
      case SAI_TAM_EVENT_THRESHOLD_ATTR_UNIT:
        threshold.unit_ = attr->value.s32;
        break;
      case SAI_TAM_EVENT_THRESHOLD_ATTR_RATE:
        threshold.rate_ = attr->value.u32;
        break;
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
    }
  } catch (...) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_tam_transport(
    sai_object_id_t* id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  sai_int32_t transportType{};
  std::optional<sai_uint32_t> srcPort;
  std::optional<sai_uint32_t> dstPort;
  std::optional<sai_uint32_t> mtu;
  std::optional<folly::MacAddress> srcMac;
  std::optional<folly::MacAddress> dstMac;

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::optional<sai_uint16_t> greProtocolType;
#endif

  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_TRANSPORT_ATTR_TRANSPORT_TYPE:
        transportType = attr_list[i].value.s32;
        break;

      case SAI_TAM_TRANSPORT_ATTR_SRC_PORT:
        srcPort = attr_list[i].value.u32;
        break;

      case SAI_TAM_TRANSPORT_ATTR_DST_PORT:
        dstPort = attr_list[i].value.u32;
        break;

      case SAI_TAM_TRANSPORT_ATTR_MTU:
        mtu = attr_list[i].value.u32;
        break;

      case SAI_TAM_TRANSPORT_ATTR_FAKE_SRC_MAC_ADDRESS:
        srcMac = facebook::fboss::fromSaiMacAddress(attr_list[i].value.mac);
        break;

      case SAI_TAM_TRANSPORT_ATTR_FAKE_DST_MAC_ADDRESS:
        dstMac = facebook::fboss::fromSaiMacAddress(attr_list[i].value.mac);
        break;

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_TAM_TRANSPORT_ATTR_GRE_PROTOCOL_TYPE:
        greProtocolType = attr_list[i].value.u16;
        break;
#endif
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  auto fs = FakeSai::getInstance();
  *id = fs->tamTransportManager.create(
      transportType, srcPort, dstPort, mtu, srcMac, dstMac);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  fs->tamTransportManager.get(*id).greProtocolType_ = greProtocolType;
#endif
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_tam_transport(sai_object_id_t id) {
  auto fs = FakeSai::getInstance();
  fs->tamTransportManager.remove(id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_tam_transport_attribute(
    sai_object_id_t id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& transport = fs->tamTransportManager.get(id);
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_TRANSPORT_ATTR_TRANSPORT_TYPE:
        attr_list[i].value.s32 = transport.transportType_;
        break;

      case SAI_TAM_TRANSPORT_ATTR_SRC_PORT:
        if (!transport.srcPort_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        attr_list[i].value.u32 = *transport.srcPort_;
        break;

      case SAI_TAM_TRANSPORT_ATTR_DST_PORT:
        if (!transport.dstPort_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        attr_list[i].value.u32 = *transport.dstPort_;
        break;

      case SAI_TAM_TRANSPORT_ATTR_MTU:
        if (!transport.mtu_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        attr_list[i].value.u32 = *transport.mtu_;
        break;

      case SAI_TAM_TRANSPORT_ATTR_FAKE_SRC_MAC_ADDRESS:
        if (!transport.srcMac_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        facebook::fboss::toSaiMacAddress(
            transport.srcMac_.value(), attr_list[i].value.mac);
        break;

      case SAI_TAM_TRANSPORT_ATTR_FAKE_DST_MAC_ADDRESS:
        if (!transport.dstMac_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        facebook::fboss::toSaiMacAddress(
            transport.dstMac_.value(), attr_list[i].value.mac);
        break;

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_TAM_TRANSPORT_ATTR_GRE_PROTOCOL_TYPE:
        if (!transport.greProtocolType_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        attr_list[i].value.u16 = *transport.greProtocolType_;
        break;
#endif
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_tam_transport_attribute(
    sai_object_id_t id,
    const sai_attribute_t* attr) {
  try {
    auto fs = FakeSai::getInstance();
    auto& transport = fs->tamTransportManager.get(id);
    switch (attr->id) {
      case SAI_TAM_TRANSPORT_ATTR_TRANSPORT_TYPE:
        transport.transportType_ = attr->value.s32;
        break;

      case SAI_TAM_TRANSPORT_ATTR_SRC_PORT:
        transport.srcPort_ = attr->value.u32;
        break;

      case SAI_TAM_TRANSPORT_ATTR_DST_PORT:
        transport.dstPort_ = attr->value.u32;
        break;

      case SAI_TAM_TRANSPORT_ATTR_MTU:
        transport.mtu_ = attr->value.u32;
        break;

      case SAI_TAM_TRANSPORT_ATTR_FAKE_SRC_MAC_ADDRESS:
        transport.srcMac_ = facebook::fboss::fromSaiMacAddress(attr->value.mac);
        break;

      case SAI_TAM_TRANSPORT_ATTR_FAKE_DST_MAC_ADDRESS:
        transport.dstMac_ = facebook::fboss::fromSaiMacAddress(attr->value.mac);
        break;

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_TAM_TRANSPORT_ATTR_GRE_PROTOCOL_TYPE:
        transport.greProtocolType_ = attr->value.u16;
        break;
#endif
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
    }
  } catch (...) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_tam_collector(
    sai_object_id_t* id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  folly::IPAddress srcIp{};
  folly::IPAddress dstIp{};
  std::optional<sai_uint16_t> truncateSize{};
  sai_object_id_t transport{};
  std::optional<sai_uint8_t> dscp{};

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  std::optional<folly::MacAddress> srcMac;
  std::optional<folly::MacAddress> dstMac;
  std::optional<sai_object_id_t> destination;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  std::optional<bool> localhost;
#endif

  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_COLLECTOR_ATTR_SRC_IP:
        srcIp = facebook::fboss::fromSaiIpAddress(attr_list[i].value.ipaddr);
        break;

      case SAI_TAM_COLLECTOR_ATTR_DST_IP:
        dstIp = facebook::fboss::fromSaiIpAddress(attr_list[i].value.ipaddr);
        break;

      case SAI_TAM_COLLECTOR_ATTR_TRUNCATE_SIZE:
        truncateSize = attr_list[i].value.u16;
        break;

      case SAI_TAM_COLLECTOR_ATTR_TRANSPORT:
        transport = attr_list[i].value.oid;
        break;

      case SAI_TAM_COLLECTOR_ATTR_DSCP_VALUE:
        dscp = attr_list[i].value.u8;
        break;

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_TAM_COLLECTOR_ATTR_SRC_MAC:
        srcMac = facebook::fboss::fromSaiMacAddress(attr_list[i].value.mac);
        break;
      case SAI_TAM_COLLECTOR_ATTR_DST_MAC:
        dstMac = facebook::fboss::fromSaiMacAddress(attr_list[i].value.mac);
        break;
      case SAI_TAM_COLLECTOR_ATTR_DESTINATION:
        destination = attr_list[i].value.oid;
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      case SAI_TAM_COLLECTOR_ATTR_LOCALHOST:
        localhost = attr_list[i].value.booldata;
        break;
#endif
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  auto fs = FakeSai::getInstance();
  *id = fs->tamCollectorManager.create(
      srcIp, dstIp, truncateSize, transport, dscp);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  fs->tamCollectorManager.get(*id).srcMac_ = srcMac;
  fs->tamCollectorManager.get(*id).dstMac_ = dstMac;
  fs->tamCollectorManager.get(*id).destination_ = destination;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  fs->tamCollectorManager.get(*id).localhost_ = localhost;
#endif
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_tam_collector(sai_object_id_t id) {
  auto fs = FakeSai::getInstance();
  fs->tamCollectorManager.remove(id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_tam_collector_attribute(
    sai_object_id_t id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& collector = fs->tamCollectorManager.get(id);
  for (auto i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_TAM_COLLECTOR_ATTR_SRC_IP:
        attr_list[i].value.ipaddr =
            facebook::fboss::toSaiIpAddress(folly::IPAddress(collector.srcIp_));
        break;

      case SAI_TAM_COLLECTOR_ATTR_DST_IP:
        attr_list[i].value.ipaddr =
            facebook::fboss::toSaiIpAddress(folly::IPAddress(collector.dstIp_));
        break;

      case SAI_TAM_COLLECTOR_ATTR_TRUNCATE_SIZE:
        if (!collector.truncateSize_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        attr_list[i].value.u16 = collector.truncateSize_.value();
        break;

      case SAI_TAM_COLLECTOR_ATTR_TRANSPORT:
        attr_list[i].value.oid = collector.transport_;
        break;

      case SAI_TAM_COLLECTOR_ATTR_DSCP_VALUE:
        if (!collector.dscp_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        attr_list[i].value.u8 = collector.dscp_.value();
        break;

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_TAM_COLLECTOR_ATTR_SRC_MAC:
        if (!collector.srcMac_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        facebook::fboss::toSaiMacAddress(
            *collector.srcMac_, attr_list[i].value.mac);
        break;
      case SAI_TAM_COLLECTOR_ATTR_DST_MAC:
        if (!collector.dstMac_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        facebook::fboss::toSaiMacAddress(
            *collector.dstMac_, attr_list[i].value.mac);
        break;
      case SAI_TAM_COLLECTOR_ATTR_DESTINATION:
        if (!collector.destination_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        attr_list[i].value.oid = *collector.destination_;
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      case SAI_TAM_COLLECTOR_ATTR_LOCALHOST:
        if (!collector.localhost_) {
          return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + i;
        }
        attr_list[i].value.booldata = *collector.localhost_;
        break;
#endif
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0 + i;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_tam_collector_attribute(
    sai_object_id_t id,
    const sai_attribute_t* attr) {
  try {
    auto fs = FakeSai::getInstance();
    auto& collector = fs->tamCollectorManager.get(id);

    switch (attr->id) {
      case SAI_TAM_COLLECTOR_ATTR_SRC_IP:
        collector.srcIp_ =
            facebook::fboss::fromSaiIpAddress(attr->value.ipaddr);
        break;

      case SAI_TAM_COLLECTOR_ATTR_DST_IP:
        collector.dstIp_ =
            facebook::fboss::fromSaiIpAddress(attr->value.ipaddr);
        break;

      case SAI_TAM_COLLECTOR_ATTR_TRUNCATE_SIZE:
        collector.truncateSize_ = attr->value.u16;
        break;

      case SAI_TAM_COLLECTOR_ATTR_TRANSPORT:
        collector.transport_ = attr->value.oid;
        break;

      case SAI_TAM_COLLECTOR_ATTR_DSCP_VALUE:
        collector.dscp_ = attr->value.u8;
        break;

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_TAM_COLLECTOR_ATTR_SRC_MAC:
        collector.srcMac_ = facebook::fboss::fromSaiMacAddress(attr->value.mac);
        break;
      case SAI_TAM_COLLECTOR_ATTR_DST_MAC:
        collector.dstMac_ = facebook::fboss::fromSaiMacAddress(attr->value.mac);
        break;
      case SAI_TAM_COLLECTOR_ATTR_DESTINATION:
        collector.destination_ = attr->value.oid;
        break;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      case SAI_TAM_COLLECTOR_ATTR_LOCALHOST:
        collector.localhost_ = attr->value.booldata;
        break;
#endif
      default:
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
    }
  } catch (...) {
    return SAI_STATUS_ITEM_NOT_FOUND;
  }
  return SAI_STATUS_SUCCESS;
}

} // namespace

namespace facebook::fboss {
static sai_tam_api_t _tam_api;

void populate_tam_api(sai_tam_api_t** tam_api) {
  _tam_api.create_tam = &create_tam;
  _tam_api.remove_tam = &remove_tam;
  _tam_api.set_tam_attribute = &set_tam_attribute;
  _tam_api.get_tam_attribute = &get_tam_attribute;

  _tam_api.create_tam_event = &create_tam_event;
  _tam_api.remove_tam_event = &remove_tam_event;
  _tam_api.set_tam_event_attribute = &set_tam_event_attribute;
  _tam_api.get_tam_event_attribute = &get_tam_event_attribute;

  _tam_api.create_tam_event_action = &create_tam_event_action;
  _tam_api.remove_tam_event_action = &remove_tam_event_action;
  _tam_api.set_tam_event_action_attribute = &set_tam_event_action_attribute;
  _tam_api.get_tam_event_action_attribute = &get_tam_event_action_attribute;

  _tam_api.create_tam_report = &create_tam_report;
  _tam_api.remove_tam_report = &remove_tam_report;
  _tam_api.set_tam_report_attribute = &set_tam_report_attribute;
  _tam_api.get_tam_report_attribute = &get_tam_report_attribute;

  _tam_api.create_tam_transport = &create_tam_transport;
  _tam_api.remove_tam_transport = &remove_tam_transport;
  _tam_api.set_tam_transport_attribute = &set_tam_transport_attribute;
  _tam_api.get_tam_transport_attribute = &get_tam_transport_attribute;

  _tam_api.create_tam_collector = &create_tam_collector;
  _tam_api.remove_tam_collector = &remove_tam_collector;
  _tam_api.set_tam_collector_attribute = &set_tam_collector_attribute;
  _tam_api.get_tam_collector_attribute = &get_tam_collector_attribute;

  _tam_api.create_tam_event_threshold = &create_tam_event_threshold;
  _tam_api.remove_tam_event_threshold = &remove_tam_event_threshold;
  _tam_api.set_tam_event_threshold_attribute =
      &set_tam_event_threshold_attribute;
  _tam_api.get_tam_event_threshold_attribute =
      &get_tam_event_threshold_attribute;

  *tam_api = &_tam_api;
}

} // namespace facebook::fboss
