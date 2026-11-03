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

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/sai/api/MyMacApi.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"

#include <memory>
#include <vector>

namespace facebook::fboss {

class SaiStore;

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
using SaiMyMac = SaiObject<SaiMyMacTraits>;
#endif

class SaiMyMacManager {
 public:
  explicit SaiMyMacManager(SaiStore* saiStore);
  // Programs exactly the given MY_MAC entries, removing any others
  void programMyMacs(const std::vector<cfg::MacAndVlan>& myMacs);

 private:
  SaiStore* saiStore_;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  std::vector<std::shared_ptr<SaiMyMac>> myMacs_;
#endif
};

} // namespace facebook::fboss
