/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/TamApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/TamApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _TamMap{
    SAI_ATTR_MAP(Tam, EventObjectList),
    SAI_ATTR_MAP(Tam, TamBindPointList),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _TamEventMap{
    SAI_ATTR_MAP(TamEvent, Type),
    SAI_ATTR_MAP(TamEvent, ActionList),
    SAI_ATTR_MAP(TamEvent, CollectorList),
    SAI_ATTR_MAP(TamEvent, Threshold),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _TamEventActionMap{
    SAI_ATTR_MAP(TamEventAction, ReportType),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _TamReportMap{
    SAI_ATTR_MAP(TamReport, Type),
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    SAI_ATTR_MAP(TamReport, SampleRate),
    SAI_ATTR_MAP(TamReport, MaxReportRate),
    SAI_ATTR_MAP(TamReport, MaxReportBurst),
#endif
};

// NOLINTNEXTLINE(facebook-avoid-non-const-global-variables)
std::map<int32_t, std::pair<std::string, std::size_t>> _TamEventThresholdMap{
    SAI_ATTR_MAP(TamEventThreshold, Unit),
    SAI_ATTR_MAP(TamEventThreshold, Rate),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _TamTransportMap{
    SAI_ATTR_MAP(TamTransport, Type),
    SAI_ATTR_MAP(TamTransport, SrcPort),
    SAI_ATTR_MAP(TamTransport, DstPort),
    SAI_ATTR_MAP(TamTransport, Mtu),
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    SAI_ATTR_MAP(TamTransport, GreProtocolType),
#endif
};

std::map<int32_t, std::pair<std::string, std::size_t>> _TamCollectorMap{
    SAI_ATTR_MAP(TamCollector, SrcIp),
    SAI_ATTR_MAP(TamCollector, DstIp),
    SAI_ATTR_MAP(TamCollector, TruncateSize),
    SAI_ATTR_MAP(TamCollector, Transport),
    SAI_ATTR_MAP(TamCollector, DscpValue),
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    SAI_ATTR_MAP(TamCollector, SrcMacAddress),
    SAI_ATTR_MAP(TamCollector, DstMacAddress),
    SAI_ATTR_MAP(TamCollector, Destination),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    SAI_ATTR_MAP(TamCollector, Localhost),
#endif
};

void handleExtensionAttributes() {
  SAI_EXT_ATTR_MAP(TamEvent, SwitchEventType)
  SAI_EXT_ATTR_MAP(TamEvent, DeviceId)
  SAI_EXT_ATTR_MAP(TamEvent, SwitchEventId)
  SAI_EXT_ATTR_MAP(TamEvent, ExtensionsCollectorList)
  SAI_EXT_ATTR_MAP(TamEvent, PacketDropTypeMmu)
  SAI_EXT_ATTR_MAP(TamEvent, PacketDropTypeIngress)
  SAI_EXT_ATTR_MAP(TamEvent, AgingGroup)
  SAI_EXT_ATTR_MAP(TamEvent, IngressSamplepacketEnable)
  SAI_EXT_ATTR_MAP(TamTransport, SrcMacAddress)
  SAI_EXT_ATTR_MAP(TamTransport, DstMacAddress)
}

} // namespace

namespace facebook::fboss {

WRAP_CREATE_FUNC(tam, SAI_OBJECT_TYPE_TAM, tam);
WRAP_REMOVE_FUNC(tam, SAI_OBJECT_TYPE_TAM, tam);
WRAP_SET_ATTR_FUNC(tam, SAI_OBJECT_TYPE_TAM, tam);
WRAP_GET_ATTR_FUNC(tam, SAI_OBJECT_TYPE_TAM, tam);

WRAP_CREATE_FUNC(tam_event, SAI_OBJECT_TYPE_TAM_EVENT, tam);
WRAP_REMOVE_FUNC(tam_event, SAI_OBJECT_TYPE_TAM_EVENT, tam);
WRAP_SET_ATTR_FUNC(tam_event, SAI_OBJECT_TYPE_TAM_EVENT, tam);
WRAP_GET_ATTR_FUNC(tam_event, SAI_OBJECT_TYPE_TAM_EVENT, tam);

WRAP_CREATE_FUNC(tam_event_action, SAI_OBJECT_TYPE_TAM_EVENT_ACTION, tam);
WRAP_REMOVE_FUNC(tam_event_action, SAI_OBJECT_TYPE_TAM_EVENT_ACTION, tam);
WRAP_SET_ATTR_FUNC(tam_event_action, SAI_OBJECT_TYPE_TAM_EVENT_ACTION, tam);
WRAP_GET_ATTR_FUNC(tam_event_action, SAI_OBJECT_TYPE_TAM_EVENT_ACTION, tam);

WRAP_CREATE_FUNC(tam_report, SAI_OBJECT_TYPE_TAM_REPORT, tam);
WRAP_REMOVE_FUNC(tam_report, SAI_OBJECT_TYPE_TAM_REPORT, tam);
WRAP_SET_ATTR_FUNC(tam_report, SAI_OBJECT_TYPE_TAM_REPORT, tam);
WRAP_GET_ATTR_FUNC(tam_report, SAI_OBJECT_TYPE_TAM_REPORT, tam);

WRAP_CREATE_FUNC(tam_transport, SAI_OBJECT_TYPE_TAM_TRANSPORT, tam);
WRAP_REMOVE_FUNC(tam_transport, SAI_OBJECT_TYPE_TAM_TRANSPORT, tam);
WRAP_SET_ATTR_FUNC(tam_transport, SAI_OBJECT_TYPE_TAM_TRANSPORT, tam);
WRAP_GET_ATTR_FUNC(tam_transport, SAI_OBJECT_TYPE_TAM_TRANSPORT, tam);

WRAP_CREATE_FUNC(tam_collector, SAI_OBJECT_TYPE_TAM_COLLECTOR, tam);
WRAP_REMOVE_FUNC(tam_collector, SAI_OBJECT_TYPE_TAM_COLLECTOR, tam);
WRAP_SET_ATTR_FUNC(tam_collector, SAI_OBJECT_TYPE_TAM_COLLECTOR, tam);
WRAP_GET_ATTR_FUNC(tam_collector, SAI_OBJECT_TYPE_TAM_COLLECTOR, tam);

WRAP_CREATE_FUNC(tam_event_threshold, SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD, tam);
WRAP_REMOVE_FUNC(tam_event_threshold, SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD, tam);
WRAP_SET_ATTR_FUNC(
    tam_event_threshold,
    SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD,
    tam);
WRAP_GET_ATTR_FUNC(
    tam_event_threshold,
    SAI_OBJECT_TYPE_TAM_EVENT_THRESHOLD,
    tam);

sai_tam_api_t* wrappedTamApi() {
  handleExtensionAttributes();
  static sai_tam_api_t tamWrappers;
  tamWrappers.create_tam = &wrap_create_tam;
  tamWrappers.remove_tam = &wrap_remove_tam;
  tamWrappers.set_tam_attribute = &wrap_set_tam_attribute;
  tamWrappers.get_tam_attribute = &wrap_get_tam_attribute;

  tamWrappers.create_tam_event = &wrap_create_tam_event;
  tamWrappers.remove_tam_event = &wrap_remove_tam_event;
  tamWrappers.set_tam_event_attribute = &wrap_set_tam_event_attribute;
  tamWrappers.get_tam_event_attribute = &wrap_get_tam_event_attribute;

  tamWrappers.create_tam_event_action = &wrap_create_tam_event_action;
  tamWrappers.remove_tam_event_action = &wrap_remove_tam_event_action;
  tamWrappers.set_tam_event_action_attribute =
      &wrap_set_tam_event_action_attribute;
  tamWrappers.get_tam_event_action_attribute =
      &wrap_get_tam_event_action_attribute;

  tamWrappers.create_tam_report = &wrap_create_tam_report;
  tamWrappers.remove_tam_report = &wrap_remove_tam_report;
  tamWrappers.set_tam_report_attribute = &wrap_set_tam_report_attribute;
  tamWrappers.get_tam_report_attribute = &wrap_get_tam_report_attribute;

  tamWrappers.create_tam_transport = &wrap_create_tam_transport;
  tamWrappers.remove_tam_transport = &wrap_remove_tam_transport;
  tamWrappers.set_tam_transport_attribute = &wrap_set_tam_transport_attribute;
  tamWrappers.get_tam_transport_attribute = &wrap_get_tam_transport_attribute;

  tamWrappers.create_tam_collector = &wrap_create_tam_collector;
  tamWrappers.remove_tam_collector = &wrap_remove_tam_collector;
  tamWrappers.set_tam_collector_attribute = &wrap_set_tam_collector_attribute;
  tamWrappers.get_tam_collector_attribute = &wrap_get_tam_collector_attribute;

  tamWrappers.create_tam_event_threshold = &wrap_create_tam_event_threshold;
  tamWrappers.remove_tam_event_threshold = &wrap_remove_tam_event_threshold;
  tamWrappers.set_tam_event_threshold_attribute =
      &wrap_set_tam_event_threshold_attribute;
  tamWrappers.get_tam_event_threshold_attribute =
      &wrap_get_tam_event_threshold_attribute;

  return &tamWrappers;
}

SET_SAI_ATTRIBUTES(Tam)
SET_SAI_ATTRIBUTES(TamEvent)
SET_SAI_ATTRIBUTES(TamEventAction)
SET_SAI_ATTRIBUTES(TamReport)
SET_SAI_ATTRIBUTES(TamTransport)
SET_SAI_ATTRIBUTES(TamCollector)
SET_SAI_ATTRIBUTES(TamEventThreshold)

} // namespace facebook::fboss
