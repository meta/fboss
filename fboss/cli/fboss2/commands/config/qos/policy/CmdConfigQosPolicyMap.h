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

#include <cstdint>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/qos/policy/CmdConfigQosPolicy.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

/**
 * Enum representing which QosMap to modify.
 */
enum class QosMapType {
  TC_TO_QUEUE, // trafficClassToQueueId
  PFC_PRI_TO_QUEUE, // pfcPriorityToQueueId
  TC_TO_PG, // trafficClassToPgId
  PFC_PRI_TO_PG, // pfcPriorityToPgId
  DSCP, // dscpMaps
  MPLS_EXP, // expMaps
  DOT1P, // pcpMaps
};

/**
 * Direction for DSCP/EXP/DOT1P maps.
 * INGRESS: codepoint -> traffic class (additive, classification)
 * EGRESS: traffic class -> codepoint (rewrite)
 */
enum class QosMapDirection {
  INGRESS,
  EGRESS,
};

/**
 * Custom type for QoS map entry configuration.
 *
 * Parses command line arguments in the format:
 *   For simple maps (tc-to-queue, pfc-pri-to-queue, tc-to-pg, pfc-pri-to-pg):
 *     <map-type> <key> <value>
 *     Example: tc-to-queue 0 0
 *
 *   For DSCP/EXP/DOT1P maps (source -> destination):
 *     dscp <value> tc <tc>           - maps DSCP to traffic class (additive)
 *     traffic-class <tc> dscp <value> - maps traffic class to DSCP
 *     mpls-exp <value> traffic-class <tc> - maps MPLS EXP to traffic class
 *     traffic-class <tc> mpls-exp <value> - maps traffic class to MPLS EXP
 *     dot1p <value> traffic-class <tc> - maps DOT1P to traffic class
 *     traffic-class <tc> dot1p <value> - maps traffic class to DOT1P
 */
class QosMapConfig : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ QosMapConfig(std::vector<std::string> v);

  QosMapType getMapType() const {
    return mapType_;
  }

  int16_t getKey() const {
    return key_;
  }

  int16_t getValue() const {
    return value_;
  }

  /**
   * For DSCP/EXP/DOT1P maps, returns the traffic class.
   */
  int16_t getTrafficClass() const {
    return trafficClass_;
  }

  /**
   * For DSCP/EXP/DOT1P maps, returns the direction (ingress or egress).
   */
  QosMapDirection getDirection() const {
    return direction_;
  }

  /**
   * Returns true if this is a DSCP/EXP/DOT1P map type.
   */
  bool isListMapType() const;

 private:
  void parseListMapType(const std::vector<std::string>& v);

  QosMapType mapType_{QosMapType::TC_TO_QUEUE};
  int16_t key_{0};
  int16_t value_{0};
  // For DSCP/EXP/DOT1P maps
  int16_t trafficClass_{0};
  QosMapDirection direction_{QosMapDirection::INGRESS};
};

struct CmdConfigQosPolicyMapTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigQosPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "map_entry",
        args,
        "<map-type> ... where map-type is one of:\n"
        "  tc-to-queue (traffic class to queue)\n"
        "  pfc-pri-to-queue (PFC priority to queue)\n"
        "  tc-to-pg (traffic class to priority group)\n"
        "  pfc-pri-to-pg (PFC priority to priority group)\n"
        "  dscp (DSCP marking)\n"
        "  mpls-exp (MPLS EXP bits)\n"
        "  dot1p (802.1p priority)\n"
        "  traffic-class");
  }
  using ObjectArgType = QosMapConfig;
  using RetType = std::string;
};

class CmdConfigQosPolicyMap
    : public CmdHandler<CmdConfigQosPolicyMap, CmdConfigQosPolicyMapTraits> {
 public:
  using ObjectArgType = CmdConfigQosPolicyMapTraits::ObjectArgType;
  using RetType = CmdConfigQosPolicyMapTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const QosPolicyName& policyName,
      const ObjectArgType& config);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
