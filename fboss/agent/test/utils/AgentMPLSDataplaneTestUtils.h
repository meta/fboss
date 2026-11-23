// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fboss/agent/packet/MPLSHdr.h"
#include "fboss/agent/state/LabelForwardingAction.h"

namespace facebook::fboss::utility::mpls_dataplane_test {

enum class MplsTrapPacketMechanism {
  SrcPortAcl,
  TtlExpiry,
};

const char* name(MplsTrapPacketMechanism mechanism);

LabelForwardingAction::LabelStack makeLabelStack(
    uint32_t baseLabel,
    uint32_t count);
std::vector<uint32_t> expectedWireOrderLabelValues(
    const LabelForwardingAction::LabelStack& expectedPushStack);
std::vector<uint32_t> capturedLabelValues(
    const std::vector<MPLSHdr::Label>& labelStack);
std::string labelValuesStr(const std::vector<uint32_t>& labels);
bool bottomOfStackBitsValid(const std::vector<MPLSHdr::Label>& labelStack);

} // namespace facebook::fboss::utility::mpls_dataplane_test
