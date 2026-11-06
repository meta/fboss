/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <gtest/gtest.h>
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/types.h"

#include <cstddef>
#include <cstdint>

DECLARE_int32(ecmp_resource_percentage);

namespace facebook::fboss {
class HwAsic;
class SwSwitch;

namespace utility {

constexpr auto oddUcmpWeight = 3;
constexpr auto evenUcmpWeight = 2;

// C(n, k): ceiling on how many width-wide distinct groups n ports supply.
uint64_t numDistinctEcmpGroups(size_t numPorts, size_t groupWidth);

std::vector<std::vector<PortDescriptor>> generateEcmpGroupScale(
    const std::vector<PortDescriptor>& inputs,
    const int maxEcmpGroups,
    const int maxEcmpGroupSize,
    const int minEcmpGroupSize = 2);

std::vector<std::vector<PortDescriptor>> generateEcmpMemberScale(
    const std::vector<PortDescriptor>& inputs,
    const int maxEcmpMembers);

std::vector<std::vector<PortDescriptor>> generateEcmpGroupAndMemberScale(
    const std::vector<PortDescriptor>& inputs,
    const int maxEcmpGroups,
    const int maxEcmpMembers);

std::vector<std::vector<PortDescriptor>> getUcmpMembersAndWeight(
    const std::vector<std::vector<PortDescriptor>>& inputs,
    std::vector<std::vector<NextHopWeight>>& weightsOutput,
    const int maxEcmpMembers,
    const uint32_t maxVariableWidthEcmpSize);

void assignUcmpWeights(
    const std::vector<std::vector<PortDescriptor>>& inputs,
    std::vector<std::vector<NextHopWeight>>& weightsOutput,
    int oddWeight = oddUcmpWeight,
    int evenWeight = evenUcmpWeight);

uint32_t getMaxEcmpGroups(const std::vector<const HwAsic*>& asics);
uint32_t getMaxEcmpMembers(const std::vector<const HwAsic*>& asics);
uint32_t getMaxUcmpMembers(const std::vector<const HwAsic*>& asics);
uint32_t getMaxVariableWidthEcmpSize(const std::vector<const HwAsic*>& asics);
} // namespace utility
} // namespace facebook::fboss
