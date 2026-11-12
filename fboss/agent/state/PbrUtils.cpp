/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/PbrUtils.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/ClassBasedPolicyNode.h"
#include "fboss/agent/state/MatchAction.h"

#include <folly/Conv.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

namespace facebook::fboss {

std::string makePbrAclEntryName(
    const std::string& policyName,
    ForwardingClass trafficClass,
    bool temporary) {
  return folly::to<std::string>(
      temporary ? std::string(1, kPbrTempAclEntryPrefix) : std::string(),
      policyName,
      "_tc",
      static_cast<int>(trafficClass));
}

bool isPbrTempAclEntry(const std::string& entryName) {
  return !entryName.empty() && entryName.front() == kPbrTempAclEntryPrefix;
}

std::string makePbrCounterName(
    ForwardingClass trafficClass,
    const std::string& redirectNhgName) {
  return folly::to<std::string>(
      apache::thrift::util::enumNameSafe(trafficClass), "_", redirectNhgName);
}

std::shared_ptr<AclEntry> makePbrAclEntry(
    const std::shared_ptr<ClassBasedPolicyNode>& policy,
    ForwardingClass trafficClass,
    bool temporary) {
  const auto& class2Nhg = policy->getClass2NextHopGroup();
  auto it = class2Nhg.find(trafficClass);
  if (it == class2Nhg.end()) {
    throw FbossError(
        "PBR policy '",
        policy->getID(),
        "' has no next hop group for traffic class ",
        apache::thrift::util::enumNameSafe(trafficClass));
  }
  const auto& nhg = it->second;
  auto entry = std::make_shared<AclEntry>(
      FLAGS_pbr_acl_priority,
      makePbrAclEntryName(policy->getID(), trafficClass, temporary));
  entry->setNextHopGroupId(*policy->getDefaultNextHopGroup().id());
  entry->setTrafficClass(static_cast<uint8_t>(trafficClass));
  MatchAction action;
  action.setRedirectNextHopGroupId(*nhg.id());
  cfg::TrafficCounter counter;
  counter.name() = makePbrCounterName(trafficClass, *nhg.name());
  counter.types() = {cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
  action.setTrafficCounter(counter);
  entry->setAclAction(action);
  return entry;
}

std::vector<std::shared_ptr<AclEntry>> createAclEntriesFromPolicy(
    const std::shared_ptr<ClassBasedPolicyNode>& policy,
    bool temporary) {
  std::vector<std::shared_ptr<AclEntry>> entries;
  for (const auto& [trafficClass, _] : policy->getClass2NextHopGroup()) {
    entries.push_back(makePbrAclEntry(policy, trafficClass, temporary));
  }
  return entries;
}

} // namespace facebook::fboss
