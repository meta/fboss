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

#include "fboss/agent/L2Entry.h"

#include <memory>
#include <optional>

namespace facebook::fboss {

class Port;
class SwSwitch;
class SwitchState;

class MacTableManager {
 public:
  explicit MacTableManager(SwSwitch* sw);

  void handleL2LearningUpdate(
      L2Entry l2Entry,
      L2EntryUpdateType l2EntryUpdateType);
  bool isHwUpdateProtected();

 private:
  // Forbidden copy constructor and assignment operator
  MacTableManager(MacTableManager const&) = delete;
  MacTableManager& operator=(MacTableManager const&) = delete;

  static std::shared_ptr<SwitchState> learnMac(
      SwSwitch* sw,
      const std::shared_ptr<SwitchState>& state,
      const L2Entry& l2Entry);

  SwSwitch* sw_{nullptr};
};

} // namespace facebook::fboss
