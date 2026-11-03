/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/qos/policy/CmdDeleteQosPolicyMap.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/qos/QosPolicyUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {
constexpr auto kSupportedMapTypes =
    "dscp, mpls-exp, dot1p, tc-to-queue, pfc-pri-to-queue, tc-to-pg, "
    "pfc-pri-to-pg";
// Traffic classes, PFC priorities, EXP and PCP codepoints are all 3-bit.
constexpr int16_t kMinMapKey = 0;
constexpr int16_t kMaxMapKey = 7;
} // namespace

DeleteQosMapEntry::DeleteQosMapEntry(std::vector<std::string> v) {
  if (v.size() != 2) {
    throw std::invalid_argument(
        fmt::format(
            "Expected '<map-type> <value>', got {} argument(s). "
            "Valid map types: {}",
            v.size(),
            kSupportedMapTypes));
  }

  const std::string& mapType = v[0];
  if (mapType == "dscp") {
    mapType_ = DeleteQosMapType::DSCP;
  } else if (mapType == "mpls-exp") {
    mapType_ = DeleteQosMapType::MPLS_EXP;
  } else if (mapType == "dot1p") {
    mapType_ = DeleteQosMapType::DOT1P;
  } else if (mapType == "tc-to-queue") {
    mapType_ = DeleteQosMapType::TC_TO_QUEUE;
  } else if (mapType == "pfc-pri-to-queue") {
    mapType_ = DeleteQosMapType::PFC_PRI_TO_QUEUE;
  } else if (mapType == "tc-to-pg") {
    mapType_ = DeleteQosMapType::TC_TO_PG;
  } else if (mapType == "pfc-pri-to-pg") {
    mapType_ = DeleteQosMapType::PFC_PRI_TO_PG;
  } else {
    throw std::invalid_argument(
        fmt::format(
            "Unknown map type '{}'. Valid map types: {}",
            mapType,
            kSupportedMapTypes));
  }

  auto parsed = folly::tryTo<int16_t>(v[1]);
  if (!parsed.hasValue()) {
    throw std::invalid_argument(
        fmt::format("Invalid value '{}'. Must be an integer", v[1]));
  }
  key_ = parsed.value();

  if (mapType_ == DeleteQosMapType::DSCP) {
    if (key_ < utils::kMinDscp || key_ > utils::kMaxDscp) {
      throw std::invalid_argument(
          fmt::format(
              "dscp value must be between {} and {}, got: {}",
              utils::kMinDscp,
              utils::kMaxDscp,
              v[1]));
    }
  } else if (key_ < kMinMapKey || key_ > kMaxMapKey) {
    throw std::invalid_argument(
        fmt::format(
            "{} value must be between {} and {}, got: {}",
            mapType,
            kMinMapKey,
            kMaxMapKey,
            v[1]));
  }

  data_ = std::move(v);
}

namespace {

// Removes `codepoint` from whichever map entry's ingress list carries it. An
// entry that ends up with no ingress codepoints and no egress rewrite carries
// no information, so it is dropped rather than left behind as an empty shell.
// Shared by the three structurally identical list maps (DscpQosMap, ExpQosMap,
// PcpQosMap); the projections select the ingress list and the egress rewrite.
template <typename MapEntry, typename GetIngressList, typename GetEgressValue>
bool eraseCodepoint(
    std::vector<MapEntry>& maps,
    int16_t codepoint,
    GetIngressList getIngressList,
    GetEgressValue getEgressValue) {
  auto byteVal = static_cast<int8_t>(codepoint);
  bool erased = false;

  for (auto& entry : maps) {
    auto& fromList = getIngressList(entry);
    auto it = std::find(fromList.begin(), fromList.end(), byteVal);
    if (it != fromList.end()) {
      fromList.erase(it);
      erased = true;
    }
  }

  if (erased) {
    maps.erase(
        std::remove_if(
            maps.begin(),
            maps.end(),
            [&](const MapEntry& entry) {
              return getIngressList(const_cast<MapEntry&>(entry)).empty() &&
                  !getEgressValue(entry).has_value();
            }),
        maps.end());
  }

  return erased;
}

bool eraseDscp(std::vector<cfg::DscpQosMap>& dscpMaps, int16_t dscp) {
  return eraseCodepoint(
      dscpMaps,
      dscp,
      [](cfg::DscpQosMap& e) -> auto& { return *e.fromDscpToTrafficClass(); },
      [](const cfg::DscpQosMap& e) { return e.fromTrafficClassToDscp(); });
}

bool eraseExp(std::vector<cfg::ExpQosMap>& expMaps, int16_t exp) {
  return eraseCodepoint(
      expMaps,
      exp,
      [](cfg::ExpQosMap& e) -> auto& { return *e.fromExpToTrafficClass(); },
      [](const cfg::ExpQosMap& e) { return e.fromTrafficClassToExp(); });
}

bool erasePcp(std::vector<cfg::PcpQosMap>& pcpMaps, int16_t pcp) {
  return eraseCodepoint(
      pcpMaps,
      pcp,
      [](cfg::PcpQosMap& e) -> auto& { return *e.fromPcpToTrafficClass(); },
      [](const cfg::PcpQosMap& e) { return e.fromTrafficClassToPcp(); });
}

// Erases `key` from an optional map<i16, i16> field. Returns false when the
// field is unset or the key is absent. A map left empty by the erase is
// reset, so the field returns to its unset default.
template <typename OptionalMapRef>
bool eraseOptionalMapKey(OptionalMapRef field, int16_t key) {
  if (!field.has_value()) {
    return false;
  }
  if (field->erase(key) == 0) {
    return false;
  }
  if (field->empty()) {
    field.reset();
  }
  return true;
}

// Sets `changed` to false when the mapping was absent and nothing was erased.
std::string deleteQosPolicyMapEntry(
    cfg::QosMap& qosMap,
    const std::string& policyName,
    const DeleteQosMapEntry& entry,
    bool& changed) {
  changed = true;
  switch (entry.getMapType()) {
    case DeleteQosMapType::DSCP: {
      if (!eraseDscp(*qosMap.dscpMaps(), entry.getKey())) {
        changed = false;
        return fmt::format(
            "Warning: QoS policy '{}' has no dscp mapping for value {}, "
            "nothing to delete",
            policyName,
            entry.getKey());
      }
      return fmt::format(
          "Successfully deleted QoS policy '{}' dscp mapping for value {}",
          policyName,
          entry.getKey());
    }
    case DeleteQosMapType::MPLS_EXP: {
      if (!eraseExp(*qosMap.expMaps(), entry.getKey())) {
        changed = false;
        return fmt::format(
            "Warning: QoS policy '{}' has no mpls-exp mapping for value {}, "
            "nothing to delete",
            policyName,
            entry.getKey());
      }
      return fmt::format(
          "Successfully deleted QoS policy '{}' mpls-exp mapping for value {}",
          policyName,
          entry.getKey());
    }
    case DeleteQosMapType::DOT1P: {
      auto pcpMaps = qosMap.pcpMaps();
      if (!pcpMaps.has_value() || !erasePcp(*pcpMaps, entry.getKey())) {
        changed = false;
        return fmt::format(
            "Warning: QoS policy '{}' has no dot1p mapping for value {}, "
            "nothing to delete",
            policyName,
            entry.getKey());
      }
      if (pcpMaps->empty()) {
        pcpMaps.reset();
      }
      return fmt::format(
          "Successfully deleted QoS policy '{}' dot1p mapping for value {}",
          policyName,
          entry.getKey());
    }
    case DeleteQosMapType::TC_TO_QUEUE: {
      auto& tcToQueue = *qosMap.trafficClassToQueueId();
      if (tcToQueue.erase(entry.getKey()) == 0) {
        changed = false;
        return fmt::format(
            "Warning: QoS policy '{}' has no tc-to-queue mapping for traffic class {}, "
            "nothing to delete",
            policyName,
            entry.getKey());
      }
      return fmt::format(
          "Successfully deleted QoS policy '{}' tc-to-queue mapping for traffic class {}",
          policyName,
          entry.getKey());
    }
    case DeleteQosMapType::PFC_PRI_TO_QUEUE: {
      if (!eraseOptionalMapKey(qosMap.pfcPriorityToQueueId(), entry.getKey())) {
        changed = false;
        return fmt::format(
            "Warning: QoS policy '{}' has no pfc-pri-to-queue mapping for priority {}, "
            "nothing to delete",
            policyName,
            entry.getKey());
      }
      return fmt::format(
          "Successfully deleted QoS policy '{}' pfc-pri-to-queue mapping for priority {}",
          policyName,
          entry.getKey());
    }
    case DeleteQosMapType::TC_TO_PG: {
      if (!eraseOptionalMapKey(qosMap.trafficClassToPgId(), entry.getKey())) {
        changed = false;
        return fmt::format(
            "Warning: QoS policy '{}' has no tc-to-pg mapping for traffic class {}, "
            "nothing to delete",
            policyName,
            entry.getKey());
      }
      return fmt::format(
          "Successfully deleted QoS policy '{}' tc-to-pg mapping for traffic class {}",
          policyName,
          entry.getKey());
    }
    case DeleteQosMapType::PFC_PRI_TO_PG: {
      if (!eraseOptionalMapKey(qosMap.pfcPriorityToPgId(), entry.getKey())) {
        changed = false;
        return fmt::format(
            "Warning: QoS policy '{}' has no pfc-pri-to-pg mapping for priority {}, "
            "nothing to delete",
            policyName,
            entry.getKey());
      }
      return fmt::format(
          "Successfully deleted QoS policy '{}' pfc-pri-to-pg mapping for priority {}",
          policyName,
          entry.getKey());
    }
  }

  throw std::runtime_error("Unhandled map type");
}

} // namespace

CmdDeleteQosPolicyMapTraits::RetType CmdDeleteQosPolicyMap::queryClient(
    const HostInfo& /* hostInfo */,
    const QosPolicyName& policyName,
    const ObjectArgType& entry) {
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();

  const std::string name = policyName.getName();
  auto& qosPolicies = *switchConfig.qosPolicies();

  auto policyIt = utils::findQosPolicyOrThrow(qosPolicies, name);

  if (!policyIt->qosMap().has_value()) {
    return fmt::format(
        "Warning: QoS policy '{}' has no qosMap configured, "
        "nothing to delete",
        name);
  }
  auto& qosMap = *policyIt->qosMap();

  bool changed = false;
  const auto logMsg = deleteQosPolicyMapEntry(qosMap, name, entry, changed);
  if (changed) {
    session.saveConfig();
  }
  return logMsg;
}

void CmdDeleteQosPolicyMap::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

template void
CmdHandler<CmdDeleteQosPolicyMap, CmdDeleteQosPolicyMapTraits>::run();

} // namespace facebook::fboss
