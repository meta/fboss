// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

#include <optional>
#include <string>

namespace facebook::fboss {

class Vrf;
USE_THRIFT_COW(Vrf)

class Vrf : public ThriftStructNode<Vrf, state::VrfFields> {
 public:
  using Base = ThriftStructNode<Vrf, state::VrfFields>;

  explicit Vrf(RouterID id) {
    set<switch_state_tags::vrfId>(static_cast<int32_t>(id));
  }

  RouterID getID() const {
    return RouterID(cref<switch_state_tags::vrfId>()->cref());
  }

  std::optional<std::string> getName() const {
    const auto& name = cref<switch_state_tags::name>();
    if (!name) {
      return std::nullopt;
    }
    return name->cref();
  }
  void setName(const std::optional<std::string>& name) {
    if (!name) {
      ref<switch_state_tags::name>().reset();
      return;
    }
    set<switch_state_tags::name>(*name);
  }

 private:
  // Inherit the constructors required for clone()
  using Base::Base;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
