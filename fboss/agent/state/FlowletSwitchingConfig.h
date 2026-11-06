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

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

class FlowletSwitchingConfig;
USE_THRIFT_COW(FlowletSwitchingConfig);

class FlowletSwitchingConfig : public ThriftStructNode<
                                   FlowletSwitchingConfig,
                                   cfg::FlowletSwitchingConfig> {
 public:
  using BaseT =
      ThriftStructNode<FlowletSwitchingConfig, cfg::FlowletSwitchingConfig>;
  using BaseT::modify;
  explicit FlowletSwitchingConfig() = default;

  void setInactivityIntervalUsecs(const uint16_t inactivityIntervalUsecs) {
    set<switch_config_tags::inactivityIntervalUsecs>(inactivityIntervalUsecs);
  }

  uint16_t getInactivityIntervalUsecs() const {
    return get<switch_config_tags::inactivityIntervalUsecs>()->cref();
  }

  void setFlowletTableSize(const uint16_t flowletTableSize) {
    set<switch_config_tags::flowletTableSize>(flowletTableSize);
  }

  uint16_t getFlowletTableSize() const {
    return get<switch_config_tags::flowletTableSize>()->cref();
  }

  void setDynamicEgressLoadExponent(const uint16_t dynamicEgressLoadExponent) {
    set<switch_config_tags::dynamicEgressLoadExponent>(
        dynamicEgressLoadExponent);
  }

  uint16_t getDynamicEgressLoadExponent() const {
    return get<switch_config_tags::dynamicEgressLoadExponent>()->cref();
  }

  void setDynamicQueueExponent(const uint16_t dynamicQueueExponent) {
    set<switch_config_tags::dynamicQueueExponent>(dynamicQueueExponent);
  }

  uint16_t getDynamicQueueExponent() const {
    return get<switch_config_tags::dynamicQueueExponent>()->cref();
  }

  void setDynamicQueueMinThresholdBytes(
      const uint32_t dynamicQueueMinThresholdBytes) {
    set<switch_config_tags::dynamicQueueMinThresholdBytes>(
        dynamicQueueMinThresholdBytes);
  }

  uint32_t getDynamicQueueMinThresholdBytes() const {
    return get<switch_config_tags::dynamicQueueMinThresholdBytes>()->cref();
  }

  void setDynamicQueueMaxThresholdBytes(
      const uint32_t dynamicQueueMaxThresholdBytes) {
    set<switch_config_tags::dynamicQueueMaxThresholdBytes>(
        dynamicQueueMaxThresholdBytes);
  }

  uint32_t getDynamicQueueMaxThresholdBytes() const {
    return get<switch_config_tags::dynamicQueueMaxThresholdBytes>()->cref();
  }

  void setDynamicSampleRate(const uint32_t dynamicSampleRate) {
    set<switch_config_tags::dynamicSampleRate>(dynamicSampleRate);
  }

  uint32_t getDynamicSampleRate() const {
    return get<switch_config_tags::dynamicSampleRate>()->cref();
  }

  void setDynamicEgressMinThresholdBytes(
      const uint32_t dynamicEgressMinThresholdBytes) {
    set<switch_config_tags::dynamicEgressMinThresholdBytes>(
        dynamicEgressMinThresholdBytes);
  }

  uint32_t getDynamicEgressMinThresholdBytes() const {
    return get<switch_config_tags::dynamicEgressMinThresholdBytes>()->cref();
  }

  void setDynamicEgressMaxThresholdBytes(
      const uint32_t dynamicEgressMaxThresholdBytes) {
    set<switch_config_tags::dynamicEgressMaxThresholdBytes>(
        dynamicEgressMaxThresholdBytes);
  }

  uint32_t getDynamicEgressMaxThresholdBytes() const {
    return get<switch_config_tags::dynamicEgressMaxThresholdBytes>()->cref();
  }

  void setDynamicPhysicalQueueExponent(
      const uint16_t dynamicPhysicalQueueExponent) {
    set<switch_config_tags::dynamicPhysicalQueueExponent>(
        dynamicPhysicalQueueExponent);
  }

  uint16_t getDynamicPhysicalQueueExponent() const {
    return get<switch_config_tags::dynamicPhysicalQueueExponent>()->cref();
  }

  void setMaxLinks(const uint16_t maxLinks) {
    set<switch_config_tags::maxLinks>(maxLinks);
  }

  uint16_t getMaxLinks() const {
    return get<switch_config_tags::maxLinks>()->cref();
  }

  void setSwitchingMode(cfg::SwitchingMode mode) {
    set<switch_config_tags::switchingMode>(mode);
  }

  cfg::SwitchingMode getSwitchingMode() const {
    return get<switch_config_tags::switchingMode>()->cref();
  }

  void setBackupSwitchingMode(cfg::SwitchingMode mode) {
    set<switch_config_tags::backupSwitchingMode>(mode);
  }

  cfg::SwitchingMode getBackupSwitchingMode() const {
    return get<switch_config_tags::backupSwitchingMode>()->cref();
  }

  void setPrimaryPathQualityThreshold(
      const std::optional<int32_t>& primaryPathQualityThreshold) {
    if (primaryPathQualityThreshold) {
      set<switch_config_tags::primaryPathQualityThreshold>(
          *primaryPathQualityThreshold);
    } else {
      ref<switch_config_tags::primaryPathQualityThreshold>().reset();
    }
  }

  std::optional<int32_t> getPrimaryPathQualityThreshold() const {
    if (auto primaryPathQualityThreshold =
            get<switch_config_tags::primaryPathQualityThreshold>()) {
      return primaryPathQualityThreshold->cref();
    }
    return std::nullopt;
  }

  void setAlternatePathCost(const std::optional<int32_t>& alternatePathCost) {
    if (alternatePathCost) {
      set<switch_config_tags::alternatePathCost>(*alternatePathCost);
    } else {
      ref<switch_config_tags::alternatePathCost>().reset();
    }
  }

  std::optional<int32_t> getAlternatePathCost() const {
    if (auto alternatePathCost = get<switch_config_tags::alternatePathCost>()) {
      return alternatePathCost->cref();
    }
    return std::nullopt;
  }

  void setAlternatePathBias(const std::optional<int32_t>& alternatePathBias) {
    if (alternatePathBias) {
      set<switch_config_tags::alternatePathBias>(*alternatePathBias);
    } else {
      ref<switch_config_tags::alternatePathBias>().reset();
    }
  }

  std::optional<int32_t> getAlternatePathBias() const {
    if (auto alternatePathBias = get<switch_config_tags::alternatePathBias>()) {
      return alternatePathBias->cref();
    }
    return std::nullopt;
  }

  void setMinWidthForArsVirtualGroup(
      const std::optional<int32_t>& minWidthForArsVirtualGroup) {
    if (minWidthForArsVirtualGroup) {
      set<switch_config_tags::minWidthForArsVirtualGroup>(
          *minWidthForArsVirtualGroup);
    } else {
      ref<switch_config_tags::minWidthForArsVirtualGroup>().reset();
    }
  }

  std::optional<int32_t> getMinWidthForArsVirtualGroup() const {
    if (auto minWidthForArsVirtualGroup =
            get<switch_config_tags::minWidthForArsVirtualGroup>()) {
      return minWidthForArsVirtualGroup->cref();
    }
    return std::nullopt;
  }

  void setMaxArsVirtualGroupWidth(
      const std::optional<int32_t>& maxArsVirtualGroupWidth) {
    if (maxArsVirtualGroupWidth) {
      set<switch_config_tags::maxArsVirtualGroupWidth>(
          *maxArsVirtualGroupWidth);
    } else {
      ref<switch_config_tags::maxArsVirtualGroupWidth>().reset();
    }
  }

  std::optional<int32_t> getMaxArsVirtualGroupWidth() const {
    if (auto maxArsVirtualGroupWidth =
            get<switch_config_tags::maxArsVirtualGroupWidth>()) {
      return maxArsVirtualGroupWidth->cref();
    }
    return std::nullopt;
  }

  void setMaxArsVirtualGroups(
      const std::optional<int32_t>& maxArsVirtualGroups) {
    if (maxArsVirtualGroups) {
      set<switch_config_tags::maxArsVirtualGroups>(*maxArsVirtualGroups);
    } else {
      ref<switch_config_tags::maxArsVirtualGroups>().reset();
    }
  }

  std::optional<int32_t> getMaxArsVirtualGroups() const {
    if (auto maxArsVirtualGroups =
            get<switch_config_tags::maxArsVirtualGroups>()) {
      return maxArsVirtualGroups->cref();
    }
    return std::nullopt;
  }

  void setStandbySwitchingMode(
      const std::optional<cfg::SwitchingMode>& standbySwitchingMode) {
    if (standbySwitchingMode) {
      set<switch_config_tags::standbySwitchingMode>(*standbySwitchingMode);
    } else {
      ref<switch_config_tags::standbySwitchingMode>().reset();
    }
  }

  std::optional<cfg::SwitchingMode> getStandbySwitchingMode() const {
    if (auto standbySwitchingMode =
            get<switch_config_tags::standbySwitchingMode>()) {
      return standbySwitchingMode->cref();
    }
    return std::nullopt;
  }

  void setStandbyInactivityIntervalUsecs(
      const std::optional<int16_t>& standbyInactivityIntervalUsecs) {
    if (standbyInactivityIntervalUsecs) {
      set<switch_config_tags::standbyInactivityIntervalUsecs>(
          *standbyInactivityIntervalUsecs);
    } else {
      ref<switch_config_tags::standbyInactivityIntervalUsecs>().reset();
    }
  }

  std::optional<int16_t> getStandbyInactivityIntervalUsecs() const {
    if (auto standbyInactivityIntervalUsecs =
            get<switch_config_tags::standbyInactivityIntervalUsecs>()) {
      return standbyInactivityIntervalUsecs->cref();
    }
    return std::nullopt;
  }

  void setStandbyFlowletTableSize(
      const std::optional<int16_t>& standbyFlowletTableSize) {
    if (standbyFlowletTableSize) {
      set<switch_config_tags::standbyFlowletTableSize>(
          *standbyFlowletTableSize);
    } else {
      ref<switch_config_tags::standbyFlowletTableSize>().reset();
    }
  }

  std::optional<int16_t> getStandbyFlowletTableSize() const {
    if (auto standbyFlowletTableSize =
            get<switch_config_tags::standbyFlowletTableSize>()) {
      return standbyFlowletTableSize->cref();
    }
    return std::nullopt;
  }

  void setArsVirtualGroupAlternateMembers(
      const std::optional<int32_t>& arsVirtualGroupAlternateMembers) {
    if (arsVirtualGroupAlternateMembers) {
      set<switch_config_tags::arsVirtualGroupAlternateMembers>(
          *arsVirtualGroupAlternateMembers);
    } else {
      ref<switch_config_tags::arsVirtualGroupAlternateMembers>().reset();
    }
  }

  std::optional<int32_t> getArsVirtualGroupAlternateMembers() const {
    if (auto arsVirtualGroupAlternateMembers =
            get<switch_config_tags::arsVirtualGroupAlternateMembers>()) {
      return arsVirtualGroupAlternateMembers->cref();
    }
    return std::nullopt;
  }

  void setArsVirtualGroupCommonMembersThreshold(
      const std::optional<int32_t>& arsVirtualGroupCommonMembersThreshold) {
    if (arsVirtualGroupCommonMembersThreshold) {
      set<switch_config_tags::arsVirtualGroupCommonMembersThreshold>(
          *arsVirtualGroupCommonMembersThreshold);
    } else {
      ref<switch_config_tags::arsVirtualGroupCommonMembersThreshold>().reset();
    }
  }

  std::optional<int32_t> getArsVirtualGroupCommonMembersThreshold() const {
    if (auto arsVirtualGroupCommonMembersThreshold =
            get<switch_config_tags::arsVirtualGroupCommonMembersThreshold>()) {
      return arsVirtualGroupCommonMembersThreshold->cref();
    }
    return std::nullopt;
  }

  void setArsGroupWidth(const std::optional<int32_t>& arsGroupWidth) {
    if (arsGroupWidth) {
      set<switch_config_tags::arsGroupWidth>(*arsGroupWidth);
    } else {
      ref<switch_config_tags::arsGroupWidth>().reset();
    }
  }

  std::optional<int32_t> getArsGroupWidth() const {
    if (auto arsGroupWidth = get<switch_config_tags::arsGroupWidth>()) {
      return arsGroupWidth->cref();
    }
    return std::nullopt;
  }

  FlowletSwitchingConfig* modify(std::shared_ptr<SwitchState>* state);

 private:
  // Inherit the constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
