/*
 * Copyright (c) 2004-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "fboss/qsfp_service/module/sff/SffFieldInfo.h"

/*
 * Parse transceiver data fields, as outlined in various documents
 * by the Small Form Factor (SFF) committee, including SFP and QSFP+
 * modules.
 */

namespace facebook {
namespace fboss {

double SffFieldInfo::getTemp(const uint16_t temp) {
  double data;
  data = temp / 256.0;
  if (data >= 128) {
    data = data - 256;
  }
  return data;
}

double SffFieldInfo::getVcc(const uint16_t temp) {
  double data;
  data = temp / 10000.0;
  return data;
}

double SffFieldInfo::getTxBias(const uint16_t temp) {
  double data;
  data = temp * 2.0 / 1000;
  return data;
}

double SffFieldInfo::getPwr(const uint16_t temp) {
  double data;
  data = temp * 0.1 / 1000;
  return data;
}

FeatureState SffFieldInfo::getFeatureState(
    const uint8_t support,
    const uint8_t enabled) {
  if (!support) {
    return FeatureState::UNSUPPORTED;
  }
  return enabled ? FeatureState::ENABLED : FeatureState::DISABLED;
}

} // namespace fboss
} // namespace facebook
