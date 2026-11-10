// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include "fboss/agent/test/utils/AgentDropReasonReporting.h"

#include <set>
#include <string_view>

namespace facebook::fboss {

// Reason names as they appear in the log: the
// SAI_PACKET_DROP_TYPE_{INGRESS,EGRESS}_ prefix is stripped at decode.
constexpr auto kL3DstDiscard = "L3_DST_DISCARD";
constexpr auto kL3TtlError = "L3_TTL_ERROR";
// A multicast source MAC reports SRC_ROUTE_DROP, not the MACSA_MULTICAST the
// name suggests. Both enumerators exist; this is the one the ASIC raises.
constexpr auto kSrcRouteDrop = "SRC_ROUTE_DROP";
// An ACL deny reports RFILDR, the filtered-on-receive reason, not the IFP one
// that the ingress field processor's name suggests. Both enumerators exist;
// this is the one the ASIC raises.
constexpr auto kRxFilterDrop = "RFILDR";
constexpr auto kEgrMtu = "EGR_MTU";
constexpr auto kSrcPortKnockout = "SRC_PORT_KNOCKOUT_DROP";

class ListDropReasonReportingImpl {
 public:
  struct DropReasons {
    std::set<std::string> ingress;
    std::set<std::string> egress;
  };

  enum class DropStage { Ingress, Egress };

  explicit ListDropReasonReportingImpl(DropReasonReportingContext context);

  static void accumulate(DropReasons& dst, const DropReasons& src);

  static void parseReasons(
      const std::string& message,
      std::string_view marker,
      std::set<std::string>& out);

  DropReasons getAggregatedDropReasons();

  static std::string toString(const DropReasons& reasons);

  void verifyOnlyExpectedReason(
      const DropReasons& reasons,
      DropStage dropStage,
      const std::string& expected,
      const char* desc) const;

 private:
  DropReasonReportingContext context_;
};

} // namespace facebook::fboss
