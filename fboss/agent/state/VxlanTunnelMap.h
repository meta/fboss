// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/NodeMap.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/state/VxlanTunnel.h"
#include "fboss/agent/types.h"

#include <string>

namespace facebook::fboss {

class SwitchState;

using VxlanTunnelMapLegacyTraits = NodeMapTraits<std::string, VxlanTunnel>;

using VxlanTunnelMapClass = apache::thrift::type_class::map<
    apache::thrift::type_class::string,
    apache::thrift::type_class::structure>;
using VxlanTunnelMapThriftType =
    std::map<std::string, state::VxLanTunnelFields>;

class VxlanTunnelMap;
using VxlanTunnelMapTraits = ThriftMapNodeTraits<
    VxlanTunnelMap,
    VxlanTunnelMapClass,
    VxlanTunnelMapThriftType,
    VxlanTunnel>;
/*
 * A container for all the present VxlanTunnels
 */
class VxlanTunnelMap
    : public ThriftMapNode<VxlanTunnelMap, VxlanTunnelMapTraits> {
 public:
  using Base = ThriftMapNode<VxlanTunnelMap, VxlanTunnelMapTraits>;
  using Traits = VxlanTunnelMapTraits;
  VxlanTunnelMap();
  ~VxlanTunnelMap() override;

  const std::shared_ptr<VxlanTunnel> getTunnel(std::string id) const {
    return getNode(id);
  }
  std::shared_ptr<VxlanTunnel> getTunnelIf(std::string id) const {
    return getNodeIf(id);
  }

  void addTunnel(const std::shared_ptr<VxlanTunnel>& tunnel);
  void updateTunnel(const std::shared_ptr<VxlanTunnel>& tunnel);
  void removeTunnel(std::string id);

 private:
  // Inherit the constructors required for clone()
  using Base::Base;
  friend class CloneAllocator;
};

using MultiSwitchVxlanTunnelMapTypeClass = apache::thrift::type_class::
    map<apache::thrift::type_class::string, VxlanTunnelMapClass>;
using MultiSwitchVxlanTunnelMapThriftType =
    std::map<std::string, VxlanTunnelMapThriftType>;

class MultiSwitchVxlanTunnelMap;

using MultiSwitchVxlanTunnelMapTraits = ThriftMultiSwitchMapNodeTraits<
    MultiSwitchVxlanTunnelMap,
    MultiSwitchVxlanTunnelMapTypeClass,
    MultiSwitchVxlanTunnelMapThriftType,
    VxlanTunnelMap>;

class HwSwitchMatcher;

class MultiSwitchVxlanTunnelMap : public ThriftMultiSwitchMapNode<
                                      MultiSwitchVxlanTunnelMap,
                                      MultiSwitchVxlanTunnelMapTraits> {
 public:
  using Traits = MultiSwitchVxlanTunnelMapTraits;
  using BaseT = ThriftMultiSwitchMapNode<
      MultiSwitchVxlanTunnelMap,
      MultiSwitchVxlanTunnelMapTraits>;
  using BaseT::modify;

  MultiSwitchVxlanTunnelMap() = default;
  virtual ~MultiSwitchVxlanTunnelMap() = default;

  MultiSwitchVxlanTunnelMap* modify(std::shared_ptr<SwitchState>* state);

 private:
  // Inherit the constructors required for clone()
  using BaseT::BaseT;
  friend class CloneAllocator;
};
} // namespace facebook::fboss
