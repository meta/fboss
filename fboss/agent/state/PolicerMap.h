// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <map>
#include <memory>
#include <string>

#include "fboss/agent/state/Policer.h"
#include "fboss/agent/state/Thrifty.h"

namespace facebook::fboss {

using PolicerMapTypeClass = apache::thrift::type_class::map<
    apache::thrift::type_class::string,
    apache::thrift::type_class::structure>;
using PolicerMapThriftType = std::map<std::string, cfg::Policer>;

class PolicerMap;
using PolicerMapTraits = ThriftMapNodeTraits<
    PolicerMap,
    PolicerMapTypeClass,
    PolicerMapThriftType,
    Policer>;

class PolicerMap : public ThriftMapNode<PolicerMap, PolicerMapTraits> {
 public:
  using Traits = PolicerMapTraits;
  using BaseT = ThriftMapNode<PolicerMap, PolicerMapTraits>;

  PolicerMap() = default;

 private:
  using BaseT::BaseT;
  friend class CloneAllocator;
};

using MultiSwitchPolicerMapTypeClass = apache::thrift::type_class::
    map<apache::thrift::type_class::string, PolicerMapTypeClass>;
using MultiSwitchPolicerMapThriftType =
    std::map<std::string, PolicerMapThriftType>;

class MultiSwitchPolicerMap;
using MultiSwitchPolicerMapTraits = ThriftMultiSwitchMapNodeTraits<
    MultiSwitchPolicerMap,
    MultiSwitchPolicerMapTypeClass,
    MultiSwitchPolicerMapThriftType,
    PolicerMap>;

class MultiSwitchPolicerMap : public ThriftMultiSwitchMapNode<
                                  MultiSwitchPolicerMap,
                                  MultiSwitchPolicerMapTraits> {
 public:
  using Traits = MultiSwitchPolicerMapTraits;
  using BaseT = ThriftMultiSwitchMapNode<
      MultiSwitchPolicerMap,
      MultiSwitchPolicerMapTraits>;
  using BaseT::modify;

  MultiSwitchPolicerMap* modify(std::shared_ptr<SwitchState>* state);

 private:
  using BaseT::BaseT;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
