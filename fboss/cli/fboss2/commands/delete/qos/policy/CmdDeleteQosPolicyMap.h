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
#include "fboss/cli/fboss2/commands/delete/qos/policy/CmdDeleteQosPolicy.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

/**
 * Which QosMap a delete targets — every map type CmdConfigQosPolicyMap can
 * write is removable, so config and delete cannot drift apart.
 */
enum class DeleteQosMapType {
  DSCP, // QosMap.dscpMaps
  MPLS_EXP, // QosMap.expMaps
  DOT1P, // QosMap.pcpMaps
  TC_TO_QUEUE, // QosMap.trafficClassToQueueId
  PFC_PRI_TO_QUEUE, // QosMap.pfcPriorityToQueueId
  TC_TO_PG, // QosMap.trafficClassToPgId
  PFC_PRI_TO_PG, // QosMap.pfcPriorityToPgId
};

/**
 * Parses the entry to remove:
 *
 *   dscp <dscp-value>       - drop a DSCP codepoint from the policy's dscpMaps
 *   mpls-exp <exp>          - drop an EXP codepoint from expMaps
 *   dot1p <pcp>             - drop a PCP codepoint from pcpMaps
 *   tc-to-queue <tc>        - drop a traffic class from trafficClassToQueueId
 *   pfc-pri-to-queue <pri>  - drop a PFC priority from pfcPriorityToQueueId
 *   tc-to-pg <tc>           - drop a traffic class from trafficClassToPgId
 *   pfc-pri-to-pg <pri>     - drop a PFC priority from pfcPriorityToPgId
 *
 * The value alone identifies the entry: a codepoint appears in at most one
 * ingress map entry, and the rest are map keys.
 */
class DeleteQosMapEntry : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ DeleteQosMapEntry(std::vector<std::string> v);

  DeleteQosMapType getMapType() const {
    return mapType_;
  }

  int16_t getKey() const {
    return key_;
  }

 private:
  DeleteQosMapType mapType_{DeleteQosMapType::TC_TO_QUEUE};
  int16_t key_{0};
};

struct CmdDeleteQosPolicyMapTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdDeleteQosPolicy;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
           "map_entry",
           args,
           "<map-type> <value> where map-type is one of:\n"
           "  dscp <dscp-value>       remove a DSCP to traffic-class mapping\n"
           "  mpls-exp <exp>          remove an EXP to traffic-class mapping\n"
           "  dot1p <pcp>             remove a PCP to traffic-class mapping\n"
           "  tc-to-queue <tc>        remove a traffic-class to queue mapping\n"
           "  pfc-pri-to-queue <pri>  remove a PFC priority to queue mapping\n"
           "  tc-to-pg <tc>           remove a traffic-class to PG mapping\n"
           "  pfc-pri-to-pg <pri>     remove a PFC priority to PG mapping")
        ->required()
        ->expected(2);
  }
  using ObjectArgType = DeleteQosMapEntry;
  using RetType = std::string;
};

class CmdDeleteQosPolicyMap
    : public CmdHandler<CmdDeleteQosPolicyMap, CmdDeleteQosPolicyMapTraits> {
 public:
  using ObjectArgType = CmdDeleteQosPolicyMapTraits::ObjectArgType;
  using RetType = CmdDeleteQosPolicyMapTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const QosPolicyName& policyName,
      const ObjectArgType& entry);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
