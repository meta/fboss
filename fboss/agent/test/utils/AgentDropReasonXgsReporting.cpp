// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "fboss/agent/test/utils/AgentDropReasonListReporting.h"

#include "fboss/agent/FbossError.h"

namespace facebook::fboss {
namespace {

// Reason names as they appear in the log: the
// SAI_PACKET_DROP_TYPE_{INGRESS,EGRESS}_ prefix is stripped at decode.
constexpr auto kL3DstDiscard = "L3_DST_DISCARD";
constexpr auto kL3TtlError = "L3_TTL_ERROR";
// Invalid IP versions report L3_HDR_ERROR with the matching IP discard counter.
constexpr auto kL3HdrError = "L3_HDR_ERROR";
constexpr auto kIpv4Discards = "RIPD4";
constexpr auto kIpv6Discards = "RIPD6";
// A multicast source MAC reports SRC_ROUTE_DROP, not the MACSA_MULTICAST the
// name suggests. Both enumerators exist; this is the one the ASIC raises.
constexpr auto kSrcRouteDrop = "SRC_ROUTE_DROP";
// An ACL deny reports RFILDR, the filtered-on-receive reason, not the IFP one
// that the ingress field processor's name suggests. Both enumerators exist;
// this is the one the ASIC raises.
constexpr auto kRxFilterDrop = "RFILDR";
constexpr auto kEgrMtu = "EGR_MTU";
constexpr auto kSrcPortKnockout = "SRC_PORT_KNOCKOUT_DROP";

// Reasons are read back from the HwAgent log rather than from stats: they are
// decoded and logged in the process that owns the ASIC and never cross into
// SwSwitch. installLogCapture() resets the capture window, so each test starts
// from a clean slate, and the window then accumulates every reason the ASIC
// reported. The lists are cleared on read, so each verify loop below re-sends
// the drop triggering traffic on every WITH_RETRIES iteration -- a drop missed
// in one collection window is covered by the next.
class ListDropReasonReportingAdapter : public DropReasonReportingImpl {
 public:
  explicit ListDropReasonReportingAdapter(DropReasonReportingContext context)
      : reporting_(std::move(context)) {}

  void prepareDropObservation() override {
    reasons_ = {};
  }

  void collectDropObservation() override {
    ListDropReasonReportingImpl::accumulate(
        reasons_, reporting_.getAggregatedDropReasons());
  }

  bool expectedDropReasonObserved(DropScenario scenario) const override {
    // NOLINTNEXTLINE(clang-diagnostic-switch-enum): unsupported scenarios.
    switch (scenario) {
      case DropScenario::L3DstDiscard:
        return reasons_.ingress.contains(kL3DstDiscard);
      case DropScenario::TtlError:
        return reasons_.ingress.contains(kL3TtlError);
      case DropScenario::Ipv4HeaderError:
        return reasons_.ingress.contains(kL3HdrError) &&
            reasons_.ingress.contains(kIpv4Discards);
      case DropScenario::Ipv6HeaderError:
        return reasons_.ingress.contains(kL3HdrError) &&
            reasons_.ingress.contains(kIpv6Discards);
      case DropScenario::MacSaMulticast:
        return reasons_.ingress.contains(kSrcRouteDrop);
      case DropScenario::AclDeny:
        return reasons_.ingress.contains(kRxFilterDrop);
      case DropScenario::EgressMtu:
        return reasons_.egress.contains(kEgrMtu);
      case DropScenario::SrcPortKnockout:
        return reasons_.ingress.contains(kSrcPortKnockout);
      default:
        throw FbossError("Unsupported reason-list drop scenario");
    }
  }

  void verifyOnlyExpectedDropReasonReported(
      DropScenario scenario) const override {
    // NOLINTNEXTLINE(clang-diagnostic-switch-enum): unsupported scenarios.
    switch (scenario) {
      case DropScenario::L3DstDiscard:
        reporting_.verifyOnlyExpectedReason(
            reasons_, DropStage::Ingress, kL3DstDiscard, "L3 DST discard");
        return;
      case DropScenario::MacSaMulticast:
        reporting_.verifyOnlyExpectedReason(
            reasons_, DropStage::Ingress, kSrcRouteDrop, "MACSA multicast");
        return;
      case DropScenario::EgressMtu:
        reporting_.verifyOnlyExpectedReason(
            reasons_, DropStage::Egress, kEgrMtu, "egress MTU");
        return;
      case DropScenario::SrcPortKnockout:
        reporting_.verifyOnlyExpectedReason(
            reasons_,
            DropStage::Ingress,
            kSrcPortKnockout,
            "src port knockout");
        return;
      // Deliberately no verifyOnlyExpectedReason() here, unlike its siblings:
      // the ASIC reports RIPD6 alongside L3_TTL_ERROR, and RIPD6 is a real
      // specific reason rather than a generic one, so an exclusivity check
      // would always fail.
      case DropScenario::TtlError:
        return;
      // Header errors also report the family-wide IP discard counters.
      case DropScenario::Ipv4HeaderError:
      case DropScenario::Ipv6HeaderError:
      case DropScenario::AclDeny:
        return;
      default:
        throw FbossError("Unsupported reason-list drop scenario");
    }
  }

  std::string expectedDropLog(DropScenario scenario) const override {
    // TODO: Include the expected reason along with the stage prefix in a
    // follow-up diff so list and bitmap implementations both check the stage
    // and reason in the same log message.
    switch (scenario) {
      case DropScenario::L3DstDiscard:
      case DropScenario::TtlError:
      case DropScenario::MacSaMulticast:
      case DropScenario::AclDeny:
      case DropScenario::SrcPortKnockout:
      case DropScenario::Ipv4HeaderError:
      case DropScenario::Ipv6HeaderError:
        return "DROP reasons ingress";
      case DropScenario::EgressMtu:
        return "DROP reasons egress";
      case DropScenario::InvalidEtherType:
        break;
    }
    throw FbossError("Unsupported reason-list drop scenario");
  }

  std::string dropReasonToString() const override {
    return ListDropReasonReportingImpl::toString(reasons_);
  }

 private:
  using DropStage = ListDropReasonReportingImpl::DropStage;

  ListDropReasonReportingImpl reporting_;
  ListDropReasonReportingImpl::DropReasons reasons_;
};

} // namespace

std::unique_ptr<DropReasonReportingImpl> createListDropReasonReportingImpl(
    DropReasonReportingContext context) {
  return std::make_unique<ListDropReasonReportingAdapter>(std::move(context));
}

} // namespace facebook::fboss
