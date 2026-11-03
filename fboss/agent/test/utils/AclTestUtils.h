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

#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/StateUtils.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/lib/config/agent/AclConfigUtils.h"

class SwSwitch;

namespace facebook::fboss::utility {

using AclStatGetFunc = std::function<int64_t(
    std::shared_ptr<SwitchState>,
    const std::string&,
    const std::string&,
    cfg::AclStage,
    const std::optional<std::string>&)>;

std::string kDefaultAclTable();

std::string kTtldAclTable();

std::string kIpv6AclTable();

cfg::AclEntry* addAclEntry(
    cfg::SwitchConfig* cfg,
    const cfg::AclEntry& acl,
    const std::string& tableName,
    cfg::AclStage aclStage = cfg::AclStage::INGRESS);

cfg::AclEntry* addAcl_DEPRECATED(
    cfg::SwitchConfig* cfg,
    const std::string& aclName,
    const cfg::AclActionType& aclActionType = cfg::AclActionType::PERMIT,
    const std::optional<std::string>& tableName = std::nullopt);

cfg::AclEntry* addAcl(
    cfg::SwitchConfig* cfg,
    const cfg::AclEntry& acl,
    cfg::AclStage aclStage);

void addEtherTypeToAcl(
    const HwAsic* asic,
    cfg::AclEntry* acl,
    const cfg::EtherType& etherType = cfg::EtherType::IPv6);

void addUdfTableToAcl(
    cfg::AclEntry* acl,
    const std::string& udfGroups,
    const std::vector<int8_t>& roceBytes,
    const std::vector<int8_t>& roceMask);

int getAclTableIndex(
    cfg::AclTableGroup* aclTableGroup,
    const std::string& tableName);

std::shared_ptr<AclEntry> getAclEntryByName(
    const std::shared_ptr<SwitchState> state,
    cfg::AclStage aclStage,
    const std::string& tableName,
    const std::string& aclName);

std::optional<cfg::TrafficCounter> getAclTrafficCounter(
    const std::shared_ptr<SwitchState> state,
    const std::string& aclName);

std::string kDefaultAclTableGroupName();

std::vector<cfg::AclEntry>& getAcls(
    cfg::SwitchConfig* cfg,
    const std::optional<std::string>& tableName);

void delAcl(
    cfg::SwitchConfig* cfg,
    const std::string& aclName,
    const std::optional<std::string>& tableName = std::nullopt);

void delLastAddedAcl(cfg::SwitchConfig* cfg);

void addAclTableGroup(
    cfg::SwitchConfig* cfg,
    cfg::AclStage aclStage,
    const std::string& aclTableGroupName = "AclTableGroup1");

void addDefaultAclTable(
    cfg::SwitchConfig& cfg,
    const std::vector<std::string>& udfGroups = {});

cfg::AclTable* addAclTable(
    cfg::SwitchConfig* cfg,
    const std::string& aclTableName,
    const int aclTablePriority,
    const std::vector<cfg::AclTableActionType>& actionTypes,
    const std::vector<cfg::AclTableQualifier>& qualifiers,
    const std::vector<std::string>& udfGroups = {});

cfg::AclTable* addAclTable(
    cfg::SwitchConfig* cfg,
    cfg::AclStage aclStage,
    const std::string& aclTableName,
    const int aclTablePriority,
    const std::vector<cfg::AclTableActionType>& actionTypes,
    const std::vector<cfg::AclTableQualifier>& qualifiers,
    const std::vector<std::string>& udfGroups = {});

cfg::AclTable* addTtldAclTable(
    cfg::SwitchConfig* cfg,
    cfg::AclStage aclStage,
    const int aclTablePriority);

cfg::AclTable* getAclTable(
    cfg::SwitchConfig& cfg,
    cfg::AclStage aclStage,
    const std::string& aclTableName);

void delAclTable(cfg::SwitchConfig* cfg, const std::string& aclTableName);

void addTrafficCounter(
    cfg::SwitchConfig* config,
    const std::string& counterName,
    const std::optional<std::vector<cfg::CounterType>>& counterTypes =
        std::nullopt);

void addAclStat(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& counterName,
    std::vector<cfg::CounterType> counterTypes = {});

void delAclStat(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& counterName);

void addMatcher(
    cfg::SwitchConfig* config,
    const std::string& matcherName,
    const cfg::MatchAction& matchAction);
void delMatcher(cfg::SwitchConfig* config, const std::string& matcherName);

void addAclMirrorAction(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& counterName,
    const std::string& mirrorName,
    bool ingress = true);

void addAclDscpQueueAction(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& counterName,
    int32_t dscpValue = 0,
    int queueId = -1);

void addAclEcmpHashCancelAction(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& counterName);

void renameAclStat(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& oldCounterName,
    const std::string& newCounterName);

std::vector<cfg::CounterType> getAclCounterTypes(
    const std::vector<const HwAsic*>& asics);

uint64_t getAclInOutPackets(
    const SwSwitch* sw,
    const std::string& statName,
    bool bytes = false);

std::map<std::string, uint64_t> getAclInOutPacketsMap(
    const SwSwitch* sw,
    const std::vector<std::string>& statNames,
    bool bytes = false);

uint64_t getAclInOutPackets(
    const HwSwitch* hw,
    const std::string& statName,
    bool bytes = false);

template <typename SwitchT>
AclStatGetFunc getAclStatGetFn(SwitchT* sw) {
  return [sw](
             std::shared_ptr<SwitchState> /*state*/,
             const std::string& /*aclName*/,
             const std::string& statName,
             cfg::AclStage /*aclStage*/,
             const std::optional<std::string>& /*aclTableName*/) {
    return getAclInOutPackets(sw, statName);
  };
}

std::shared_ptr<AclEntry> getAclEntry(
    const std::shared_ptr<SwitchState>& state,
    const std::string& name,
    bool enableAclTableGroup);

cfg::AclTableGroup* FOLLY_NULLABLE getAclTableGroup(cfg::SwitchConfig& config);

cfg::AclTableGroup* FOLLY_NULLABLE
getAclTableGroup(cfg::SwitchConfig& config, cfg::AclStage aclStage);

void setupDefaultAclTableGroups(cfg::SwitchConfig& config);

void setupDefaultPostLookupIngressAclTableGroup(cfg::SwitchConfig& config);

void setupDefaultIngressAclTableGroup(cfg::SwitchConfig& config);

std::set<cfg::AclTableQualifier> getRequiredQualifers(
    const cfg::AclEntry& aclEntry);

bool aclEntrySupported(
    const cfg::AclTable* aclTable,
    const cfg::AclEntry& aclEntry);

std::string getAclTableForAclEntry(
    cfg::SwitchConfig& config,
    const cfg::AclEntry& aclEntry,
    cfg::AclStage stage = cfg::AclStage::INGRESS);
} // namespace facebook::fboss::utility
