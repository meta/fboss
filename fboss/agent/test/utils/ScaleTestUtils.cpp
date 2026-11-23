/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/utils/ScaleTestUtils.h"
#include "fboss/agent/AsicUtils.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"

#include <algorithm>
#include <limits>

namespace facebook::fboss::utility {
const int kMaxEcmpGroups = 5000;

uint32_t getMaxEcmpGroups(const std::vector<const HwAsic*>& asics) {
  auto asic = checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing);
  auto maxEcmpGroups = asic->getMaxEcmpGroups();
  CHECK(maxEcmpGroups.has_value());
  return maxEcmpGroups.value();
}
// TH3/TH4 reserve part of the ECMP member table for SDK defrag (CS00012426319),
// so scale tests target a percentage of the reported max to stay programmable.
constexpr uint32_t kEcmpMemberScaleTestPct = 85;

uint32_t getMaxEcmpMembers(const std::vector<const HwAsic*>& asics) {
  auto asic = checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing);
  auto maxEcmpMembers = asic->getMaxEcmpMembers();
  CHECK(maxEcmpMembers.has_value());
  switch (asic->getAsicType()) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK3:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
      return maxEcmpMembers.value() * kEcmpMemberScaleTestPct / 100;
    default:
      return maxEcmpMembers.value();
  }
}

uint32_t getMaxVariableWidthEcmpSize(const std::vector<const HwAsic*>& asics) {
  auto asic = checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing);
  auto maxVariableWidthEcmpSize = asic->getMaxVariableWidthEcmpSize();
  return maxVariableWidthEcmpSize;
}

uint32_t getMaxUcmpMembers(const std::vector<const HwAsic*>& asics) {
  auto asic = checkSameAndGetAsic(asics, FLAGS_switch_id_for_testing);
  // UCMP members cost 4 member-table entries each on TH4/TH5.
  auto maxUcmpMembers = getMaxEcmpMembers(asics);
  if (asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK4 ||
      asic->getAsicType() == cfg::AsicType::ASIC_TYPE_TOMAHAWK5) {
    return maxUcmpMembers / 4;
  }
  return maxUcmpMembers;
}

// Binomial coefficient C(n, k). Returns 0 when k > n.
uint64_t numDistinctEcmpGroups(size_t numPorts, size_t groupWidth) {
  if (groupWidth > numPorts) {
    return 0;
  }
  size_t k = std::min(groupWidth, numPorts - groupWidth);
  // __int128 intermediate, clamped at uint64_t max to avoid wrapping.
  __int128 result = 1;
  for (size_t i = 0; i < k; ++i) {
    result = result * (numPorts - i) / (i + 1);
    if (result > std::numeric_limits<uint64_t>::max()) {
      return std::numeric_limits<uint64_t>::max();
    }
  }
  return static_cast<uint64_t>(result);
}

// Generate all possible combinations of k selections of the input
// vector.
// taken from fbcode/axon/common/coro_util.h
std::vector<std::vector<PortDescriptor>> genCombinations(
    const std::vector<PortDescriptor>& inputs,
    size_t k,
    size_t max_combinations = kMaxEcmpGroups) {
  size_t n = inputs.size();
  if (k == 0 || k > n) {
    return {};
  }
  std::vector<std::vector<PortDescriptor>> output;
  std::vector<bool> picked(n);
  std::fill(picked.begin(), picked.begin() + k, true);
  do {
    std::vector<PortDescriptor> currentCombination;
    for (size_t idx = 0; idx < n; idx++) {
      if (picked[idx]) {
        currentCombination.push_back(inputs[idx]);
      }
    }
    output.push_back(currentCombination);
  } while (std::prev_permutation(picked.begin(), picked.end()) &&
           output.size() < max_combinations);

  return output;
}

// Generate all possible combinations of k selections of the input starting from
// minGroupSize to inputs.size()
std::vector<std::vector<PortDescriptor>> generateEcmpGroupScale(
    const std::vector<PortDescriptor>& inputs,
    const int maxEcmpGroups,
    const int maxEcmpGroupSize,
    const int minEcmpGroupSize) {
  int groupsGenerated = 0;
  std::vector<std::vector<PortDescriptor>> currCombination;
  std::vector<std::vector<PortDescriptor>> allCombinations;
  for (int i = minEcmpGroupSize; i <= maxEcmpGroupSize; i++) {
    currCombination =
        genCombinations(inputs, i, maxEcmpGroups - groupsGenerated);
    if (currCombination.empty()) {
      // Width i exceeds the port count; wider widths cannot help either.
      break;
    }
    if ((groupsGenerated + currCombination.size()) >= maxEcmpGroups) {
      int remainingGrp = maxEcmpGroups - groupsGenerated;
      allCombinations.insert(
          allCombinations.end(),
          currCombination.begin(),
          currCombination.begin() + remainingGrp);
      break;
    }
    groupsGenerated += currCombination.size();
    allCombinations.insert(
        allCombinations.end(), currCombination.begin(), currCombination.end());
  }
  CHECK_EQ(allCombinations.size(), maxEcmpGroups)
      << "Cannot build " << maxEcmpGroups << " distinct " << minEcmpGroupSize
      << "-" << maxEcmpGroupSize << "-wide groups from " << inputs.size()
      << " ports. C(" << inputs.size() << "," << maxEcmpGroupSize
      << ")=" << numDistinctEcmpGroups(inputs.size(), maxEcmpGroupSize)
      << " is the ceiling.";
  return allCombinations;
}

// Generate all possible combinations of k selections of the input starting from
// inputs.size() to minGroupSize
std::vector<std::vector<PortDescriptor>> generateEcmpMemberScale(
    const std::vector<PortDescriptor>& inputs,
    const int maxEcmpMembers) {
  const int minGroupSize = 2;
  int membersGenerated = 0;
  std::vector<std::vector<PortDescriptor>> currCombination;
  std::vector<std::vector<PortDescriptor>> allCombinations;
  for (int i = inputs.size(); i >= minGroupSize; i--) {
    currCombination = genCombinations(inputs, i);
    // Check if after adding currCombination we would hit maxEcmpMembers
    if ((membersGenerated + currCombination.size() * i) >= maxEcmpMembers) {
      int remainingMem = maxEcmpMembers - membersGenerated;
      int remainingGrp = remainingMem / i;
      allCombinations.insert(
          allCombinations.end(),
          currCombination.begin(),
          currCombination.begin() + remainingGrp);
      if (remainingMem % i > 0) {
        allCombinations.emplace_back(
            inputs.begin(), inputs.begin() + (remainingMem % i));
      }
      membersGenerated += remainingMem;
      break;
    }
    allCombinations.insert(
        allCombinations.end(), currCombination.begin(), currCombination.end());
    membersGenerated += currCombination.size() * i;
  }
  EXPECT_EQ(membersGenerated, maxEcmpMembers);
  return allCombinations;
}

std::vector<std::vector<PortDescriptor>> generateEcmpGroupAndMemberScale(
    const std::vector<PortDescriptor>& inputs,
    const int maxEcmpGroups,
    const int maxEcmpMembers) {
  int membersPerGroup =
      std::min((int)(maxEcmpMembers / maxEcmpGroups), (int)inputs.size());
  return generateEcmpGroupScale(
      inputs, maxEcmpGroups, membersPerGroup, membersPerGroup);
}

// Create weightsOutputs where sum of weights {2,3,2,3,2,3} = maxEcmpMembers
// and return the corresponding ecmp members which are subset of inputs
std::vector<std::vector<PortDescriptor>> getUcmpMembersAndWeight(
    const std::vector<std::vector<PortDescriptor>>& inputs,
    std::vector<std::vector<NextHopWeight>>& weightsOutput,
    const int maxEcmpMembers,
    const uint32_t maxVariableWidthEcmpSize) {
  int runningWeight = 0;
  std::vector<std::vector<PortDescriptor>> output;
  for (int i = 0; i < inputs.size(); i++) {
    std::vector<NextHopWeight> weightsTemp;
    std::vector<PortDescriptor> outputTemp;
    int groupWeight = 0;

    for (int j = 0; j < inputs[i].size(); j++) {
      // Assign weights 3 and 2 to ECMP members.
      int currWeight = (j % 2) ? oddUcmpWeight : evenUcmpWeight;
      if (runningWeight + currWeight > maxEcmpMembers ||
          groupWeight + currWeight > maxVariableWidthEcmpSize) {
        currWeight = 1;
      }

      groupWeight += currWeight;
      runningWeight += currWeight;
      weightsTemp.push_back(currWeight);
      outputTemp.push_back(inputs[i][j]);

      if (runningWeight == maxEcmpMembers) {
        weightsOutput.push_back(std::move(weightsTemp));
        output.push_back(std::move(outputTemp));
        return output;
      }

      if (groupWeight == maxVariableWidthEcmpSize) {
        weightsOutput.push_back(std::move(weightsTemp));
        output.push_back(std::move(outputTemp));
        break;
      }
    }

    if (groupWeight < maxVariableWidthEcmpSize) {
      weightsOutput.push_back(std::move(weightsTemp));
      output.push_back(std::move(outputTemp));
    }
  }
  return output;
}

// Create weightsOutputs {2,3,2,3,2,3} for all the inputs
// Currently used for TH4
void assignUcmpWeights(
    const std::vector<std::vector<PortDescriptor>>& inputs,
    std::vector<std::vector<NextHopWeight>>& weightsOutput,
    int oddWeight,
    int evenWeight) {
  for (int i = 0; i < inputs.size(); i++) {
    std::vector<NextHopWeight> temp;
    for (int j = 0; j < inputs[i].size(); j++) {
      int num = (j % 2) ? oddWeight : evenWeight;
      temp.push_back(num);
    }
    weightsOutput.push_back(std::move(temp));
  }
}
} // namespace facebook::fboss::utility
