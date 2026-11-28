/*
 * Copyright (c) 2004-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "fboss/qsfp_service/module/cmis/CmisFieldInfo.h"

/*
 * Parse transceiver data fields, as outlined in
 * Common Management Interface Specification by SFF commitee at
 * http://www.qsfp-dd.com/wp-content/uploads/2019/05/QSFP-DD-CMIS-rev4p0.pdf
 */

namespace facebook {
namespace fboss {

double CmisFieldInfo::getTemp(const uint16_t temp) {
  double data;
  data = temp / 256.0;
  if (data >= 128) {
    data = data - 256;
  }
  return data;
}

double CmisFieldInfo::getVcc(const uint16_t temp) {
  double data;
  data = temp / 10000.0;
  return data;
}

double CmisFieldInfo::getTxBias(const uint16_t temp) {
  double data;
  data = temp * 2.0 / 1000;
  return data;
}

double CmisFieldInfo::getPwr(const uint16_t temp) {
  double data;
  data = temp * 0.1 / 1000;
  return data;
}

double CmisFieldInfo::getSnr(const uint16_t data) {
  double snr;
  snr = data / 256.0;
  return snr;
}

double CmisFieldInfo::getPreCursor(const uint8_t data) {
  double pre;
  pre = (data > 7) ? 0 : data * 0.5;
  return pre;
}

double CmisFieldInfo::getPostCursor(const uint8_t data) {
  double post;
  post = (data > 7) ? 0 : data;
  return post;
}

std::pair<uint32_t, uint32_t> CmisFieldInfo::getAmplitude(const uint8_t data) {
  std::vector<std::pair<uint32_t, uint32_t>> rxAmp = {
      {100, 400}, {300, 600}, {400, 800}, {600, 1200}};
  if (data > 3) {
    return std::pair<uint32_t, uint32_t>(0, 0);
  } else {
    return rxAmp[data];
  }
}

FeatureState CmisFieldInfo::getFeatureState(
    const uint8_t support,
    const uint8_t enabled) {
  if (!support) {
    return FeatureState::UNSUPPORTED;
  }
  return enabled ? FeatureState::ENABLED : FeatureState::DISABLED;
}

uint8_t CmisFieldInfo::getTxBiasMultiplier(const uint8_t data) {
  uint8_t multiplier = (data & FieldMasks::TX_BIAS_MULTIPLIER_MASK) >> 3;
  switch (multiplier) {
    case 0:
      return 1;
    case 1:
      return 2;
    case 2:
      return 4;
    default:
      return 1;
  }
}
} // namespace fboss
} // namespace facebook
