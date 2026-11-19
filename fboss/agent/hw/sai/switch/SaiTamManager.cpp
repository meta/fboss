// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/switch/SaiTamManager.h"

#include "fboss/agent/hw/sai/store/SaiStore.h"

namespace facebook::fboss {

#if !defined(TAJO_SAI_SDK)
SaiTamManager::SaiTamManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {}

#endif

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

std::shared_ptr<SaiTam> SaiTamManager::createTam(
    const std::vector<sai_object_id_t>& eventIds,
    const std::vector<sai_int32_t>& bindpoints) {
  SaiTamTraits::CreateAttributes tamTraits;
  std::get<SaiTamTraits::Attributes::EventObjectList>(tamTraits) = eventIds;
  std::get<SaiTamTraits::Attributes::TamBindPointList>(tamTraits) = bindpoints;
  auto& tamStore = saiStore_->get<SaiTamTraits>();
  return tamStore.setObject(tamTraits, tamTraits);
}

std::vector<sai_object_id_t> getSwitchTamIds(
    folly::F14FastMap<std::string, std::unique_ptr<SaiEventTamHandle>>&
        tamHandles) {
  std::vector<sai_object_id_t> switchTamIds;
  for (const auto& [_, tamHandle] : tamHandles) {
    if (tamHandle->tam) {
      switchTamIds.push_back(tamHandle->tam->adapterKey());
    }
  }
  return switchTamIds;
}

std::vector<sai_object_id_t> getSwitchTamIds(
    folly::F14FastMap<std::string, std::unique_ptr<SaiEventTamHandle>>&
        eventTamHandles,
    const SaiCsigTamHandle* csigTamHandle) {
  auto tamIds = getSwitchTamIds(eventTamHandles);
  if (csigTamHandle && csigTamHandle->tam) {
    tamIds.push_back(csigTamHandle->tam->adapterKey());
  }
  return tamIds;
}

} // namespace facebook::fboss
