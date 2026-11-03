// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeMap.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/state/Vrf.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

using VrfMapTypeClass = apache::thrift::type_class::map<
    apache::thrift::type_class::integral,
    apache::thrift::type_class::structure>;
using VrfMapThriftType = std::map<int32_t, state::VrfFields>;

class VrfMap;
using VrfMapTraits =
    ThriftMapNodeTraits<VrfMap, VrfMapTypeClass, VrfMapThriftType, Vrf>;

class VrfMap : public ThriftMapNode<VrfMap, VrfMapTraits> {
 public:
  using Base = ThriftMapNode<VrfMap, VrfMapTraits>;
  using Traits = VrfMapTraits;
  VrfMap() = default;
  virtual ~VrfMap() = default;

 private:
  // Inherit the constructors required for clone()
  using Base::Base;
  friend class CloneAllocator;
};

using MultiSwitchVrfMapTypeClass = apache::thrift::type_class::
    map<apache::thrift::type_class::string, VrfMapTypeClass>;
using MultiSwitchVrfMapThriftType = std::map<std::string, VrfMapThriftType>;

class MultiSwitchVrfMap;

using MultiSwitchVrfMapTraits = ThriftMultiSwitchMapNodeTraits<
    MultiSwitchVrfMap,
    MultiSwitchVrfMapTypeClass,
    MultiSwitchVrfMapThriftType,
    VrfMap>;

class HwSwitchMatcher;

class MultiSwitchVrfMap : public ThriftMultiSwitchMapNode<
                              MultiSwitchVrfMap,
                              MultiSwitchVrfMapTraits> {
 public:
  using Traits = MultiSwitchVrfMapTraits;
  using BaseT =
      ThriftMultiSwitchMapNode<MultiSwitchVrfMap, MultiSwitchVrfMapTraits>;
  using BaseT::modify;

  MultiSwitchVrfMap() = default;
  virtual ~MultiSwitchVrfMap() = default;

  MultiSwitchVrfMap* modify(std::shared_ptr<SwitchState>* state);

 private:
  // Inherit the constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
