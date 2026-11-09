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

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/Thrifty.h"

#include <map>
#include <string>

namespace facebook::fboss {

USE_THRIFT_COW(ClassBasedPolicyNode);

class ClassBasedPolicyNode : public ThriftStructNode<
                                 ClassBasedPolicyNode,
                                 state::ClassBasedPolicyFields> {
 public:
  using BaseT =
      ThriftStructNode<ClassBasedPolicyNode, state::ClassBasedPolicyFields>;
  using BaseT::modify;

  explicit ClassBasedPolicyNode(const std::string& name);
  ClassBasedPolicyNode(
      const std::string& name,
      const state::NamedNextHopGroupAndID& defaultNextHopGroup,
      const std::map<ForwardingClass, state::NamedNextHopGroupAndID>&
          class2NextHopGroup,
      bool referenced);

  const std::string& getID() const {
    return cref<switch_state_tags::name>()->cref();
  }

  state::NamedNextHopGroupAndID getDefaultNextHopGroup() const {
    return cref<switch_state_tags::defaultNextHopGroup>()->toThrift();
  }

  void setDefaultNextHopGroup(
      const state::NamedNextHopGroupAndID& defaultNextHopGroup) {
    set<switch_state_tags::defaultNextHopGroup>(defaultNextHopGroup);
  }

  std::map<ForwardingClass, state::NamedNextHopGroupAndID>
  getClass2NextHopGroup() const {
    return cref<switch_state_tags::class2NextHopGroup>()->toThrift();
  }

  void setClass2NextHopGroup(
      const std::map<ForwardingClass, state::NamedNextHopGroupAndID>&
          class2NextHopGroup) {
    set<switch_state_tags::class2NextHopGroup>(class2NextHopGroup);
  }

  bool isReferenced() const {
    return cref<switch_state_tags::referenced>()->cref();
  }

  void setReferenced(bool referenced) {
    set<switch_state_tags::referenced>(referenced);
  }

 private:
  using BaseT::BaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
