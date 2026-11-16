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

#include <folly/IPAddress.h>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeMap.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

class Interface;
class SwitchState;

using InterfaceMapTypeClass = apache::thrift::type_class::map<
    apache::thrift::type_class::integral,
    apache::thrift::type_class::structure>;
using InterfaceMapThriftType = std::map<int32_t, state::InterfaceFields>;

class InterfaceMap;
using InterfaceMapTraits = ThriftMapNodeTraits<
    InterfaceMap,
    InterfaceMapTypeClass,
    InterfaceMapThriftType,
    Interface>;
/*
 * A container for the set of INTERFACEs.
 */
class InterfaceMap : public ThriftMapNode<InterfaceMap, InterfaceMapTraits> {
 public:
  using ThriftType = InterfaceMapThriftType;
  using Traits = InterfaceMapTraits;
  using Base = ThriftMapNode<InterfaceMap, InterfaceMapTraits>;
  using Base::modify;

  InterfaceMap() = default;
  ~InterfaceMap() override = default;

  using Interfaces = std::vector<std::shared_ptr<Interface>>;

  /*
   *  Get interface which has the given vlan. If multiple
   *  interfaces exist, this will return the first interface.
   */
  std::shared_ptr<Interface> getInterfaceInVlanIf(VlanID vlan) const;

  /*
   * Same as getInterfaceInVlanIf but throws a execption
   * instead of returning null
   */
  const std::shared_ptr<Interface> getInterfaceInVlan(VlanID vlan) const;

 private:
  // Inherit the constructors required for clone()
  using Base::Base;
  friend class CloneAllocator;
};

using MultiSwitchInterfaceMapTypeClass = apache::thrift::type_class::
    map<apache::thrift::type_class::string, InterfaceMapTypeClass>;
using MultiSwitchInterfaceMapThriftType =
    std::map<std::string, InterfaceMapThriftType>;

class MultiSwitchInterfaceMap;

using MultiSwitchInterfaceMapTraits = ThriftMultiSwitchMapNodeTraits<
    MultiSwitchInterfaceMap,
    MultiSwitchInterfaceMapTypeClass,
    MultiSwitchInterfaceMapThriftType,
    InterfaceMap>;

class HwSwitchMatcher;

class MultiSwitchInterfaceMap : public ThriftMultiSwitchMapNode<
                                    MultiSwitchInterfaceMap,
                                    MultiSwitchInterfaceMapTraits> {
 public:
  using Traits = MultiSwitchInterfaceMapTraits;
  using BaseT = ThriftMultiSwitchMapNode<
      MultiSwitchInterfaceMap,
      MultiSwitchInterfaceMapTraits>;
  using BaseT::modify;

  MultiSwitchInterfaceMap() = default;
  virtual ~MultiSwitchInterfaceMap() = default;
  MultiSwitchInterfaceMap* modify(std::shared_ptr<SwitchState>* state);
  /*
   *  Get interface which has the given vlan. If multiple
   *  interfaces exist, this will return the first interface.
   */
  std::shared_ptr<Interface> getInterfaceInVlanIf(VlanID vlan) const;

  /*
   * Same as getInterfaceInVlanIf but throws a execption
   * instead of returning null
   */
  const std::shared_ptr<Interface> getInterfaceInVlan(VlanID vlan) const;

 private:
  // Inherit the constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};
} // namespace facebook::fboss
