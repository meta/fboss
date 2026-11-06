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
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

#include <set>

namespace facebook::fboss {

class IsolationGroup;
USE_THRIFT_COW(IsolationGroup)

/*
 * A named set of ports that traffic must not reach. Referenced per-port by
 * Port::getIsolationGroup(): packets ingressing a port that names this group
 * are never forwarded to any of its members.
 */
class IsolationGroup
    : public ThriftStructNode<IsolationGroup, state::IsolationGroupFields> {
 public:
  using Base = ThriftStructNode<IsolationGroup, state::IsolationGroupFields>;

  explicit IsolationGroup(const std::string& id) {
    set<switch_state_tags::id>(id);
  }

  const std::string& getID() const {
    return cref<switch_state_tags::id>()->cref();
  }

  cfg::IsolationGroupType getType() const {
    return get<switch_state_tags::type>()->cref();
  }
  void setType(cfg::IsolationGroupType type) {
    set<switch_state_tags::type>(type);
  }

  std::set<int32_t> getMemberPorts() const {
    return get<switch_state_tags::memberPorts>()->toThrift();
  }
  void setMemberPorts(const std::set<int32_t>& memberPorts) {
    set<switch_state_tags::memberPorts>(memberPorts);
  }

 private:
  // Inherit the constructors required for clone()
  using Base::Base;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
