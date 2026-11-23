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

#include "fboss/agent/FbossError.h"

#include "fboss/agent/hw/bcm/BcmSdkVer.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>

extern "C" {
#include <bcm/error.h>
#include <shared/error.h>

#if (defined(IS_OPENNSA))
#define BCM_WARM_BOOT_SUPPORT
#include <soc/opensoc.h>
#else
#include <soc/error.h>
#endif
}

namespace facebook::fboss {

/**
 * A class for errors from the Broadcom SDK.
 */
class BcmError : public FbossError {
 public:
  template <typename... Args>
  BcmError(int err, Args&&... args)
      : FbossError(std::forward<Args>(args)..., ": ", bcm_errmsg(err)),
        err_(static_cast<bcm_error_t>(err)) {}

  template <typename... Args>
  BcmError(bcm_error_t err, Args&&... args)
      : FbossError(std::forward<Args>(args)..., ": ", bcm_errmsg(err)),
        err_(err) {}

  template <typename... Args>
  BcmError(soc_error_t err, Args&&... args)
      : FbossError(std::forward<Args>(args)..., ": ", bcm_errmsg(err)),
        // Fortunately bcm_error_t and soc_error_t both use the exact same
        // enum values, so we don't bother distinguishing them.
        err_(static_cast<bcm_error_t>(err)) {}

  ~BcmError() noexcept override = default;
  BcmError(const BcmError&) = default;
  BcmError& operator=(const BcmError&) = default;
  BcmError(BcmError&&) noexcept = default;
  BcmError& operator=(BcmError&&) noexcept = default;

  bcm_error_t getBcmError() const {
    return err_;
  }

 private:
  bcm_error_t err_;
};

template <typename... Args>
void bcmLogError(int err, Args&&... msgArgs) {
  if (BCM_FAILURE(err)) {
    XLOG(ERR) << folly::to<std::string>(std::forward<Args>(msgArgs)...) << ": "
              << bcm_errmsg(err) << ", " << err;
  }
}

#define BCM_CHECK_ERROR(...) \
  bcmCheckError(__VA_ARGS__, " at ", __FILE__, ":", __LINE__)

template <typename... Args>
void bcmCheckError(int err, Args&&... msgArgs) {
  if (BCM_FAILURE(err)) {
    // log the error before throwing in case the noexcept also has an error
    bcmLogError(err, std::forward<Args>(msgArgs)...);
    throw BcmError(err, std::forward<Args>(msgArgs)...);
  }
}

template <typename... Args>
void bcmLogFatal(int err, const BcmSwitchIf* hw, Args&&... msgArgs) {
  if (BCM_FAILURE(err)) {
    auto errMsg = fmt::format(
        "{}: {}, {}",
        folly::to<std::string>(std::forward<Args>(msgArgs)...),
        bcm_errmsg(err),
        err);

    // Make sure we log the error message, in case hw->exitFatal throws.
    XLOG(ERR) << errMsg;
    hw->exitFatal();
    XLOG(FATAL) << errMsg;
  }
}

} // namespace facebook::fboss
