/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/state/ClassBasedPolicyNode.h"
#include "fboss/agent/state/NodeBase-defs.h"

namespace facebook::fboss {

ClassBasedPolicyNode::ClassBasedPolicyNode(const std::string& name) {
  set<switch_state_tags::name>(name);
}

ClassBasedPolicyNode::ClassBasedPolicyNode(
    const std::string& name,
    const state::NamedNextHopGroupAndID& defaultNextHopGroup,
    const std::map<ForwardingClass, state::NamedNextHopGroupAndID>&
        class2NextHopGroup,
    bool referenced) {
  set<switch_state_tags::name>(name);
  set<switch_state_tags::defaultNextHopGroup>(defaultNextHopGroup);
  set<switch_state_tags::class2NextHopGroup>(class2NextHopGroup);
  set<switch_state_tags::referenced>(referenced);
}

template struct ThriftStructNode<
    ClassBasedPolicyNode,
    state::ClassBasedPolicyFields>;

} // namespace facebook::fboss
