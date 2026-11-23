// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

class InterfaceVrf;
USE_THRIFT_COW(InterfaceVrf)

class InterfaceVrf
    : public ThriftStructNode<InterfaceVrf, state::InterfaceVrfFields> {
 public:
  using Base = ThriftStructNode<InterfaceVrf, state::InterfaceVrfFields>;

  InterfaceVrf(InterfaceID intf, RouterID vrf) {
    set<switch_state_tags::interfaceId>(static_cast<int32_t>(intf));
    set<switch_state_tags::vrfId>(static_cast<int32_t>(vrf));
  }

  InterfaceID getID() const {
    return InterfaceID(cref<switch_state_tags::interfaceId>()->cref());
  }

  RouterID getVrf() const {
    return RouterID(cref<switch_state_tags::vrfId>()->cref());
  }
  void setVrf(RouterID vrf) {
    set<switch_state_tags::vrfId>(static_cast<int32_t>(vrf));
  }

 private:
  // Inherit the constructors required for clone()
  using Base::Base;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
