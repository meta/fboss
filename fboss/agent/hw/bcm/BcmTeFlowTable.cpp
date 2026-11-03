/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmTeFlowTable.h"
#include <fboss/agent/hw/bcm/BcmAclStat.h>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/CounterUtils.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmExactMatchUtils.h"
#include "fboss/agent/hw/bcm/BcmStatUpdater.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/BcmWarmBootCache.h"
#include "fboss/agent/hw/bcm/types.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"

#include <folly/CppAttributes.h>

namespace facebook::fboss {

using namespace facebook::fboss::utility;

void BcmTeFlowTable::processTeFlowConfigChanged(
    const std::shared_ptr<SwitchSettings>& switchSettings) {
  auto dstIpPrefixLength = 0;
  auto exactMatchTableConfigs = switchSettings->getExactMatchTableConfig();
  std::string teFlowTableName(cfg::switch_config_constants::TeFlowTableName());
  for (const auto& tableConfig : *exactMatchTableConfigs) {
    if ((tableConfig->cref<switch_config_tags::name>() == teFlowTableName) &&
        tableConfig->cref<switch_config_tags::dstPrefixLength>().has_value()) {
      dstIpPrefixLength =
          tableConfig->cref<switch_config_tags::dstPrefixLength>()->toThrift();
    }
  }
  const auto warmBootCache = hw_->getWarmBootCache();
  if (warmBootCache->getTeFlowDstIpPrefixLength()) {
    XLOG(DBG3) << "Warm boot -Teflow group already exists";
    // Recover from warmboot cache
    dstIpPrefixLength_ = warmBootCache->getTeFlowDstIpPrefixLength();
    hintId_ = warmBootCache->getTeFlowHintId();
    teFlowGroupId_ = warmBootCache->getTeFlowGroupId();
    CHECK(hintId_);
    CHECK(teFlowGroupId_);
    CHECK_EQ(dstIpPrefixLength_, dstIpPrefixLength);
    // Recover Counter Id from warmboot cache if flex counter supported
    if (hw_->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::INGRESS_FIELD_PROCESSOR_FLEX_COUNTER)) {
      auto flexCounterHandle = warmBootCache->getTeFlowFlexCounterId();
      if (flexCounterHandle) {
        exactMatchFlexCounter_ =
            std::make_unique<BcmIngressFieldProcessorFlexCounter>(
                hw_->getUnit(),
                getEMGroupID(teFlowGroupId_),
                std::optional<BcmTeFlowStatHandle>(flexCounterHandle),
                BcmTeFlowStatType::EM);
      }
    }
    // clear warmboot cache
    warmBootCache->teFlowTableProgrammed();
    return;
  }
  if (dstIpPrefixLength) {
    createTeFlowGroup(dstIpPrefixLength);
  } else if (dstIpPrefixLength_) {
    deleteTeFlowGroup();
  }
}

void BcmTeFlowTable::createTeFlowGroup(int dstIpPrefixLength) {
  if (hw_->getPlatform()->getAsic()->isSupported(
          HwAsic::Feature::EXACT_MATCH)) {
    teFlowGroupId_ = hw_->getPlatform()->getAsic()->getDefaultTeFlowGroupID();
    if (!dstIpPrefixLength_) {
      initEMTable(hw_->getUnit(), teFlowGroupId_, hintId_, dstIpPrefixLength);
    } else {
      reInitEMTable(hw_->getUnit(), teFlowGroupId_, hintId_, dstIpPrefixLength);
    }
    dstIpPrefixLength_ = dstIpPrefixLength;
  } else {
    throw FbossError("Enabling exact match is not supported on this platform");
  }
}

void BcmTeFlowTable::deleteTeFlowGroup() {
  if (!teFlowEntryMap_.empty()) {
    throw FbossError(
        "TeFlow table has entries. Delete of TeFlow group not allowed");
  }
  if (teFlowGroupId_) {
    auto rv =
        bcm_field_group_destroy(hw_->getUnit(), getEMGroupID(teFlowGroupId_));
    bcmLogFatal(rv, hw_, "failed to delete field group id ", teFlowGroupId_);
    teFlowGroupId_ = 0;
  }
  if (hintId_) {
    auto rv = bcm_field_hints_destroy(hw_->getUnit(), hintId_);
    bcmLogFatal(rv, hw_, "failed to delete hint id ", hintId_);
    hintId_ = 0;
  }
  dstIpPrefixLength_ = 0;
}

/*
 * Release all teflow, stat entries.
 * Should only be called when we are about to reset/destroy the teflow table
 */
void BcmTeFlowTable::releaseTeFlows() {
  teFlowEntryMap_.clear();
}

void BcmTeFlowTable::processAddedTeFlow(
    const int groupId,
    const std::shared_ptr<TeFlowEntry>& teFlow) {
  auto id = teFlow->getFlow()->toThrift();
  if (teFlowEntryMap_.find(id) != teFlowEntryMap_.end()) {
    throw FbossError("TeFlow=", teFlow->str(), " already exists");
  }

  std::unique_ptr<BcmTeFlowEntry> bcmTeFlow =
      std::make_unique<BcmTeFlowEntry>(hw_, groupId, teFlow);
  const auto& entry = teFlowEntryMap_.emplace(id, std::move(bcmTeFlow));
  if (!entry.second) {
    throw FbossError("Failed to add an existing TeFlow entry");
  }
}

void BcmTeFlowTable::processRemovedTeFlow(
    const std::shared_ptr<TeFlowEntry>& teFlow) {
  auto id = teFlow->getFlow()->toThrift();
  auto iter = teFlowEntryMap_.find(id);
  if (iter == teFlowEntryMap_.end()) {
    XLOG(DBG3) << "Failed to erase an non-existing TeFlow=", teFlow->str();
    return;
  }
  teFlowEntryMap_.erase(iter);
}

BcmTeFlowEntry* FOLLY_NULLABLE
BcmTeFlowTable::getTeFlowIf(const std::shared_ptr<TeFlowEntry>& teFlow) const {
  auto id = teFlow->getFlow()->toThrift();
  auto iter = teFlowEntryMap_.find(id);
  if (iter == teFlowEntryMap_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

BcmTeFlowEntry* BcmTeFlowTable::getTeFlow(
    const std::shared_ptr<TeFlowEntry>& teFlow) const {
  auto teFlowEntry = getTeFlowIf(teFlow);
  if (!teFlowEntry) {
    throw FbossError("BCM TeFlow does not exist");
  }
  return teFlowEntry;
}

void BcmTeFlowTable::processChangedTeFlow(
    const std::shared_ptr<TeFlowEntry>& oldTeFlow,
    const std::shared_ptr<TeFlowEntry>& newTeFlow) {
  getTeFlow(oldTeFlow)->updateTeFlowEntry(newTeFlow);
}

BcmTeFlowTable::~BcmTeFlowTable() {
  deleteTeFlowGroup();
}

BcmTeFlowStat* BcmTeFlowTable::incRefOrCreateBcmTeFlowStat(
    const std::string& counterName,
    int gid) {
  auto teFlowStatItr = teFlowStatMap_.find(counterName);
  if (teFlowStatItr == teFlowStatMap_.end()) {
    std::unique_ptr<BcmTeFlowStat> newStat;
    auto counterTypes = {cfg::CounterType::BYTES};
    uint32_t actionIndex{BcmAclStat::kDefaultAclActionIndex};
    if (hw_->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::INGRESS_FIELD_PROCESSOR_FLEX_COUNTER)) {
      if (!exactMatchFlexCounter_) {
        exactMatchFlexCounter_ =
            std::make_unique<BcmIngressFieldProcessorFlexCounter>(
                hw_->getUnit(),
                getEMGroupID(gid),
                std::nullopt,
                BcmTeFlowStatType::EM);
      }
      actionIndex = allocateActionIndex();
      newStat = std::make_unique<BcmTeFlowStat>(
          hw_,
          BcmAclStatHandle(exactMatchFlexCounter_->getID()),
          BcmTeFlowStatType::EM,
          actionIndex);
    } else {
      newStat = std::make_unique<BcmTeFlowStat>(
          hw_, getEMGroupID(gid), counterTypes, BcmTeFlowStatType::EM);
    }
    auto stat = newStat.get();
    teFlowStatMap_.emplace(counterName, std::make_pair(std::move(newStat), 1));
    statsNames_.wlock()->insert(counterName);
    hw_->getStatUpdater()->toBeAddedTeFlowStat(
        stat->getHandle(), counterName, counterTypes, actionIndex);
    return stat;
  } else {
    teFlowStatItr->second.second++;
    return teFlowStatItr->second.first.get();
  }
}

BcmTeFlowStat* BcmTeFlowTable::incRefOrCreateBcmTeFlowStat(
    const std::string& counterName,
    BcmTeFlowStatHandle statHandle,
    BcmAclStatActionIndex actionIndex) {
  auto teFlowStatItr = teFlowStatMap_.find(counterName);
  if (teFlowStatItr == teFlowStatMap_.end()) {
    auto counterTypes = {cfg::CounterType::BYTES};
    std::unique_ptr<BcmTeFlowStat> newStat;
    if (hw_->getPlatform()->getAsic()->isSupported(
            HwAsic::Feature::INGRESS_FIELD_PROCESSOR_FLEX_COUNTER)) {
      // Recover flex counter
      setActionIndex(actionIndex);
      newStat = std::make_unique<BcmTeFlowStat>(
          hw_, statHandle, BcmTeFlowStatType::EM, actionIndex);
    } else {
      newStat = std::make_unique<BcmTeFlowStat>(
          hw_, statHandle, BcmTeFlowStatType::EM);
    }
    auto stat = newStat.get();
    teFlowStatMap_.emplace(counterName, std::make_pair(std::move(newStat), 1));
    statsNames_.wlock()->insert(counterName);
    hw_->getStatUpdater()->toBeAddedTeFlowStat(
        stat->getHandle(), counterName, counterTypes, actionIndex);
    return stat;
  } else {
    CHECK(statHandle == teFlowStatItr->second.first->getHandle());
    // check the action index
    auto bcmTeFlowStat = teFlowStatItr->second.first.get();
    if (bcmTeFlowStat->hasFlexCounter()) {
      CHECK_EQ(actionIndex, bcmTeFlowStat->getActionIndex());
    }
    teFlowStatItr->second.second++;
    return bcmTeFlowStat;
  }
}

void BcmTeFlowTable::derefBcmTeFlowStat(const std::string& counterName) {
  auto teFlowStatItr = teFlowStatMap_.find(counterName);
  if (teFlowStatItr == teFlowStatMap_.end()) {
    throw FbossError(
        "Tried to delete a non-existent TE flow stat: ", counterName);
  }
  auto bcmTeFlowStat = teFlowStatItr->second.first.get();
  auto& teFlowStatRefCnt = teFlowStatItr->second.second;
  teFlowStatRefCnt--;
  if (!teFlowStatRefCnt) {
    auto actionIndex = bcmTeFlowStat->getActionIndex();
    if (bcmTeFlowStat->hasFlexCounter()) {
      clearActionIndex(actionIndex);
    }
    utility::deleteCounter(
        utility::statNameFromCounterType(counterName, cfg::CounterType::BYTES));
    hw_->getStatUpdater()->toBeRemovedTeFlowStat(
        bcmTeFlowStat->getHandle(), actionIndex);
    statsNames_.wlock()->erase(counterName);
    teFlowStatMap_.erase(teFlowStatItr);
  }
}

BcmTeFlowStat* FOLLY_NULLABLE
BcmTeFlowTable::getTeFlowStatIf(const std::string& name) const {
  auto iter = teFlowStatMap_.find(name);
  if (iter == teFlowStatMap_.end()) {
    return nullptr;
  } else {
    return iter->second.first.get();
  }
}

BcmTeFlowStat* BcmTeFlowTable::getTeFlowStat(const std::string& stat) const {
  auto bcmStat = getTeFlowStatIf(stat);
  if (!bcmStat) {
    throw FbossError("Stat: ", stat, " does not exist");
  }
  return bcmStat;
}

uint32_t BcmTeFlowTable::allocateActionIndex() {
  for (auto i = 0; i < BcmAclStat::kMaxExactMatchStatEntries; i++) {
    if (!actionIndexMap_.test(i)) {
      actionIndexMap_.set(i);
      return i;
    }
  }
  // throw Bcm full error so that overflow error handling kicks in
  throw BcmError(BCM_E_FULL, "Route Flexcounter offsets exhausted");
}

void BcmTeFlowTable::clearActionIndex(uint32_t offset) {
  DCHECK_EQ(actionIndexMap_.test(offset), true);
  actionIndexMap_.reset(offset);
}

void BcmTeFlowTable::setActionIndex(uint32_t offset) {
  DCHECK_EQ(actionIndexMap_.test(offset), false);
  actionIndexMap_.set(offset);
}

TeFlowStats BcmTeFlowTable::getFlowStats() const {
  TeFlowStats flowStats;
  std::map<std::string, HwTeFlowStats> hwTeFlowStats;
  auto statMap = facebook::fb303::fbData->getStatMap();
  auto statNames = statsNames_.rlock();
  for (const auto& counterId : *statNames) {
    auto statName = folly::to<std::string>(counterId, ".bytes");
    // returns default stat if statName does not exists
    auto statPtr = statMap->getStatPtrNoExport(statName);
    auto lockedStatPtr = statPtr->wlock();
    lockedStatPtr->update(
        std::chrono::seconds(facebook::fb303::get_legacy_stats_time()));
    auto numLevels = lockedStatPtr->numLevels();
    // Cumulative (ALLTIME) counters are at (numLevels - 1)
    HwTeFlowStats flowStat;
    flowStat.bytes() = lockedStatPtr->sum(numLevels - 1);
    hwTeFlowStats.emplace(counterId, std::move(flowStat));
  }
  return flowStats;
}

} // namespace facebook::fboss
