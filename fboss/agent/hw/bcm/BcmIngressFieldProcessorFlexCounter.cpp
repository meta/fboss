/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmIngressFieldProcessorFlexCounter.h"

#include <fboss/agent/hw/bcm/BcmAclStat.h>
#include "fboss/agent/hw/bcm/BcmError.h"

extern "C" {
#include <bcm/field.h>
#include <bcm/flexctr.h>
}

using namespace facebook::fboss;

namespace {
struct BcmGetNumAclStatsCountUserData {
  int count{0};
  int groupID{0};
  std::set<uint32> statCounterIDs;
};

// Broadcom provided some reference code in CS00011186214 to implement the
// details of IFP. Most of the constants are following their examples.
constexpr auto kIFPActionIndexNum = 2;
// ACL total allocated counter indexes are 256.
// Hence actionIndex mask is 8 bits
constexpr auto kIFPActionIndexObj0Mask = 8;
constexpr auto kIFPActionIndexObj1Mask = 1;
constexpr auto kIFPActionPacketOperationObj0Mask = 16;
constexpr auto kIFPActionPacketOperationObj1Mask = 1;
constexpr auto kNumDefaultIFPFlexCounterPerAcl = 1;

void setIFPActionConfig(
    bcm_flexctr_action_t* action,
    int groupID,
    facebook::fboss::BcmAclStatType type) {
  action->flags = 0;
  /* Group ID passed as hint and IFP as source */
  action->hint = groupID;
  action->hint_type = bcmFlexctrHintFieldGroupId;
  action->mode = bcmFlexctrCounterModeNormal;
  action->drop_count_mode = bcmFlexctrDropCountModeAll;
  if (type == facebook::fboss::BcmAclStatType::IFP) {
    action->index_num = kIFPActionIndexNum;
    action->source = bcmFlexctrSourceIfp;
  } else {
    action->index_num = facebook::fboss::BcmAclStat::kMaxExactMatchStatEntries;
    action->source = bcmFlexctrSourceExactMatch;
  }
}

void setIFPActionIndex(
    bcm_flexctr_action_t* action,
    facebook::fboss::BcmAclStatType type) {
  bcm_flexctr_index_operation_t* index = &(action->index_operation);
  /* Counter index is PKT_ATTR_OBJ0. */
  if (type == facebook::fboss::BcmAclStatType::IFP) {
    index->object[0] = bcmFlexctrObjectStaticIngFieldStageIngress;
    index->mask_size[0] = kIFPActionIndexObj0Mask;
  } else {
    index->object[0] = bcmFlexctrObjectStaticIngExactMatch;
    index->mask_size[0] = kEMActionIndexObj0Mask;
  }
  index->object_id[0] = 0;
  index->shift[0] = 0;
  index->object[1] = bcmFlexctrObjectConstZero;
  index->mask_size[1] = kIFPActionIndexObj1Mask;
  index->shift[1] = 0;
}

using CounterType = facebook::fboss::cfg::CounterType;
void setActionPerPacketValueOperation(
    bcm_flexctr_value_operation_t* operation) {
  operation->select = bcmFlexctrValueSelectCounterValue;
  operation->object[0] = bcmFlexctrObjectConstOne;
  operation->mask_size[0] = kIFPActionPacketOperationObj0Mask;
  operation->shift[0] = 0;
  operation->object[1] = bcmFlexctrObjectConstZero;
  operation->mask_size[1] = kIFPActionPacketOperationObj1Mask;
  operation->shift[1] = 0;
  operation->type = bcmFlexctrValueOperationTypeInc;
}

void setActionPerBytesValueOperation(bcm_flexctr_value_operation_t* operation) {
  operation->select = bcmFlexctrValueSelectPacketLength;
  operation->type = bcmFlexctrValueOperationTypeInc;
}

void setActionValue(bcm_flexctr_action_t* action) {
  // PACKET type always use operation_a
  setActionPerPacketValueOperation(&(action->operation_a));
  setActionPerBytesValueOperation(&(action->operation_b));
}

int getNumAclStatsCounterCallback(
    int /* unit */,
    uint32 stat_counter_id,
    bcm_flexctr_action_t* action,
    void* user_data) {
  auto* cbUserData = static_cast<BcmGetNumAclStatsCountUserData*>(user_data);
  // Check action config has the same group id
  if ((action->source != bcmFlexctrSourceIfp &&
       action->source != bcmFlexctrSourceExactMatch) ||
      action->hint_type != bcmFlexctrHintFieldGroupId ||
      action->hint != cbUserData->groupID) {
    return 0;
  }
  cbUserData->count++;
  cbUserData->statCounterIDs.insert(stat_counter_id);
  return 0;
}

} // namespace

namespace facebook::fboss {
BcmIngressFieldProcessorFlexCounter::BcmIngressFieldProcessorFlexCounter(
    int unit,
    std::optional<int> groupID,
    std::optional<BcmAclStatHandle> statHandle,
    BcmAclStatType type)
    : BcmFlexCounter(unit), statType_(type) {
  if (!groupID.has_value() && !statHandle.has_value()) {
    throw FbossError(
        "Failed to create BcmIngressFieldProcessorFlexCounter ",
        "with empty groupID and statHandle");
  }
  if (statHandle.has_value()) {
    // as long as statHandle has value, usually means the counter has been
    // created in HW, like during warmboot, we don't need to create such counter
    // again.
    counterID_ = *statHandle;
  } else {
    // Ideally if bcm_flexctr_action_t is opensource, we can just have most of
    // the common logics below implement in the parent class BcmFlexCounter, so
    // that any of the child class FlexCounter won't need to re-implement this
    // creating bcm_flexctr_action_t logic in their constructors.
    // Right now, we only use flex counter for IFP ACLs, hence keep the logic
    // in the unnamed namespace for now.
    bcm_flexctr_action_t action;
    bcm_flexctr_action_t_init(&action);

    // set counter action config
    setIFPActionConfig(&action, *groupID, statType_);

    // set counter action index
    setIFPActionIndex(&action, statType_);

    // set counter action value
    setActionValue(&action);

    auto rv = bcm_flexctr_action_create(
        unit, 0 /* 0 options */, &action, &counterID_);
    bcmCheckError(
        rv,
        "Failed to create Ingress Field Processor Flex Counter for group:",
        *groupID);
    XLOG(DBG1) << "Successfully created Ingress Field Processor Flex Counter:"
               << counterID_ << " for group:" << *groupID;
  }
}

void BcmIngressFieldProcessorFlexCounter::attach(
    BcmAclEntryHandle acl,
    BcmAclStatActionIndex actionIndex) {
  bcm_field_flexctr_config_t flexCounter;
  flexCounter.flexctr_action_id = counterID_;
  flexCounter.counter_index = actionIndex;
  flexCounter.color = BCM_FIELD_COUNT_ALL;
  auto rv = bcm_field_entry_flexctr_attach(unit_, acl, &flexCounter);
  bcmCheckError(
      rv, "Failed to attach flex counter:", counterID_, " to acl:", acl);
}

void BcmIngressFieldProcessorFlexCounter::detach(
    int unit,
    BcmAclEntryHandle acl,
    BcmAclStatHandle aclStatHandle,
    BcmAclStatActionIndex actionIndex) {
  bcm_field_flexctr_config_t flexCounter;
  flexCounter.flexctr_action_id = aclStatHandle;
  flexCounter.counter_index = actionIndex;
  flexCounter.color = BCM_FIELD_COUNT_ALL;
  auto rv = bcm_field_entry_flexctr_detach(unit, acl, &flexCounter);
  bcmCheckError(
      rv, "Failed to detach flex counter:", aclStatHandle, " from acl:", acl);
}

std::set<cfg::CounterType>
BcmIngressFieldProcessorFlexCounter::getCounterTypeList(
    int unit,
    uint32_t counterID) {
  std::set<cfg::CounterType> types;
  bcm_flexctr_action_t action;
  bcm_flexctr_action_t_init(&action);
  auto rv = bcm_flexctr_action_get(unit, counterID, &action);
  bcmCheckError(rv, "Failed to get FlexCounter action for id:", counterID);
  // Check the index object needs to be IFP
  if (action.index_operation.object[0] !=
          bcmFlexctrObjectStaticIngFieldStageIngress &&
      action.index_operation.object[0] != bcmFlexctrObjectStaticIngExactMatch) {
    throw FbossError("FlexCounter id:", counterID, " is not a IFP/EM counter");
  }
  // Flex counter always have two operations
  for (auto* operation : {&action.operation_a, &action.operation_b}) {
    switch (operation->select) {
      case bcmFlexctrValueSelectCounterValue:
        types.insert(cfg::CounterType::PACKETS);
        break;
      case bcmFlexctrValueSelectPacketLength:
        types.insert(cfg::CounterType::BYTES);
        break;
      default:
        throw FbossError(
            "FlexCounter id:",
            counterID,
            " has unrecognized counter type:",
            operation->select);
    }
  }
  return types;
}

int BcmIngressFieldProcessorFlexCounter::getNumAclStatsInFpGroup(
    int unit,
    int gid) {
  BcmGetNumAclStatsCountUserData userData;
  userData.groupID = gid;
  auto rv = bcm_flexctr_action_traverse(
      unit, &getNumAclStatsCounterCallback, static_cast<void*>(&userData));
  bcmCheckError(rv, "Failed to traverse flex counters for IFP id=", gid);
  return userData.count;
}

void BcmIngressFieldProcessorFlexCounter::removeAllCountersInFpGroup(
    [[maybe_unused]] int unit,
    [[maybe_unused]] int gid) {
  int entryCount = 0;
  auto rv = bcm_field_entry_multi_get(unit, gid, 0, nullptr, &entryCount);
  // First find all the acl entry has flex counter attached
  if (rv == BCM_E_NONE && entryCount > 0) {
    std::vector<bcm_field_entry_t> bcmEntries(entryCount);
    rv = bcm_field_entry_multi_get(
        unit, gid, entryCount, bcmEntries.data(), &entryCount);
    bcmCheckError(rv, "Unable to get field entry information for group=", gid);
    for (auto bcmEntry : bcmEntries) {
      if (auto counterID = BcmIngressFieldProcessorFlexCounter::
              getFlexCounterIDFromAttachedAcl(unit, gid, bcmEntry)) {
        // detach the counter from acl
        BcmIngressFieldProcessorFlexCounter::detach(
            unit,
            bcmEntry,
            BcmAclStatHandle((*counterID).first),
            (*counterID).second);
      }
    }
  } else if (rv == BCM_E_NOT_FOUND || entryCount == 0) {
    // no-op
  } else {
    bcmCheckError(rv, "Unable to get count of field entry for group: ", gid);
  }

  // Make sure to remove detached IFP FlexCounter
  BcmGetNumAclStatsCountUserData userData;
  userData.groupID = gid;
  rv = bcm_flexctr_action_traverse(
      unit, &getNumAclStatsCounterCallback, static_cast<void*>(&userData));
  bcmCheckError(rv, "Failed to traverse flex counters for IFP id=", gid);
  for (auto counterID : userData.statCounterIDs) {
    BcmFlexCounter::destroy(unit, counterID);
  }
}

std::optional<std::pair<uint32_t, uint32_t>>
BcmIngressFieldProcessorFlexCounter::getFlexCounterIDFromAttachedAcl(
    int unit,
    int groupID,
    BcmAclEntryHandle acl) {
  bcm_field_entry_config_t aclEntry;
  bcm_field_entry_config_t_init(&aclEntry);
  aclEntry.entry_id = acl;
  aclEntry.group_id = groupID;
  auto rv = bcm_field_entry_config_get(unit, &aclEntry);
  bcmCheckError(
      rv,
      "Failed to get acl entry config for group id=",
      groupID,
      ", acl id=",
      acl);

  return aclEntry.flex_counter_action_id <= 0
      ? std::nullopt
      : std::optional<std::pair<uint32_t, uint32_t>>{
            {aclEntry.flex_counter_action_id, aclEntry.counter_id}};
}

BcmTrafficCounterStats
BcmIngressFieldProcessorFlexCounter::getAclTrafficFlexCounterStats(
    [[maybe_unused]] int unit,
    [[maybe_unused]] BcmAclStatHandle handle,
    [[maybe_unused]] BcmAclStatActionIndex actionIndex,
    [[maybe_unused]] const std::vector<cfg::CounterType>& counters) {
  BcmTrafficCounterStats stats;
  bcm_flexctr_counter_value_t values;
  memset(&values, 0, sizeof(values));
  // bcm_flexctr_stat_get needs to pass in a pointer for index
  uint32_t index = actionIndex;
  // Each acl has one flex counter entry and index is 1
  auto rv = bcm_flexctr_stat_get(
      unit, handle, kNumDefaultIFPFlexCounterPerAcl, &index, &values);
  bcmCheckError(rv, "Failed to get IFP FlexCounter stats for id:", handle);
  for (auto counterType : counters) {
    // CounterType::PACKETS is always operation_a
    switch (counterType) {
      case cfg::CounterType::PACKETS:
        stats.emplace(cfg::CounterType::PACKETS, values.value[0]);
        break;
      case cfg::CounterType::BYTES:
        stats.emplace(cfg::CounterType::BYTES, values.value[1]);
        break;
    }
  }
  return stats;
}
} // namespace facebook::fboss
