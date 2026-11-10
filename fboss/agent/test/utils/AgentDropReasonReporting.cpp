// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "fboss/agent/test/utils/AgentDropReasonReporting.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/test/utils/AgentDropReasonBitmapReporting.h"
#include "fboss/agent/test/utils/AgentDropReasonListReporting.h"

#include <fmt/format.h>

namespace facebook::fboss {
namespace {

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

// Drop bitmaps are READ_AND_CLEAR: each HwAgent stats-collection cycle reads
// and clears the HW bitmap and pushes that one snapshot to the SwSwitch cache
// these tests read, so a single drop is easy to miss — especially post-warmboot
// while the HwAgent->SwSwitch stats sink is re-establishing. Each verify loop
// below re-sends the drop-triggering traffic on every WITH_RETRIES iteration
// (the ~1s collection interval) and OR-accumulates: a miss in one collection
// window is covered by the next, and the first observed drop is latched.
class BitmapDropReasonReportingAdapter : public DropReasonReportingImpl {
 public:
  explicit BitmapDropReasonReportingAdapter(DropReasonReportingContext context)
      : reporting_(std::move(context)) {}

  void prepareDropObservation() override {
    observed_ = {};
    reporting_.waitForDropBitmapsToClear();
  }

  void collectDropObservation() override {
    BitmapDropReasonReportingImpl::orBitmapStats(
        observed_, reporting_.getAggregatedSwitchDropBitmapStats());
  }

  bool expectedDropReasonObserved(DropScenario scenario) const override {
    // NOLINTNEXTLINE(clang-diagnostic-switch-enum): unsupported scenarios.
    switch (scenario) {
      case DropScenario::TtlError:
        return observed_.pipelineIpv6DropBitmap().value_or(0) &
            (1LL << PipelineIpDropBit::kUcTtl);
      case DropScenario::Ipv4HeaderError:
        return observed_.pipelineIpv4DropBitmap().value_or(0) &
            (1LL << PipelineIpv4DropBit::kIpv4NotIpHeaderOk);
      case DropScenario::Ipv6HeaderError:
        return observed_.pipelineIpv6DropBitmap().value_or(0) &
            (1LL << PipelineIpv6DropBit::kIpv6NotIpHeaderOk);
      case DropScenario::MacSaMulticast:
        return observed_.pipelineMacDropBitmap().value_or(0) &
            (1LL << PipelineMacDropBit::kSmacIsMulticast);
      case DropScenario::SrcPortKnockout:
        return observed_.pipelineMacDropBitmap().value_or(0) &
            (1LL << PipelineMacDropBit::kUcLoopbackFilter);
      case DropScenario::InvalidEtherType:
        return observed_.ingressMacDropBitmap().value_or(0) &
            (1LL << IngressMacDropBit::kTypeOutOfRange);
      case DropScenario::EgressMtu:
        return observed_.egressMacDropBitmap().value_or(0) &
            (1LL << EgressMacDropBit::kEgressMtuError);
      default:
        throw FbossError("Unsupported bitmap drop scenario");
    }
  }

  void verifyOnlyExpectedDropReasonReported(
      DropScenario scenario) const override {
    // NOLINTNEXTLINE(clang-diagnostic-switch-enum): unsupported scenarios.
    switch (scenario) {
      case DropScenario::TtlError:
        reporting_.verifyOnlyExpectedDrop(
            observed_,
            DropBitmapField::PIPELINE_IPV6,
            PipelineIpDropBit::kUcTtl,
            "L3 TTL error");
        return;
      case DropScenario::Ipv4HeaderError:
        reporting_.verifyOnlyExpectedDrop(
            observed_,
            DropBitmapField::PIPELINE_IPV4,
            PipelineIpv4DropBit::kIpv4NotIpHeaderOk,
            "IPv4 header error");
        return;
      case DropScenario::Ipv6HeaderError:
        reporting_.verifyOnlyExpectedDrop(
            observed_,
            DropBitmapField::PIPELINE_IPV6,
            PipelineIpv6DropBit::kIpv6NotIpHeaderOk,
            "IPv6 header error");
        return;
      case DropScenario::MacSaMulticast:
        reporting_.verifyOnlyExpectedDrop(
            observed_,
            DropBitmapField::PIPELINE_MAC,
            PipelineMacDropBit::kSmacIsMulticast,
            "Pipeline MAC SMAC multicast");
        return;
      case DropScenario::InvalidEtherType:
        // Ingress MAC: type out of range drop
        // EtherType 0x05E0 is in the reserved range 0x05DD..0x05FF
        // This also triggers PIPELINE_MAC_INVALID_ETHERTYPE — the packet
        // traverses multiple pipeline stages that each flag it independently.
        // We verify the primary drop (ingressMac) without cross-contamination
        // checks since multi-stage drops are expected.
        return;
      case DropScenario::SrcPortKnockout:
        reporting_.verifyOnlyExpectedDrop(
            observed_,
            DropBitmapField::PIPELINE_MAC,
            PipelineMacDropBit::kUcLoopbackFilter,
            "src port knockout");
        return;
      case DropScenario::EgressMtu:
        // Assert only the egress MAC MTU bit (not verifyOnlyExpectedDrop): the
        // oversized routed packet can also trigger collateral drops in the
        // loopback test topology, so an exclusivity check would be flaky.
        return;
      default:
        throw FbossError("Unsupported bitmap drop scenario");
    }
  }

  std::string expectedDropLog(DropScenario scenario) const override {
    // NOLINTNEXTLINE(clang-diagnostic-switch-enum): unsupported scenarios.
    switch (scenario) {
      case DropScenario::TtlError:
        return "DROP reasons pipelineIpv6: (0x100000) PIPELINE_IPV6_UC_TTL";
      case DropScenario::Ipv4HeaderError:
        return "DROP reasons pipelineIpv4: (0x100) PIPELINE_IPV4_NOT_IP_HEADER_OK";
      case DropScenario::Ipv6HeaderError:
        return "DROP reasons pipelineIpv6: (0x80) PIPELINE_IPV6_NOT_IP_HEADER_OK";
      case DropScenario::MacSaMulticast:
        return "DROP reasons pipelineMac: (0x800) PIPELINE_MAC_SMAC_IS_MC";
      case DropScenario::SrcPortKnockout:
        return "DROP reasons pipelineMac: (0x100) PIPELINE_MAC_UC_LOOPBCK_FILTER";
      case DropScenario::InvalidEtherType:
        return "DROP reasons ingressMac: (0x10) INGRESS_MAC_TYPE_OUT_OF_RANGE";
      case DropScenario::EgressMtu:
        return "DROP reasons egressMac: (0x1) EGRESS_MAC_EGRESS_MTU_ERROR";
      default:
        throw FbossError("Unsupported bitmap drop scenario");
    }
  }

  std::string dropReasonToString() const override {
    return fmt::format(
        "buffer=0x{:x} egressMac=0x{:x} egressPipe=0x{:x} host=0x{:x} "
        "ingressMac=0x{:x} pipelineGeneral=0x{:x} pipelineIpv4=0x{:x} "
        "pipelineIpv6=0x{:x} pipelineMac=0x{:x} pipelineMpls=0x{:x} "
        "pipelineTunnel=0x{:x} queue=0x{:x}",
        observed_.bufferDropBitmap().value_or(0),
        observed_.egressMacDropBitmap().value_or(0),
        observed_.egressPipeDropBitmap().value_or(0),
        observed_.hostDropBitmap().value_or(0),
        observed_.ingressMacDropBitmap().value_or(0),
        observed_.pipelineGeneralDropBitmap().value_or(0),
        observed_.pipelineIpv4DropBitmap().value_or(0),
        observed_.pipelineIpv6DropBitmap().value_or(0),
        observed_.pipelineMacDropBitmap().value_or(0),
        observed_.pipelineMplsDropBitmap().value_or(0),
        observed_.pipelineTunnelDropBitmap().value_or(0),
        observed_.queueDropBitmap().value_or(0));
  }

 private:
  using DropBitmapField = BitmapDropReasonReportingImpl::DropBitmapField;

  BitmapDropReasonReportingImpl reporting_;
  HwSwitchDropBitmapStats observed_;
};

} // namespace

std::unique_ptr<DropReasonReportingImpl> createListDropReasonReportingImpl(
    DropReasonReportingContext context) {
  return std::make_unique<ListDropReasonReportingAdapter>(std::move(context));
}

std::unique_ptr<DropReasonReportingImpl> createBitmapDropReasonReportingImpl(
    DropReasonReportingContext context) {
  return std::make_unique<BitmapDropReasonReportingAdapter>(std::move(context));
}

std::unique_ptr<DropReasonReportingImpl> createDropReasonReportingImpl(
    cfg::AsicType type,
    DropReasonReportingContext context) {
  switch (type) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      return createListDropReasonReportingImpl(std::move(context));
    case cfg::AsicType::ASIC_TYPE_CHENAB:
    case cfg::AsicType::ASIC_TYPE_CHENAB2:
      return createBitmapDropReasonReportingImpl(std::move(context));
    case cfg::AsicType::ASIC_TYPE_FAKE:
    case cfg::AsicType::ASIC_TYPE_MOCK:
    case cfg::AsicType::ASIC_TYPE_TRIDENT2:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK3:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
    case cfg::AsicType::ASIC_TYPE_ELBERT_8DD:
    case cfg::AsicType::ASIC_TYPE_EBRO:
    case cfg::AsicType::ASIC_TYPE_GARONNE:
    case cfg::AsicType::ASIC_TYPE_SANDIA_PHY:
    case cfg::AsicType::ASIC_TYPE_JERICHO2:
    case cfg::AsicType::ASIC_TYPE_RAMON:
    case cfg::AsicType::ASIC_TYPE_JERICHO3:
    case cfg::AsicType::ASIC_TYPE_YUBA:
    case cfg::AsicType::ASIC_TYPE_RAMON3:
    case cfg::AsicType::ASIC_TYPE_AGERA3:
    case cfg::AsicType::ASIC_TYPE_G202X:
    case cfg::AsicType::ASIC_TYPE_FAKE_NO_WARMBOOT:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWKULTRA1:
    case cfg::AsicType::ASIC_TYPE_QUMRAN4D:
    case cfg::AsicType::ASIC_TYPE_JERICHO4:
    case cfg::AsicType::ASIC_TYPE_P200:
      throw FbossError(
          "No drop reporting adapter for ASIC ", static_cast<int>(type));
  }
  throw FbossError("Invalid ASIC type ", static_cast<int>(type));
}

} // namespace facebook::fboss
