// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/multiswitch_ctrl_types.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace facebook::fboss {

enum class DropScenario {
  L3DstDiscard,
  TtlError,
  MacSaMulticast,
  AclDeny,
  EgressMtu,
  SrcPortKnockout,
  Ipv4LoopbackDip,
  Ipv6LoopbackDip,
  InvalidEtherType,
};

// Callbacks keep packet setup and HwAgent access in the hardware fixture.
// The adapters also accept synthetic observations in unit tests.
struct DropReasonReportingContext {
  std::function<std::vector<std::string>()> readLogMessages;
  std::function<std::map<uint16_t, multiswitch::HwSwitchStats>()>
      readSwitchStats;
  std::vector<uint16_t> switchIndices;
};

class DropReasonReportingImpl {
 public:
  virtual ~DropReasonReportingImpl() = default;
  // Reset accumulated observations and drain stale hardware state if needed.
  virtual void prepareDropObservation() = 0;
  virtual void collectDropObservation() = 0;
  virtual bool expectedDropReasonObserved(DropScenario scenario) const = 0;
  virtual void verifyOnlyExpectedDropReasonReported(
      DropScenario scenario) const = 0;
  virtual std::string expectedDropLog(DropScenario scenario) const = 0;
  virtual std::string dropReasonToString() const = 0;
};

std::unique_ptr<DropReasonReportingImpl> createDropReasonReportingImpl(
    cfg::AsicType type,
    DropReasonReportingContext context);
std::unique_ptr<DropReasonReportingImpl> createListDropReasonReportingImpl(
    DropReasonReportingContext context);
std::unique_ptr<DropReasonReportingImpl> createBitmapDropReasonReportingImpl(
    DropReasonReportingContext context);

} // namespace facebook::fboss
