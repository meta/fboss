// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiTamManager.h"

extern "C" {
#if defined(TAJO_SDK_GTE_26_5)
#include <saiextensions.h>
#else
#include <experimental/sai_attr_ext.h>
#endif
}

#include <folly/logging/xlog.h>

#include "fboss/agent/hw/sai/api/SwitchApi.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/lib/TupleUtils.h"

namespace facebook::fboss {

namespace {

// Tajo TAM bind-point list per the Tajo SAI MoD design (Tajo TAM API doc):
// the BIND_POINT_TYPE_LIST attribute on the TAM object only declares SWITCH.
// The TAM is still attached to a port for MoD egress via the port manager.
const std::vector<sai_int32_t> kTajoTamBindpoints = {
    SAI_TAM_BIND_POINT_TYPE_SWITCH};

// Sentinel handle name for the default switch-event (parity / SMS-out-of-bank)
// TAM that the constructor sets up. MoD reports use their report->getID() as
// the handle key; this constant prevents collisions and makes filtering for
// "non-MoD handle" explicit in rebuildAndRebindTam / getAllMirrorOnDropPortIds.
constexpr auto kDefaultTamHandle = "default";

// Walk every TAM handle and collect all event SAI ids in sorted order.
// Sort is required because tamHandles is an F14FastMap with non-deterministic
// iteration order — without sorting, the resulting EventObjectList changes
// across runs even when the set of events is identical, defeating the
// SaiStore's AdapterHostKey dedup and churning the TAM object on warmboot.
std::vector<sai_object_id_t> gatherAllEventIds(
    const folly::F14FastMap<std::string, std::unique_ptr<SaiTamHandle>>&
        tamHandles) {
  std::vector<sai_object_id_t> eventIds;
  for (const auto& [name, handle] : tamHandles) {
    for (const auto& event : handle->events) {
      eventIds.push_back(event->adapterKey());
    }
  }
  std::sort(eventIds.begin(), eventIds.end());
  return eventIds;
}

// Tajo SAI permits at most one TAM object on the device. To add or remove
// a MoD event we must (a) detach the live TAM from switch and ports,
// (b) drop our references so the SAI object is destroyed,
// (c) create a fresh one whose EventObjectList carries the union of every
// handle's events, and (d) re-bind it to the switch and to every MoD
// egress port. The destroy-before-create ordering is forced by Tajo's
// single-TAM limit (a create-before-destroy would fail at the SAI layer
// because the old TAM still holds the slot). If step (c) throws, the
// system is left without a TAM until the next state delta retry.
void rebuildAndRebindTam(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    folly::F14FastMap<std::string, std::unique_ptr<SaiTamHandle>>& tamHandles) {
  std::vector<PortID> modPorts;
  for (const auto& [name, handle] : tamHandles) {
    if (name != kDefaultTamHandle) {
      modPorts.push_back(handle->portId);
    }
  }

  managerTable->switchManager().setTamObject({});
  for (const auto& port : modPorts) {
    managerTable->portManager().setTamObject(port, {});
  }
  for (auto& [_, handle] : tamHandles) {
    handle->tam.reset();
  }

  auto allEvents = gatherAllEventIds(tamHandles);
  if (allEvents.empty()) {
    XLOG(INFO) << "Tajo TAM rebuild: no events to attach, leaving TAM unset";
    return;
  }

  SaiTamTraits::CreateAttributes tamTraits;
  std::get<SaiTamTraits::Attributes::EventObjectList>(tamTraits) = allEvents;
  std::get<SaiTamTraits::Attributes::TamBindPointList>(tamTraits) =
      kTajoTamBindpoints;
  auto newTam = saiStore->get<SaiTamTraits>().setObject(tamTraits, tamTraits);

  // Every handle observes the same shared TAM since Tajo only supports one.
  for (auto& [_, handle] : tamHandles) {
    handle->tam = newTam;
  }
  managerTable->switchManager().setTamObject({newTam->adapterKey()});
  for (const auto& port : modPorts) {
    managerTable->portManager().setTamObject(port, {newTam->adapterKey()});
  }
  XLOG(INFO) << "Tajo TAM rebuilt with " << allEvents.size()
             << " event(s); bound to switch and " << modPorts.size()
             << " port(s)";
}

// Build a TAM_EVENT_THRESHOLD object whose UNIT is packets and RATE is the
// configured packet-per-second cap. Always returns a threshold object: the
// Leaba SDK's create_tam_event rejects a PACKET_DROP (MoD) event with a
// missing SAI_TAM_EVENT_ATTR_THRESHOLD. When no positive rate is configured we
// use rate=0, which selects the SDK's default MoD sampling rate rather than
// disabling rate limiting.
std::shared_ptr<SaiTamEventThreshold> createTamEventThreshold(
    SaiStore* saiStore,
    std::optional<int32_t> rateThreshold) {
  // A TAM_EVENT_THRESHOLD object is mandatory for every PACKET_DROP (MoD)
  // event: the Leaba SDK's create_tam_event rejects a missing
  // SAI_TAM_EVENT_ATTR_THRESHOLD with SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING.
  // So always create one. Rate 0 selects the SDK's default MoD sampling
  // (~2 KPPS per drop reason); a positive dropPacketRateThreshold caps sampling
  // explicitly.
  sai_uint32_t rate = 0;
  if (rateThreshold.has_value()) {
    if (*rateThreshold > 0) {
      rate = static_cast<sai_uint32_t>(*rateThreshold);
    } else {
      XLOG(WARN) << "MirrorOnDropReport dropPacketRateThreshold="
                 << *rateThreshold
                 << " is non-positive; using default MoD sampling rate";
    }
  }
  SaiTamEventThresholdTraits::CreateAttributes thresholdTraits;
  std::get<std::optional<SaiTamEventThresholdTraits::Attributes::Unit>>(
      thresholdTraits) =
      static_cast<sai_int32_t>(SAI_TAM_EVENT_THRESHOLD_UNIT_PACKETS);
  std::get<std::optional<SaiTamEventThresholdTraits::Attributes::Rate>>(
      thresholdTraits) = rate;
  auto& store = saiStore->get<SaiTamEventThresholdTraits>();
  return store.setObject(thresholdTraits, thresholdTraits);
}

// Build the single all-drop-events TAM_EVENT for MoD. Per the Tajo TAM API
// doc, Tajo does not implement modEventToConfigMap — drops are captured all
// or nothing, so we create one PACKET_DROP event covering everything.
std::shared_ptr<SaiTamEvent> createMirrorOnDropEvent(
    SaiStore* saiStore,
    sai_object_id_t actionId,
    sai_object_id_t collectorId,
    std::shared_ptr<SaiTamEventThreshold> threshold) {
  std::vector<sai_object_id_t> actions{actionId};
  std::vector<sai_object_id_t> collectors{collectorId};

  SaiTamEventTraits::CreateAttributes eventTraits;
  std::get<SaiTamEventTraits::Attributes::Type>(eventTraits) =
      SAI_TAM_EVENT_TYPE_PACKET_DROP;
  std::get<SaiTamEventTraits::Attributes::ActionList>(eventTraits) = actions;
  std::get<SaiTamEventTraits::Attributes::CollectorList>(eventTraits) =
      collectors;
  if (threshold) {
    sai_object_id_t thresholdKey = threshold->adapterKey();
    std::get<std::optional<SaiTamEventTraits::Attributes::Threshold>>(
        eventTraits) = thresholdKey;
  }

  auto& eventStore = saiStore->get<SaiTamEventTraits>();
  auto eventAdapterHostKey = tupleProjection<
      SaiTamEventTraits::CreateAttributes,
      SaiTamEventTraits::AdapterHostKey>(eventTraits);
  return eventStore.setObject(eventAdapterHostKey, eventTraits);
}

// Tajo-specific TamReport: vendor-extn type plus optional sample-rate /
// max-report-rate / max-report-burst attributes (Tajo SDK 25.5+, SAI v1.16+).
// SampleRate maps to MoD samplingRate and MaxReportRate to the MoD aggregate
// rate cap. The SDK hard-codes the aggregate burst, so MaxReportBurst stays
// nullopt.
std::shared_ptr<SaiTamReport> createMirrorOnDropReport(
    SaiStore* saiStore,
    std::optional<int32_t> samplingRate,
    std::optional<int32_t> aggregateRateThreshold) {
#if SAI_API_VERSION < SAI_VERSION(1, 16, 0)
  if (samplingRate.has_value()) {
    XLOG_EVERY_MS(WARN, 60'000)
        << "MirrorOnDropReport configured with samplingRate=" << *samplingRate
        << " but SAI_TAM_REPORT_ATTR_SAMPLE_RATE requires SAI >= 1.16; "
        << "sampling will be ignored";
  }
#endif
  SaiTamReportTraits::CreateAttributes reportTraits{
      SAI_TAM_REPORT_TYPE_VENDOR_EXTN
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      (samplingRate.has_value() && *samplingRate > 0)
          ? std::optional<SaiTamReportTraits::Attributes::SampleRate>(
                static_cast<sai_uint32_t>(*samplingRate))
          : std::nullopt,
      (aggregateRateThreshold.has_value() && *aggregateRateThreshold > 0)
          ? std::optional<SaiTamReportTraits::Attributes::MaxReportRate>(
                static_cast<sai_uint64_t>(*aggregateRateThreshold))
          : std::nullopt,
      std::nullopt /* MaxReportBurst */
#endif
  };
  auto& reportStore = saiStore->get<SaiTamReportTraits>();
  return reportStore.setObject(reportTraits, reportTraits);
}

} // namespace

std::shared_ptr<SaiTamReport> SaiTamManager::createTamReport(
    sai_int32_t reportType) {
  auto& reportStore = saiStore_->get<SaiTamReportTraits>();
  auto reportTraits = SaiTamReportTraits::CreateAttributes{
      reportType
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      ,
      std::nullopt /* SampleRate */,
      std::nullopt /* MaxReportRate */,
      std::nullopt /* MaxReportBurst */
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
    std::optional<folly::MacAddress> /* dstMac */) {
  auto& transportStore = saiStore_->get<SaiTamTransportTraits>();
  auto transportTraits = SaiTamTransportTraits::AdapterHostKey{
      transportType,
      report->getLocalSrcPort(),
      report->getCollectorPort(),
      report->getMtu(),
      std::nullopt, // srcMac unsupported on Tajo (SDK derives it from RIF)
      std::nullopt // dstMac unsupported on Tajo
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
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  auto collectorKey = collectorTraits;
  // Match SDK default readback without programming the omitted attribute.
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

SaiTamManager::SaiTamManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::TELEMETRY_AND_MONITORING)) {
    return;
  }

  // To address SMS out of bank error seen in S337271, TAJO has a new
  // notification which will notify FBOSS agent on SMS out of bank
  // errors in the fleet via SAI_SWITCH_EVENT_TYPE_LACK_OF_RESOURCES.
  // However, this would need the SwitchEventType to be modified and
  // TAJO has a limitation that only one TAM object can be created at
  // any time. This means, warm boot upgrade is not possible to a new
  // version which will try to create a new TAM object as that would
  // end up with 2 TAM objects being created and the create would fail.
  // The work around is to delete the TAM object and creating a new one.
  if (platform_->getHwSwitch()->getBootType() == BootType::WARM_BOOT) {
    // Reset the tam object to start with.
    // TODO: As of now, resetTamObject() API fails for TAJO, while this
    // is being fixed in MT-665, using the work around to setTamObject()
    // with empty vector to reset.
    managerTable_->switchManager().setTamObject({});
    saiStore_->get<SaiTamTraits>().removeUnexpectedUnclaimedWarmbootHandles();
    saiStore_->get<SaiTamEventTraits>()
        .removeUnexpectedUnclaimedWarmbootHandles();
    saiStore_->get<SaiTamEventActionTraits>()
        .removeUnexpectedUnclaimedWarmbootHandles();
    saiStore_->get<SaiTamReportTraits>()
        .removeUnexpectedUnclaimedWarmbootHandles();
  }

  // create report
  auto report = createTamReport(SAI_TAM_REPORT_TYPE_VENDOR_EXTN);

  // create action
  auto& actionStore = saiStore_->get<SaiTamEventActionTraits>();
  auto actionTraits =
      SaiTamEventActionTraits::CreateAttributes{report->adapterKey()};
  auto action = actionStore.setObject(actionTraits, actionTraits);

  // create event
  SaiTamEventTraits::CreateAttributes eventTraits;
  std::vector<sai_object_id_t> actions{action->adapterKey()};
  std::vector<sai_object_id_t> collectors{};
  std::vector<sai_int32_t> eventTypes{
      SAI_SWITCH_EVENT_TYPE_PARITY_ERROR
#if defined(TAJO_SDK_VERSION_1_42_8)
      ,
      SAI_SWITCH_EVENT_TYPE_LACK_OF_RESOURCES
#endif
  };
  std::get<SaiTamEventTraits::Attributes::Type>(eventTraits) =
      SAI_TAM_EVENT_TYPE_SWITCH;
  std::get<SaiTamEventTraits::Attributes::ActionList>(eventTraits) = actions;
  std::get<SaiTamEventTraits::Attributes::CollectorList>(eventTraits) =
      collectors;
  std::get<std::optional<SaiTamEventTraits::Attributes::SwitchEventType>>(
      eventTraits) = eventTypes;
  auto& eventStore = saiStore_->get<SaiTamEventTraits>();
  auto eventAdapterHostKey = tupleProjection<
      SaiTamEventTraits::CreateAttributes,
      SaiTamEventTraits::AdapterHostKey>(eventTraits);
  auto event = eventStore.setObject(eventAdapterHostKey, eventTraits);

  // create tam
  std::vector<sai_object_id_t> events = {event->adapterKey()};
  SaiTamTraits::CreateAttributes tamTraits;
  std::get<SaiTamTraits::Attributes::EventObjectList>(tamTraits) = events;
  std::get<SaiTamTraits::Attributes::TamBindPointList>(tamTraits) =
      kTajoTamBindpoints;
  auto& tamStore = saiStore_->get<SaiTamTraits>();
  auto tam = tamStore.setObject(tamTraits, tamTraits);

  auto tamHandle = std::make_unique<SaiTamHandle>();
  tamHandle->report = report;
  tamHandle->action = action;
  tamHandle->events.push_back(event);
  tamHandle->tam = tam;
  tamHandle->managerTable = managerTable_;
  // associate TAM with switch
  managerTable_->switchManager().setTamObject({tamHandle->tam->adapterKey()});

  tamHandles_.emplace(kDefaultTamHandle, std::move(tamHandle));
}

void SaiTamManager::addMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& report) {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::TELEMETRY_AND_MONITORING)) {
    XLOG(WARN) << "MirrorOnDrop requested but TAM is unsupported on this ASIC";
    return;
  }
  if (tamHandles_.contains(report->getID())) {
    XLOG(WARN) << "MirrorOnDropReport " << report->getID()
               << " already exists; skipping add";
    return;
  }
  // Per Tajo TAM API doc, MoD packets are routed out a front-panel port.
  // Wait for collector IP resolution to know which port that is.
  if (!report->isResolved()) {
    XLOG(INFO) << "Skipping Tajo MirrorOnDropReport " << report->getID()
               << " - collector IP not yet resolved";
    return;
  }
  auto resolvedPortDesc = report->getResolvedEgressPort().value();
  PortID egressPort = PortID(*resolvedPortDesc.portId());

  XLOG(INFO) << "Adding Tajo MirrorOnDropReport " << report->getID()
             << " srcIp=" << report->getLocalSrcIp().str()
             << " srcPort=" << report->getLocalSrcPort()
             << " collectorIp=" << report->getCollectorIp().str()
             << " collectorPort=" << report->getCollectorPort()
             << " dscp=" << static_cast<int>(report->getDscp())
             << " egressPort=" << egressPort << " dropPacketRateThreshold="
             << report->getDropPacketRateThreshold().value_or(0)
             << " aggregateDropPacketRateThreshold="
             << report->getAggregateDropPacketRateThreshold().value_or(0);

  auto reportObj = createMirrorOnDropReport(
      saiStore_,
      report->getSamplingRate(),
      report->getAggregateDropPacketRateThreshold());
  auto action = createTamAction(reportObj->adapterKey());
  auto transport = createTamTransport(report, SAI_TAM_TRANSPORT_TYPE_UDP);
  auto collector = createTamCollector(
      report, transport->adapterKey(), report->getTruncateSize());
  auto threshold =
      createTamEventThreshold(saiStore_, report->getDropPacketRateThreshold());
  auto event = createMirrorOnDropEvent(
      saiStore_, action->adapterKey(), collector->adapterKey(), threshold);

  auto handle = std::make_unique<SaiTamHandle>();
  handle->report = reportObj;
  handle->action = action;
  handle->transport = transport;
  handle->collector = collector;
  handle->eventThreshold = threshold;
  handle->events.push_back(event);
  handle->portId = egressPort;
  handle->managerTable = managerTable_;
  tamHandles_.emplace(report->getID(), std::move(handle));

  rebuildAndRebindTam(saiStore_, managerTable_, tamHandles_);
}

void SaiTamManager::removeMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& report) {
  auto it = tamHandles_.find(report->getID());
  if (it == tamHandles_.end()) {
    XLOG(INFO) << "MirrorOnDropReport " << report->getID()
               << " not present; nothing to remove";
    return;
  }
  XLOG(INFO) << "Removing Tajo MirrorOnDropReport " << report->getID();
  // Clear the port's TAM binding before erasing the handle. rebuildAndRebind
  // derives the port set from live tamHandles_, so once erased the port would
  // never get unbound and would retain a dangling reference to the destroyed
  // TAM object.
  managerTable_->portManager().setTamObject(it->second->portId, {});
  tamHandles_.erase(it);
  rebuildAndRebindTam(saiStore_, managerTable_, tamHandles_);
}

void SaiTamManager::changeMirrorOnDropReport(
    const std::shared_ptr<MirrorOnDropReport>& oldReport,
    const std::shared_ptr<MirrorOnDropReport>& newReport) {
  // If the new report cannot be added (collector IP unresolved), retain the
  // old report instead of silently dropping MoD coverage. The next state
  // delta after IP resolution will retry the change.
  if (!newReport->isResolved()) {
    XLOG(INFO) << "Deferring MirrorOnDropReport change for "
               << newReport->getID()
               << ": collector IP unresolved. Retaining old report "
               << oldReport->getID();
    return;
  }
  // Known limitation: this triggers two full rebuildAndRebindTam cycles
  // (one in remove, one in add) which transiently unbinds the parity-error
  // TAM from the switch and from every other MoD port during the gap. A
  // future optimization can extract no-rebuild variants of remove/add and
  // trigger a single rebuild here.
  removeMirrorOnDropReport(oldReport);
  addMirrorOnDropReport(newReport);
}

std::vector<PortID> SaiTamManager::getAllMirrorOnDropPortIds() {
  std::vector<PortID> portIds;
  for (const auto& [name, handle] : tamHandles_) {
    if (name == kDefaultTamHandle) {
      continue;
    }
    portIds.push_back(handle->portId);
  }
  return portIds;
}

} // namespace facebook::fboss
