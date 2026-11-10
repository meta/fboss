// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include "fboss/agent/test/utils/AgentDropReasonReporting.h"

namespace facebook::fboss {

// Drop cause bit positions from the Chenab SDK telemetry pipeline.
// These map to sx_tele_drop_cause_*_reason_e enums in sx_tele_auto.h.
enum PipelineIpDropBit : int {
  kUcTtl = 20,
};

enum PipelineIpv4DropBit : int {
  kIpv4NotIpHeaderOk = 8,
};

enum PipelineIpv6DropBit : int {
  kIpv6NotIpHeaderOk = 7,
};

enum PipelineMacDropBit : int {
  kSmacIsMulticast = 11,
};

enum IngressMacDropBit : int {
  kTypeOutOfRange = 4,
};

enum EgressMacDropBit : int {
  kEgressMtuError = 0,
};

class BitmapDropReasonReportingImpl {
 public:
  enum class DropBitmapField {
    INGRESS_MAC,
    EGRESS_PIPE,
    EGRESS_MAC,
    PIPELINE_GENERAL,
    PIPELINE_MAC,
    PIPELINE_MPLS,
    PIPELINE_TUNNEL,
    PIPELINE_IPV4,
    PIPELINE_IPV6,
    BUFFER,
    QUEUE,
    HOST,
  };

  explicit BitmapDropReasonReportingImpl(DropReasonReportingContext context);

  static void orBitmapStats(
      HwSwitchDropBitmapStats& dst,
      const HwSwitchDropBitmapStats& src);

  HwSwitchDropBitmapStats getAggregatedSwitchDropBitmapStats();

  static bool hasAnyDrop(const HwSwitchDropBitmapStats& s);

  void waitForDropBitmapsToClear();

  void verifyOnlyExpectedDrop(
      const HwSwitchDropBitmapStats& stats,
      DropBitmapField expectedField,
      int expectedBit,
      const char* desc) const;

 private:
  DropReasonReportingContext context_;
};

} // namespace facebook::fboss
