/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/bcm/BcmCosQueueFBConvertors.h"
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/agent/FbossError.h"

#include <boost/container/flat_map.hpp>

namespace {

using facebook::fboss::cfg::QueueCongestionBehavior;
static const boost::container::flat_map<QueueCongestionBehavior, int>
    kCosqAqmBehaviorToFlags = {
        {QueueCongestionBehavior::EARLY_DROP, BCM_COSQ_DISCARD_TCP},
        {QueueCongestionBehavior::ECN,
         (BCM_COSQ_DISCARD_MARK_CONGESTION | BCM_COSQ_DISCARD_ECT_MARKED)}};
} // unnamed namespace

namespace facebook::fboss::utility {
bcm_cosq_control_drop_limit_alpha_value_t cfgAlphaToBcmAlpha(
    cfg::MMUScalingFactor cfgAlpha) {
  switch (cfgAlpha) {
    case cfg::MMUScalingFactor::ONE_128TH:
      return bcmCosqControlDropLimitAlpha_1_128;
    case cfg::MMUScalingFactor::ONE_64TH:
      return bcmCosqControlDropLimitAlpha_1_64;
    case cfg::MMUScalingFactor::ONE_32TH:
      return bcmCosqControlDropLimitAlpha_1_32;
    case cfg::MMUScalingFactor::ONE_16TH:
      return bcmCosqControlDropLimitAlpha_1_16;
    case cfg::MMUScalingFactor::ONE_8TH:
      return bcmCosqControlDropLimitAlpha_1_8;
    case cfg::MMUScalingFactor::ONE_QUARTER:
      return bcmCosqControlDropLimitAlpha_1_4;
    case cfg::MMUScalingFactor::ONE_HALF:
      return bcmCosqControlDropLimitAlpha_1_2;
    case cfg::MMUScalingFactor::ONE:
      return bcmCosqControlDropLimitAlpha_1;
    case cfg::MMUScalingFactor::TWO:
      return bcmCosqControlDropLimitAlpha_2;
    case cfg::MMUScalingFactor::FOUR:
      return bcmCosqControlDropLimitAlpha_4;
    case cfg::MMUScalingFactor::EIGHT:
      return bcmCosqControlDropLimitAlpha_8;
    default:
      // should return in one of the cases
      throw FbossError(
          "Unknown config MMUScalingFactor: ",
          apache::thrift::util::enumNameSafe(cfgAlpha));
      ;
  }
}

cfg::MMUScalingFactor bcmAlphaToCfgAlpha(
    bcm_cosq_control_drop_limit_alpha_value_t bcmAlpha) {
  switch (bcmAlpha) {
    case bcmCosqControlDropLimitAlpha_1_128:
      return cfg::MMUScalingFactor::ONE_128TH;
    case bcmCosqControlDropLimitAlpha_1_64:
      return cfg::MMUScalingFactor::ONE_64TH;
    case bcmCosqControlDropLimitAlpha_1_32:
      return cfg::MMUScalingFactor::ONE_32TH;
    case bcmCosqControlDropLimitAlpha_1_16:
      return cfg::MMUScalingFactor::ONE_16TH;
    case bcmCosqControlDropLimitAlpha_1_8:
      return cfg::MMUScalingFactor::ONE_8TH;
    case bcmCosqControlDropLimitAlpha_1_4:
      return cfg::MMUScalingFactor::ONE_QUARTER;
    case bcmCosqControlDropLimitAlpha_1_2:
      return cfg::MMUScalingFactor::ONE_HALF;
    case bcmCosqControlDropLimitAlpha_1:
      return cfg::MMUScalingFactor::ONE;
    case bcmCosqControlDropLimitAlpha_2:
      return cfg::MMUScalingFactor::TWO;
    case bcmCosqControlDropLimitAlpha_4:
      return cfg::MMUScalingFactor::FOUR;
    case bcmCosqControlDropLimitAlpha_8:
      return cfg::MMUScalingFactor::EIGHT;
    default:
      // should return in one of the cases
      throw FbossError("Unknown bcm MMUScalingFactor: ", bcmAlpha);
  }
}

cfg::QueueCongestionBehavior cfgQuenBehaviorFromBcmAqm(
    const bcm_cosq_gport_discard_t& discard) {
  for (const auto& behaviorToFlags : kCosqAqmBehaviorToFlags) {
    if (discard.flags & behaviorToFlags.second) {
      return behaviorToFlags.first;
    }
  }

  throw FbossError(
      "Can't recognize queue congestion behavior from flags: ", discard.flags);
}

bcm_cosq_gport_discard_t initBcmCosqDiscard(
    cfg::QueueCongestionBehavior behavior) {
  bcm_cosq_gport_discard_t discard;
  bcm_cosq_gport_discard_t_init(&discard);
  discard.flags |=
      (BCM_COSQ_DISCARD_BYTES | BCM_COSQ_DISCARD_COLOR_GREEN |
       kCosqAqmBehaviorToFlags.at(behavior));
  return discard;
}

bcm_cosq_gport_discard_t cfgAqmToBcmAqm(
    cfg::QueueCongestionBehavior behavior,
    std::optional<cfg::QueueCongestionDetection> detection,
    const cfg::ActiveQueueManagement& defaultAqm) {
  bcm_cosq_gport_discard_t discard = initBcmCosqDiscard(behavior);

  // if detection is set, we enable discard
  if (detection) {
    discard.flags |= BCM_COSQ_DISCARD_ENABLE;
    switch (detection.value().getType()) {
      case cfg::QueueCongestionDetection::Type::linear:
        discard.min_thresh =
            *detection.value().linear().as_const()->minimumLength();
        discard.max_thresh =
            *detection.value().linear().as_const()->maximumLength();
        discard.drop_probability = folly::copy(
            detection.value().linear().as_const()->probability().value());
        break;
      case cfg::QueueCongestionDetection::Type::__EMPTY__:
        LOG(WARNING) << "Invalid queue congestion detection config";
        break;
    }
  } else {
    auto defaultDetection = defaultAqm.detection()->linear().value();
    // reset the threshold to default
    discard.min_thresh = *defaultDetection.minimumLength();
    discard.max_thresh = *defaultDetection.maximumLength();
  }

  return discard;
}

std::optional<cfg::ActiveQueueManagement> bcmAqmToCfgAqm(
    const bcm_cosq_gport_discard_t& discard,
    const cfg::ActiveQueueManagement& defaultAqm) {
  auto defaultDetection = defaultAqm.detection()->linear().value();
  bool isMinThreshSet = discard.min_thresh &&
      discard.min_thresh != *defaultDetection.minimumLength();
  bool isMaxThreshSet = discard.max_thresh &&
      discard.max_thresh != *defaultDetection.maximumLength();
  // Profile isn't enabled or thresholds aren't set, skip this profile
  if (!(discard.flags & BCM_COSQ_DISCARD_ENABLE) ||
      !(isMinThreshSet && isMaxThreshSet)) {
    return std::nullopt;
  }

  cfg::ActiveQueueManagement aqm;
  cfg::QueueCongestionDetection detection;
  cfg::LinearQueueCongestionDetection linear;
  auto behavior = cfgQuenBehaviorFromBcmAqm(discard);
  *linear.minimumLength() = discard.min_thresh;
  *linear.maximumLength() = discard.max_thresh;
  linear.probability() = discard.drop_probability;
  detection.linear() = linear;
  aqm.detection() = detection;
  aqm.behavior() = behavior;
  return aqm;
}

BcmSchedulingAndWeight cfgSchedulingAndWeightToBcm(
    const CfgSchedulingAndWeight& pair) {
  int weight;
  if (pair.first == cfg::QueueScheduling::STRICT_PRIORITY) {
    weight = BCM_COSQ_WEIGHT_STRICT; // 0
  } else if (pair.first == cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN) {
    weight = pair.second;
  } else if (pair.first == cfg::QueueScheduling::DEFICIT_ROUND_ROBIN) {
    weight = pair.second;
  } else {
    throw FbossError("Unsupported cosQ scheduling mode: ", pair.first);
  }
  // we always program round_robin to BCM
  return std::make_pair(BCM_COSQ_WEIGHTED_ROUND_ROBIN, weight);
}

CfgSchedulingAndWeight bcmSchedulingAndWeightToCfg(
    const BcmSchedulingAndWeight& pair) {
  cfg::QueueScheduling scheduling;
  int weight;
  if (pair.first == BCM_COSQ_STRICT ||
      (pair.first == BCM_COSQ_WEIGHTED_ROUND_ROBIN && pair.second == 0) ||
      (pair.first == BCM_COSQ_DEFICIT_ROUND_ROBIN && pair.second == 0)) {
    scheduling = cfg::QueueScheduling::STRICT_PRIORITY;
    weight = 1;
  } else if (pair.first == BCM_COSQ_WEIGHTED_ROUND_ROBIN) {
    scheduling = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
    weight = pair.second;
  } else if (pair.first == BCM_COSQ_DEFICIT_ROUND_ROBIN) {
    scheduling = cfg::QueueScheduling::DEFICIT_ROUND_ROBIN;
    weight = pair.second;
  } else {
    throw FbossError("Unknown cosQ scheduling mode: ", pair.first);
  }
  return std::make_pair(scheduling, weight);
}

} // namespace facebook::fboss::utility
