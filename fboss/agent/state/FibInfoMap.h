/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/types.h"

#include <memory>

namespace facebook::fboss {

class SwitchState;
class HwSwitchMatcher;

using MultiSwitchFibInfoMapTypeClass = apache::thrift::type_class::map<
    apache::thrift::type_class::string,
    apache::thrift::type_class::structure>;

using MultiSwitchFibInfoMapThriftType =
    std::map<std::string, state::FibInfoFields>;

class MultiSwitchFibInfoMap;

using MultiSwitchFibInfoMapTraits = ThriftMapNodeTraits<
    MultiSwitchFibInfoMap,
    MultiSwitchFibInfoMapTypeClass,
    MultiSwitchFibInfoMapThriftType,
    FibInfo>;

class MultiSwitchFibInfoMap
    : public ThriftMapNode<MultiSwitchFibInfoMap, MultiSwitchFibInfoMapTraits> {
 public:
  using Traits = MultiSwitchFibInfoMapTraits;
  using BaseT =
      ThriftMapNode<MultiSwitchFibInfoMap, MultiSwitchFibInfoMapTraits>;
  using BaseT::modify;

  MultiSwitchFibInfoMap() = default;
  virtual ~MultiSwitchFibInfoMap() = default;

  // Get FibInfo for a specific switch
  std::shared_ptr<FibInfo> getFibInfo(const HwSwitchMatcher& matcher) const {
    return this->getNodeIf(matcher.matcherString());
  }

  // Get FibContainer directly by RouterID by searching all switches
  // Mirrors MultiSwitchForwardingInformationBaseMap::getNode() behavior and
  // returns ForwardingInformationBaseContainer
  // This is on the current assumption that the VRFs in all the switchIdLists
  // are mutually exclusive. Meaning a VRF can only be present in one
  // switchIdList. Therefore, we can just return the first FibContainer we find.
  std::shared_ptr<ForwardingInformationBaseContainer> getFibContainerIf(
      RouterID vrf) const {
    for (const auto& [_, fibInfo] : std::as_const(*this)) {
      if (auto fibContainer = fibInfo->getFibContainerIf(vrf)) {
        return fibContainer;
      }
    }
    return nullptr;
  }

  // Resolve an ERM override NextHopSetId via the first FibInfo that has it.
  std::optional<std::vector<NextHop>> resolveOverrideNextHopSetFromIdIf(
      NextHopSetId id) const {
    for (const auto& [_, fibInfo] : std::as_const(*this)) {
      if (auto nhops = fibInfo->resolveOverrideNextHopSetFromIdIf(id)) {
        return nhops;
      }
    }
    return std::nullopt;
  }

  // Get FibContainer directly by RouterID - throws if not found
  std::shared_ptr<ForwardingInformationBaseContainer> getFibContainer(
      RouterID vrf) const {
    auto container = getFibContainerIf(vrf);
    if (!container) {
      throw FbossError("FibContainer not found for VRF: ", vrf);
    }
    return container;
  }

  // Get FibContainer from the specified switch in HwSwitchMatcher and RouterID
  // Returns ForwardingInformationBaseContainer
  std::shared_ptr<ForwardingInformationBaseContainer> getFibContainerIf(
      const HwSwitchMatcher& matcher,
      RouterID vrf) const {
    auto fibInfo = getFibInfo(matcher);
    if (fibInfo) {
      return fibInfo->getFibContainerIf(vrf);
    }
    return nullptr;
  }

  // Update or add FibInfo for a specific switch
  void updateFibInfo(
      const std::shared_ptr<FibInfo>& fibInfo,
      const HwSwitchMatcher& matcher);

  // Merge FibContainers from all switches into a single
  // ForwardingInformationBaseMap, This mirrors
  // MultiSwitchForwardingInformationBaseMap::getAllNodes() behavior
  std::shared_ptr<ForwardingInformationBaseMap> getAllFibNodes() const;

  // Find which FibInfo contains a specific FibContainer
  // Returns the FibInfo that contains the container, or nullptr if not found
  std::shared_ptr<FibInfo> findFibInfoForContainer(
      const ForwardingInformationBaseContainer* container) const {
    for (const auto& [_, fibInfo] : std::as_const(*this)) {
      auto fibsMap = fibInfo->getfibsMap();
      if (!fibsMap) {
        continue;
      }
      auto foundContainer = fibsMap->getNodeIf(container->getID());

      if (foundContainer && foundContainer.get() == container) {
        return fibInfo;
      }
    }
    return nullptr;
  }

  // Get total route count across all FibInfo objects (v4, v6)
  std::pair<uint64_t, uint64_t> getRouteCount() const;

  // Get total number of VRFs across all FibInfo objects
  size_t getVrfCount() const;

  MultiSwitchFibInfoMap* modify(std::shared_ptr<SwitchState>* state);

 private:
  // Inherit constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
