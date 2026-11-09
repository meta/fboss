# CMake to build libraries and binaries in fboss/agent/state

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(nodebase
  fboss/agent/StrongTypes.h
  fboss/agent/state/DeltaFunctions.h
  fboss/agent/state/DeltaFunctions-detail.h
  fboss/agent/state/MapDelta.h
  fboss/agent/state/NodeBase.cpp
  fboss/agent/state/NodeBase.h
  fboss/agent/state/NodeBase-defs.h
  fboss/agent/state/NodeMap.h
  fboss/agent/state/NodeMap-defs.h
  fboss/agent/state/NodeMapDelta.h
  fboss/agent/state/NodeMapIterator.h
)

target_link_libraries(nodebase
  Boost::container
  Folly::folly
  fmt::fmt
  glog::glog
)

# NodeMap headers use FbossError, but the NodeBase-only OSS surface does not.
# Keep the existing dependency for the full and fsdb_client profiles while
# allowing the exported_libraries profile to build NodeBase independently.
if (NOT FBOSS_BUILD_PROFILE STREQUAL "exported_libraries")
  target_link_libraries(nodebase fboss_error)
endif()

add_library(lacp_types
  fboss/agent/LacpTypes.cpp
)

target_link_libraries(lacp_types
  switch_state_cpp2
  Folly::folly
  fboss_error
  ctrl_cpp2
)

add_library(state
  fboss/agent/state/AclEntry.cpp
  fboss/agent/state/AclMap.cpp
  fboss/agent/state/AclTable.cpp
  fboss/agent/state/AclTableGroup.cpp
  fboss/agent/state/AclTableGroupMap.cpp
  fboss/agent/state/AclTableMap.cpp
  fboss/agent/state/AggregatePort.cpp
  fboss/agent/state/AggregatePortMap.cpp
  fboss/agent/state/ArpEntry.cpp
  fboss/agent/state/ArpResponseEntry.cpp
  fboss/agent/state/ArpResponseTable.cpp
  fboss/agent/state/ArpTable.cpp
  fboss/agent/state/ClassBasedPolicyMap.cpp
  fboss/agent/state/ClassBasedPolicyNode.cpp
  fboss/agent/state/ControlPlane.cpp
  fboss/agent/state/DsfNode.cpp
  fboss/agent/state/DsfNodeMap.cpp
  fboss/agent/state/FibInfo.cpp
  fboss/agent/state/FibInfoMap.cpp
  fboss/agent/state/FlowletSwitchingConfig.cpp
  fboss/agent/state/ForwardingInformationBase.cpp
  fboss/agent/state/ForwardingInformationBaseContainer.cpp
  fboss/agent/state/ForwardingInformationBaseDelta.cpp
  fboss/agent/state/ForwardingInformationBaseMap.cpp
  fboss/agent/state/Interface.cpp
  fboss/agent/state/InterfaceMap.cpp
  fboss/agent/state/InterfaceMapDelta.cpp
  fboss/agent/state/IpTunnel.cpp
  fboss/agent/state/IsolationGroup.cpp
  fboss/agent/state/IsolationGroupMap.cpp
  fboss/agent/state/LabelForwardingInformationBase.cpp
  fboss/agent/state/LlrConfig.cpp
  fboss/agent/state/LlrConfigMap.cpp
  fboss/agent/state/LoadBalancer.cpp
  fboss/agent/state/LoadBalancerMap.cpp
  fboss/agent/state/MacEntry.cpp
  fboss/agent/state/MacTable.cpp
  fboss/agent/state/MatchAction.cpp
  fboss/agent/state/Mirror.cpp
  fboss/agent/state/MirrorMap.cpp
  fboss/agent/state/MirrorOnDropReport.cpp
  fboss/agent/state/MirrorOnDropReportMap.cpp
  fboss/agent/state/MySid.cpp
  fboss/agent/state/MySidMap.cpp
  fboss/agent/state/NdpEntry.cpp
  fboss/agent/state/NdpResponseEntry.cpp
  fboss/agent/state/NdpResponseTable.cpp
  fboss/agent/state/NdpTable.cpp
  fboss/agent/state/PbrUtils.cpp
  fboss/agent/state/Port.cpp
  fboss/agent/state/PortMap.cpp
  fboss/agent/state/PortFlowletConfig.cpp
  fboss/agent/state/PortFlowletConfigMap.cpp
  fboss/agent/state/PortQueue.cpp
  fboss/agent/state/BufferPoolConfig.cpp
  fboss/agent/state/BufferPoolConfigMap.cpp
  fboss/agent/state/PortPgConfig.cpp
  fboss/agent/state/QosPolicy.cpp
  fboss/agent/state/QosPolicyMap.cpp
  fboss/agent/state/Route.cpp
  fboss/agent/state/RouteNextHop.cpp
  fboss/agent/state/RouteNextHopEntry.cpp
  fboss/agent/state/RouteNextHopsMulti.cpp
  fboss/agent/state/RouteTypes.cpp
  fboss/agent/state/SflowCollector.cpp
  fboss/agent/state/SflowCollectorMap.cpp
  fboss/agent/state/Srv6Tunnel.cpp
  fboss/agent/state/Srv6TunnelMap.cpp
  fboss/agent/state/StateDelta.cpp
  fboss/agent/state/StateDelta-computeOperDelta.cpp
  fboss/agent/state/StateUtils.cpp
  fboss/agent/state/SwitchSettings.cpp
  fboss/agent/state/SystemPort.cpp
  fboss/agent/state/SystemPortMap.cpp
  fboss/agent/state/QcmConfig.cpp
  fboss/agent/state/SwitchState.cpp
  fboss/agent/state/TeFlowEntry.cpp
  fboss/agent/state/TeFlowTable.cpp
  fboss/agent/state/Transceiver.cpp
  fboss/agent/state/TransceiverMap.cpp
  fboss/agent/state/UdfConfig.cpp
  fboss/agent/state/UdfGroup.cpp
  fboss/agent/state/UdfGroupMap.cpp
  fboss/agent/state/UdfPacketMatcher.cpp
  fboss/agent/state/UdfPacketMatcherMap.cpp
  fboss/agent/state/IpTunnelMap.cpp
  fboss/agent/state/Vlan.cpp
  fboss/agent/state/VlanMap.cpp
  fboss/agent/state/VlanMapDelta.cpp
  fboss/agent/state/Vrf.cpp
  fboss/agent/state/VrfMap.cpp
  fboss/agent/state/VxlanTunnel.cpp
  fboss/agent/state/VxlanTunnelMap.cpp
)

target_link_libraries(state
  address_utils
  agent_features
  fboss_error
  platform_config_cpp2
  switch_config_cpp2
  switch_state_cpp2
  mka_structs_cpp2
  fboss_cpp2
  fsdb_helper
  label_forwarding_action
  hwswitch_matcher
  nodebase
  state_utils
  radix_tree
  phy_cpp2
  thrift_cow_nodes
  Folly::folly
  lacp_types
)

set_target_properties(state PROPERTIES COMPILE_FLAGS "-DENABLE_DYNAMIC_APIS")

add_library(state_utils
  fboss/agent/state/StateUtils.cpp
)

target_link_libraries(state_utils
  fboss_error
  fboss_types
  hwswitch_matcher
  state
  switch_state_cpp2
  thrift_cow_serializer
)

add_library(label_forwarding_action
  fboss/agent/state/LabelForwardingAction.cpp
)

target_link_libraries(label_forwarding_action
  ctrl_cpp2
  fboss_error
  fboss_cpp2
  Folly::folly
)
