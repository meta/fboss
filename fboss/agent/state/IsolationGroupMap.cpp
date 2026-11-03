/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/IsolationGroupMap.h"

#include "fboss/agent/state/IsolationGroup.h"
#include "fboss/agent/state/NodeMap-defs.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

MultiSwitchIsolationGroupMap* MultiSwitchIsolationGroupMap::modify(
    std::shared_ptr<SwitchState>* state) {
  return SwitchState::modify<switch_state_tags::isolationGroupMaps>(state);
}

template struct ThriftMapNode<IsolationGroupMap, IsolationGroupMapTraits>;

} // namespace facebook::fboss
