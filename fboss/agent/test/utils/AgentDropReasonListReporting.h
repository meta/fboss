// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include "fboss/agent/test/utils/AgentDropReasonReporting.h"

#include <set>
#include <string_view>
#include <utility>

namespace facebook::fboss {

class ListDropReasonReportingImpl {
 public:
  using DropReasons = std::map<std::string, std::set<std::string>>;

  explicit ListDropReasonReportingImpl(DropReasonReportingContext context);

  static void accumulate(DropReasons& dst, const DropReasons& src);

  static void parseReasons(
      const std::string& message,
      std::string_view marker,
      std::set<std::string>& out);

  DropReasons getAggregatedDropReasons();

  void drainDropReasons();

  static bool expectedDropReasonsObserved(
      const DropReasons& reasons,
      const DropReasons& expected);

  static std::string toString(const DropReasons& reasons);

  void verifyOnlyExpectedReason(
      const DropReasons& reasons,
      const DropReasons& expected,
      const DropReasons& optional,
      const char* desc) const;

 private:
  DropReasonReportingContext context_;
};

// Reasons are read back from the HwAgent log rather than from stats: they are
// decoded and logged in the process that owns the ASIC and never cross into
// SwSwitch. installLogCapture() resets the capture window, so each test starts
// from a clean slate, and the window then accumulates every reason the ASIC
// reported. The lists are cleared on read, so the hardware tests re-send drop
// triggering traffic on every WITH_RETRIES iteration -- a drop missed in one
// collection window is covered by the next.
class ListDropReasonReportingAdapter : public DropReasonReportingImpl {
 public:
  explicit ListDropReasonReportingAdapter(DropReasonReportingContext context)
      : reporting_(std::move(context)) {}

  void prepareDropObservation() override {
    reasons_.clear();
  }

  void collectDropObservation() override {
    ListDropReasonReportingImpl::accumulate(
        reasons_, reporting_.getAggregatedDropReasons());
  }

  bool expectedDropReasonObserved(DropScenario scenario) const override {
    return ListDropReasonReportingImpl::expectedDropReasonsObserved(
        reasons_, expectedDropReasons(scenario));
  }

  std::string expectedDropLog(DropScenario scenario) const override {
    return "DROP reasons " + expectedDropReason(scenario).first;
  }

  std::string dropReasonToString() const override {
    return ListDropReasonReportingImpl::toString(reasons_);
  }

 protected:
  using DropReasons = ListDropReasonReportingImpl::DropReasons;

  void verifyExpectedDropReasons(DropScenario scenario) const {
    reporting_.verifyOnlyExpectedReason(
        reasons_,
        expectedDropReasons(scenario),
        optionalDropReasons(scenario),
        "Drop reason test");
  }

  virtual DropReasons expectedDropReasons(DropScenario scenario) const = 0;
  virtual DropReasons optionalDropReasons(DropScenario scenario) const = 0;
  virtual std::pair<std::string, std::string> expectedDropReason(
      DropScenario scenario) const = 0;

  ListDropReasonReportingImpl reporting_;

 private:
  DropReasons reasons_;
};

} // namespace facebook::fboss
