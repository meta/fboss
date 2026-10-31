/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/utils/AclTestUtils.h"

#include <memory>

#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/SwSwitch.h"

DECLARE_bool(enable_acl_table_group);

namespace facebook::fboss::utility {

std::string kDefaultAclTable() {
  return cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE();
}

std::string kTtldAclTable() {
  return "ttld-acl-table";
}

std::string kIpv6AclTable() {
  return "ipv6-acl-table";
}

int getAclTableIndex(
    cfg::AclTableGroup* aclTableGroup,
    const std::string& tableName) {
  int tableIndex;
  std::vector<cfg::AclTable> aclTables = *aclTableGroup->aclTables();
  std::vector<cfg::AclTable>::iterator it = std::find_if(
      aclTables.begin(), aclTables.end(), [&](cfg::AclTable const& aclTable) {
        return *aclTable.name() == tableName;
      });
  if (it != aclTables.end()) {
    tableIndex = std::distance(aclTables.begin(), it);
  } else {
    throw FbossError(
        "Table with name ", tableName, " does not exist in config");
  }
  return tableIndex;
}

cfg::AclEntry* addAclEntry(
    cfg::SwitchConfig* cfg,
    const cfg::AclEntry& acl,
    const std::string& aclTableName,
    cfg::AclStage aclStage) {
  if (FLAGS_enable_acl_table_group) {
    auto aclTableGroup = getAclTableGroup(*cfg, aclStage);

    int tableNumber = getAclTableIndex(aclTableGroup, aclTableName);
    CHECK(aclTableGroup);
    auto& aclTable = aclTableGroup->aclTables()[tableNumber];
    if (!aclEntrySupported(&aclTable, acl)) {
      throw FbossError(
          "acl entry ",
          *acl.name(),
          " is can not be added in acl table ",
          aclTableName);
    }
    aclTable.aclEntries()->push_back(acl);
    return &aclTable.aclEntries()->back();
  } else {
    cfg->acls()->push_back(acl);
    return &cfg->acls()->back();
  }
}

cfg::AclEntry* addAcl_DEPRECATED(
    cfg::SwitchConfig* cfg,
    const std::string& aclName,
    const cfg::AclActionType& aclActionType,
    const std::optional<std::string>& tableName) {
  auto acl = cfg::AclEntry();
  *acl.name() = aclName;
  *acl.actionType() = aclActionType;

  if (!tableName) {
    return addAclEntry(cfg, acl, kDefaultAclTable());
  }
  return addAclEntry(cfg, acl, *tableName);
}

cfg::AclEntry* addAcl(
    cfg::SwitchConfig* cfg,
    const cfg::AclEntry& acl,
    cfg::AclStage aclStage) {
  if (!FLAGS_enable_acl_table_group) {
    if (aclStage != cfg::AclStage::INGRESS) {
      throw FbossError(
          "Only Ingress ACL Stage is supported with single ACL table");
    }
    cfg->acls()->push_back(acl);
    return &cfg->acls()->back();
  }
  auto selectedTableName = getAclTableForAclEntry(*cfg, acl, aclStage);
  return addAclEntry(cfg, acl, selectedTableName, aclStage);
}

void addEtherTypeToAcl(
    const HwAsic* asic,
    cfg::AclEntry* acl,
    const cfg::EtherType& etherType) {
  if (asic->isSupported(HwAsic::Feature::ACL_ENTRY_ETHER_TYPE)) {
    acl->etherType() = etherType;
  }
}

void addUdfTableToAcl(
    cfg::AclEntry* acl,
    const std::string& udfGroup,
    const std::vector<int8_t>& roceBytes,
    const std::vector<int8_t>& roceMask) {
  cfg::AclUdfEntry aclUdfEntry;
  aclUdfEntry.udfGroup() = udfGroup;
  aclUdfEntry.roceBytes() = roceBytes;
  aclUdfEntry.roceMask() = roceMask;
  if (!acl->udfTable()) {
    acl->udfTable() = {};
  }
  acl->udfTable()->push_back(aclUdfEntry);
}

std::optional<cfg::TrafficCounter> getAclTrafficCounter(
    const std::shared_ptr<SwitchState> state,
    const std::string& aclName) {
  auto swAcl = getAclEntryByName(state, aclName);
  if (swAcl && swAcl->getAclAction()) {
    return swAcl->getAclAction()
        ->cref<switch_state_tags::trafficCounter>()
        ->toThrift();
  }
  return std::nullopt;
}

std::string kDefaultAclTableGroupName() {
  return "acl-table-group-ingress";
}

std::vector<cfg::AclEntry>& getAcls(
    cfg::SwitchConfig* cfg,
    const std::optional<std::string>& tableName) {
  auto aclTableName = tableName.has_value()
      ? tableName.value()
      : cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE();
  if (FLAGS_enable_acl_table_group) {
    auto* aclTableGroup = getAclTableGroup(*cfg);
    CHECK(aclTableGroup);
    return *aclTableGroup
                ->aclTables()[getAclTableIndex(aclTableGroup, aclTableName)]
                .aclEntries();
  }
  return *cfg->acls();
}

void delAcl(
    cfg::SwitchConfig* cfg,
    const std::string& aclName,
    const std::optional<std::string>& tableName) {
  auto& acls = getAcls(cfg, tableName);

  acls.erase(
      std::remove_if(
          acls.begin(),
          acls.end(),
          [&](cfg::AclEntry const& acl) { return *acl.name() == aclName; }),
      acls.end());
}

void delLastAddedAcl(cfg::SwitchConfig* cfg) {
  auto& acls = getAcls(cfg, std::nullopt);
  acls.pop_back();
}

void addAclTableGroup(
    cfg::SwitchConfig* cfg,
    cfg::AclStage aclStage,
    const std::string& aclTableGroupName) {
  cfg::AclTableGroup cfgTableGroup;
  cfgTableGroup.name() = aclTableGroupName;
  cfgTableGroup.stage() = aclStage;
  if (auto aclTableGroup = getAclTableGroup(*cfg, aclStage)) {
    *aclTableGroup = cfgTableGroup;
    return;
  }
  if (!cfg->aclTableGroups()) {
    cfg->aclTableGroups() = {};
  }
  cfg->aclTableGroups()->push_back(std::move(cfgTableGroup));
}

void addDefaultAclTable(
    cfg::SwitchConfig& cfg,
    const std::vector<std::string>& udfGroups) {
  std::optional<cfg::SdkVersion> version{};
  if (cfg.sdkVersion()) {
    version = *cfg.sdkVersion();
  }

  HwAsicTable asicTable(
      *cfg.switchSettings()->switchIdToSwitchInfo(), version, *cfg.dsfNodes());
  auto asic =
      checkSameAndGetAsic(asicTable.getL3Asics(), FLAGS_switch_id_for_testing);
  utility::addDefaultAclTable(cfg, *asic, udfGroups);
}

cfg::AclTable* addAclTable(
    cfg::SwitchConfig* cfg,
    const std::string& aclTableName,
    const int aclTablePriority,
    const std::vector<cfg::AclTableActionType>& actionTypes,
    const std::vector<cfg::AclTableQualifier>& qualifiers,
    const std::vector<std::string>& udfGroups) {
  return addAclTable(
      cfg,
      cfg::AclStage::INGRESS,
      aclTableName,
      aclTablePriority,
      actionTypes,
      qualifiers,
      udfGroups);
}

cfg::AclTable* addAclTable(
    cfg::SwitchConfig* cfg,
    cfg::AclStage aclStage,
    const std::string& aclTableName,
    const int aclTablePriority,
    const std::vector<cfg::AclTableActionType>& actionTypes,
    const std::vector<cfg::AclTableQualifier>& qualifiers,
    const std::vector<std::string>& udfGroups) {
  auto aclTableGroup = getAclTableGroup(*cfg, aclStage);
  if (!aclTableGroup) {
    throw FbossError(
        "Attempted to add acl table ",
        aclTableName,
        " without first creating acl table group");
  }

  cfg::AclTable aclTable;
  aclTable.name() = aclTableName;
  aclTable.priority() = aclTablePriority;
  aclTable.actionTypes() = actionTypes;
  aclTable.qualifiers() = qualifiers;
  aclTable.udfGroups() = udfGroups;

  aclTableGroup->aclTables()->push_back(aclTable);
  return &aclTableGroup->aclTables()->back();
}

void delAclTable(cfg::SwitchConfig* cfg, const std::string& aclTableName) {
  auto aclTableGroup = getAclTableGroup(*cfg);
  if (!aclTableGroup) {
    throw FbossError(
        "Attempted to delete acl table (",
        aclTableName,
        ") from uninstantiated table group");
  }
  auto& aclTables = *aclTableGroup->aclTables();
  aclTables.erase(
      std::remove_if(
          aclTables.begin(),
          aclTables.end(),
          [&](cfg::AclTable const& aclTable) {
            return *aclTable.name() == aclTableName;
          }),
      aclTables.end());
}

void addTrafficCounter(
    cfg::SwitchConfig* cfg,
    const std::string& counterName,
    const std::optional<std::vector<cfg::CounterType>>& counterTypes) {
  auto counter = cfg::TrafficCounter();
  *counter.name() = counterName;
  if (counterTypes.has_value() && counterTypes.value().size() > 0) {
    *counter.types() = counterTypes.value();
  } else {
    *counter.types() = {cfg::CounterType::PACKETS};
  }
  bool counterExists = false;
  for (auto& c : *cfg->trafficCounters()) {
    if (*c.name() == counterName) {
      counterExists = true;
      break;
    }
  }
  if (!counterExists) {
    cfg->trafficCounters()->push_back(counter);
  }
}

void addAclStat(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& counterName,
    std::vector<cfg::CounterType> counterTypes) {
  addTrafficCounter(cfg, counterName, std::move(counterTypes));

  auto matchAction = cfg::MatchAction();
  matchAction.counter() = counterName;
  auto action = cfg::MatchToAction();
  *action.matcher() = matcher;
  *action.action() = matchAction;

  if (!cfg->dataPlaneTrafficPolicy()) {
    cfg->dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
  }
  cfg->dataPlaneTrafficPolicy()->matchToAction()->push_back(action);
}

void delAclStat(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& counterName) {
  int refCnt = 0;
  if (auto dataPlaneTrafficPolicy = cfg->dataPlaneTrafficPolicy()) {
    auto& matchActions = *dataPlaneTrafficPolicy->matchToAction();
    matchActions.erase(
        std::remove_if(
            matchActions.begin(),
            matchActions.end(),
            [&](cfg::MatchToAction const& matchAction) {
              if (*matchAction.matcher() == matcher &&
                  matchAction.action()->counter().value_or({}) == counterName) {
                ++refCnt;
                return true;
              }
              return false;
            }),
        matchActions.end());
  }
  if (refCnt == 0) {
    auto& counters = *cfg->trafficCounters();
    counters.erase(
        std::remove_if(
            counters.begin(),
            counters.end(),
            [&](cfg::TrafficCounter const& counter) {
              return *counter.name() == counterName;
            }),
        counters.end());
  }
}

void renameAclStat(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& oldCounterName,
    const std::string& newCounterName) {
  delAclStat(cfg, matcher, oldCounterName);
  addAclStat(cfg, matcher, newCounterName);
}

void addMatcher(
    cfg::SwitchConfig* config,
    const std::string& matcherName,
    const cfg::MatchAction& matchAction) {
  cfg::MatchToAction action = cfg::MatchToAction();
  *action.matcher() = matcherName;
  *action.action() = matchAction;
  cfg::TrafficPolicyConfig egressTrafficPolicy;
  if (auto dataPlaneTrafficPolicy = config->dataPlaneTrafficPolicy()) {
    egressTrafficPolicy = *dataPlaneTrafficPolicy;
  }
  auto curNumMatchActions = egressTrafficPolicy.matchToAction()->size();
  egressTrafficPolicy.matchToAction()->resize(curNumMatchActions + 1);
  egressTrafficPolicy.matchToAction()[curNumMatchActions] = action;
  config->dataPlaneTrafficPolicy() = egressTrafficPolicy;
}

void delMatcher(cfg::SwitchConfig* config, const std::string& matcherName) {
  if (auto dataPlaneTrafficPolicy = config->dataPlaneTrafficPolicy()) {
    auto& matchActions = *dataPlaneTrafficPolicy->matchToAction();
    matchActions.erase(
        std::remove_if(
            matchActions.begin(),
            matchActions.end(),
            [&](cfg::MatchToAction const& matchAction) {
              if (*matchAction.matcher() == matcherName) {
                return true;
              }
              return false;
            }),
        matchActions.end());
  }
}

void addAclMirrorAction(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& counterName,
    const std::string& mirrorName,
    bool ingress) {
  cfg::MatchAction matchAction = cfg::MatchAction();
  if (ingress) {
    matchAction.ingressMirror() = mirrorName;
  } else {
    matchAction.egressMirror() = mirrorName;
  }

  matchAction.counter() = counterName;
  std::vector<cfg::CounterType> setCounterTypes{
      cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
  utility::addTrafficCounter(cfg, counterName, std::move(setCounterTypes));
  utility::addMatcher(cfg, matcher, matchAction);
}

void addAclDscpQueueAction(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& counterName,
    int32_t dscpValue,
    int queueId) {
  cfg::MatchAction matchAction = cfg::MatchAction();
  if (dscpValue) {
    cfg::SetDscpMatchAction setDscpMatchAction;
    *setDscpMatchAction.dscpValue() = dscpValue;
    matchAction.setDscp() = std::move(setDscpMatchAction);
  }
  if (queueId >= 0) {
    cfg::QueueMatchAction queueAction;
    queueAction.queueId() = queueId;
    matchAction.sendToQueue() = queueAction;
  }

  matchAction.counter() = counterName;
  std::vector<cfg::CounterType> setCounterTypes{
      cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
  utility::addTrafficCounter(cfg, counterName, std::move(setCounterTypes));
  utility::addMatcher(cfg, matcher, matchAction);
}

void addAclEcmpHashCancelAction(
    cfg::SwitchConfig* cfg,
    const std::string& matcher,
    const std::string& counterName) {
  cfg::MatchAction matchAction = cfg::MatchAction();
  auto asicType = checkSameAndGetAsicType(*cfg);
  // Chenab doesn't require/support hash action, just use permit
  if (asicType != cfg::AsicType::ASIC_TYPE_CHENAB &&
      asicType != cfg::AsicType::ASIC_TYPE_CHENAB2) {
    matchAction.ecmpHashAction() = cfg::SetEcmpHashAction();
    matchAction.ecmpHashAction()->switchingMode() =
        cfg::SwitchingMode::FIXED_ASSIGNMENT;
  }

  matchAction.counter() = counterName;
  std::vector<cfg::CounterType> counterTypes{
      cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
  utility::addTrafficCounter(cfg, counterName, std::move(counterTypes));
  utility::addMatcher(cfg, matcher, matchAction);
}

std::vector<cfg::CounterType> getAclCounterTypes(
    const std::vector<const HwAsic*>& asics) {
  auto asic = checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing);
  // At times, it is non-trivial for SAI implementations to support enabling
  // bytes counters only or packet counters only. In such cases, SAI
  // implementations enable bytes as well as packet counters even if only
  // one of the two is enabled. FBOSS use case does not require enabling
  // only one, but always enables both packets and bytes counters. Thus,
  // enable both in the test. Reference: CS00012271364
  if (!asic->isSupported(HwAsic::Feature::ACL_BYTE_COUNTER) ||
      asic->isSupported(
          HwAsic::Feature::SEPARATE_BYTE_AND_PACKET_ACL_COUNTER)) {
    return {cfg::CounterType::PACKETS};
  }
  return {cfg::CounterType::BYTES, cfg::CounterType::PACKETS};
}

std::map<std::string, uint64_t> getAclInOutPacketsMap(
    const SwSwitch* sw,
    const std::vector<std::string>& statNames,
    bool bytes) {
  // getHwSwitchStatsExpensive() copies every switch's stats, so a caller
  // reading many counters must fetch once rather than once per counter.
  auto hwSwitchStatsMap = sw->getHwSwitchStatsExpensive();
  std::map<std::string, uint64_t> statValues;
  for (const auto& statName : statNames) {
    statValues.emplace(statName, 0);
  }
  const std::string suffix = bytes ? ".bytes" : ".packets";
  for (const auto& [switchIndex, hwswitchStats] : hwSwitchStatsMap) {
    const auto& nameToCounter =
        *hwswitchStats.aclStats()->statNameToCounterMap();
    for (auto& [statName, statValue] : statValues) {
      auto entry = nameToCounter.find(statName + suffix);
      if (entry != nameToCounter.end()) {
        statValue += entry->second;
      }
    }
  }
  return statValues;
}

uint64_t getAclInOutPackets(
    const SwSwitch* sw,
    const std::string& statName,
    bool bytes) {
  return getAclInOutPacketsMap(sw, {statName}, bytes).at(statName);
}

uint64_t getAclInOutPackets(
    const HwSwitch* hw,
    const std::string& statName,
    bool bytes) {
  auto statStr = bytes ? statName + ".bytes" : statName + ".packets";
  int64_t statValue = 0;
  const_cast<HwSwitch*>(hw)->updateStats();
  auto aclStats = hw->getAclStats();
  auto entry = aclStats.statNameToCounterMap()->find(statStr);
  if (entry != aclStats.statNameToCounterMap()->end()) {
    statValue += entry->second;
  }
  return statValue;
}

std::shared_ptr<AclEntry> getAclEntryByName(
    const std::shared_ptr<SwitchState> state,
    cfg::AclStage aclStage,
    const std::string& tableName,
    const std::string& aclName) {
  return state->getAclsForTable(aclStage, tableName)->getNodeIf(aclName);
}

std::shared_ptr<AclEntry> getAclEntry(
    const std::shared_ptr<SwitchState>& state,
    const std::string& name,
    bool enableAclTableGroup) {
  if (enableAclTableGroup) {
    auto defaultAcls = state->getAclsForTable(
        cfg::AclStage::INGRESS,
        cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
    auto entry = defaultAcls ? defaultAcls->getNodeIf(name) : nullptr;
    // acl entries are expected to have unique name across all groups
    if (!entry) {
      if (auto aclTableGroups = state->getAclTableGroups()) {
        for (const auto& groupMap : std::as_const(*aclTableGroups)) {
          for (const auto& [stage, group] : std::as_const(*groupMap.second)) {
            for (const auto& [tableName, table] :
                 std::as_const(*group->getAclTableMap())) {
              std::ignore = tableName;
              entry = table->getAclMap()->getNodeIf(name);
              if (entry) {
                return entry;
              }
            }
          }
        }
      }
    }
    return entry;
  }
  return state->getAcl(name);
}

cfg::AclTableGroup* FOLLY_NULLABLE getAclTableGroup(cfg::SwitchConfig& config) {
  return getAclTableGroup(config, cfg::AclStage::INGRESS);
}

cfg::AclTableGroup* FOLLY_NULLABLE
getAclTableGroup(cfg::SwitchConfig& config, cfg::AclStage aclStage) {
  if (!config.aclTableGroups()) {
    return nullptr;
  }
  for (auto iter = config.aclTableGroups()->begin();
       iter != config.aclTableGroups()->end();
       iter++) {
    if (iter->stage().value() == aclStage) {
      return std::addressof(*iter);
    }
  }
  return nullptr;
}

void setupDefaultPostLookupIngressAclTableGroup(cfg::SwitchConfig& config) {
  if (config.switchSettings()->switchIdToSwitchInfo()->empty()) {
    return;
  }
  auto switchId2SwitchInfo = *config.switchSettings()->switchIdToSwitchInfo();
  std::optional<cfg::SdkVersion> version{};
  if (config.sdkVersion()) {
    version = *config.sdkVersion();
  }

  HwAsicTable asicTable(switchId2SwitchInfo, version, *config.dsfNodes());

  if (!asicTable.isFeatureSupportedOnAnyAsic(
          HwAsic::Feature::INGRESS_POST_LOOKUP_ACL_TABLE)) {
    return;
  }

  if (getAclTableGroup(config, cfg::AclStage::INGRESS_POST_LOOKUP)) {
    return;
  }
  utility::addAclTableGroup(
      &config,
      cfg::AclStage::INGRESS_POST_LOOKUP,
      cfg::switch_config_constants::
          DEFAULT_POST_LOOKUP_INGRESS_ACL_TABLE_GROUP());
  utility::addAclTable(
      &config,
      cfg::AclStage::INGRESS_POST_LOOKUP,
      cfg::switch_config_constants::DEFAULT_POST_LOOKUP_INGRESS_ACL_TABLE(),
      0,
      {
          cfg::AclTableActionType::COUNTER,
          cfg::AclTableActionType::PACKET_ACTION,
          cfg::AclTableActionType::SET_USER_DEFINED_TRAP,
          cfg::AclTableActionType::SET_DSCP,
      },
      {
          cfg::AclTableQualifier::ETHER_TYPE,
          cfg::AclTableQualifier::DSCP,
          cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE,
      },
      {});
}

void setupDefaultIngressAclTableGroup(cfg::SwitchConfig& config) {
  if (getAclTableGroup(config, cfg::AclStage::INGRESS)) {
    // default table group already exists
    return;
  }
  utility::addAclTableGroup(
      &config, cfg::AclStage::INGRESS, utility::kDefaultAclTableGroupName());
  utility::addDefaultAclTable(config);
}

void setupDefaultAclTableGroups(cfg::SwitchConfig& config) {
  std::optional<cfg::SdkVersion> version;
  if (config.sdkVersion()) {
    version = *config.sdkVersion();
  }
  HwAsicTable asicTable(
      *config.switchSettings()->switchIdToSwitchInfo(),
      version,
      *config.dsfNodes());
  const auto* asic =
      checkSameAndGetAsic(asicTable.getL3Asics(), FLAGS_switch_id_for_testing);
  utility::setupDefaultAclTableGroups(config, *asic);
}

cfg::AclTable* getAclTable(
    cfg::SwitchConfig& cfg,
    cfg::AclStage aclStage,
    const std::string& aclTableName) {
  auto aclTableGroup = getAclTableGroup(cfg, aclStage);
  if (!aclTableGroup) {
    return nullptr;
  }
  for (auto& aclTable : *aclTableGroup->aclTables()) {
    if (*aclTable.name() == aclTableName) {
      return &aclTable;
    }
  }
  return nullptr;
}

cfg::AclTable* addTtldAclTable(
    cfg::SwitchConfig* cfg,
    cfg::AclStage aclStage,
    const int aclTablePriority) {
  return addAclTable(
      cfg,
      aclStage,
      kTtldAclTable(),
      aclTablePriority,
      {
          // action types
      },
      {
          cfg::AclTableQualifier::ETHER_TYPE,
          cfg::AclTableQualifier::SRC_IPV4,
          cfg::AclTableQualifier::SRC_IPV6,
          cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
          cfg::AclTableQualifier::IP_TYPE,
          cfg::AclTableQualifier::IPV6_NEXT_HEADER,
          cfg::AclTableQualifier::TTL,
      },
      {
          // udf groups
      });
}

std::set<cfg::AclTableQualifier> getRequiredQualifers(
    const cfg::AclEntry& aclEntry) {
  int minValue = static_cast<int>(
      apache::thrift::TEnumTraits<cfg::AclTableQualifier>::min());
  int maxValue = static_cast<int>(
      apache::thrift::TEnumTraits<cfg::AclTableQualifier>::max());
  std::set<cfg::AclTableQualifier> requiredQualifiers{};

  auto addQualifier = [&requiredQualifiers](
                          bool isSet, cfg::AclTableQualifier qualifer) {
    if (isSet) {
      requiredQualifiers.insert(qualifer);
    }
  };

  for (int iter = minValue; iter <= maxValue; ++iter) {
    auto qualifier = static_cast<cfg::AclTableQualifier>(iter);
    switch (static_cast<cfg::AclTableQualifier>(qualifier)) {
      case cfg::AclTableQualifier::DST_IPV4:
        addQualifier(
            aclEntry.dstIp().has_value() &&
                folly::IPAddress::createNetwork(*aclEntry.dstIp()).first.isV4(),
            qualifier);
        break;

      case cfg::AclTableQualifier::DST_IPV6:
        addQualifier(
            aclEntry.dstIp().has_value() &&
                folly::IPAddress::createNetwork(*aclEntry.dstIp()).first.isV6(),
            qualifier);
        break;

      case cfg::AclTableQualifier::DST_IPV6_WORD3:
        addQualifier(aclEntry.dstIpV6Word3().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::DST_IPV6_WORD2:
        addQualifier(aclEntry.dstIpV6Word2().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::SRC_IPV4:
        addQualifier(
            aclEntry.srcIp().has_value() &&
                folly::IPAddress::createNetwork(*aclEntry.srcIp()).first.isV4(),
            qualifier);
        break;

      case cfg::AclTableQualifier::SRC_IPV6:
        addQualifier(
            aclEntry.srcIp().has_value() &&
                folly::IPAddress::createNetwork(*aclEntry.srcIp()).first.isV6(),
            qualifier);
        break;

      case cfg::AclTableQualifier::L4_SRC_PORT:
        addQualifier(aclEntry.l4SrcPort().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::L4_DST_PORT:
        addQualifier(aclEntry.l4DstPort().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::L4_DST_PORT_RANGE:
        addQualifier(aclEntry.l4DstPortRange().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::IP_PROTOCOL_NUMBER:
        if (!aclEntry.etherType().has_value() ||
            *aclEntry.etherType() != cfg::EtherType::IPv6) {
          addQualifier(aclEntry.proto().has_value(), qualifier);
        }
        break;

      case cfg::AclTableQualifier::TCP_FLAGS:
        addQualifier(aclEntry.tcpFlagsBitMap().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::SRC_PORT:
        addQualifier(aclEntry.srcPort().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::OUT_PORT:
        addQualifier(aclEntry.dstPort().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::IP_FRAG:
        addQualifier(aclEntry.ipFrag().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::ICMPV4_TYPE:
        if (aclEntry.proto().has_value() && *aclEntry.proto() == 1) {
          addQualifier(aclEntry.icmpType().has_value(), qualifier);
        }
        break;

      case cfg::AclTableQualifier::ICMPV4_CODE:
        if (aclEntry.proto().has_value() && *aclEntry.proto() == 1) {
          addQualifier(aclEntry.icmpCode().has_value(), qualifier);
        }
        break;

      case cfg::AclTableQualifier::ICMPV6_TYPE:
        if (aclEntry.proto().has_value() && *aclEntry.proto() == 58) {
          addQualifier(aclEntry.icmpType().has_value(), qualifier);
        }
        break;

      case cfg::AclTableQualifier::ICMPV6_CODE:
        if (aclEntry.proto().has_value() && *aclEntry.proto() == 58) {
          addQualifier(aclEntry.icmpCode().has_value(), qualifier);
        }
        break;

      case cfg::AclTableQualifier::DSCP:
        addQualifier(aclEntry.dscp().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::DST_MAC:
        addQualifier(aclEntry.dstMac().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::IP_TYPE:
        addQualifier(aclEntry.ipType().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::TTL:
        addQualifier(aclEntry.ttl().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::LOOKUP_CLASS_L2:
        addQualifier(aclEntry.lookupClassL2().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR:
        addQualifier(aclEntry.lookupClassNeighbor().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE:
        addQualifier(aclEntry.lookupClassRoute().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::LOOKUP_CLASS_PORT:
        addQualifier(aclEntry.lookupClassPort().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::MPLS_LABEL0_TTL:
        addQualifier(aclEntry.mplsLabel0Ttl().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::ETHER_TYPE:
        addQualifier(aclEntry.etherType().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::OUTER_VLAN:
        addQualifier(aclEntry.vlanID().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::IPV6_NEXT_HEADER:
        if (aclEntry.etherType().has_value() &&
            *aclEntry.etherType() == cfg::EtherType::IPv6) {
          addQualifier(aclEntry.proto().has_value(), qualifier);
        }
        break;

      case cfg::AclTableQualifier::BTH_OPCODE:
        addQualifier(aclEntry.roceOpcode().has_value(), qualifier);
        break;

      case cfg::AclTableQualifier::UDF:
        // handled with getRequiredUdfGroups
        break;

      case cfg::AclTableQualifier::TC:
      case cfg::AclTableQualifier::NEXT_HOP_GROUP_ID:
        // PBR-only qualifiers, synthesized into state AclEntry; not represented
        // in cfg::AclEntry.
        break;
    }
  }
  return requiredQualifiers;
}

std::set<std::string> getRequiredUdfGroups(const cfg::AclEntry& aclEntry) {
  std::set<std::string> requiredUdfGroups{};
  if (aclEntry.udfTable().has_value()) {
    for (auto& udfTableEntry : *aclEntry.udfTable()) {
      requiredUdfGroups.insert(udfTableEntry.udfGroup().value());
    }
  }
  return requiredUdfGroups;
}

bool aclEntrySupported(
    const cfg::AclTable* aclTable,
    const cfg::AclEntry& aclEntry) {
  auto aclTableQualifiers = aclTable->qualifiers();
  auto aclUdfGroups = aclTable->udfGroups();

  if (aclTableQualifiers->empty()) {
    // acl table supports all supported qualifiers
    return true;
  }
  auto requiredQualifiers = getRequiredQualifers(aclEntry);
  std::set<cfg::AclTableQualifier> aclTableQualifiersSet(
      aclTableQualifiers->begin(), aclTableQualifiers->end());
  std::set<cfg::AclTableQualifier> difference{};
  std::set_difference(
      requiredQualifiers.begin(),
      requiredQualifiers.end(),
      aclTableQualifiersSet.begin(),
      aclTableQualifiersSet.end(),
      std::inserter(difference, difference.begin()));

  if (!difference.empty()) {
    std::stringstream ss;
    for (const auto& qualifier : difference) {
      ss << apache::thrift::util::enumNameSafe(qualifier) << ", ";
    }
    XLOG(ERR) << "Acl table " << *aclTable->name()
              << " does not support qualifiers: " << ss.str() << " for acl "
              << *aclEntry.name();
    return false;
  }

  auto requiredUdfGroups = getRequiredUdfGroups(aclEntry);
  std::set<std::string> aclTableUdfGroups(
      aclUdfGroups->begin(), aclUdfGroups->end());
  std::set<std::string> differenceUdfGroups{};
  std::set_difference(
      requiredUdfGroups.begin(),
      requiredUdfGroups.end(),
      aclTableUdfGroups.begin(),
      aclTableUdfGroups.end(),
      std::inserter(differenceUdfGroups, differenceUdfGroups.begin()));

  if (!differenceUdfGroups.empty()) {
    std::stringstream ss;
    for (const auto& udfGroup : differenceUdfGroups) {
      ss << udfGroup << ", ";
    }
    XLOG(ERR) << "Acl table " << *aclTable->name()
              << " does not support udf groups: " << ss.str() << " for acl "
              << *aclEntry.name();
    return false;
  }

  return difference.empty() && differenceUdfGroups.empty();
}

std::string getAclTableForAclEntry(
    cfg::SwitchConfig& config,
    const cfg::AclEntry& aclEntry,
    cfg::AclStage stage) {
  auto aclTableGroup = getAclTableGroup(config, stage);
  if (!aclTableGroup) {
    throw FbossError("Acl table group not found");
  }
  for (auto& aclTable : *aclTableGroup->aclTables()) {
    if (aclEntrySupported(&aclTable, aclEntry)) {
      return *aclTable.name();
    }
  }
  throw FbossError("No acl table found for acl entry ", *aclEntry.name());
}

} // namespace facebook::fboss::utility
