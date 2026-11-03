/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiCounterManager.h"
#include "fboss/agent/hw/sai/api/CounterApi.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include <algorithm>

namespace facebook::fboss {

namespace {
constexpr size_t kMaxCounterLabelSize = 31;
} // namespace

std::shared_ptr<SaiCounterHandle> SaiCounterManager::incRefOrAddRouteCounter(
    std::string counterID) {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  bool useExtendedLabel = false;
#if defined(TAJO_SDK_GTE_26_2) && !defined(TAJO_SDK_P200)
  useExtendedLabel = true;
#endif
  if (!useExtendedLabel && counterID.size() > kMaxCounterLabelSize) {
    throw FbossError(
        "CounterID label size ",
        counterID.size(),
        " exceeds max(",
        kMaxCounterLabelSize,
        ")");
  }
  auto [entry, emplaced] = routeCounters_.refOrEmplace(counterID, counterID);
  if (emplaced) {
    SaiCharArray32 labelValue{};
    std::optional<SaiCounterTraits::Attributes::LabelExtended> labelExtended;
    if (useExtendedLabel) {
      labelExtended = SaiCounterTraits::Attributes::LabelExtended{
          std::vector<int8_t>(counterID.begin(), counterID.end())};
    } else {
      std::copy(counterID.begin(), counterID.end(), labelValue.begin());
    }
    SaiCounterTraits::CreateAttributes attrs{
        labelValue, SAI_COUNTER_TYPE_REGULAR, std::move(labelExtended)};
    auto& counterStore = saiStore_->get<SaiCounterTraits>();
    entry->counter = counterStore.setObject(attrs, attrs);
  }
  return entry;
#else
  return nullptr;
#endif
}

void SaiCounterManager::updateStats() {
  for (const auto& counter : routeCounters_) {
    auto counterHandle = counter.second.lock();
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    counterHandle->counter->updateStats(
        {SAI_COUNTER_STAT_BYTES, SAI_COUNTER_STAT_PACKETS},
        SAI_STATS_MODE_READ);
#endif
    auto& stats = counterHandle->counter->getStats();
    counterHandle->hwSwitchCounter.packets() =
        stats.at(SAI_COUNTER_STAT_PACKETS);
    counterHandle->hwSwitchCounter.bytes() = stats.at(SAI_COUNTER_STAT_BYTES);
  }
}

HwSwitchCounterStats SaiCounterManager::getHwSwitchCounterStats() const {
  HwSwitchCounterStats counterStats;
  for (const auto& counter : routeCounters_) {
    auto counterHandle = counter.second.lock();
    counterStats.routeCounters()[counter.first] =
        counterHandle->hwSwitchCounter;
  }
  return counterStats;
}

uint64_t SaiCounterManager::getStats(std::string counterID) const {
  auto handle = routeCounters_.get(counterID);
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  handle->counter->updateStats({SAI_COUNTER_STAT_BYTES}, SAI_STATS_MODE_READ);
#endif
  auto stats = handle->counter->getStats();
  return stats.begin()->second;
}

} // namespace facebook::fboss
