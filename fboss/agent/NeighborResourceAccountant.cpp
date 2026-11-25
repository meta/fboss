// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/NeighborResourceAccountant.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/logging/xlog.h>

#include <set>

namespace facebook::fboss {

namespace {
constexpr auto kHundredPercentage = 100;
} // namespace

NeighborResourceAccountant::NeighborResourceAccountant(
    const HwAsicTable* asicTable,
    const SwitchIdScopeResolver* scopeResolver)
    : asicTable_(asicTable), scopeResolver_(scopeResolver) {}

// get switchId from neighbor entry
SwitchID NeighborResourceAccountant::getSwitchIdFromNeighborEntry(
    std::shared_ptr<SwitchState> state,
    const auto& nbrEntry) {
  const auto& interfaceMap = state->getInterfaces();
  InterfaceID interfaceId = nbrEntry->getIntfID();
  std::shared_ptr<Interface> intf = interfaceMap->getNodeIf(interfaceId);
  if (!intf) {
    throw FbossError("No interface found for interfaceId: ", interfaceId);
  }
  SwitchID switchId = scopeResolver_->scope(intf, state).switchId();

  return switchId;
}

// get total neighbor table size from ASIC
template <typename TableT>
std::optional<uint32_t> NeighborResourceAccountant::getMaxAsicNeighborTableSize(
    SwitchID switchId,
    uint8_t resourcePercentage) {
  uint32_t size = 0;
  auto hwAsic = asicTable_->getHwAsicIf(SwitchID(switchId));

  if constexpr (std::is_same_v<TableT, NdpTable>) {
    size = hwAsic->getMaxNdpTableSize().has_value()
        ? hwAsic->getMaxNdpTableSize().value()
        : 0;
  } else if constexpr (std::is_same_v<TableT, ArpTable>) {
    size = hwAsic->getMaxArpTableSize().has_value()
        ? hwAsic->getMaxArpTableSize().value()
        : 0;
  } else { // Invalid resource type
    throw FbossError("Invalid resource type");
  }
  if (size == 0) {
    return std::nullopt;
  }
  return (size * resourcePercentage) / kHundredPercentage;
}

// get total unified neighbor table size from ASIC
// unified table is shared by both ARP and NDP tables
std::optional<uint32_t>
NeighborResourceAccountant::getMaxAsicUnifiedNeighborTableSize(
    const SwitchID& switchId,
    uint8_t resourcePercentage) {
  uint32_t size = 0;
  auto hwAsic = asicTable_->getHwAsicIf(SwitchID(switchId));

  size = hwAsic->getMaxUnifiedNeighborTableSize().has_value()
      ? hwAsic->getMaxUnifiedNeighborTableSize().value()
      : 0;
  if (size == 0) {
    return std::nullopt;
  }
  return (size * resourcePercentage) / kHundredPercentage;
}

// get max neighbor table size suported by resourceAccountant
template <typename TableT>
uint32_t NeighborResourceAccountant::getMaxConfiguredNeighborTableSize() {
  if constexpr (std::is_same_v<TableT, NdpTable>) {
    return FLAGS_max_ndp_entries;
  } else if constexpr (std::is_same_v<TableT, ArpTable>) {
    return FLAGS_max_arp_entries;
  }
  throw FbossError("Invalid resource type");
}

template <typename TableT>
std::unordered_map<SwitchID, uint32_t>&
NeighborResourceAccountant::getNeighborEntriesMap() {
  if constexpr (std::is_same_v<TableT, NdpTable>) {
    return ndpEntriesMap_;
  } else if constexpr (std::is_same_v<TableT, ArpTable>) {
    return arpEntriesMap_;
  } else { // Invalid resource type
    throw FbossError("Invalid resource type");
  }
}
// calculate new update for neighbor entries from the delta
template <typename TableT>
void NeighborResourceAccountant::neighborStateChangedImpl(
    const StateDelta& delta) {
  auto processDelta = [&](const auto& deltaNbr, auto& entriesMap) {
    DeltaFunctions::forEachChanged(
        deltaNbr,
        [&](const auto& /*old*/, const auto& /*new*/) {
          return LoopAction::CONTINUE;
        },
        [&](const auto& newNbr) {
          auto switchId =
              getSwitchIdFromNeighborEntry(delta.newState(), newNbr);
          entriesMap[switchId]++;
          return LoopAction::CONTINUE;
        },
        [&](const auto& deleted) {
          auto switchId =
              getSwitchIdFromNeighborEntry(delta.oldState(), deleted);
          entriesMap[switchId]--;
          return LoopAction::CONTINUE;
        });
  };

  for (auto& intfDelta : delta.getIntfsDelta()) {
    processDelta(
        intfDelta.getNeighborDelta<TableT>(), getNeighborEntriesMap<TableT>());
  }
}

bool NeighborResourceAccountant::checkNeighborResource() {
  std::set<SwitchID> allSwitchIds;
  for (const auto& [switchId, _] : ndpEntriesMap_) {
    allSwitchIds.insert(switchId);
  }
  for (const auto& [switchId, _] : arpEntriesMap_) {
    allSwitchIds.insert(switchId);
  }

  // Check each switch ID
  for (const auto& switchId : allSwitchIds) {
    auto ndpIt = ndpEntriesMap_.find(switchId);
    auto arpIt = arpEntriesMap_.find(switchId);
    uint32_t ndpCount = (ndpIt != ndpEntriesMap_.end()) ? ndpIt->second : 0;
    uint32_t arpCount = (arpIt != arpEntriesMap_.end()) ? arpIt->second : 0;

    // Check NDP limits
    if (ndpCount > 0) {
      auto maxNdpSize = getMaxConfiguredNeighborTableSize<NdpTable>();
      if (FLAGS_enforce_resource_hw_limits) {
        auto asicNdpSize =
            getMaxAsicNeighborTableSize<NdpTable>(switchId, kHundredPercentage);
        if (asicNdpSize.has_value()) {
          maxNdpSize = std::min(asicNdpSize.value(), maxNdpSize);
        }
      }
      if (ndpCount > maxNdpSize) {
        XLOG(ERR) << "Total NDP entries in new switchState: " << ndpCount
                  << " exceeds the limit: " << maxNdpSize
                  << " for switchId: " << switchId;
        return false;
      }
    }

    // Check ARP limits
    if (arpCount > 0) {
      auto maxArpSize = getMaxConfiguredNeighborTableSize<ArpTable>();
      if (FLAGS_enforce_resource_hw_limits) {
        auto asicArpSize =
            getMaxAsicNeighborTableSize<ArpTable>(switchId, kHundredPercentage);
        if (asicArpSize.has_value()) {
          maxArpSize = std::min(asicArpSize.value(), maxArpSize);
        }
      }
      if (arpCount > maxArpSize) {
        XLOG(ERR) << "Total ARP entries in new switchState: " << arpCount
                  << " exceeds the limit: " << maxArpSize
                  << " for switchId: " << switchId;
        return false;
      }
    }

    // Check unified table limits if enabled and available
    if (FLAGS_enforce_resource_hw_limits &&
        getMaxAsicUnifiedNeighborTableSize(switchId, kHundredPercentage)
            .has_value()) {
      uint32_t unifiedCount = ndpCount + arpCount;
      uint32_t maxUnifiedSize =
          getMaxAsicUnifiedNeighborTableSize(switchId, kHundredPercentage)
              .value();
      if (unifiedCount > maxUnifiedSize) {
        XLOG(ERR) << "Total unified neighbor entries in new switchState: "
                  << unifiedCount << " exceeds the limit: " << maxUnifiedSize
                  << " for switchId: " << switchId;
        return false;
      }
    }
  }

  return true;
}

void NeighborResourceAccountant::stateChanged(const StateDelta& delta) {
  neighborStateChangedImpl<NdpTable>(delta);
  neighborStateChangedImpl<ArpTable>(delta);
}

bool NeighborResourceAccountant::isValidUpdate(const StateDelta& delta) {
  stateChanged(delta);
  return checkNeighborResource();
}

} // namespace facebook::fboss
