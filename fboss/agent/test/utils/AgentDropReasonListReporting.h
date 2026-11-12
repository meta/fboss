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
