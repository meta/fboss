// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/types.h"

#include <gtest/gtest_prod.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace facebook::fboss {

class HwAsicTable;
class StateDelta;
class SwitchIdScopeResolver;
class SwitchState;

class NeighborResourceAccountant {
 public:
  NeighborResourceAccountant(
      const HwAsicTable* asicTable,
      const SwitchIdScopeResolver* scopeResolver);
  void stateChanged(const StateDelta& delta);
  bool isValidUpdate(const StateDelta& delta);

 private:
  bool checkNeighborResource();

  template <typename TableT>
  void neighborStateChangedImpl(const StateDelta& delta);

  template <typename TableT>
  std::optional<uint32_t> getMaxAsicNeighborTableSize(
      SwitchID switchId,
      uint8_t resourcePercentage);
  std::optional<uint32_t> getMaxAsicUnifiedNeighborTableSize(
      const SwitchID& switchId,
      uint8_t resourcePercentage);
  template <typename TableT>
  uint32_t getMaxConfiguredNeighborTableSize();
  SwitchID getSwitchIdFromNeighborEntry(
      std::shared_ptr<SwitchState> state,
      const auto& nbrEntry);
  template <typename TableT>
  std::unordered_map<SwitchID, uint32_t>& getNeighborEntriesMap();

  const HwAsicTable* asicTable_;
  const SwitchIdScopeResolver* scopeResolver_;
  std::unordered_map<SwitchID, uint32_t> ndpEntriesMap_;
  std::unordered_map<SwitchID, uint32_t> arpEntriesMap_;

  FRIEND_TEST(NeighborResourceAccountantTest, checkNeighborResource);
  FRIEND_TEST(NeighborResourceAccountantTest, removeInterfaceWithNeighbors);
};

} // namespace facebook::fboss
