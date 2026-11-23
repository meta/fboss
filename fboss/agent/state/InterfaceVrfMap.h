// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/InterfaceVrf.h"
#include "fboss/agent/state/NodeMap.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

using InterfaceVrfMapTypeClass = apache::thrift::type_class::map<
    apache::thrift::type_class::integral,
    apache::thrift::type_class::structure>;
using InterfaceVrfMapThriftType = std::map<int32_t, state::InterfaceVrfFields>;

class InterfaceVrfMap;
using InterfaceVrfMapTraits = ThriftMapNodeTraits<
    InterfaceVrfMap,
    InterfaceVrfMapTypeClass,
    InterfaceVrfMapThriftType,
    InterfaceVrf>;

class InterfaceVrfMap
    : public ThriftMapNode<InterfaceVrfMap, InterfaceVrfMapTraits> {
 public:
  using Base = ThriftMapNode<InterfaceVrfMap, InterfaceVrfMapTraits>;
  using Traits = InterfaceVrfMapTraits;
  InterfaceVrfMap() = default;
  virtual ~InterfaceVrfMap() = default;

 private:
  // Inherit the constructors required for clone()
  using Base::Base;
  friend class CloneAllocator;
};

using MultiSwitchInterfaceVrfMapTypeClass = apache::thrift::type_class::
    map<apache::thrift::type_class::string, InterfaceVrfMapTypeClass>;
using MultiSwitchInterfaceVrfMapThriftType =
    std::map<std::string, InterfaceVrfMapThriftType>;

class MultiSwitchInterfaceVrfMap;

using MultiSwitchInterfaceVrfMapTraits = ThriftMultiSwitchMapNodeTraits<
    MultiSwitchInterfaceVrfMap,
    MultiSwitchInterfaceVrfMapTypeClass,
    MultiSwitchInterfaceVrfMapThriftType,
    InterfaceVrfMap>;

class HwSwitchMatcher;

class MultiSwitchInterfaceVrfMap : public ThriftMultiSwitchMapNode<
                                       MultiSwitchInterfaceVrfMap,
                                       MultiSwitchInterfaceVrfMapTraits> {
 public:
  using Traits = MultiSwitchInterfaceVrfMapTraits;
  using BaseT = ThriftMultiSwitchMapNode<
      MultiSwitchInterfaceVrfMap,
      MultiSwitchInterfaceVrfMapTraits>;
  using BaseT::modify;

  MultiSwitchInterfaceVrfMap() = default;
  virtual ~MultiSwitchInterfaceVrfMap() = default;

  MultiSwitchInterfaceVrfMap* modify(std::shared_ptr<SwitchState>* state);

  // VRF of an interface. An interface without an entry is in VRF 0.
  RouterID getVrf(InterfaceID intf) const;

 private:
  // Inherit the constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
