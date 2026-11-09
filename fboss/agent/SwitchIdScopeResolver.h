// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/types.h"

#include <boost/container/flat_set.hpp>
#include <unordered_set>

namespace facebook::fboss {
namespace cfg {
class Mirror;
class MirrorOnDropReport;
class DsfNode;
class IpInIpTunnel;
class Srv6Tunnel;
class VxLanTunnel;
class VrfConfig;
class AclEntry;
class SystemPort;
class Port;
class BufferPoolConfig;
class PortFlowletConfig;
class QosPolicy;
class SflowCollector;
} // namespace cfg
class Mirror;
class MirrorOnDropReport;
class DsfNode;
class IpTunnel;
class Srv6Tunnel;
class VxlanTunnel;
class Vrf;
class AclEntry;
class SystemPort;
class Vlan;
class LoadBalancer;
class TeFlowEntry;
class SwitchState;
class Interface;
class Port;
class PortDescriptor;
class AclTableGroup;
class ForwardingInformationBaseContainer;
class FibInfo;
class BufferPoolCfg;
class PortFlowletCfg;
class LlrConfig;
class QosPolicy;
template <typename T>
class Route;
class ControlPlane;
class AggregatePort;
class SflowCollector;
class SwitchSettings;
class MySid;
class MySidMap;

class SwitchIdScopeResolver {
 public:
  explicit SwitchIdScopeResolver(
      const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo);

  const HwSwitchMatcher& scope(const cfg::Mirror& /*m*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(const cfg::DsfNode& /*m*/) const {
    return allSwitchMatcher();
  }
  const HwSwitchMatcher& scope(const std::shared_ptr<DsfNode>& /*m*/) const {
    return allSwitchMatcher();
  }
  const HwSwitchMatcher& scope(const cfg::LoadBalancer& /*m*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(
      const std::shared_ptr<LoadBalancer>& /*m*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(
      const std::shared_ptr<TeFlowEntry>& /*m*/) const {
    return l3SwitchMatcher();
  }
  HwSwitchMatcher scope(PortID portId) const;
  HwSwitchMatcher scope(const std::shared_ptr<Port>& port) const;
  HwSwitchMatcher scope(const cfg::Port& port) const;
  HwSwitchMatcher scope(const std::vector<PortID>& portIds) const;
  HwSwitchMatcher scope(const cfg::AggregatePort& aggPort) const;
  HwSwitchMatcher scope(const std::shared_ptr<AggregatePort>& aggPort) const;
  HwSwitchMatcher scope(const std::shared_ptr<Mirror>& mirror) const;
  HwSwitchMatcher scope(const cfg::MirrorOnDropReport& report) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<MirrorOnDropReport>& report) const;
  const HwSwitchMatcher& scope(const cfg::IpInIpTunnel& /*m*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(const std::shared_ptr<IpTunnel>& /*m*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(const cfg::VrfConfig& /*vrf*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(const std::shared_ptr<Vrf>& /*vrf*/) const {
    return l3SwitchMatcher();
  }
  HwSwitchMatcher scope(
      const cfg::Srv6Tunnel& tunnel,
      const cfg::SwitchConfig& cfg) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<Srv6Tunnel>& tunnel,
      const std::shared_ptr<SwitchState>& state) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<Srv6Tunnel>& tunnel,
      const cfg::SwitchConfig& cfg) const;
  HwSwitchMatcher scope(
      const cfg::VxLanTunnel& tunnel,
      const cfg::SwitchConfig& cfg) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<VxlanTunnel>& tunnel,
      const std::shared_ptr<SwitchState>& state) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<VxlanTunnel>& tunnel,
      const cfg::SwitchConfig& cfg) const;
  HwSwitchMatcher scope(const cfg::AclEntry& acl) const;
  HwSwitchMatcher scope(const std::shared_ptr<AclEntry>& acl) const;
  const HwSwitchMatcher scope(const std::shared_ptr<Vlan>& vlan) const;

  HwSwitchMatcher scope(SystemPortID sysPortID) const;
  HwSwitchMatcher scope(const std::shared_ptr<SystemPort>& sysPort) const;
  HwSwitchMatcher scope(const cfg::SystemPort& sysPort) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<Interface>& intf,
      const std::shared_ptr<SwitchState>& state) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<Interface>& intf,
      const cfg::SwitchConfig& cfg) const;
  HwSwitchMatcher scope(
      const cfg::InterfaceType& type,
      const InterfaceID& interfaceId,
      const cfg::SwitchConfig& cfg) const;

  HwSwitchMatcher scope(const cfg::AclTableGroup& aclTableGroup) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<AclTableGroup>& aclTableGroup) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<ForwardingInformationBaseContainer>& fibs) const;
  HwSwitchMatcher scope(const std::shared_ptr<FibInfo>& fibInfo) const;
  HwSwitchMatcher scope(const std::shared_ptr<Route<LabelID>>& entry) const;
  const HwSwitchMatcher& scope(const cfg::BufferPoolConfig& /*b*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(
      const std::shared_ptr<BufferPoolCfg>& /*b*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(const cfg::QosPolicy& /*q*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(const std::shared_ptr<QosPolicy>& /*q*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(
      const std::shared_ptr<ControlPlane>& /*c*/) const;
  const HwSwitchMatcher& scope(
      const std::shared_ptr<SflowCollector>& collector) const;
  const HwSwitchMatcher& scope(const cfg::SflowCollector& collector) const;
  const HwSwitchMatcher& scope(
      const std::shared_ptr<SwitchSettings>& /*s*/) const;
  const HwSwitchMatcher& scope(const cfg::PortFlowletConfig& /*p*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(
      const std::shared_ptr<PortFlowletCfg>& /*p*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(const std::shared_ptr<LlrConfig>& /*l*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(const std::shared_ptr<MySid>& /*m*/) const {
    return l3SwitchMatcher();
  }
  const HwSwitchMatcher& scope(const std::shared_ptr<MySidMap>& /*m*/) const {
    return l3SwitchMatcher();
  }

  HwSwitchMatcher scope(
      const std::shared_ptr<SwitchState>& state,
      const boost::container::flat_set<PortDescriptor>& ports) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<SwitchState>& state,
      const PortDescriptor& portDesc) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<SwitchState>& state,
      const PortID& portId) const;
  HwSwitchMatcher scope(
      const std::shared_ptr<SwitchState>& state,
      const AggregatePortID& aggPortId) const;

  const std::map<int64_t, cfg::SwitchInfo>& switchIdToSwitchInfo() const {
    return switchIdToSwitchInfo_;
  }

  /* returns true if at least one switch supports l3 programming */
  bool hasL3() const {
    return l3SwitchMatcher_ != nullptr;
  }

  bool hasVoq() const {
    return voqSwitchMatcher_ != nullptr;
  }

  bool hasMultipleSwitches() const {
    return allSwitchMatcher().switchIds().size() > 1;
  }

  HwSwitchMatcher scope(cfg::SwitchType type) const;

 private:
  void checkL3() const;
  void checkVoq() const;
  const HwSwitchMatcher& l3SwitchMatcher() const;
  const HwSwitchMatcher& allSwitchMatcher() const;
  const HwSwitchMatcher& voqSwitchMatcher() const;
  std::unordered_set<SwitchID> getSwitchIdsOfType(cfg::SwitchType type) const;
  std::map<int64_t, cfg::SwitchInfo> switchIdToSwitchInfo_;
  std::unique_ptr<HwSwitchMatcher> l3SwitchMatcher_;
  std::unique_ptr<HwSwitchMatcher> allSwitchMatcher_;
  std::unique_ptr<HwSwitchMatcher> voqSwitchMatcher_;
};
} // namespace facebook::fboss
