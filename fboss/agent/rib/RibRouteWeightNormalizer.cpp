/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/rib/RibRouteWeightNormalizer.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

RibRouteWeightNormalizer::RibRouteWeightNormalizer(
    int numRacks,
    int numPlanePathsPerRack,
    int rackId,
    int numSpineFailuresToSkip,
    int spinePruneStepCount,
    bool enableFpfCapacityPruning)
    : numRacks_(numRacks),
      numPlanePathsPerRack_(numPlanePathsPerRack),
      rackId_(rackId),
      numSpineFailuresToSkip_(numSpineFailuresToSkip),
      spinePruneStepCount_(spinePruneStepCount),
      enableFpfCapacityPruning_(enableFpfCapacityPruning),
      pruneLookupTable_(
          numRacks * numPlanePathsPerRack + 1,
          std::vector<std::vector<int>>(
              numRacks_,
              std::vector<int>(numRacks_, 0))) {
  /*
    initialize a lookup table to determine how many paths to prune for each
    dst/src rack for N spine plane failures. The lookup table is a 3D array
    with the first dimension being the number of failures. The prunes are
    distributed evenly across the destination racks as illustrated in the below
    table

  Pruning at pod 2 rtsws for  1 spine plane failure
  +-----------+-----------+-----------+-----------+-----------+----------+
  |  rtsw1.p2 | rtsw2.p2  | rtsw3.p2  | rtsw4.p2  | rtsw5.p2  | rtsw6.p2 |
  +-----------+-----------+-----------+-----------+-----------+----------+
  |rtsw1.p1 1 |rtsw1.p1 0 |rtsw1.p1 0 |rtsw1.p1 0 |rtsw1.p1 0 |rtsw1.p1 0|
  |rtsw2.p1 0 |rtsw2.p1 1 |rtsw2.p1 0 |rtsw2.p1 0 |rtsw2.p1 0 |rtsw2.p1 0|
  |rtsw3.p1 0 |rtsw3.p1 0 |rtsw3.p1 1 |rtsw3.p1 0 |rtsw3.p1 0 |rtsw3.p1 0|
  |rtsw4.p1 0 |rtsw4.p1 0 |rtsw4.p1 0 |rtsw4.p1 1 |rtsw4.p1 0 |rtsw4.p1 0|
  |rtsw5.p1 0 |rtsw5.p1 0 |rtsw5.p1 0 |rtsw5.p1 0 |rtsw5.p1 1 |rtsw5.p1 0|
  |rtsw6.p1 0 |rtsw6.p1 0 |rtsw6.p1 0 |rtsw6.p1 0 |rtsw6.p1 0 |rtsw6.p1 1|
  +-----------+-----------+-----------+-----------+-----------+----------+

  Pruning at pod 2 rtsws for  2 spine plane failures
  +-----------+-----------+-----------+-----------+-----------+----------+
  |  rtsw1.p2 | rtsw2.p2  | rtsw3.p2  | rtsw4.p2  | rtsw5.p2  | rtsw6.p2 |
  +-----------+-----------+-----------+-----------+-----------+----------+
  |rtsw1.p1 1 |rtsw1.p1 0 |rtsw1.p1 0 |rtsw1.p1 0 |rtsw1.p1 0 |rtsw1.p1 1|
  |rtsw2.p1 1 |rtsw2.p1 1 |rtsw2.p1 0 |rtsw2.p1 0 |rtsw2.p1 0 |rtsw2.p1 0|
  |rtsw3.p1 0 |rtsw3.p1 1 |rtsw3.p1 1 |rtsw3.p1 0 |rtsw3.p1 0 |rtsw3.p1 0|
  |rtsw4.p1 0 |rtsw4.p1 0 |rtsw4.p1 1 |rtsw4.p1 1 |rtsw4.p1 0 |rtsw4.p1 0|
  |rtsw5.p1 0 |rtsw5.p1 0 |rtsw5.p1 0 |rtsw5.p1 1 |rtsw5.p1 1 |rtsw5.p1 0|
  |rtsw6.p1 0 |rtsw6.p1 0 |rtsw6.p1 0 |rtsw6.p1 0 |rtsw6.p1 1 |rtsw6.p1 1|
  +-----------+-----------+-----------+-----------+-----------+----------+

  Pruning at pod 2 rtsws for  7 spine plane failures
  +-----------+-----------+-----------+-----------+-----------+----------+
  |  rtsw1.p2 | rtsw2.p2  | rtsw3.p2  | rtsw4.p2  | rtsw5.p2  | rtsw6.p2 |
  +-----------+-----------+-----------+-----------+-----------+----------+
  |rtsw1.p1 2 |rtsw1.p1 1 |rtsw1.p1 1 |rtsw1.p1 1 |rtsw1.p1 1 |rtsw1.p1 1|
  |rtsw2.p1 1 |rtsw2.p1 2 |rtsw2.p1 1 |rtsw2.p1 1 |rtsw2.p1 1 |rtsw2.p1 1|
  |rtsw3.p1 1 |rtsw3.p1 1 |rtsw3.p1 2 |rtsw3.p1 1 |rtsw3.p1 1 |rtsw3.p1 1|
  |rtsw4.p1 1 |rtsw4.p1 1 |rtsw4.p1 1 |rtsw4.p1 2 |rtsw4.p1 1 |rtsw4.p1 1|
  |rtsw5.p1 1 |rtsw5.p1 1 |rtsw5.p1 1 |rtsw5.p1 1 |rtsw5.p1 2 |rtsw5.p1 1|
  |rtsw6.p1 1 |rtsw6.p1 1 |rtsw6.p1 1 |rtsw6.p1 1 |rtsw6.p1 1 |rtsw6.p1 2|
  +-----------+-----------+-----------+-----------+-----------+----------+
  */
  int numPathsToPrune = 1;
  for (int failureCount = 0; failureCount <= numRacks * numPlanePathsPerRack;
       failureCount++) {
    if (failureCount == 0) {
      // no paths to prune for zero failures
      continue;
    }
    pruneLookupTable_[failureCount] = pruneLookupTable_[failureCount - 1];
    // add one more destination for each source rack id to prune for the new
    // failure
    for (int srcRackId = 0; srcRackId < numRacks; srcRackId++) {
      int impactedDestRackId = (srcRackId + failureCount - 1) % numRacks;
      pruneLookupTable_[failureCount][impactedDestRackId][srcRackId] =
          numPathsToPrune;
    }
    if (failureCount % numRacks == 0) {
      numPathsToPrune++;
    }
  }
}

int RibRouteWeightNormalizer::getNumPathsToPrune(
    int numFailures,
    RackId dstRack,
    RackId srcRack) {
  // Rack ids are offset 1 based. RackId is a signed int; reject non-positive
  // values explicitly so an attacker-supplied negative rack_id cannot index
  // pruneLookupTable_ out of bounds below.
  if (dstRack <= 0 || dstRack > numRacks_) {
    throw FbossError("invalid dst rack id ", dstRack);
  }
  if (srcRack <= 0 || srcRack > numRacks_) {
    throw FbossError("invalid src rack id ", srcRack);
  }
  if (numFailures < 0 || numFailures > numRacks_ * numPlanePathsPerRack_) {
    throw FbossError("invalid number of failures ", numFailures);
  }
  return pruneLookupTable_[numFailures][dstRack - 1][srcRack - 1];
}

RouteNextHopSet RibRouteWeightNormalizer::getNormalizedNexthops(
    RouteNextHopSet& nhops) {
  RouteNextHopSet normalizedNexthops;
  std::vector<ResolvedNextHop> resolvedNexthops;
  for (const auto& nhop : nhops) {
    resolvedNexthops.emplace_back(
        nhop.addr(),
        nhop.intf(),
        nhop.weight(),
        nhop.labelForwardingAction(),
        nhop.disableTTLDecrement(),
        nhop.topologyInfo(),
        nhop.adjustedWeight(),
        nhop.srv6SegmentList(),
        nhop.tunnelType(),
        nhop.tunnelId(),
        nhop.cost(),
        nhop.role(),
        nhop.overlayDstMac());
  }
  normalizeWeightsForNexthops(resolvedNexthops);
  for (auto& nhop : resolvedNexthops) {
    normalizedNexthops.insert(std::move(nhop));
  }
  return normalizedNexthops;
}

int RibRouteWeightNormalizer::quantizeToStep(int rawPrunes) const {
  if (rawPrunes <= 0) {
    return 0;
  }
  int stepGroup = (rawPrunes - 1) / spinePruneStepCount_;
  return stepGroup * spinePruneStepCount_ + 1;
}

void RibRouteWeightNormalizer::normalizeWeightsForNexthops(
    std::vector<ResolvedNextHop>& nhs) {
  if (enableFpfCapacityPruning_) {
    normalizeWeightsForNexthopsForFpf(nhs);
  } else {
    normalizeWeightsForNexthopsForNsf(nhs);
  }
}

void RibRouteWeightNormalizer::normalizeWeightsForNexthopsForNsf(
    std::vector<ResolvedNextHop>& nhs) {
  RackId dstRack;
  std::unordered_map<PlaneId, int> localPlaneCapacity;
  // plane id to rack and spine failures
  std::unordered_map<PlaneId, std::pair<int, int>> planeIdToFailures;
  bool hasFailure = false;
  // walk through the nexthops and collect the connectivity information
  for (const auto& nh : nhs) {
    // ignore prefixes that not originated by racks
    if (!nh.topologyInfo().has_value() ||
        !nh.topologyInfo()->rack_id().has_value()) {
      return;
    }
    dstRack = nh.topologyInfo()->rack_id().value();
    int numRackFailures = 0;
    NetworkTopologyInformation topologyInfo = nh.topologyInfo().value();
    // For remote pod prefixes, check rack failures
    if (topologyInfo.remote_rack_capacity() &&
        *topologyInfo.remote_rack_capacity()) {
      numRackFailures =
          numPlanePathsPerRack_ - *topologyInfo.remote_rack_capacity();
    } else if (
        topologyInfo.local_rack_capacity() &&
        *topologyInfo.local_rack_capacity()) {
      // rack failures for local pod prefixes are handled same as remote pod
      // rack failures
      numRackFailures =
          numPlanePathsPerRack_ - *topologyInfo.local_rack_capacity();
    }
    int numSpineFailures = 0;
    if (topologyInfo.spine_capacity() && *topologyInfo.spine_capacity()) {
      // total spine capaity and rack capacity in pod should be matching
      numSpineFailures = std::max(
          0,
          numRacks_ * numPlanePathsPerRack_ - *topologyInfo.spine_capacity() -
              numSpineFailuresToSkip_);
      // spines can absorb some capacity loss, prune only in step increments
      numSpineFailures = quantizeToStep(numSpineFailures);
    }
    if (numRackFailures || numSpineFailures) {
      hasFailure = true;
    }
    if (!topologyInfo.plane_id().has_value()) {
      throw FbossError("plane id not set in topology info");
    }
    auto planeId = PlaneId(*topologyInfo.plane_id());
    planeIdToFailures.insert(
        std::make_pair(
            planeId, std::make_pair(numRackFailures, numSpineFailures)));

    // update local capacity information for each plane
    auto localPlaneInfo = localPlaneCapacity.find(planeId);
    if (localPlaneInfo == localPlaneCapacity.end()) {
      localPlaneCapacity.insert(std::make_pair(planeId, 1));
    } else {
      localPlaneInfo->second++;
    }
  }

  if (!hasFailure) {
    return;
  }
  // walk through the nexthops and normalize the weights
  for (const auto& planeFailures : planeIdToFailures) {
    auto planeId = planeFailures.first;
    auto rackFailures = planeFailures.second.first;
    auto spineFailures = planeFailures.second.second;
    auto localPlaneInfo = localPlaneCapacity.find(planeId);
    if (localPlaneInfo == localPlaneCapacity.end()) {
      throw FbossError("Invalid plane id ", planeId);
    }
    auto numLocalFailures = numPlanePathsPerRack_ - localPlaneInfo->second;

    // Determine how many paths to prune for spine failures for the src/dst pair
    auto numSpineFailuresToAct =
        getNumPathsToPrune(spineFailures, dstRack, rackId_);

    // Total prunes needed is the max of rack and spine failures to act on
    // after discounting for the  local failures
    auto numPrunesNeeded = std::max(
        std::max(rackFailures, numSpineFailuresToAct) - numLocalFailures, 0);
    XLOG(DBG4) << "Pruning " << numPrunesNeeded << " paths for plane "
               << planeId << " dst rack " << dstRack << " src rack " << rackId_
               << " remote rack failures " << rackFailures << " spine failures "
               << spineFailures << " local failures " << numLocalFailures;
    if (numPrunesNeeded) {
      for (auto& nh : nhs) {
        if (numPrunesNeeded && nh.topologyInfo().has_value() &&
            nh.topologyInfo()->plane_id().has_value() &&
            *nh.topologyInfo()->plane_id() == planeId) {
          numPrunesNeeded--;
          // set adjusted weight to 0 to indicate that path is pruned
          nh.setAdjustedWeight(0);
        }
      }
    }
    if (numPrunesNeeded) {
      throw FbossError("Invalid number of prunes needed ", numPrunesNeeded);
    }
  }
}

void RibRouteWeightNormalizer::normalizeWeightsForNexthopsForFpf(
    std::vector<ResolvedNextHop>& nhs) {
  // In FPF, pruning is scoped per STSW (spine_id). The number of nexthops
  // toward an STSW is the local GTSW->STSW path count; remote_rack_capacity
  // carries the STSW->remote GTSW path count. Prune local paths that exceed the
  // remote capacity so the STSW is not oversubscribed.
  std::unordered_map<int, int> stswIdToLocalPathCount;
  std::unordered_map<int, int> stswIdToRemoteCapacity;
  for (const auto& nh : nhs) {
    // ignore prefixes that do not carry FPF spine topology information
    const auto& topologyInfo = nh.topologyInfo();
    if (!topologyInfo.has_value() || !topologyInfo->spine_id().has_value()) {
      return;
    }
    auto stswId = *topologyInfo->spine_id();
    stswIdToLocalPathCount[stswId]++;
    // remote capacity is identical across all nexthops of an STSW
    if (topologyInfo->remote_rack_capacity().has_value()) {
      stswIdToRemoteCapacity[stswId] = *topologyInfo->remote_rack_capacity();
    }
  }

  for (const auto& [stswId, localPathCount] : stswIdToLocalPathCount) {
    auto remoteInfo = stswIdToRemoteCapacity.find(stswId);
    if (remoteInfo == stswIdToRemoteCapacity.end()) {
      continue;
    }
    // spines can absorb some capacity loss, prune only in step increments
    auto numPrunesNeeded = quantizeToStep(
        localPathCount - remoteInfo->second - numSpineFailuresToSkip_);
    XLOG(DBG4) << "Pruning " << numPrunesNeeded << " paths for spine "
               << stswId;
    for (auto& nh : nhs) {
      if (!numPrunesNeeded) {
        break;
      }
      if (nh.topologyInfo().has_value() &&
          nh.topologyInfo()->spine_id().has_value() &&
          *nh.topologyInfo()->spine_id() == stswId) {
        // set adjusted weight to 0 to indicate that path is pruned
        nh.setAdjustedWeight(0);
        numPrunesNeeded--;
      }
    }
    // by construction numPrunesNeeded <= localPathCount, so the loop above
    // should always satisfy it; surface any mismatch (e.g. from an upstream
    // bug) rather than swallow it, mirroring the NSF path.
    if (numPrunesNeeded) {
      throw FbossError("Invalid number of prunes needed ", numPrunesNeeded);
    }
  }
}
} // namespace facebook::fboss
