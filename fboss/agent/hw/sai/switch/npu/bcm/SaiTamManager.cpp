// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiTamManager.h"

#include "fboss/agent/Utils.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/lib/TupleUtils.h"

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0) || defined(BRCM_SAI_SDK_XGS_GTE_13_0)
extern "C" {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
#include <experimental/saiexperimentaltameventaginggroup.h>
#endif
#include <experimental/saitamextensions.h>
}
#endif

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
namespace {
using facebook::fboss::FbossError;
using facebook::fboss::cfg::MirrorOnDropAgingGroup;
using facebook::fboss::cfg::MirrorOnDropReasonAggregation;

// Must be less than BRCM_SAI_DNX_MOD_MAX_SUPP_AGE_TIME (776 on J3).
constexpr int kDefaultAgingIntervalUsecs = 500;

const std::map<MirrorOnDropReasonAggregation, std::vector<sai_int32_t>>
    kMirrorOnDropReasons = {
        {
            MirrorOnDropReasonAggregation::UNEXPECTED_REASON_DISCARDS,
            {
                SAI_PACKET_DROP_TYPE_MMU_DROP_PRECEDENCE_LEVEL,
                SAI_PACKET_DROP_TYPE_MMU_LAG_PROTECTION,
                SAI_PACKET_DROP_TYPE_MMU_LAG_REMOTE,
                SAI_PACKET_DROP_TYPE_MMU_LATENCY,
                SAI_PACKET_DROP_TYPE_MMU_MACSEC_ERROR,
                SAI_PACKET_DROP_TYPE_MMU_MULTICAST_FIFO_FULL,
                SAI_PACKET_DROP_TYPE_MMU_MULTICAST_REPLICATION_ERROR,
                SAI_PACKET_DROP_TYPE_MMU_VOQ_SYSTEM_RED,
                SAI_PACKET_DROP_TYPE_MMU_VOQ_WRED,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_A_SRAM_BUFFERS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_B_SRAM_BUFFERS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_C_SRAM_BUFFERS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_A_SRAM_PDS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_B_SRAM_PDS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_C_SRAM_PDS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_A_WORDS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_B_WORDS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_C_WORDS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_A_WRED,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_B_WRED,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_C_WRED,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_D_WRED,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_E_WRED,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_F_WRED,
            },
        },
        {
            MirrorOnDropReasonAggregation::INGRESS_MISC_DISCARDS,
            {
                SAI_PACKET_DROP_TYPE_MMU_EXTERNAL_ERROR,
                SAI_PACKET_DROP_TYPE_MMU_ITPP_DELTA_ERROR,
                SAI_PACKET_DROP_TYPE_MMU_PACKET_SIZE_ERROR,
                SAI_PACKET_DROP_TYPE_MMU_TAR_FIFO_FULL,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_D_SRAM_PDS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_D_SRAM_BUFFERS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VSQ_D_WORDS_SHARED_MAX_SIZE,
            },
        },
        {
            MirrorOnDropReasonAggregation::INGRESS_PACKET_PROCESSING_DISCARDS,
            {
                SAI_PACKET_DROP_TYPE_MMU_PP_ERROR,
            },
        },
        {
            MirrorOnDropReasonAggregation::INGRESS_QUEUE_RESOLUTION_DISCARDS,
            {
                SAI_PACKET_DROP_TYPE_MMU_QUEUE_NUM_RESOLUTION_ERROR,
                SAI_PACKET_DROP_TYPE_MMU_QUEUE_NUM_NOT_VALID,
            },
        },
        {
            MirrorOnDropReasonAggregation::INGRESS_SOURCE_CONGESTION_DISCARDS,
            {
                SAI_PACKET_DROP_TYPE_MMU_PB_VSQ_SRAM_PDS_TOTAL_FREE_SHARED,
                SAI_PACKET_DROP_TYPE_MMU_PB_VSQ_SRAM_PDS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_PB_VSQ_SRAM_BUFFERS_TOTAL_FREE_SHARED,
                SAI_PACKET_DROP_TYPE_MMU_PB_VSQ_SRAM_BUFFERS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_PB_VSQ_WORDS_TOTAL_FREE_SHARED,
                SAI_PACKET_DROP_TYPE_MMU_PB_VSQ_WORDS_SHARED_MAX_SIZE,
            },
        },
        {
            MirrorOnDropReasonAggregation::
                INGRESS_DESTINATION_CONGESTION_DISCARDS,
            {
                SAI_PACKET_DROP_TYPE_MMU_VOQ_SRAM_PDS_TOTAL_FREE_SHARED,
                SAI_PACKET_DROP_TYPE_MMU_VOQ_SRAM_PDS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VOQ_SRAM_BUFFERS_TOTAL_FREE_SHARED,
                SAI_PACKET_DROP_TYPE_MMU_VOQ_SRAM_BUFFERS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VOQ_WORDS_TOTAL_FREE_SHARED,
                SAI_PACKET_DROP_TYPE_MMU_VOQ_WORDS_SHARED_MAX_SIZE,
                SAI_PACKET_DROP_TYPE_MMU_VOQ_DRAM_BLOCK,
            },
        },
        {
            MirrorOnDropReasonAggregation::INGRESS_GLOBAL_CONGESTION_DISCARDS,
            {
                SAI_PACKET_DROP_TYPE_MMU_GLOBAL_DRAM_BDBS,
                SAI_PACKET_DROP_TYPE_MMU_GLOBAL_SRAM_BUFFERS,
                SAI_PACKET_DROP_TYPE_MMU_GLOBAL_SRAM_PDBS,
                SAI_PACKET_DROP_TYPE_MMU_SRAM_RESOURCE_ERROR,
            },
        },
};

std::vector<sai_int32_t> getMirrorOnDropReasonsFromAggregation(
    const std::vector<MirrorOnDropReasonAggregation>& reasonAggs) {
  std::vector<sai_int32_t> allReasons;
  for (auto agg : reasonAggs) {
    auto reasons = kMirrorOnDropReasons.find(agg);
    if (reasons == kMirrorOnDropReasons.end()) {
      throw FbossError("Unknown MirrorOnDropReasonAggregation: ", agg);
    }
    allReasons.insert(
        allReasons.end(), reasons->second.begin(), reasons->second.end());
  }
  return allReasons;
}

sai_tam_event_aging_group_type_t getMirrorOnDropAgingGroupType(
    MirrorOnDropAgingGroup group) {
  switch (group) {
    case MirrorOnDropAgingGroup::PORT:
      return SAI_TAM_EVENT_AGING_GROUP_TYPE_PORT;
    case MirrorOnDropAgingGroup::PRIORITY_GROUP:
      return SAI_TAM_EVENT_AGING_GROUP_TYPE_PRIORITY_GROUP;
    case MirrorOnDropAgingGroup::VOQ:
      return SAI_TAM_EVENT_AGING_GROUP_TYPE_VOQ;
    case MirrorOnDropAgingGroup::GLOBAL:
    default:
      return SAI_TAM_EVENT_AGING_GROUP_TYPE_GLOBAL;
  }
}

} // namespace
#endif

namespace facebook::fboss {

namespace {

std::vector<sai_object_id_t> getSwitchTamIds(
    folly::F14FastMap<std::string, std::unique_ptr<SaiTamHandle>>& tamHandles) {
  std::vector<sai_object_id_t> switchTamIds;
  for (const auto& [_, tamHandle] : tamHandles) {
    switchTamIds.push_back(tamHandle->tam->adapterKey());
  }
  return switchTamIds;
}

std::vector<sai_object_id_t> getPortTamIds(
    folly::F14FastMap<std::string, std::unique_ptr<SaiTamHandle>>& tamHandles,
    PortID portId) {
  std::vector<sai_object_id_t> portTamIds;
  for (const auto& [_, tamHandle] : tamHandles) {
    if (tamHandle->portId == portId) {
      portTamIds.push_back(tamHandle->tam->adapterKey());
    }
  }
  return portTamIds;
}

void bindTamObjectToSwitchAndPort(
    SaiManagerTable* managerTable,
    folly::F14FastMap<std::string, std::unique_ptr<SaiTamHandle>>& tamHandles,
    sai_object_id_t tamId,
    PortID portId) {
  // Bind MOD port before binding to the switch, according to Broadcom example.
  XLOG(INFO) << "Binding TAM object " << tamId << " to port " << portId;
  managerTable->portManager().setTamObject(
      portId, getPortTamIds(tamHandles, portId));
  XLOG(INFO) << "Binding TAM object " << tamId << " to switch";
  managerTable->switchManager().setTamObject(getSwitchTamIds(tamHandles));
}

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0) || defined(BRCM_SAI_SDK_XGS_GTE_13_0)
void unbindTamObjectFromSwitchAndPort(
    SaiManagerTable* managerTable,
    folly::F14FastMap<std::string, std::unique_ptr<SaiTamHandle>>& tamHandles,
    sai_object_id_t tamId,
    PortID portId) {
  // This must be the reverse of binding.
  XLOG(INFO) << "Unbinding TAM object " << tamId << " from switch";
  managerTable->switchManager().setTamObject(getSwitchTamIds(tamHandles));
  XLOG(INFO) << "Unbinding TAM object " << tamId << " from port " << portId;
  managerTable->portManager().setTamObject(
      portId, getPortTamIds(tamHandles, portId));
}
#endif

} // namespace

SaiTamManager::SaiTamManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {}

std::string SaiTamManager::getDestMacWithOverride(
    const std::string& defaultMac) const {
  if (!FLAGS_mod_dest_mac_override.empty()) {
    XLOG(INFO) << "MirrorOnDrop dest MAC overridden to "
               << FLAGS_mod_dest_mac_override;
    return FLAGS_mod_dest_mac_override;
  }
  return defaultMac;
}

std::shared_ptr<SaiTamReport> SaiTamManager::createTamReport(
    sai_int32_t reportType) {
  auto& reportStore = saiStore_->get<SaiTamReportTraits>();
  auto reportTraits = SaiTamReportTraits::CreateAttributes{
      reportType
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      std::nullopt,
      std::nullopt,
      std::nullopt
#endif
  };
  return reportStore.setObject(reportTraits, reportTraits);
}

std::shared_ptr<SaiTamEventAction> SaiTamManager::createTamAction(
    sai_object_id_t reportId) {
  auto& actionStore = saiStore_->get<SaiTamEventActionTraits>();
  auto actionTraits = SaiTamEventActionTraits::CreateAttributes{reportId};
  return actionStore.setObject(actionTraits, actionTraits);
}

std::shared_ptr<SaiTamTransport> SaiTamManager::createTamTransport(
    const std::shared_ptr<MirrorOnDropReport>& report,
    sai_int32_t transportType,
    std::optional<folly::MacAddress> dstMac) {
  auto& transportStore = saiStore_->get<SaiTamTransportTraits>();
  auto transportTraits = SaiTamTransportTraits::AdapterHostKey{
      transportType,
      report->getLocalSrcPort(),
      report->getCollectorPort(),
      report->getMtu(),
      folly::MacAddress(report->getSwitchMac()),
      dstMac
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      std::nullopt // GreProtocolType
#endif
  };
  return transportStore.setObject(transportTraits, transportTraits);
}

std::shared_ptr<SaiTamCollector> SaiTamManager::createTamCollector(
    const std::shared_ptr<MirrorOnDropReport>& report,
    sai_object_id_t transportId,
    std::optional<uint16_t> truncateSize) {
  auto& collectorStore = saiStore_->get<SaiTamCollectorTraits>();
  auto collectorTraits = SaiTamCollectorTraits::CreateAttributes{
      report->getLocalSrcIp(),
      report->getCollectorIp(),
      truncateSize,
      transportId,
      report->getDscp()
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
          ,
      std::nullopt, // SrcMacAddress
      std::nullopt, // DstMacAddress
      std::nullopt // Destination
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
      ,
      std::nullopt // Localhost
#endif
  };
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_GTE_14_0)
  auto collectorKey = collectorTraits;
  // These SDKs return zero MACs and false localhost for unset collector fields.
  std::get<std::optional<SaiTamCollectorTraits::Attributes::SrcMacAddress>>(
      collectorKey) = folly::MacAddress{};
  std::get<std::optional<SaiTamCollectorTraits::Attributes::DstMacAddress>>(
      collectorKey) = folly::MacAddress{};
  std::get<std::optional<SaiTamCollectorTraits::Attributes::Localhost>>(
      collectorKey) = false;
#else
  const auto& collectorKey = collectorTraits;
#endif
  return collectorStore.setObject(collectorKey, collectorTraits);
}

std::shared_ptr<SaiTam> SaiTamManager::createTam(
    const std::vector<sai_object_id_t>& eventIds,
    const std::vector<sai_int32_t>& bindpoints) {
  SaiTamTraits::CreateAttributes tamTraits;
  std::get<SaiTamTraits::Attributes::EventObjectList>(tamTraits) = eventIds;
  std::get<SaiTamTraits::Attributes::TamBindPointList>(tamTraits) = bindpoints;
  auto& tamStore = saiStore_->get<SaiTamTraits>();
  return tamStore.setObject(tamTraits, tamTraits);
}

void SaiTamManager::bindTam(sai_object_id_t tamId, const PortID& portId) {
  bindTamObjectToSwitchAndPort(managerTable_, tamHandles_, tamId, portId);
}

void SaiTamManager::storeTamHandle(
    const std::string& reportId,
    std::shared_ptr<SaiTamReport> report,
    std::shared_ptr<SaiTamEventAction> action,
    std::shared_ptr<SaiTamTransport> transport,
    std::shared_ptr<SaiTamCollector> collector,
    const std::vector<std::shared_ptr<SaiTamEvent>>& events,
    std::shared_ptr<SaiTam> tam,
    const PortID& portId
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
    ,
    std::vector<std::shared_ptr<SaiTamEventAgingGroup>> agingGroups
#endif
#if defined(BRCM_SAI_SDK_XGS_GTE_13_0)
    ,
    std::shared_ptr<SaiSamplePacket> samplePacket
#endif
) {
  auto tamHandle = std::make_unique<SaiTamHandle>();
  tamHandle->report = std::move(report);
  tamHandle->action = std::move(action);
  tamHandle->transport = std::move(transport);
  tamHandle->collector = std::move(collector);
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  tamHandle->agingGroups = std::move(agingGroups);
#endif
#if defined(BRCM_SAI_SDK_XGS_GTE_13_0)
  tamHandle->samplePacket = samplePacket;
#endif
  tamHandle->events = events;
  tamHandle->tam = std::move(tam);
  tamHandle->portId = portId;
  tamHandle->managerTable = managerTable_;
  tamHandles_.emplace(reportId, std::move(tamHandle));
}

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
void SaiTamManager::addDnxMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& report) {
  std::string destMac = getDestMacWithOverride(report->getFirstInterfaceMac());
  XLOG(INFO) << "Creating MirrorOnDropReport " << report->getID()
             << " srcIp=" << report->getLocalSrcIp().str()
             << " srcPort=" << report->getLocalSrcPort()
             << " collectorIp=" << report->getCollectorIp().str()
             << " collectorPort=" << report->getCollectorPort()
             << " srcMac=" << report->getSwitchMac() << " dstMac=" << destMac;

  auto reportObj = createTamReport(SAI_TAM_REPORT_TYPE_MOD_OVER_UDP);
  auto action = createTamAction(reportObj->adapterKey());
  auto transport = createTamTransport(
      report, SAI_TAM_TRANSPORT_TYPE_PORT, folly::MacAddress(destMac));
  auto collector = createTamCollector(
      report, transport->adapterKey(), report->getTruncateSize());

  std::vector<std::shared_ptr<SaiTamEventAgingGroup>> agingGroups;
  std::vector<std::shared_ptr<SaiTamEvent>> events;
  std::vector<sai_object_id_t> eventIds;

  for (const auto& [eventId, eventCfg] : report->getModEventToConfigMap()) {
    auto reasons = getMirrorOnDropReasonsFromAggregation(
        *eventCfg.dropReasonAggregations());
    if (reasons.empty()) {
      XLOG(WARN) << "No mirror on drop reasons specified for event " << eventId;
      continue;
    }

    // Create aging group
    auto& agingGroupStore = saiStore_->get<SaiTamEventAgingGroupTraits>();
    auto groupType =
        eventCfg.agingGroup().value_or(cfg::MirrorOnDropAgingGroup::GLOBAL);
    auto agingIntervals = report->getAgingGroupAgingIntervalUsecs();
    auto agingTimeIt = agingIntervals.find(groupType);
    sai_uint32_t agingTime = agingTimeIt == agingIntervals.end()
        ? kDefaultAgingIntervalUsecs
        : agingTimeIt->second;
    auto agingGroupTraits = SaiTamEventAgingGroupTraits::CreateAttributes{
        getMirrorOnDropAgingGroupType(groupType), agingTime};
    auto agingGroup =
        agingGroupStore.setObject(agingGroupTraits, agingGroupTraits);
    agingGroups.push_back(agingGroup);

    // Create event
    std::vector<sai_object_id_t> actions{action->adapterKey()};
    std::vector<sai_object_id_t> collectors{collector->adapterKey()};
    sai_object_id_t agingGroupKey = agingGroup->adapterKey();
    SaiTamEventTraits::CreateAttributes eventTraits;
    std::get<SaiTamEventTraits::Attributes::Type>(eventTraits) =
        SAI_TAM_EVENT_TYPE_PACKET_DROP_STATEFUL;
    std::get<SaiTamEventTraits::Attributes::ActionList>(eventTraits) = actions;
    std::get<std::optional<SaiTamEventTraits::Attributes::DeviceId>>(
        eventTraits) = 0; // specify 0 explicitly so that round trip works
    std::get<std::optional<SaiTamEventTraits::Attributes::SwitchEventId>>(
        eventTraits) = eventId;
    std::get<
        std::optional<SaiTamEventTraits::Attributes::ExtensionsCollectorList>>(
        eventTraits) = collectors;
    std::get<std::optional<SaiTamEventTraits::Attributes::PacketDropTypeMmu>>(
        eventTraits) = reasons;
    std::get<std::optional<SaiTamEventTraits::Attributes::AgingGroup>>(
        eventTraits) = agingGroupKey;
    auto& eventStore = saiStore_->get<SaiTamEventTraits>();
    auto eventAdapterHostKey = tupleProjection<
        SaiTamEventTraits::CreateAttributes,
        SaiTamEventTraits::AdapterHostKey>(eventTraits);
    auto event = eventStore.setObject(eventAdapterHostKey, eventTraits);
    events.push_back(event);
    eventIds.push_back(event->adapterKey());
    XLOG(INFO) << "Created event ID " << eventId << " with tam event "
               << event->adapterKey() << " and aging group " << agingGroupKey;
  }

  std::vector<sai_int32_t> bindpoints = {
      SAI_TAM_BIND_POINT_TYPE_SWITCH, SAI_TAM_BIND_POINT_TYPE_PORT};
  auto tam = createTam(eventIds, bindpoints);
  // DNX reports always carry a mirror port.
  const auto mirrorPortId = report->getMirrorPortId().value();
  storeTamHandle(
      report->getID(),
      reportObj,
      action,
      transport,
      collector,
      events,
      tam,
      mirrorPortId,
      agingGroups);
  bindTam(tam->adapterKey(), mirrorPortId);
}
#endif

#if defined(BRCM_SAI_SDK_XGS_GTE_13_0)
void SaiTamManager::addXgsMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& report) {
  // Only program the hardware if the collector IP has been resolved
  if (!report->isResolved()) {
    XLOG(INFO) << "Skipping MirrorOnDropReport " << report->getID()
               << " - collector IP not yet resolved";
    return;
  }

  std::string destMac = getDestMacWithOverride(
      report->getResolvedCollectorMac().value().toString());

  // Get the resolved egress port
  auto resolvedPortDesc = report->getResolvedEgressPort().value();
  PortID egressPort = PortID(*resolvedPortDesc.portId());

  XLOG(INFO) << "Creating XGS MirrorOnDropReport " << report->getID()
             << " srcIp=" << report->getLocalSrcIp().str()
             << " srcPort=" << report->getLocalSrcPort()
             << " collectorIp=" << report->getCollectorIp().str()
             << " collectorPort=" << report->getCollectorPort()
             << " srcMac=" << report->getSwitchMac() << " dstMac=" << destMac
             << " egressPort=" << egressPort;

  auto reportObj = createTamReport(SAI_TAM_REPORT_TYPE_IPFIX);
  auto action = createTamAction(reportObj->adapterKey());
  auto transport = createTamTransport(
      report, SAI_TAM_TRANSPORT_TYPE_PORT, folly::MacAddress(destMac));
  // 0 means "do not truncate". It still has to be passed explicitly
  // TruncateSize is part of SaiTamCollectorTraits::AdapterHostKey and warm boot
  // re-derives that key from HW GETs. 16.0 implements it and returns 0
#if defined(BRCM_SAI_SDK_XGS_GTE_16_0)
  const std::optional<uint16_t> truncateSize{0};
#else
  const std::optional<uint16_t> truncateSize{std::nullopt};
#endif
  auto collector =
      createTamCollector(report, transport->adapterKey(), truncateSize);

  // Create SamplePacket if samplingRate is configured
  std::shared_ptr<SaiSamplePacket> samplePacket = nullptr;
  auto samplingRate = report->getSamplingRate();
  if (samplingRate.has_value() && samplingRate.value() > 0) {
    XLOG(INFO) << "Creating SamplePacket with rate " << samplingRate.value()
               << " for MirrorOnDropReport " << report->getID();
    auto& samplePacketStore = saiStore_->get<SaiSamplePacketTraits>();
    SaiSamplePacketTraits::AdapterHostKey samplePacketKey{
        static_cast<sai_uint32_t>(samplingRate.value()),
        SAI_SAMPLEPACKET_TYPE_MIRROR_SESSION,
        SAI_SAMPLEPACKET_MODE_EXCLUSIVE};
    samplePacket =
        samplePacketStore.setObject(samplePacketKey, samplePacketKey);
  }

  std::vector<std::shared_ptr<SaiTamEvent>> events;
  std::vector<sai_object_id_t> eventIds;

  // For XGS, configure both Ingress and MMU packet drop types.
  // Ingress drops capture forwarding-related drops (ACL, routing, etc).
  // MMU drops capture congestion-related drops (buffer exhaustion).
  std::vector<sai_int32_t> ingressDropTypes = {
      SAI_PACKET_DROP_TYPE_INGRESS_ALL};
  std::vector<sai_int32_t> mmuDropTypes = {SAI_PACKET_DROP_TYPE_MMU_ALL};
  std::vector<sai_object_id_t> actions{action->adapterKey()};
  std::vector<sai_object_id_t> collectors{collector->adapterKey()};

  // Currently use only one switch event ID for all the drop type events.
  SaiTamEventTraits::CreateAttributes eventTraits;
  std::get<SaiTamEventTraits::Attributes::Type>(eventTraits) =
      SAI_TAM_EVENT_TYPE_PACKET_DROP;
  std::get<std::optional<SaiTamEventTraits::Attributes::SwitchEventId>>(
      eventTraits) = tam::kModSwitchEventId;
  std::get<std::optional<SaiTamEventTraits::Attributes::DeviceId>>(
      eventTraits) = tam::kModDeviceId;
  std::get<std::optional<SaiTamEventTraits::Attributes::PacketDropTypeIngress>>(
      eventTraits) = ingressDropTypes;
  std::get<std::optional<SaiTamEventTraits::Attributes::PacketDropTypeMmu>>(
      eventTraits) = mmuDropTypes;
  std::get<SaiTamEventTraits::Attributes::ActionList>(eventTraits) = actions;
  std::get<
      std::optional<SaiTamEventTraits::Attributes::ExtensionsCollectorList>>(
      eventTraits) = collectors;

  // Set IngressSamplepacketEnable if samplePacket was created
  if (samplePacket) {
    std::get<std::optional<
        SaiTamEventTraits::Attributes::IngressSamplepacketEnable>>(
        eventTraits) = static_cast<sai_object_id_t>(samplePacket->adapterKey());
    XLOG(INFO) << "Setting IngressSamplepacketEnable to "
               << samplePacket->adapterKey() << " on TAM event";
  }

  auto& eventStore = saiStore_->get<SaiTamEventTraits>();
  auto eventAdapterHostKey = tupleProjection<
      SaiTamEventTraits::CreateAttributes,
      SaiTamEventTraits::AdapterHostKey>(eventTraits);
  auto event = eventStore.setObject(eventAdapterHostKey, eventTraits);
  events.push_back(event);
  eventIds.push_back(event->adapterKey());
  XLOG(INFO) << "Created event ID " << tam::kModSwitchEventId
             << " with tam event " << event->adapterKey()
             << " for Ingress and MMU drop types.";

  std::vector<sai_int32_t> bindpoints = {
      SAI_TAM_BIND_POINT_TYPE_SWITCH, SAI_TAM_BIND_POINT_TYPE_PORT};
  auto tam = createTam(eventIds, bindpoints);
  storeTamHandle(
      report->getID(),
      reportObj,
      action,
      transport,
      collector,
      events,
      tam,
      egressPort,
      samplePacket);
  bindTam(tam->adapterKey(), egressPort);
}
#endif

void SaiTamManager::addMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& report) {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  addDnxMirrorOnDropReport(report);
#elif defined(BRCM_SAI_SDK_XGS_GTE_13_0)
  addXgsMirrorOnDropReport(report);
#endif
}

void SaiTamManager::removeMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& report) {
  // Check if the report was ever added (it might not have been if unresolved)
  auto it = tamHandles_.find(report->getID());
  if (it == tamHandles_.end()) {
    XLOG(INFO) << "MirrorOnDropReport " << report->getID()
               << " was not programmed, nothing to remove";
    return;
  }

  XLOG(INFO) << "Removing MirrorOnDropReport " << report->getID();
  auto handle = std::move(it->second);
  tamHandles_.erase(it);

#if defined(BRCM_SAI_SDK_DNX_GTE_11_0) || defined(BRCM_SAI_SDK_XGS_GTE_13_0)
  unbindTamObjectFromSwitchAndPort(
      managerTable_, tamHandles_, handle->tam->adapterKey(), handle->portId);
#endif
}

void SaiTamManager::changeMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& oldReport,
    const std::shared_ptr<MirrorOnDropReport>& newReport) {
  removeMirrorOnDropReport(oldReport);
  addMirrorOnDropReport(newReport);
}

std::vector<PortID> SaiTamManager::getAllMirrorOnDropPortIds() {
  std::vector<PortID> portIds;
  for (const auto& [_, tamHandle] : tamHandles_) {
    portIds.push_back(tamHandle->portId);
  }
  return portIds;
}

} // namespace facebook::fboss
