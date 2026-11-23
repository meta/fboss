// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/switch/SaiTamManager.h"

#include "fboss/agent/hw/sai/store/SaiStore.h"

#include "fboss/agent/hw/sai/api/PortApi.h"

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 1) && !defined(CHENAB_SAI_SDK)
#include <algorithm>
#include <set>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/api/SaiObjectApi.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#endif

namespace facebook::fboss {

namespace {

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 1) && !defined(CHENAB_SAI_SDK)
bool isCsigAttributeSupported(
    sai_object_id_t switchId,
    sai_attr_id_t attribute) {
  const auto capability =
      getAttributeCapability(switchId, SAI_OBJECT_TYPE_TAM_INT, attribute);
  return capability.create_implemented && capability.set_implemented;
}

#endif

} // namespace

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

void SaiTamManager::clearCsig() {
  if (SaiPortTraits::Attributes::AttributeCsigTagActionWrapper{}()) {
    return;
  }
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 1) && !defined(CHENAB_SAI_SDK)
  managerTable_->switchManager().setTamObject(
      getSwitchTamIds(eventTamHandles_));
  csigTamHandle_.reset();
#endif
}

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 1) && !defined(CHENAB_SAI_SDK)
void SaiTamManager::checkCsigCapabilities(
    std::vector<SaiTamIntTraits::CreateAttributes>& profiles) const {
  if (profiles.empty()) {
    return;
  }
  using A = SaiTamIntTraits::Attributes;
  const auto switchId = managerTable_->switchManager().getSwitchSaiId();
  const auto types = getAttributeEnumValuesCapability(
      switchId, SAI_OBJECT_TYPE_TAM_INT, SAI_TAM_INT_ATTR_TYPE);
  const auto signals = getAttributeEnumValuesCapability(
      switchId, SAI_OBJECT_TYPE_TAM_INT, SAI_TAM_INT_ATTR_CSIG_SIGNAL_TYPE);
  std::set<sai_uint32_t> intervals;
  for (auto& attributes : profiles) {
    const auto type = std::get<A::Type>(attributes).value();
    const auto signal = std::get<A::CsigSignalType>(attributes).value();
    if (std::ranges::find(types, type) == types.end() ||
        std::ranges::find(signals, signal) == signals.end()) {
      throw FbossError("Unsupported CSIG tag format or signal");
    }
    if (std::get<std::optional<A::QuantBandId>>(attributes) &&
        !isCsigAttributeSupported(switchId, SAI_TAM_INT_ATTR_QUANT_BAND_ID)) {
      throw FbossError(
          "CSIG attribute is unsupported: ", SAI_TAM_INT_ATTR_QUANT_BAND_ID);
    }
    auto& dBit = std::get<std::optional<A::CsigDBit>>(attributes);
    if (dBit &&
        !isCsigAttributeSupported(switchId, SAI_TAM_INT_ATTR_CSIG_D_BIT)) {
      if (dBit->value()) {
        throw FbossError("CSIG marking of trimmed packets is unsupported");
      }
      dBit.reset();
    }
    if (const auto& bandwidth =
            std::get<std::optional<A::BwTimeInterval>>(attributes)) {
      intervals.insert(bandwidth->value());
    }
  }
  if (intervals.empty()) {
    return;
  }
  const auto& api = SaiApiTable::getInstance()->switchApi();
  const auto maxTimers =
      api.getAttribute(switchId, SaiSwitchTraits::Attributes::MaxCsigTimers{});
  if (intervals.size() > maxTimers) {
    throw FbossError("CSIG bandwidth timer capacity exceeded");
  }
  const auto supported = api.getAttribute(
      switchId, SaiSwitchTraits::Attributes::CsigBwTimeIntervalInUsec{});
  for (const auto interval : intervals) {
    if (std::ranges::find(supported, interval) == supported.end()) {
      throw FbossError("Unsupported CSIG bandwidth interval: ", interval);
    }
  }
}

void SaiTamManager::programCsigProfiles(
    std::vector<SaiTamIntTraits::CreateAttributes> profiles,
    bool bindToSwitch) {
  if (!platform_->getAsic()->isSupported(HwAsic::Feature::CSIG)) {
    throw FbossError("CSIG is not supported on this ASIC/SDK");
  }
  if (SaiPortTraits::Attributes::AttributeCsigTagActionWrapper{}()) {
    if (!profiles.empty()) {
      throw FbossError("This CSIG SDK does not support SAI TAM INT profiles");
    }
    return;
  }
  if (bindToSwitch && profiles.empty()) {
    throw FbossError("CSIG UPDATE requires measurement profiles");
  }
  checkCsigCapabilities(profiles);
  auto handle = std::make_unique<SaiCsigTamHandle>();
  std::vector<sai_object_id_t> intIds;
  for (const auto& attributes : profiles) {
    const auto key = tupleProjection<
        SaiTamIntTraits::CreateAttributes,
        SaiTamIntTraits::AdapterHostKey>(attributes);
    auto object = saiStore_->get<SaiTamIntTraits>().setObject(key, attributes);
    intIds.push_back(object->adapterKey());
    handle->profiles.push_back(std::move(object));
  }
  auto tamIds = getSwitchTamIds(eventTamHandles_);
  if (bindToSwitch) {
    SaiTamTraits::CreateAttributes tamAttrs{
        std::vector<sai_object_id_t>{},
        std::vector<sai_int32_t>{SAI_TAM_BIND_POINT_TYPE_SWITCH}
#if !defined(TAJO_SDK)
        ,
        intIds
#endif
    };
    handle->tam = saiStore_->get<SaiTamTraits>().setObject(tamAttrs, tamAttrs);
    tamIds.push_back(handle->tam->adapterKey());
  }
  managerTable_->switchManager().setTamObject(std::move(tamIds));
  csigTamHandle_ = std::move(handle);
}
#endif

} // namespace facebook::fboss
