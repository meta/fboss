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

extern "C" {
#include <bcm/field.h>
}

#include "fboss/agent/Utils.h"
#include "fboss/agent/hw/bcm/BcmAclStat.h"
#include "fboss/agent/hw/bcm/BcmIngressFieldProcessorFlexCounter.h"
#include "fboss/agent/hw/bcm/BcmTeFlowEntry.h"
#include "fboss/agent/hw/bcm/types.h"
#include "fboss/agent/state/SwitchSettings.h"

namespace facebook::fboss {

using BcmTeFlowStat = BcmAclStat;
using BcmTeFlowStatHandle = BcmAclStatHandle;
using BcmTeFlowStatType = BcmAclStatType;

class BcmSwitch;

/**
 * A class to keep state related to TeFlow entries in BcmSwitch
 */
class BcmTeFlowTable {
 public:
  explicit BcmTeFlowTable(BcmSwitch* hw) : hw_(hw) {}
  ~BcmTeFlowTable();
  BcmTeFlowTable(const BcmTeFlowTable&) = delete;
  BcmTeFlowTable& operator=(const BcmTeFlowTable&) = delete;
  BcmTeFlowTable(BcmTeFlowTable&&) = delete;
  BcmTeFlowTable& operator=(BcmTeFlowTable&&) = delete;
  void processTeFlowConfigChanged(
      const std::shared_ptr<SwitchSettings>& switchSettings);
  void createTeFlowGroup(int dstIpPrefixLength);
  void deleteTeFlowGroup();
  int getDstIpPrefixLength() const {
    return dstIpPrefixLength_;
  }
  bcm_field_hintid_t getHintId() const {
    return hintId_;
  }
  int getTeFlowGroupId() const {
    return teFlowGroupId_;
  }
  int getTeFlowFlexCounterId() const {
    if (exactMatchFlexCounter_) {
      return exactMatchFlexCounter_->getID();
    }
    return 0;
  }
  BcmTeFlowStat* incRefOrCreateBcmTeFlowStat(
      const std::string& counterName,
      int gid);
  BcmTeFlowStat* incRefOrCreateBcmTeFlowStat(
      const std::string& counterName,
      BcmTeFlowStatHandle statHandle,
      BcmAclStatActionIndex actionIndex);
  void derefBcmTeFlowStat(const std::string& name);

  void processAddedTeFlow(
      const int groupId,
      const std::shared_ptr<TeFlowEntry>& teFlow);
  void processRemovedTeFlow(const std::shared_ptr<TeFlowEntry>& teFlow);
  void releaseTeFlows();

  // Throw exception if not found
  BcmTeFlowEntry* getTeFlow(const std::shared_ptr<TeFlowEntry>& teFlow) const;
  // return nullptr if not found
  BcmTeFlowEntry* getTeFlowIf(const std::shared_ptr<TeFlowEntry>& teFlow) const;
  void processChangedTeFlow(
      const std::shared_ptr<TeFlowEntry>& oldTeFlow,
      const std::shared_ptr<TeFlowEntry>& newTeFlow);

  // Throw exception if not found
  BcmTeFlowStat* getTeFlowStat(const std::string& name) const;
  // return nullptr if not found
  BcmTeFlowStat* getTeFlowStatIf(const std::string& name) const;

  TeFlowStats getFlowStats() const;

 private:
  using BcmTeFlowEntryMap = std::map<TeFlow, std::unique_ptr<BcmTeFlowEntry>>;
  using BcmTeFlowStatMap = std::unordered_map<
      std::string,
      std::pair<std::unique_ptr<BcmTeFlowStat>, uint32_t>>;

  uint32_t allocateActionIndex();
  void clearActionIndex(uint32_t offset);
  void setActionIndex(uint32_t offset);

  BcmSwitch* hw_;
  bcm_field_hintid_t hintId_{0};
  int teFlowGroupId_{0};
  int dstIpPrefixLength_{0};
  BcmTeFlowEntryMap teFlowEntryMap_;
  BcmTeFlowStatMap teFlowStatMap_;
  std::unique_ptr<BcmIngressFieldProcessorFlexCounter> exactMatchFlexCounter_{
      nullptr};
  std::bitset<BcmAclStat::kMaxExactMatchStatEntries> actionIndexMap_;
  folly::Synchronized<std::unordered_set<std::string>> statsNames_;
};

} // namespace facebook::fboss
