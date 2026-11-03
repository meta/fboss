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

#include <functional>
#include <memory>
#include <ostream>

#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AclTableGroup.h"
#include "fboss/agent/state/AclTableGroupMap.h"
#include "fboss/agent/state/AclTableMap.h"
#include "fboss/agent/state/AggregatePortMap.h"
#include "fboss/agent/state/ClassBasedPolicyMap.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/DsfNodeMap.h"
#include "fboss/agent/state/FibInfoDelta.h"
#include "fboss/agent/state/FlowletSwitchingConfig.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/InterfaceMapDelta.h"
#include "fboss/agent/state/IpTunnelMap.h"
#include "fboss/agent/state/LabelForwardingInformationBase.h"
#include "fboss/agent/state/LoadBalancerMap.h"
#include "fboss/agent/state/Mirror.h"
#include "fboss/agent/state/MirrorMap.h"
#include "fboss/agent/state/MirrorOnDropReportMap.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/state/NodeMapDelta.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/QosPolicyMap.h"
#include "fboss/agent/state/Srv6TunnelMap.h"
#include "fboss/agent/state/UdfGroupMap.h"
#include "fboss/agent/state/UdfPacketMatcherMap.h"
#include "fboss/agent/state/VrfMap.h"
#include "fboss/agent/state/VxlanTunnelMap.h"

#include "fboss/agent/state/SflowCollectorMap.h"
#include "fboss/agent/state/SwitchSettings.h"
#include "fboss/agent/state/SystemPortMap.h"
#include "fboss/agent/state/TeFlowTable.h"
#include "fboss/agent/state/TransceiverMap.h"
#include "fboss/agent/state/VlanMapDelta.h"

#include "fboss/thrift_cow/nodes/Types.h"

DECLARE_bool(verify_apply_oper_delta);

namespace facebook::fboss {

class SwitchState;
class ControlPlane;
class MultiControlPlane;

/*
 * StateDelta contains code for examining the differences between two
 * SwitchStates.
 */
class StateDelta {
 public:
  StateDelta() = default;
  StateDelta(
      std::shared_ptr<SwitchState> oldState,
      std::shared_ptr<SwitchState> newState);
  StateDelta(std::shared_ptr<SwitchState> oldState, fsdb::OperDelta operDelta);
  StateDelta(StateDelta&&) = default;
  StateDelta& operator=(StateDelta&&) = default;
  virtual ~StateDelta();

  const std::shared_ptr<SwitchState>& oldState() const {
    return old_;
  }
  const std::shared_ptr<SwitchState>& newState() const {
    return new_;
  }

  MultiSwitchMapDelta<MultiSwitchPortMap> getPortsDelta() const;
  MultiSwitchMapDelta<MultiSwitchVlanMap> getVlansDelta() const;
  MultiSwitchInterfaceMapDelta getIntfsDelta() const;
  DeltaValue<QosPolicy> getDefaultDataPlaneQosPolicyDelta() const;
  AclMapDelta getAclsDelta(
      cfg::AclStage aclStage = cfg::AclStage::INGRESS,
      std::optional<std::string> tableName = std::nullopt) const;
  ThriftMapDelta<AclTableMap> getAclTablesDelta(cfg::AclStage aclStage) const;
  MultiSwitchMapDelta<MultiSwitchAclTableGroupMap> getAclTableGroupsDelta()
      const;
  MultiSwitchMapDelta<MultiSwitchQosPolicyMap> getQosPoliciesDelta() const;
  MultiSwitchMapDelta<MultiSwitchAggregatePortMap> getAggregatePortsDelta()
      const;
  MultiSwitchMapDelta<MultiSwitchSflowCollectorMap> getSflowCollectorsDelta()
      const;
  MultiSwitchMapDelta<MultiSwitchLoadBalancerMap> getLoadBalancersDelta() const;
  ThriftMapDelta<MultiControlPlane> getControlPlaneDelta() const;
  ThriftMapDelta<UdfPacketMatcherMap> getUdfPacketMatcherDelta() const;
  ThriftMapDelta<UdfGroupMap> getUdfGroupDelta() const;

  MultiSwitchMapDelta<MultiSwitchClassBasedPolicyMap>
  getClassBasedPoliciesDelta() const;

  MultiSwitchMapDelta<MultiSwitchMirrorMap> getMirrorsDelta() const;
  MultiSwitchMapDelta<MultiSwitchMirrorOnDropReportMap>
  getMirrorOnDropReportsDelta() const;

  MultiSwitchMapDelta<MultiSwitchTransceiverMap> getTransceiversDelta() const;
  MultiSwitchFibInfoMapDelta getFibsInfoDelta() const;
  MultiSwitchMapDelta<MultiLabelForwardingInformationBase>
  getLabelForwardingInformationBaseDelta() const;
  ThriftMapDelta<MultiSwitchSettings> getSwitchSettingsDelta() const;
  DeltaValue<FlowletSwitchingConfig> getFlowletSwitchingConfigDelta() const;
  MultiSwitchMapDelta<MultiSwitchSystemPortMap> getSystemPortsDelta() const;
  ThriftMapDelta<IpTunnelMap> getIpTunnelsDelta() const;
  ThriftMapDelta<Srv6TunnelMap> getSrv6TunnelsDelta() const;
  ThriftMapDelta<VxlanTunnelMap> getVxlanTunnelsDelta() const;
  ThriftMapDelta<MySidMap> getMySidsDelta() const;
  ThriftMapDelta<VrfMap> getVrfsDelta() const;
  MultiSwitchMapDelta<MultiTeFlowTable> getTeFlowEntriesDelta() const;
  // Remote object deltas
  MultiSwitchMapDelta<MultiSwitchSystemPortMap> getRemoteSystemPortsDelta()
      const;
  MultiSwitchMapDelta<MultiSwitchSystemPortMap>
  getFabricLinkMonitoringSystemPortsDelta() const;
  MultiSwitchInterfaceMapDelta getRemoteIntfsDelta() const;
  MultiSwitchMapDelta<MultiSwitchDsfNodeMap> getDsfNodesDelta() const;

  const fsdb::OperDelta& getOperDelta() const;

  // Check if routes or neighbors (ARP/NDP) changed.
  // Shared by MirrorManager and TamManager for next-hop resolution.
  bool hasRouteOrNeighborChanges() const;

 private:
  // Forbidden copy constructor and assignment operator
  StateDelta(StateDelta const&) = delete;
  StateDelta& operator=(StateDelta const&) = delete;

  std::shared_ptr<SwitchState> old_;
  std::shared_ptr<SwitchState> new_;
  // on-demand populate oper delta and keep it cached
  mutable std::optional<fsdb::OperDelta> operDelta_;
};

bool isStateDeltaEmpty(const StateDelta& stateDelta);
inline bool operator==(const StateDelta& lhs, const StateDelta& rhs) {
  return std::tie(lhs.oldState(), lhs.newState()) ==
      std::tie(rhs.oldState(), rhs.newState());
}

} // namespace facebook::fboss
