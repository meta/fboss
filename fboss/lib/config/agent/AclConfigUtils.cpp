/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/config/agent/AclConfigUtils.h"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <folly/CppAttributes.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"

namespace facebook::fboss::utility {
namespace {

constexpr std::string_view kDefaultAclTableGroupName =
    "acl-table-group-ingress";
constexpr std::string_view kIpv6AclTableName = "ipv6-acl-table";
constexpr std::string_view kTtldAclTableName = "ttld-acl-table";

std::string defaultIngressAclTableName() {
  return cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE();
}

std::string defaultPostLookupIngressAclTableGroupName() {
  return cfg::switch_config_constants::
      DEFAULT_POST_LOOKUP_INGRESS_ACL_TABLE_GROUP();
}

std::string defaultPostLookupIngressAclTableName() {
  return cfg::switch_config_constants::DEFAULT_POST_LOOKUP_INGRESS_ACL_TABLE();
}

cfg::AclTableGroup* FOLLY_NULLABLE
getAclTableGroup(cfg::SwitchConfig& config, cfg::AclStage stage) {
  if (!config.aclTableGroups()) {
    return nullptr;
  }
  for (auto& group : *config.aclTableGroups()) {
    if (*group.stage() == stage) {
      return &group;
    }
  }
  return nullptr;
}

cfg::AclTable* FOLLY_NULLABLE
getDefaultAclTable(cfg::SwitchConfig& config, cfg::AclStage stage) {
  auto* group = getAclTableGroup(config, stage);
  if (!group) {
    return nullptr;
  }
  const auto tableName = stage == cfg::AclStage::INGRESS
      ? defaultIngressAclTableName()
      : defaultPostLookupIngressAclTableName();
  const auto table = std::find_if(
      group->aclTables()->begin(),
      group->aclTables()->end(),
      [&](const auto& candidate) { return *candidate.name() == tableName; });
  return table == group->aclTables()->end() ? nullptr : &*table;
}

cfg::AclTableGroup& addAclTableGroup(
    cfg::SwitchConfig& config,
    cfg::AclStage stage,
    std::string_view name) {
  if (!config.aclTableGroups()) {
    config.aclTableGroups() = {};
  }
  cfg::AclTableGroup group;
  group.name() = name;
  group.stage() = stage;
  config.aclTableGroups()->push_back(std::move(group));
  return config.aclTableGroups()->back();
}

cfg::AclTable& addAclTable(
    cfg::AclTableGroup& group,
    std::string_view name,
    int priority,
    std::vector<cfg::AclTableActionType> actionTypes,
    std::vector<cfg::AclTableQualifier> qualifiers,
    const std::vector<std::string>& udfGroups) {
  cfg::AclTable table;
  table.name() = name;
  table.priority() = priority;
  table.actionTypes() = std::move(actionTypes);
  table.qualifiers() = std::move(qualifiers);
  table.udfGroups() = udfGroups;
  group.aclTables()->push_back(std::move(table));
  return group.aclTables()->back();
}

void setupDefaultIngressAclTableGroup(
    cfg::SwitchConfig& config,
    const HwAsic& asic) {
  if (getAclTableGroup(config, cfg::AclStage::INGRESS) ||
      config.aclTableGroup()) {
    // default table group already exists
    return;
  }
  addAclTableGroup(config, cfg::AclStage::INGRESS, kDefaultAclTableGroupName);
  addDefaultAclTable(config, asic);
}

void setupDefaultPostLookupIngressAclTableGroup(
    cfg::SwitchConfig& config,
    const HwAsic& asic) {
  if (!asic.isSupported(HwAsic::Feature::INGRESS_POST_LOOKUP_ACL_TABLE) ||
      getAclTableGroup(config, cfg::AclStage::INGRESS_POST_LOOKUP)) {
    return;
  }
  auto& group = addAclTableGroup(
      config,
      cfg::AclStage::INGRESS_POST_LOOKUP,
      defaultPostLookupIngressAclTableGroupName());
  addAclTable(
      group,
      defaultPostLookupIngressAclTableName(),
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

} // namespace

std::vector<cfg::AclTableQualifier> genAclQualifiersConfig(
    cfg::AsicType asicType) {
  std::vector<cfg::AclTableQualifier> qualifiers = {
      cfg::AclTableQualifier::SRC_IPV6,
      cfg::AclTableQualifier::DST_IPV6,
      cfg::AclTableQualifier::SRC_IPV4,
      cfg::AclTableQualifier::DST_IPV4,
      cfg::AclTableQualifier::L4_SRC_PORT,
      cfg::AclTableQualifier::L4_DST_PORT,
      cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
      cfg::AclTableQualifier::DSCP,
      cfg::AclTableQualifier::TTL,
      cfg::AclTableQualifier::ICMPV4_TYPE,
      cfg::AclTableQualifier::ICMPV4_CODE,
      cfg::AclTableQualifier::ICMPV6_TYPE,
      cfg::AclTableQualifier::ICMPV6_CODE,
      cfg::AclTableQualifier::OUTER_VLAN};
  if (asicType != cfg::AsicType::ASIC_TYPE_JERICHO3) {
    qualifiers.push_back(cfg::AclTableQualifier::IP_TYPE);
  }
  if (asicType == cfg::AsicType::ASIC_TYPE_CHENAB ||
      asicType == cfg::AsicType::ASIC_TYPE_CHENAB2) {
    const std::set<cfg::AclTableQualifier> unsupported{
        cfg::AclTableQualifier::SRC_IPV6,
        cfg::AclTableQualifier::DST_IPV6,
        cfg::AclTableQualifier::OUTER_VLAN,
    };
    qualifiers.erase(
        std::remove_if(
            qualifiers.begin(),
            qualifiers.end(),
            [&](const auto qualifier) {
              return unsupported.contains(qualifier);
            }),
        qualifiers.end());
    qualifiers.push_back(cfg::AclTableQualifier::ETHER_TYPE);
  }
  if (asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1) {
    const std::set<cfg::AclTableQualifier> unsupported{
        cfg::AclTableQualifier::TTL,
        cfg::AclTableQualifier::OUTER_VLAN,
    };
    qualifiers.erase(
        std::remove_if(
            qualifiers.begin(),
            qualifiers.end(),
            [&](const auto qualifier) {
              return unsupported.contains(qualifier);
            }),
        qualifiers.end());
  }
  if (asicType == cfg::AsicType::ASIC_TYPE_FAKE) {
    qualifiers.push_back(cfg::AclTableQualifier::L4_DST_PORT_RANGE);
  }
  return qualifiers;
}

std::vector<cfg::AclTableActionType> genAclActionTypesConfig() {
  std::vector<cfg::AclTableActionType> actions = {
      cfg::AclTableActionType::PACKET_ACTION,
      cfg::AclTableActionType::COUNTER,
      cfg::AclTableActionType::SET_TC,
      cfg::AclTableActionType::SET_DSCP,
      cfg::AclTableActionType::MIRROR_INGRESS,
      cfg::AclTableActionType::MIRROR_EGRESS,
  };
  return actions;
}

void addDefaultAclTable(
    cfg::SwitchConfig& config,
    const HwAsic& asic,
    const std::vector<std::string>& udfGroups) {
  auto* group = getAclTableGroup(config, cfg::AclStage::INGRESS);
  if (!group) {
    throw FbossError(
        "Attempted to add the default ACL table without an ingress group");
  }

  // TODO (pshaikh): create a method to return AclTables for a given asic type
  // and acl stage and retire this check
  const auto asicType = asic.getAsicType();
  const auto isChenab =
      asic.getAsicVendor() == HwAsic::AsicVendor::ASIC_VENDOR_CHENAB;

  /* Create default ACL table similar to whats being done in Agent today */
  const auto defaultTableIndex = group->aclTables()->size();
  if (asicType == cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1) {
    auto qualifiers = genAclQualifiersConfig(asicType);
    qualifiers.push_back(cfg::AclTableQualifier::SRC_PORT);
    qualifiers.push_back(cfg::AclTableQualifier::IP_FRAG);
    qualifiers.push_back(cfg::AclTableQualifier::DST_MAC);
    addAclTable(
        *group,
        defaultIngressAclTableName(),
        0 /* priority */,
        genAclActionTypesConfig(),
        std::move(qualifiers),
        udfGroups);
  } else if (
      asicType == cfg::AsicType::ASIC_TYPE_QUMRAN4D ||
      asicType == cfg::AsicType::ASIC_TYPE_JERICHO4) {
    std::vector<cfg::AclTableQualifier> ipv4Qualifiers = {
        cfg::AclTableQualifier::DST_MAC,
        cfg::AclTableQualifier::ETHER_TYPE,
        cfg::AclTableQualifier::IP_TYPE,
        cfg::AclTableQualifier::SRC_IPV4,
        cfg::AclTableQualifier::DST_IPV4,
        cfg::AclTableQualifier::SRC_PORT,
        cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
        cfg::AclTableQualifier::DSCP,
        cfg::AclTableQualifier::TTL,
        cfg::AclTableQualifier::L4_SRC_PORT,
        cfg::AclTableQualifier::L4_DST_PORT,
        cfg::AclTableQualifier::TCP_FLAGS,
        cfg::AclTableQualifier::ICMPV4_TYPE,
        cfg::AclTableQualifier::ICMPV4_CODE,
    };
    std::vector<cfg::AclTableQualifier> ipv6Qualifiers = {
        cfg::AclTableQualifier::SRC_IPV6,
        cfg::AclTableQualifier::DST_IPV6,
        cfg::AclTableQualifier::IP_TYPE,
        cfg::AclTableQualifier::SRC_PORT,
        // NOTE (Q4D/J4): OUT_PORT (FIELD_OUT_PORT) intentionally omitted.
        // It is not part of the shared DNX supported-qualifier set
        // (jericho3Qualifiers, used by J3/J4/Q4D) and no ACL entry matches
        // on egress out-port. On Q4D 16.x a table created with
        // FIELD_OUT_PORT=true reads back FIELD_OUT_PORT=false on
        // get_acl_table_attribute, which breaks warmboot/rollback
        // reconciliation (set -> NOT IMPLEMENTED -> recreate ->
        // OBJECT IN USE -> crash). J3AI never hits this because its table
        // is built from the supported-qualifier set, which omits OUT_PORT.
        cfg::AclTableQualifier::IPV6_NEXT_HEADER,
        cfg::AclTableQualifier::ETHER_TYPE,
        cfg::AclTableQualifier::DSCP,
        cfg::AclTableQualifier::TTL,
        cfg::AclTableQualifier::L4_SRC_PORT,
        cfg::AclTableQualifier::L4_DST_PORT,
        cfg::AclTableQualifier::TCP_FLAGS,
        cfg::AclTableQualifier::ICMPV6_TYPE,
        cfg::AclTableQualifier::ICMPV6_CODE,
    };
    if (asic.isSupported(HwAsic::Feature::ACL_METADATA_QUALIFER)) {
      ipv4Qualifiers.push_back(cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR);
      ipv4Qualifiers.push_back(cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE);
      ipv6Qualifiers.push_back(cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR);
      ipv6Qualifiers.push_back(cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE);
    }
    addAclTable(
        *group,
        defaultIngressAclTableName(),
        0 /* priority */,
        {},
        std::move(ipv4Qualifiers),
        udfGroups);
    addAclTable(
        *group,
        kIpv6AclTableName,
        1 /* priority */,
        {},
        std::move(ipv6Qualifiers),
        udfGroups);
  } else if (!isChenab) {
    addAclTable(
        *group,
        defaultIngressAclTableName(),
        0 /* priority */,
        {},
        {},
        udfGroups);
  } else {
    /* full set of supported and required qualifiers do not fit in single table.
     * default acl table support all use cases except TTLD and ARS */
    addAclTable(
        *group,
        defaultIngressAclTableName(),
        0 /* priority */,
        {},
        {
            cfg::AclTableQualifier::DST_IPV6,
            cfg::AclTableQualifier::DST_IPV4,
            cfg::AclTableQualifier::L4_SRC_PORT,
            cfg::AclTableQualifier::L4_DST_PORT,
            cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
            cfg::AclTableQualifier::IPV6_NEXT_HEADER,
            cfg::AclTableQualifier::SRC_PORT,
            cfg::AclTableQualifier::DSCP,
            cfg::AclTableQualifier::TTL,
            cfg::AclTableQualifier::IP_TYPE,
            cfg::AclTableQualifier::ETHER_TYPE,
            cfg::AclTableQualifier::OUTER_VLAN,
        },
        udfGroups);
    addAclTable(
        *group,
        kTtldAclTableName,
        1 /* priority */,
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

  group->aclTables()->at(defaultTableIndex).aclEntries() =
      std::move(*config.acls());
  config.acls() = {};
}

bool setupDefaultAclTableGroups(cfg::SwitchConfig& config, const HwAsic& asic) {
  if (!asic.isSupported(HwAsic::Feature::ACL_TABLE_GROUP)) {
    return false;
  }
  if (config.aclTableGroup()) {
    return true;
  }
  setupDefaultIngressAclTableGroup(config, asic);
  setupDefaultPostLookupIngressAclTableGroup(config, asic);
  return true;
}

void addAclEntryToDefaultAclTable(
    cfg::SwitchConfig& config,
    cfg::AclEntry acl,
    cfg::AclStage stage) {
  auto* table = getDefaultAclTable(config, stage);
  if (!table) {
    throw FbossError(
        "Attempted to add an ACL entry without a default table for stage ",
        apache::thrift::util::enumNameSafe(stage));
  }
  table->aclEntries()->push_back(std::move(acl));
}

} // namespace facebook::fboss::utility
