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

#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "fboss/led_service/LedManager.h"

namespace facebook::fboss {

/*
 * LedEnsemble will be the hw_test ensemble class to LedManager. This ensemble
 * will be used in led_service related hw_test to provide all LED related
 * helper function.
 */
class LedEnsemble {
 public:
  void init();
  ~LedEnsemble() {
    if (ledManager_) {
      ledManager_->stop();
    }
  }

  LedManager* getLedManager();
  const LedManager* getLedManager() const {
    return const_cast<LedEnsemble*>(this)->getLedManager();
  }

 private:
  std::unique_ptr<LedManager> ledManager_;
};
} // namespace facebook::fboss
