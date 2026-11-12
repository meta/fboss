// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "fboss/agent/test/utils/AgentDropReasonListReporting.h"

#include "fboss/agent/FbossError.h"

namespace facebook::fboss {
namespace {

class XgsDropReasonReportingAdapter : public ListDropReasonReportingAdapter {
 public:
  explicit XgsDropReasonReportingAdapter(DropReasonReportingContext context)
      : ListDropReasonReportingAdapter(std::move(context)) {}

  void verifyOnlyExpectedDropReasonReported(
      DropScenario scenario) const override {
    if (scenario == DropScenario::TtlError ||
        scenario == DropScenario::Ipv4HeaderError ||
        scenario == DropScenario::Ipv6HeaderError ||
        scenario == DropScenario::AclDeny) {
      // The ASIC reports RIPD6 alongside L3_TTL_ERROR. Header errors also
      // report the family-wide IP discard counters.
      // ACL deny requires RFILDR but leaves other reported counters
      // unconstrained.
      return;
    }
    verifyExpectedDropReasons(scenario);
  }

 protected:
  DropReasons expectedDropReasons(DropScenario scenario) const override {
    const auto [stage, reason] = expectedDropReason(scenario);
    DropReasons expected{{stage, {reason}}};
    if (scenario == DropScenario::Ipv4HeaderError) {
      expected[stage].insert("RIPD4");
    } else if (scenario == DropScenario::Ipv6HeaderError) {
      expected[stage].insert("RIPD6");
    }
    return expected;
  }

  DropReasons optionalDropReasons(DropScenario /* scenario */) const override {
    // RDROP is "Port bitmap zero drop condition", which comes up for
    // essentially any dropped packet. It is still reported and logged, but
    // says nothing about which specific reason fired.
    return {{"ingress", {"RDROP"}}, {"egress", {"RDROP"}}};
  }

  std::pair<std::string, std::string> expectedDropReason(
      DropScenario scenario) const override {
    // SAI_PACKET_DROP_TYPE_{INGRESS,EGRESS}_ is stripped from logged names.
    switch (scenario) {
      case DropScenario::L3DstDiscard:
        return {"ingress", "L3_DST_DISCARD"};
      case DropScenario::TtlError:
        return {"ingress", "L3_TTL_ERROR"};
      case DropScenario::MacSaMulticast:
        // A multicast source MAC reports SRC_ROUTE_DROP, not the
        // MACSA_MULTICAST the name suggests. Both enumerators exist; this is
        // the one the ASIC raises.
        return {"ingress", "SRC_ROUTE_DROP"};
      case DropScenario::AclDeny:
        // An ACL deny reports RFILDR, the filtered-on-receive reason, not the
        // IFP one that the ingress field processor's name suggests. Both
        // enumerators exist; this is the one the ASIC raises.
        return {"ingress", "RFILDR"};
      case DropScenario::SrcPortKnockout:
        return {"ingress", "SRC_PORT_KNOCKOUT_DROP"};
      case DropScenario::EgressMtu:
        return {"egress", "EGR_MTU"};
      case DropScenario::Ipv4HeaderError:
      case DropScenario::Ipv6HeaderError:
        // Invalid IP versions report L3_HDR_ERROR with the matching IP discard
        // counter.
        return {"ingress", "L3_HDR_ERROR"};
      case DropScenario::InvalidEtherType:
        break;
    }
    throw FbossError("Unsupported reason-list drop scenario");
  }
};

} // namespace

std::unique_ptr<DropReasonReportingImpl> createListDropReasonReportingImpl(
    DropReasonReportingContext context) {
  return std::make_unique<XgsDropReasonReportingAdapter>(std::move(context));
}

} // namespace facebook::fboss
