// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <utility>

#include "fboss/agent/hw/sai/api/TamApi.h"
#include "fboss/agent/hw/sai/api/TamEventAgingGroupApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/hw/sai/switch/SaiSamplePacketManager.h"
#include "fboss/agent/state/MirrorOnDropReport.h"

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
struct SaiPortHandle;
class SaiStore;

using SaiTamCollector = SaiObject<SaiTamCollectorTraits>;
using SaiTamTransport = SaiObject<SaiTamTransportTraits>;
using SaiTamReport = SaiObject<SaiTamReportTraits>;
using SaiTamEventAction = SaiObject<SaiTamEventActionTraits>;
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
using SaiTamEventAgingGroup = SaiObject<SaiTamEventAgingGroupTraits>;
#endif
using SaiTamEvent = SaiObject<SaiTamEventTraits>;
using SaiTamEventThreshold = SaiObject<SaiTamEventThresholdTraits>;
using SaiTam = SaiObject<SaiTamTraits>;

namespace tam {
// kModSwitchEventId used as an identifier for the event; 1 chosen arbitrarily.
constexpr uint32_t kModSwitchEventId{1};
// kModDeviceId used as an identifier for the device; 0 chosen arbitrarily.
constexpr uint32_t kModDeviceId{0};
} // namespace tam

struct SaiTamHandle {
  // Reverse declaration order is the SAI dependency destruction order.
  std::shared_ptr<SaiTamReport> report;
  std::shared_ptr<SaiTamEventAction> action;
  std::shared_ptr<SaiTamTransport> transport;
  std::shared_ptr<SaiTamCollector> collector;
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  std::vector<std::shared_ptr<SaiTamEventAgingGroup>> agingGroups;
#endif
#if defined(BRCM_SAI_SDK_XGS_GTE_13_0)
  std::shared_ptr<SaiSamplePacket> samplePacket;
#endif
  // Tajo stateless MoD: optional event-rate threshold object.
  std::shared_ptr<SaiTamEventThreshold> eventThreshold;
  std::vector<std::shared_ptr<SaiTamEvent>> events;
  std::shared_ptr<SaiTam> tam;
  PortID portId;
  SaiManagerTable* managerTable{nullptr};
};
class SaiTamManager {
 public:
  SaiTamManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      SaiPlatform* platform);

  void addMirrorOnDropReport(const std::shared_ptr<MirrorOnDropReport>& report);
  void removeMirrorOnDropReport(
      const std::shared_ptr<MirrorOnDropReport>& report);
  void changeMirrorOnDropReport(
      const std::shared_ptr<MirrorOnDropReport>& oldReport,
      const std::shared_ptr<MirrorOnDropReport>& newReport);
  std::vector<PortID> getAllMirrorOnDropPortIds();
#if defined(CHENAB_SAI_SDK)
  void removeMirrorOnDropReportsForPort(PortID portId);
#endif

  const SaiTamHandle* getTamHandle(const std::string& name) const {
    return tamHandles_.contains(name) ? tamHandles_.at(name).get() : nullptr;
  }
  SaiTamHandle* getTamHandle(const std::string& name) {
    return tamHandles_.contains(name) ? tamHandles_.at(name).get() : nullptr;
  }

  void gracefulExit() {
    tamHandles_.clear();
  }

 private:
  std::string getDestMacWithOverride(const std::string& defaultMac) const;

  std::shared_ptr<SaiTamReport> createTamReport(sai_int32_t reportType);

  std::shared_ptr<SaiTamEventAction> createTamAction(sai_object_id_t reportId);

  std::shared_ptr<SaiTamTransport> createTamTransport(
      const std::shared_ptr<MirrorOnDropReport>& report,
      sai_int32_t transportType,
      std::optional<folly::MacAddress> dstMac = std::nullopt);

  std::shared_ptr<SaiTamCollector> createTamCollector(
      const std::shared_ptr<MirrorOnDropReport>& report,
      sai_object_id_t transportId,
      std::optional<uint16_t> truncateSize);

  std::shared_ptr<SaiTam> createTam(
      const std::vector<sai_object_id_t>& eventIds,
      const std::vector<sai_int32_t>& bindpoints);

  void bindTam(sai_object_id_t tamId, const PortID& portId);

  void storeTamHandle(
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
      std::vector<std::shared_ptr<SaiTamEventAgingGroup>> agingGroups = {}
#endif
#if defined(BRCM_SAI_SDK_XGS_GTE_13_0)
      ,
      std::shared_ptr<SaiSamplePacket> samplePacket = nullptr
#endif
  );

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  SaiPlatform* platform_;
  folly::F14FastMap<std::string, std::unique_ptr<SaiTamHandle>> tamHandles_;
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  void addDnxMirrorOnDropReport(
      const std::shared_ptr<MirrorOnDropReport>& report);
#endif
#if defined(BRCM_SAI_SDK_XGS_GTE_13_0)
  void addXgsMirrorOnDropReport(
      const std::shared_ptr<MirrorOnDropReport>& report);
#endif
#if defined(CHENAB_SAI_SDK)
  void programMirrorOnDropReport(
      const std::shared_ptr<MirrorOnDropReport>& report,
      PortID portId,
      const SaiPortHandle* portHandle);
  std::shared_ptr<SaiTamTransport> createTamTransport();
  std::shared_ptr<SaiTamCollector> createTamCollector(
      const std::shared_ptr<MirrorOnDropReport>& report,
      sai_object_id_t transportId,
      sai_object_id_t destinationId);
  std::shared_ptr<SaiTamEvent> createTamEvent(
      sai_object_id_t actionId,
      sai_object_id_t collectorId);
  std::optional<std::pair<PortID, const SaiPortHandle*>>
  getMirrorOnDropPortHandle(const MirrorOnDropReport& report) const;
#endif
};

} // namespace facebook::fboss
