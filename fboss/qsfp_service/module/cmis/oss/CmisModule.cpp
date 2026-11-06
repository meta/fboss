// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/qsfp_service/module/cmis/CmisModule.h"

namespace facebook {
namespace fboss {

/*
 * Always return false for vendor-specific.
 */
bool CmisModule::programAppSelInLowPowerMode() const {
  return false;
}

/*
 * Always return the input SNR value for OSS (no correction applied).
 */
double CmisModule::applyRxSnrCorrection(
    uint16_t /* rawValue */,
    double snrValue) const {
  return snrValue;
}

/*
 * Always return false for OSS (no vendor-specific retry).
 */
bool CmisModule::shouldRetryCdbFwInfo() const {
  return false;
}

void CmisModule::setPcsToAmTransparent() {}

} // namespace fboss
} // namespace facebook
