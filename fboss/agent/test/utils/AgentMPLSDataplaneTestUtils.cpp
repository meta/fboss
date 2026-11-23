// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/test/utils/AgentMPLSDataplaneTestUtils.h"

#include <fmt/ranges.h>

namespace facebook::fboss::utility::mpls_dataplane_test {

const char* name(MplsTrapPacketMechanism mechanism) {
  switch (mechanism) {
    case MplsTrapPacketMechanism::SrcPortAcl:
      return "src-port-acl";
    case MplsTrapPacketMechanism::TtlExpiry:
      return "ttl-expiry";
  }
}

LabelForwardingAction::LabelStack makeLabelStack(
    uint32_t baseLabel,
    uint32_t count) {
  LabelForwardingAction::LabelStack stack;
  stack.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    stack.push_back(baseLabel + i);
  }
  return stack;
}

std::vector<uint32_t> expectedWireOrderLabelValues(
    const LabelForwardingAction::LabelStack& expectedPushStack) {
  std::vector<uint32_t> labels;
  labels.reserve(expectedPushStack.size());
  // LabelForwardingAction push stack is programmed inner-to-outer, while the
  // captured MPLS header stack is parsed outer-to-inner from the wire.
  for (auto itr = expectedPushStack.rbegin(); itr != expectedPushStack.rend();
       ++itr) {
    labels.push_back(static_cast<uint32_t>(*itr));
  }
  return labels;
}

std::vector<uint32_t> capturedLabelValues(
    const std::vector<MPLSHdr::Label>& labelStack) {
  std::vector<uint32_t> labels;
  labels.reserve(labelStack.size());
  for (const auto& label : labelStack) {
    labels.push_back(label.getLabelValue());
  }
  return labels;
}

std::string labelValuesStr(const std::vector<uint32_t>& labels) {
  return fmt::format("[{}]", fmt::join(labels, ", "));
}

bool bottomOfStackBitsValid(const std::vector<MPLSHdr::Label>& labelStack) {
  for (size_t i = 0; i < labelStack.size(); ++i) {
    if (labelStack[i].isbottomOfStack() != (i + 1 == labelStack.size())) {
      return false;
    }
  }
  return true;
}

} // namespace facebook::fboss::utility::mpls_dataplane_test
