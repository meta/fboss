// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiPortManager.h"

namespace facebook::fboss {

void SaiPortManager::addRemovedHandle(const PortID& /*portID*/) {}

void SaiPortManager::removeRemovedHandleIf(const PortID& /*portID*/) {}

bool SaiPortManager::checkPortSerdesAttributes(
    const SaiPortSerdesTraits::CreateAttributes& fromStore,
    const SaiPortSerdesTraits::CreateAttributes& fromSwPort) {
  auto swPortAttributes = fromSwPort;
#if defined(BRCM_SAI_SDK_GTE_13_0) ||            \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) && \
     !defined(BRCM_SAI_SDK_XGS_AND_DNX))
  auto ignoreUnsetAuxiliaryAttribute = [&](auto type) {
    using Attribute = std::decay_t<decltype(type)>;
    auto& desired = std::get<std::optional<Attribute>>(swPortAttributes);
    if (!desired.has_value()) {
      desired = std::get<std::optional<Attribute>>(fromStore);
    }
  };
#endif
#if defined(BRCM_SAI_SDK_GTE_13_0)
  ignoreUnsetAuxiliaryAttribute(SaiPortSerdesTraits::Attributes::RxReach{});
#endif
#if defined(BRCM_SAI_SDK_GTE_13_0) ||            \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) && \
     !defined(BRCM_SAI_SDK_XGS_AND_DNX))
  ignoreUnsetAuxiliaryAttribute(
      SaiPortSerdesTraits::Attributes::TxPrecodingAttr{});
  ignoreUnsetAuxiliaryAttribute(
      SaiPortSerdesTraits::Attributes::RxPrecodingAttr{});
#endif
  return swPortAttributes == fromStore;
}

void SaiPortManager::changePortByRecreate(
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  removePort(oldPort);
  addPort(newPort);
}

void SaiPortManager::changePortFlowletConfig(
    const std::shared_ptr<Port>& /* unused */,
    const std::shared_ptr<Port>& /* unused */) {}

void SaiPortManager::changeClm(
    const std::shared_ptr<Port>& /* oldPort */,
    const std::shared_ptr<Port>& /* newPort */) {}

void SaiPortManager::clearPortFlowletConfig(const PortID& /* unused */) {}

void SaiPortManager::programPfcDurationCounterEnable(
    const std::shared_ptr<Port>& /* swPort */,
    const std::optional<cfg::PortPfc>& /* newPfc */,
    const std::optional<cfg::PortPfc>& /* oldPfc */) {}

const std::vector<sai_stat_id_t>& SaiPortManager::getSupportedPfcDurationStats(
    const PortID& /* portId */) {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

void SaiPortManager::fillInSupportedVendorExtStats(
    std::vector<sai_stat_id_t>& /*counterIds*/) {}

} // namespace facebook::fboss
