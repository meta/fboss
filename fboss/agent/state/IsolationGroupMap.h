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
#include "fboss/agent/state/IsolationGroup.h"
#include "fboss/agent/state/NodeMap.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

using IsolationGroupMapTypeClass = apache::thrift::type_class::map<
    apache::thrift::type_class::string,
    apache::thrift::type_class::structure>;
using IsolationGroupMapThriftType =
    std::map<std::string, state::IsolationGroupFields>;

class IsolationGroupMap;
using IsolationGroupMapTraits = ThriftMapNodeTraits<
    IsolationGroupMap,
    IsolationGroupMapTypeClass,
    IsolationGroupMapThriftType,
    IsolationGroup>;

/*
 * A container for named isolation groups.
 */
class IsolationGroupMap
    : public ThriftMapNode<IsolationGroupMap, IsolationGroupMapTraits> {
 public:
  using Base = ThriftMapNode<IsolationGroupMap, IsolationGroupMapTraits>;
  using Traits = IsolationGroupMapTraits;
  IsolationGroupMap() = default;
  virtual ~IsolationGroupMap() = default;

 private:
  // Inherit the constructors required for clone()
  using Base::Base;
  friend class CloneAllocator;
};

using MultiSwitchIsolationGroupMapTypeClass = apache::thrift::type_class::
    map<apache::thrift::type_class::string, IsolationGroupMapTypeClass>;
using MultiSwitchIsolationGroupMapThriftType =
    std::map<std::string, IsolationGroupMapThriftType>;

class MultiSwitchIsolationGroupMap;

using MultiSwitchIsolationGroupMapTraits = ThriftMultiSwitchMapNodeTraits<
    MultiSwitchIsolationGroupMap,
    MultiSwitchIsolationGroupMapTypeClass,
    MultiSwitchIsolationGroupMapThriftType,
    IsolationGroupMap>;

class HwSwitchMatcher;

class MultiSwitchIsolationGroupMap : public ThriftMultiSwitchMapNode<
                                         MultiSwitchIsolationGroupMap,
                                         MultiSwitchIsolationGroupMapTraits> {
 public:
  using Traits = MultiSwitchIsolationGroupMapTraits;
  using BaseT = ThriftMultiSwitchMapNode<
      MultiSwitchIsolationGroupMap,
      MultiSwitchIsolationGroupMapTraits>;
  using BaseT::modify;

  MultiSwitchIsolationGroupMap() = default;
  virtual ~MultiSwitchIsolationGroupMap() = default;

  MultiSwitchIsolationGroupMap* modify(std::shared_ptr<SwitchState>* state);

 private:
  // Inherit the constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
