// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/switch/SaiTamManager.h"

#include <folly/logging/xlog.h>

#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/lib/TupleUtils.h"

namespace facebook::fboss {

SaiTamManager::SaiTamManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {}

std::shared_ptr<SaiTamReport> SaiTamManager::createTamReport(
    sai_int32_t reportType) {
  SaiTamReportTraits::CreateAttributes attributes{
      reportType
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      std::nullopt, // SampleRate
      std::nullopt, // MaxReportRate
      std::nullopt // MaxReportBurst
#endif
  };
  return saiStore_->get<SaiTamReportTraits>().setObject(attributes, attributes);
}

std::shared_ptr<SaiTamEventAction> SaiTamManager::createTamAction(
    sai_object_id_t reportId) {
  SaiTamEventActionTraits::CreateAttributes attributes{reportId};
  return saiStore_->get<SaiTamEventActionTraits>().setObject(
      attributes, attributes);
}

std::shared_ptr<SaiTamTransport> SaiTamManager::createTamTransport() {
  SaiTamTransportTraits::CreateAttributes attributes{
      SAI_TAM_TRANSPORT_TYPE_GRE,
      std::nullopt, // SrcPort
      std::nullopt, // DstPort
      std::nullopt, // Mtu
      std::nullopt, // SrcMacAddress
      std::nullopt, // DstMacAddress
      sai_uint16_t{0x6558},
  };
  return saiStore_->get<SaiTamTransportTraits>().setObject(
      attributes, attributes);
}

std::shared_ptr<SaiTamCollector> SaiTamManager::createTamCollector(
    const std::shared_ptr<MirrorOnDropReport>& report,
    sai_object_id_t transportId,
    sai_object_id_t destinationId) {
  const auto resolvedCollectorMac = report->getResolvedCollectorMac();
  CHECK(resolvedCollectorMac.has_value());
  SaiTamCollectorTraits::CreateAttributes attributes{
      report->getLocalSrcIp(),
      report->getCollectorIp(),
      static_cast<sai_uint16_t>(report->getTruncateSize()),
      transportId,
      report->getDscp(),
      folly::MacAddress{report->getFirstInterfaceMac()},
      *resolvedCollectorMac,
      destinationId,
      false,
  };
  return saiStore_->get<SaiTamCollectorTraits>().setObject(
      attributes, attributes);
}

std::shared_ptr<SaiTamEvent> SaiTamManager::createTamEvent(
    sai_object_id_t actionId,
    sai_object_id_t collectorId) {
  SaiTamEventTraits::CreateAttributes attributes;
  std::get<SaiTamEventTraits::Attributes::Type>(attributes) =
      SAI_TAM_EVENT_TYPE_PACKET_DROP;
  std::get<SaiTamEventTraits::Attributes::ActionList>(attributes) =
      std::vector<sai_object_id_t>{actionId};
  std::get<SaiTamEventTraits::Attributes::CollectorList>(attributes) =
      std::vector<sai_object_id_t>{collectorId};
  auto adapterHostKey = tupleProjection<
      SaiTamEventTraits::CreateAttributes,
      SaiTamEventTraits::AdapterHostKey>(attributes);
  return saiStore_->get<SaiTamEventTraits>().setObject(
      adapterHostKey, attributes);
}

std::shared_ptr<SaiTam> SaiTamManager::createTam(
    const std::vector<sai_object_id_t>& eventIds,
    const std::vector<sai_int32_t>& bindpoints) {
  SaiTamTraits::CreateAttributes attributes{eventIds, bindpoints};
  return saiStore_->get<SaiTamTraits>().setObject(attributes, attributes);
}

std::optional<std::pair<PortID, const SaiPortHandle*>>
SaiTamManager::getMirrorOnDropPortHandle(
    const MirrorOnDropReport& report) const {
  const auto egressPort = report.getResolvedEgressPort();
  if (!egressPort) {
    XLOG(ERR) << "MirrorOnDropReport " << report.getID()
              << " has no resolved egress port";
    return std::nullopt;
  }
  const auto resolvedPort = PortDescriptor::fromThrift(*egressPort);
  if (!resolvedPort.isPhysicalPort()) {
    XLOG(ERR) << "MirrorOnDropReport " << report.getID()
              << " resolved to non-physical port " << resolvedPort.str();
    return std::nullopt;
  }
  const auto portId = resolvedPort.phyPortID();
  const auto* portHandle = managerTable_->portManager().getPortHandle(portId);
  if (!portHandle) {
    XLOG(ERR) << "MirrorOnDropReport " << report.getID()
              << " resolved to missing port " << portId;
    return std::nullopt;
  }
  return std::make_pair(portId, portHandle);
}

void SaiTamManager::addMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& report) {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::TELEMETRY_AND_MONITORING)) {
    return;
  }
  if (!report->isResolved()) {
    XLOG_EVERY_MS(WARN, 60'000) << "MirrorOnDropReport " << report->getID()
                                << " is unresolved; skipping";
    return;
  }

  const auto resolvedPort = getMirrorOnDropPortHandle(*report);
  if (!resolvedPort) {
    return;
  }
  const auto& [portId, portHandle] = *resolvedPort;

  programMirrorOnDropReport(report, portId, portHandle);
}

void SaiTamManager::programMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& report,
    PortID portId,
    const SaiPortHandle* portHandle) {
  if (!tamHandles_.empty()) {
    XLOG(ERR) << "Chenab supports only one MirrorOnDropReport; skipping "
              << report->getID() << "; retaining "
              << tamHandles_.begin()->first;
    return;
  }
  const auto resolvedCollectorMac = report->getResolvedCollectorMac();
  if (!resolvedCollectorMac) {
    XLOG(ERR) << "MirrorOnDropReport " << report->getID()
              << " has no resolved collector MAC; skipping";
    return;
  }

  auto handle = std::make_unique<SaiTamHandle>();
  handle->report = createTamReport(SAI_TAM_REPORT_TYPE_VENDOR_EXTN);
  handle->action = createTamAction(handle->report->adapterKey());
  handle->transport = createTamTransport();
  handle->collector = createTamCollector(
      report,
      handle->transport->adapterKey(),
      static_cast<sai_object_id_t>(portHandle->port->adapterKey()));
  handle->events.push_back(createTamEvent(
      handle->action->adapterKey(), handle->collector->adapterKey()));
  handle->tam = createTam(
      {handle->events.back()->adapterKey()}, {SAI_TAM_BIND_POINT_TYPE_SWITCH});
  handle->portId = portId;
  handle->managerTable = managerTable_;
  managerTable_->switchManager().setTamObject({handle->tam->adapterKey()});
  tamHandles_.emplace(report->getID(), std::move(handle));
  XLOG(INFO) << "Programmed MirrorOnDropReport " << report->getID()
             << " srcIp=" << report->getLocalSrcIp().str()
             << " collectorIp=" << report->getCollectorIp().str()
             << " srcMac=" << report->getFirstInterfaceMac()
             << " dstMac=" << resolvedCollectorMac->toString()
             << " egressPort=" << portId;
}

void SaiTamManager::removeMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& report) {
  auto it = tamHandles_.find(report->getID());
  if (it == tamHandles_.end()) {
    return;
  }
  managerTable_->switchManager().setTamObject({});
  tamHandles_.erase(it);
  XLOG(INFO) << "Removed Chenab MirrorOnDropReport " << report->getID();
}

void SaiTamManager::removeMirrorOnDropReportsForPort(PortID portId) {
  for (auto it = tamHandles_.begin(); it != tamHandles_.end();) {
    if (it->second->portId != portId) {
      ++it;
      continue;
    }
    const auto reportId = it->first;
    managerTable_->switchManager().setTamObject({});
    it = tamHandles_.erase(it);
    XLOG(INFO) << "Removed Chenab MirrorOnDropReport " << reportId
               << " before removing collector port " << portId;
  }
}

void SaiTamManager::changeMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& oldReport,
    const std::shared_ptr<MirrorOnDropReport>& newReport) {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::TELEMETRY_AND_MONITORING)) {
    return;
  }
  if (!newReport->isResolved()) {
    removeMirrorOnDropReport(oldReport);
    return;
  }
  const auto resolvedPort = newReport->getResolvedCollectorMac().has_value()
      ? getMirrorOnDropPortHandle(*newReport)
      : std::nullopt;
  if (!resolvedPort) {
    XLOG(INFO) << "Deferring Chenab MirrorOnDropReport change for "
               << newReport->getID() << "; retaining programmed report "
               << oldReport->getID();
    return;
  }
  // Chenab rejects SET on a collector referenced by active MoD.
  removeMirrorOnDropReport(oldReport);
  const auto& [portId, portHandle] = *resolvedPort;
  programMirrorOnDropReport(newReport, portId, portHandle);
}

std::vector<PortID> SaiTamManager::getAllMirrorOnDropPortIds() {
  std::vector<PortID> portIds;
  for (const auto& [_, tamHandle] : tamHandles_) {
    portIds.push_back(tamHandle->portId);
  }
  return portIds;
}

} // namespace facebook::fboss
