// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/test/PhyCapabilities.h"
// NOLINTNEXTLINE(facebook-unused-include-check)
#include "fboss/agent/hw/sai/api/SaiVersion.h"

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

bool rxSignalDetectSupportedInSdk() {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
  return true;
#else
  return false;
#endif
}

bool rxLockStatusSupportedInSdk() {
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
  return true;
#else
  return false;
#endif
}

bool pcsRxLinkStatusSupportedInSdk() {
#if defined(TAJO_SAI_SDK)
  return true;
#endif
  return false;
}

bool fecAlignmentLockSupportedInSdk() {
#if defined(TAJO_SAI_SDK)
  return true;
#endif
  return false;
}

} // namespace facebook::fboss
