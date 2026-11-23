// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "fboss/agent/test/utils/AgentDropReasonListReporting.h"

#include "fboss/agent/FbossError.h"

#include <gtest/gtest.h>

namespace facebook::fboss {
namespace {

// Only expected names and accompanying reasons differ for categorized IDs.
// Parsing and observation use the same list implementation as XGS.
class TajoDropReasonReportingAdapter : public ListDropReasonReportingAdapter {
 public:
  explicit TajoDropReasonReportingAdapter(DropReasonReportingContext context)
      : ListDropReasonReportingAdapter(std::move(context)) {}

  void prepareDropObservation() override {
    ASSERT_NO_FATAL_FAILURE(reporting_.drainDropReasons());
    ListDropReasonReportingAdapter::prepareDropObservation();
  }

  void verifyOnlyExpectedDropReasonReported(
      DropScenario scenario) const override {
    verifyExpectedDropReasons(scenario);
  }

 protected:
  DropReasons expectedDropReasons(DropScenario scenario) const override {
    const auto [stage, reason] = expectedDropReason(scenario);
    DropReasons expected{{stage, {reason}}};
    if (stage == "NPU_INGRESS") {
      // The ingress cases also report this aggregate NPU drop indicator.
      expected["TM"].insert("RX_NPU_DROP");
    } else if (scenario == DropScenario::SrcPortKnockout) {
      expected["TM"].insert("RXPDR_MC_EMPTY_GROUP");
    }
    return expected;
  }

  DropReasons optionalDropReasons(DropScenario scenario) const override {
    if (scenario == DropScenario::SrcPortKnockout) {
      // Learning also reported a drop during the warm-boot knockout run.
      return {{"NPU_INGRESS", {"ETHERNET_LEARN_DROP"}}};
    }
    if (scenario == DropScenario::EgressMtu) {
      // These diagnostic counters accompanied the MTU drop on GR200.
      return {
          {"DIAGNOSTIC_MONITOR",
           {"counters.slice[].ifg.ifgbi.tx.protection.DroppedPackets",
            "counters.slice[].ifg.ifgbi.tx.protection.DroppedWords",
            "counters.slice[].tx.ifc.ifg0.DroppedPackets",
            "counters.slice[].tx.ifc.pif[].DebugDroppedBytes",
            "counters.slice[].tx.ifc.pif[].DebugDroppedPackets",
            "counters.slice[].tx.ifc.pif[].DroppedBytes",
            "counters.slice[].tx.ifc.pif[].DroppedPackets"}}};
    }
    return {};
  }

  std::pair<std::string, std::string> expectedDropReason(
      DropScenario scenario) const override {
    switch (scenario) {
      case DropScenario::AclDeny:
        return {"NPU_INGRESS", "L3_ACL_DROP"};
      case DropScenario::EgressMtu:
        return {"FORWARDING", "MTU"};
      case DropScenario::L3DstDiscard:
        return {"NPU_INGRESS", "L3_DROP_ADJ"};
      case DropScenario::TtlError:
        return {"NPU_INGRESS", "L3_TTL_IS_ONE_RX"};
      case DropScenario::MacSaMulticast:
        return {"NPU_INGRESS", "ETHERNET_SA_MULTICAST"};
      case DropScenario::SrcPortKnockout:
        return {"NPU_EGRESS", "ETHERNET_SAME_INTERFACE"};
      case DropScenario::Ipv4HeaderError:
        return {"NPU_INGRESS", "IPV4_HEADER_ERROR"};
      case DropScenario::Ipv6HeaderError:
        return {"NPU_INGRESS", "IPV6_HEADER_ERROR"};
      case DropScenario::InvalidEtherType:
        return {"NPU_INGRESS", "ETHERNET_UNKNOWN_L3"};
    }

    throw FbossError("Invalid drop scenario");
  }
};

} // namespace

std::unique_ptr<DropReasonReportingImpl> createTajoDropReasonReportingImpl(
    DropReasonReportingContext context) {
  return std::make_unique<TajoDropReasonReportingAdapter>(std::move(context));
}

} // namespace facebook::fboss
