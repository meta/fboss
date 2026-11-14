/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/qos/policy/CmdConfigQosPolicyMap.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/lang/Assume.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/qos/QosPolicyUtils.h"
#include "fboss/cli/fboss2/commands/config/qos/policy/CmdConfigQosPolicy.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {

constexpr int16_t kMinTCValue = 0;
constexpr int16_t kMaxTCValue = 7;
// DSCP bounds (0..63) are shared with the delete command via utils::kMinDscp /
// utils::kMaxDscp in QosPolicyUtils.h.
// MPLS EXP/TC: 3-bit field (RFC 3032, RFC 5462)
constexpr int8_t kMinExpValue = 0;
constexpr int8_t kMaxExpValue = 7;
// PCP/DOT1P: 3-bit field (IEEE 802.1Q)
constexpr int8_t kMinPcpValue = 0;
constexpr int8_t kMaxPcpValue = 7;

std::string getMapTypeString(QosMapType mapType, QosMapDirection direction) {
  switch (mapType) {
    case QosMapType::TC_TO_QUEUE:
      return "trafficClassToQueueId";
    case QosMapType::PFC_PRI_TO_QUEUE:
      return "pfcPriorityToQueueId";
    case QosMapType::TC_TO_PG:
      return "trafficClassToPgId";
    case QosMapType::PFC_PRI_TO_PG:
      return "pfcPriorityToPgId";
    case QosMapType::DSCP:
      return direction == QosMapDirection::INGRESS
          ? "dscpMaps.fromDscpToTrafficClass"
          : "dscpMaps.fromTrafficClassToDscp";
    case QosMapType::MPLS_EXP:
      return direction == QosMapDirection::INGRESS
          ? "expMaps.fromExpToTrafficClass"
          : "expMaps.fromTrafficClassToExp";
    case QosMapType::DOT1P:
      return direction == QosMapDirection::INGRESS
          ? "pcpMaps.fromPcpToTrafficClass"
          : "pcpMaps.fromTrafficClassToPcp";
  }
  folly::assume_unreachable();
}

// Validates value range based on type token and returns the map type.
QosMapType validateAndGetMapType(const std::string& typeToken, int16_t value) {
  if (typeToken == "dscp") {
    if (value < utils::kMinDscp || value > utils::kMaxDscp) {
      throw std::invalid_argument(
          fmt::format(
              "DSCP value must be between {} and {}, got: {}",
              utils::kMinDscp,
              utils::kMaxDscp,
              value));
    }
    return QosMapType::DSCP;
  } else if (typeToken == "mpls-exp") {
    if (value < kMinExpValue || value > kMaxExpValue) {
      throw std::invalid_argument(
          fmt::format(
              "MPLS EXP value must be between {} and {}, got: {}",
              kMinExpValue,
              kMaxExpValue,
              value));
    }
    return QosMapType::MPLS_EXP;
  } else if (typeToken == "dot1p") {
    if (value < kMinPcpValue || value > kMaxPcpValue) {
      throw std::invalid_argument(
          fmt::format(
              "DOT1P value must be between {} and {}, got: {}",
              kMinPcpValue,
              kMaxPcpValue,
              value));
    }
    return QosMapType::DOT1P;
  }
  folly::assume_unreachable();
}

// Helper to update a QoS map entry (DSCP/EXP/DOT1P).
// - mapsRef: reference to the list of map entries
// - config: the QosMapConfig containing trafficClass, value, and direction
// - getIngressList: lambda to get the ingress list from an entry
// - setEgressValue: lambda to set the egress value on an entry
template <typename MapEntry, typename GetIngressList, typename SetEgressValue>
void updateQosMapEntry(
    std::vector<MapEntry>& mapsRef,
    const QosMapConfig& config,
    GetIngressList getIngressList,
    SetEgressValue setEgressValue) {
  int16_t trafficClass = config.getTrafficClass();
  // Find or create entry with matching internalTrafficClass
  MapEntry* entry = nullptr;
  for (auto& m : mapsRef) {
    if (*m.internalTrafficClass() == trafficClass) {
      entry = &m;
      break;
    }
  }
  if (entry == nullptr) { // Entry not found, create a new one
    MapEntry newEntry;
    newEntry.internalTrafficClass() = trafficClass;
    mapsRef.push_back(std::move(newEntry));
    entry = &mapsRef.back();
  }
  if (config.getDirection() == QosMapDirection::INGRESS) {
    auto& fromList = getIngressList(*entry);
    auto byteVal = static_cast<int8_t>(config.getValue());
    if (std::find(fromList.begin(), fromList.end(), byteVal) ==
        fromList.end()) {
      fromList.push_back(byteVal);
    }
  } else {
    setEgressValue(*entry, static_cast<int8_t>(config.getValue()));
  }
}

} // namespace

QosMapConfig::QosMapConfig(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Expected: <map-type> ... where map-type is one of: "
        "tc-to-queue, pfc-pri-to-queue, tc-to-pg, pfc-pri-to-pg, "
        "dscp, mpls-exp, dot1p, traffic-class");
  }

  const auto& firstToken = v[0];

  // Check for simple map types first
  if (firstToken == "tc-to-queue") {
    mapType_ = QosMapType::TC_TO_QUEUE;
  } else if (firstToken == "pfc-pri-to-queue") {
    mapType_ = QosMapType::PFC_PRI_TO_QUEUE;
  } else if (firstToken == "tc-to-pg") {
    mapType_ = QosMapType::TC_TO_PG;
  } else if (firstToken == "pfc-pri-to-pg") {
    mapType_ = QosMapType::PFC_PRI_TO_PG;
  } else if (
      firstToken == "dscp" || firstToken == "mpls-exp" ||
      firstToken == "dot1p" || firstToken == "traffic-class") {
    // Handle DSCP/EXP/DOT1P maps with a different syntax
    parseListMapType(v);
    return;
  } else {
    throw std::invalid_argument(
        fmt::format(
            "Invalid map type: '{}'. Valid values are: "
            "tc-to-queue, pfc-pri-to-queue, tc-to-pg, pfc-pri-to-pg, "
            "dscp, mpls-exp, dot1p, traffic-class",
            firstToken));
  }

  // Simple map types: <map-type> <key> <value>
  data_.push_back(firstToken);

  if (v.size() < 3) {
    throw std::invalid_argument(
        fmt::format("Expected: {} <key> <value>", firstToken));
  }

  // Parse the key
  key_ = folly::to<int16_t>(v[1]);
  if (key_ < kMinTCValue || key_ > kMaxTCValue) {
    throw std::invalid_argument(
        fmt::format(
            "Key must be between {} and {}, got: {}",
            kMinTCValue,
            kMaxTCValue,
            key_));
  }
  data_.push_back(v[1]);

  // Parse the value
  value_ = folly::to<int16_t>(v[2]);
  if (value_ < kMinTCValue || value_ > kMaxTCValue) {
    throw std::invalid_argument(
        fmt::format(
            "Value must be between {} and {}, got: {}",
            kMinTCValue,
            kMaxTCValue,
            value_));
  }
  data_.push_back(v[2]);
}

bool QosMapConfig::isListMapType() const {
  return mapType_ == QosMapType::DSCP || mapType_ == QosMapType::MPLS_EXP ||
      mapType_ == QosMapType::DOT1P;
}

void QosMapConfig::parseListMapType(const std::vector<std::string>& v) {
  // Syntax for DSCP/EXP/DOT1P maps:
  // Ingress (X -> TC, additive):
  //   dscp <value> traffic-class <tc>
  //   mpls-exp <value> traffic-class <tc>
  //   dot1p <value> traffic-class <tc>
  // Egress (TC -> X):
  //   traffic-class <tc> dscp <value>
  //   traffic-class <tc> mpls-exp <value>
  //   traffic-class <tc> dot1p <value>

  const auto& firstToken = v[0];

  if (v.size() < 4) {
    if (firstToken == "traffic-class") {
      throw std::invalid_argument(
          "Expected: traffic-class <tc> <type> <value> "
          "where type is one of: dscp, mpls-exp, dot1p");
    } else {
      throw std::invalid_argument(
          fmt::format("Expected: {} <value> traffic-class <tc>", firstToken));
    }
  }

  if (firstToken == "traffic-class") {
    // Egress direction: traffic-class <tc> <type> <value>
    // Valid types are: dscp, mpls-exp, dot1p
    direction_ = QosMapDirection::EGRESS;
    trafficClass_ = folly::to<int16_t>(v[1]);
    if (trafficClass_ < kMinTCValue || trafficClass_ > kMaxTCValue) {
      throw std::invalid_argument(
          fmt::format(
              "Traffic class must be between {} and {}, got: {}",
              kMinTCValue,
              kMaxTCValue,
              trafficClass_));
    }

    const auto& typeToken = v[2];
    // Validate that the type token is a valid list map type (dscp, mpls-exp,
    // dot1p) before parsing. This prevents "queue-id" and other tokens from
    // being incorrectly processed as list maps.
    if (typeToken != "dscp" && typeToken != "mpls-exp" &&
        typeToken != "dot1p") {
      throw std::invalid_argument(
          fmt::format(
              "Invalid map type '{}' after 'traffic-class <tc>'. "
              "Valid types are: dscp, mpls-exp, dot1p. "
              "For traffic-class to queue mappings, use 'tc-to-queue' instead.",
              typeToken));
    }
    value_ = folly::to<int16_t>(v[3]);
    mapType_ = validateAndGetMapType(typeToken, value_);
  } else {
    // Ingress direction: <type> <value> traffic-class <tc>
    direction_ = QosMapDirection::INGRESS;
    value_ = folly::to<int16_t>(v[1]);

    if (v[2] != "traffic-class") {
      throw std::invalid_argument(
          fmt::format(
              "Expected 'traffic-class' after '{} <value>', got '{}'",
              firstToken,
              v[2]));
    }

    trafficClass_ = folly::to<int16_t>(v[3]);
    if (trafficClass_ < kMinTCValue || trafficClass_ > kMaxTCValue) {
      throw std::invalid_argument(
          fmt::format(
              "Traffic class must be between {} and {}, got: {}",
              kMinTCValue,
              kMaxTCValue,
              trafficClass_));
    }

    mapType_ = validateAndGetMapType(firstToken, value_);
  }

  // Populate data_ for display purposes
  for (const auto& token : v) {
    data_.push_back(token);
  }
}

CmdConfigQosPolicyMapTraits::RetType CmdConfigQosPolicyMap::queryClient(
    const HostInfo& /* hostInfo */,
    const QosPolicyName& policyName,
    const ObjectArgType& config) {
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();

  const std::string& name = policyName.getName();
  auto& qosPolicies = *switchConfig.qosPolicies();

  // Find or create the QosPolicy with the given name
  cfg::QosPolicy* targetPolicy = nullptr;
  if (auto it = utils::findQosPolicy(qosPolicies, name);
      it != qosPolicies.end()) {
    targetPolicy = &*it;
  }

  if (targetPolicy == nullptr) {
    // Create a new QosPolicy
    cfg::QosPolicy newPolicy;
    newPolicy.name() = name;
    qosPolicies.push_back(std::move(newPolicy));
    targetPolicy = &qosPolicies.back();
  }

  // Ensure qosMap is initialized
  if (!targetPolicy->qosMap().has_value()) {
    targetPolicy->qosMap() = cfg::QosMap();
  }
  auto& qosMap = *targetPolicy->qosMap();

  // Set the appropriate map entry based on map type
  if (config.isListMapType()) {
    // Handle DSCP/EXP/DOT1P maps
    switch (config.getMapType()) {
      case QosMapType::DSCP:
        updateQosMapEntry(
            *qosMap.dscpMaps(),
            config,
            [](cfg::DscpQosMap& e) -> auto& {
              return *e.fromDscpToTrafficClass();
            },
            [](cfg::DscpQosMap& e, int8_t v) {
              e.fromTrafficClassToDscp() = v;
            });
        break;
      case QosMapType::MPLS_EXP:
        updateQosMapEntry(
            *qosMap.expMaps(),
            config,
            [](cfg::ExpQosMap& e) -> auto& {
              return *e.fromExpToTrafficClass();
            },
            [](cfg::ExpQosMap& e, int8_t v) { e.fromTrafficClassToExp() = v; });
        break;
      case QosMapType::DOT1P:
        // pcpMaps is optional
        if (!qosMap.pcpMaps().has_value()) {
          qosMap.pcpMaps() = std::vector<cfg::PcpQosMap>();
        }
        updateQosMapEntry(
            *qosMap.pcpMaps(),
            config,
            [](cfg::PcpQosMap& e) -> auto& {
              return *e.fromPcpToTrafficClass();
            },
            [](cfg::PcpQosMap& e, int8_t v) { e.fromTrafficClassToPcp() = v; });
        break;
      default:
        break;
    }

    session.saveConfig();

    return fmt::format(
        "Successfully set QoS policy '{}' {} [tc={}] = {}",
        name,
        getMapTypeString(config.getMapType(), config.getDirection()),
        config.getTrafficClass(),
        config.getValue());
  } else {
    // Handle simple map types
    int16_t key = config.getKey();
    int16_t value = config.getValue();

    switch (config.getMapType()) {
      case QosMapType::TC_TO_QUEUE:
        (*qosMap.trafficClassToQueueId())[key] = value;
        break;
      case QosMapType::PFC_PRI_TO_QUEUE:
        if (!qosMap.pfcPriorityToQueueId().has_value()) {
          qosMap.pfcPriorityToQueueId() = std::map<int16_t, int16_t>();
        }
        (*qosMap.pfcPriorityToQueueId())[key] = value;
        break;
      case QosMapType::TC_TO_PG:
        if (!qosMap.trafficClassToPgId().has_value()) {
          qosMap.trafficClassToPgId() = std::map<int16_t, int16_t>();
        }
        (*qosMap.trafficClassToPgId())[key] = value;
        break;
      case QosMapType::PFC_PRI_TO_PG:
        if (!qosMap.pfcPriorityToPgId().has_value()) {
          qosMap.pfcPriorityToPgId() = std::map<int16_t, int16_t>();
        }
        (*qosMap.pfcPriorityToPgId())[key] = value;
        break;
      default:
        break;
    }

    session.saveConfig();

    return fmt::format(
        "Successfully set QoS policy '{}' {} [{}] = {}",
        name,
        getMapTypeString(config.getMapType(), config.getDirection()),
        key,
        value);
  }
}

void CmdConfigQosPolicyMap::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigQosPolicyMap, CmdConfigQosPolicyMapTraits>::run();

std::vector<std::string> CmdConfigQosPolicyMapTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completePositions(
      typed,
      {{"tc-to-queue",
        "pfc-pri-to-queue",
        "tc-to-pg",
        "pfc-pri-to-pg",
        "dscp",
        "mpls-exp",
        "dot1p",
        "traffic-class"}});
}

} // namespace facebook::fboss
