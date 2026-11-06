# CMake to build libraries and binaries in fboss/agent

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(setup_thrift
  fboss/agent/SetupThrift.cpp
  fboss/agent/oss/SetupThrift.cpp
)

target_link_libraries(setup_thrift
  load_agent_config
  thrift_method_rate_limit_setup
  thrift_service_utils
  Folly::folly
  FBThrift::thriftcpp2
)

add_library(fboss_init
  fboss/agent/oss/FbossInit.cpp
)

target_link_libraries(fboss_init
  Folly::folly
  fboss_common_cpp2
)

# TODO (rsunkad) re-enable this library for use with linking with libmain
#add_library(base INTERFACE)

#target_link_libraries(base
#  core
#  handler
#  Folly::folly
#)

add_library(main
  fboss/agent/Main.cpp
  fboss/agent/CommonInit.cpp
  fboss/agent/oss/CommonInit.cpp
  fboss/agent/oss/Main.cpp
)

target_link_libraries(main
  fboss_common_init
# base
  fboss_init
  platform_base
  Folly::folly
)

add_library(hw_switch_handler
  fboss/agent/HwSwitchHandler.cpp
)

target_link_libraries(hw_switch_handler
  fboss_event_base
  load_agent_config
  switch_config_cpp2
  utils
  multiswitch_ctrl_cpp2
  state
  hw_write_behavior
)

add_library(monolithic_switch_handler
  fboss/agent/single/MonolithicHwSwitchHandler.cpp
)

target_link_libraries(monolithic_switch_handler
  hw_switch
  packet
  platform_base
  hw_switch_fb303_stats
  switch_asics
  hw_switch_handler
)
add_library(async_packet_transport
  fboss/agent/AsyncPacketTransport.h
)

target_link_libraries(async_packet_transport
  Folly::folly
)

add_library(address_utils
  fboss/agent/AddressUtil.h
)

add_library(switchinfo_utils
  fboss/agent/SwitchInfoUtils.cpp
)

target_link_libraries(switchinfo_utils
  Folly::folly
  switch_config_cpp2
  switch_state_cpp2
  switch_asics
  ctrl_cpp2
  fboss_types
  agent_config_cpp2
  fboss_error
  load_agent_config
)

add_library(dsfnode_utils
  fboss/agent/DsfNodeUtils.cpp
)

target_link_libraries(dsfnode_utils
  agent_config_cpp2
  agent_features
  fboss_error
  load_agent_config
  switch_asics
  switch_config_cpp2
)

add_library(voq_utils
  fboss/agent/VoqUtils.cpp
)

target_link_libraries(voq_utils
  Folly::folly
  switch_config_cpp2
  agent_features
  switch_asics
  state
)

add_library(remote_intf_route_auditor
  fboss/agent/RemoteIntfRouteAuditor.cpp
)

target_link_libraries(remote_intf_route_auditor
  Folly::folly
  fib_helpers
  fib_updater
  standalone_rib
  state
  stats
  voq_utils
)

target_link_libraries(address_utils
  network_address_cpp2
  Folly::folly
)

add_library(asic_utils
  fboss/agent/AsicUtils.cpp
)

target_link_libraries(asic_utils
  switch_asics
)

add_library(utils
  fboss/agent/AlpmUtils.cpp
  fboss/agent/LabelFibUtils.cpp
  fboss/agent/Utils.cpp
  fboss/agent/oss/Utils.cpp
)

target_link_libraries(utils
  agent_dir_util
  asic_utils
  fboss_error
  ctrl_cpp2
  hw_switch_fb303_stats
  load_agent_config
  state
  switchid_scope_resolver
  Folly::folly
  meru800bia_platform_mapping
  meru800bfa_platform_mapping
  janga800bic_platform_mapping
  blackwolf800banw_platform_mapping
  icecube800bc_platform_mapping
  icetea800bc_platform_mapping
  tahansb800bc_platform_mapping
  ladakh800bcls_platform_mapping
  leh800bcls_platform_mapping
  Boost::filesystem
)

add_library(stats
  fboss/agent/AggregatePortStats.cpp
  fboss/agent/InterfaceStats.cpp
  fboss/agent/PortStats.cpp
  fboss/agent/SwitchStats.cpp
  fboss/agent/oss/SwitchStats.cpp
  fboss/agent/oss/AggregatePortStats.cpp
)

target_link_libraries(stats
  fboss_types
  agent_stats_cpp2
  common_utils
  state
  Folly::folly
)

add_library(stat_printers
  fboss/agent/StatPrinters.cpp
)

target_link_libraries(stat_printers
  FBThrift::thriftcpp2
  hardware_stats_cpp2
  phy_cpp2
  Folly::folly
)

add_library(fboss_types
  fboss/agent/types.cpp
  fboss/agent/PortDescriptorTemplate.cpp
)

target_link_libraries(fboss_types
  switch_config_cpp2
  Folly::folly
  Boost::container
)

add_library(fib_helpers
  fboss/agent/FibHelpers.cpp
)

target_link_libraries(fib_helpers
  fboss_types
  standalone_rib
  fib_updater
  state
  Folly::folly
)

add_library(override_set_id_manager
  fboss/agent/OverrideSetIdManager.cpp
)

target_link_libraries(override_set_id_manager
  fboss_error
  fboss_types
  nexthop_id_manager
)

add_library(ecmp_resource_manager
  fboss/agent/EcmpResourceManager.cpp
  fboss/agent/EcmpResourceManagerConfig.cpp
)

target_link_libraries(ecmp_resource_manager
  fib_helpers
  override_set_id_manager
  ref_map
  state
  stats
  utils
  Folly::folly
)

add_library(shel_manager
  fboss/agent/ShelManager.cpp
)

target_link_libraries(shel_manager
  utils
  fib_helpers
  state
  ${GTEST}
)

add_library(pbr_acl_manager
  fboss/agent/PbrAclManager.cpp
)

target_link_libraries(pbr_acl_manager
  fboss_error
  state
)

add_library(fsdb_adapted_sub_manager
  fboss/agent/FsdbAdaptedCowStorage.cpp
  fboss/agent/FsdbAdaptedCowStoragePatch.cpp
  fboss/agent/FsdbAdaptedSubManager.cpp
  fboss/agent/FsdbAdaptedTemplateInstantiations.cpp
)

target_link_libraries(fsdb_adapted_sub_manager
  state
  fsdb_sub_mgr
  fsdb_model
  cow_storage
)

add_library(hw_switch_thrift_client_table
  fboss/agent/HwSwitchThriftClientTable.cpp
)

target_link_libraries(hw_switch_thrift_client_table
  fboss_error
  fboss_types
  hw_ctrl_cpp2
  Folly::folly
  FBThrift::thriftcpp2
)

add_library(core
  fboss/agent/AclNexthopHandler.cpp
  fboss/agent/ApplyThriftConfig.cpp
  fboss/agent/ArpCache.cpp
  fboss/agent/ArpHandler.cpp
  fboss/agent/BufferUtils.cpp
  fboss/agent/CpuLatencyManager.cpp
  fboss/agent/DHCPv4Handler.cpp
  fboss/agent/DHCPv6Handler.cpp
  fboss/agent/DsfSession.cpp
  fboss/agent/DsfStateUpdaterUtil.cpp
  fboss/agent/DsfSubscriber.cpp
  fboss/agent/DsfSubscription.cpp
  fboss/agent/DsfUpdateValidator.cpp
  fboss/agent/FabricConnectivityManager.cpp
  fboss/agent/FabricLinkMonitoring.cpp
  fboss/agent/FabricLinkMonitoringManager.cpp
  fboss/agent/EncapIndexAllocator.cpp
  fboss/agent/HwAsicTable.cpp
  fboss/agent/HwSwitch.cpp
  fboss/agent/HwSwitchConnectionStatusTable.cpp
  fboss/agent/IPHeaderV4.cpp
  fboss/agent/IPv4Handler.cpp
  fboss/agent/IPv6Handler.cpp
  fboss/agent/L2Entry.cpp
  fboss/agent/LacpController.cpp
  fboss/agent/LacpMachines.cpp
  fboss/agent/LacpTypes.cpp
  fboss/agent/LinkAggregationManager.cpp
  fboss/agent/LldpManager.cpp
  fboss/agent/LoadBalancerConfigApplier.cpp
  fboss/agent/LookupClassUpdater.cpp
  fboss/agent/LookupClassRouteUpdater.cpp
  fboss/agent/LinkConnectivityProcessor.cpp
  fboss/agent/MKAServicePorts.cpp
  fboss/agent/MKAServiceManager.cpp
  fboss/agent/MacTableManager.cpp
  fboss/agent/MacTableUtils.cpp
  fboss/agent/MirrorManager.cpp
  fboss/agent/MirrorManagerImpl.cpp
  fboss/agent/MPLSHandler.cpp
  fboss/agent/MultiHwSwitchHandler.cpp
  fboss/agent/MultiSwitchFb303Stats.cpp
  fboss/agent/MultiSwitchPacketStreamMap.cpp
  fboss/agent/NdpCache.cpp
  fboss/agent/NeighborUpdater.cpp
  fboss/agent/NeighborUpdaterImpl.cpp
  fboss/agent/NeighborUpdaterNoopImpl.cpp
  fboss/agent/NextHopResolver.cpp
  fboss/agent/PfcUtils.cpp
  fboss/agent/PortUpdateHandler.cpp
  fboss/agent/RemoteNeighborUpdater.cpp
  fboss/agent/ResolvedNexthopMonitor.cpp
  fboss/agent/ResolvedNexthopProbe.cpp
  fboss/agent/ResolvedNexthopProbeScheduler.cpp
  fboss/agent/RouteUpdateLogger.cpp
  fboss/agent/RouteUpdateLoggingPrefixTracker.cpp
  fboss/agent/PacketStreamHandler.cpp
  fboss/agent/StaticL2ForNeighborObserver.cpp
  fboss/agent/StaticL2ForNeighborUpdater.cpp
  fboss/agent/StaticL2ForNeighborSwSwitchUpdater.cpp
  fboss/agent/SwitchInfoTable.cpp
  fboss/agent/SwitchStatsObserver.cpp
  fboss/agent/SwSwitch.cpp
  fboss/agent/MySidNeighborObserver.cpp
  fboss/agent/SwSwitchMySidUpdater.cpp
  fboss/agent/SwSwitchRouteUpdateWrapper.cpp
  fboss/agent/TamManager.cpp
  fboss/agent/TeFlowNexthopHandler.cpp
  fboss/agent/TunIntf.cpp
  fboss/agent/TunManager.cpp
  fboss/agent/ndp/IPv6RouteAdvertiser.cpp
  fboss/agent/oss/PacketLogger.cpp
  fboss/agent/oss/RouteUpdateLogger.cpp
  fboss/agent/oss/SwSwitch.cpp
  fboss/agent/FsdbSyncer.cpp
)

add_library(
   agent_fsdb_sync_manager
   fboss/agent/AgentFsdbSyncManager.cpp
   fboss/agent/AgentFsdbSyncManager-computeOperDelta.cpp
)

target_link_libraries(
  agent_fsdb_sync_manager
  agent_info_cpp2
  fsdb_syncer
  hwswitch_matcher
  state
  fsdb_model
  tuple_utils
  switch_reachability_cpp2
  switch_state_cpp2
)

set(core_libs
  agent_config_cpp2
  agent_features
  switchinfo_utils
  stats
  utils
  voq_utils
  asic_utils
  fb303::fb303
  capture
  diag_cmd_filter
  hardware_stats_cpp2
  hw_switch_fb303_stats
  hw_rif_fb303_stats
  hw_cpu_fb303_stats
  switch_asics
  switchid_scope_resolver
  ctrl_cpp2
  fboss_cpp2
  lldp
  multiswitch_ctrl_cpp2
  packet
  packet_factory
  product_info
  platform_base
  restart_time_tracker
  route_update_wrapper
  fib_updater
  network_to_route_map
  remote_intf_route_auditor
  standalone_rib
  state
  state_utils
  exponential_back_off
  fboss_config_utils
  fib_helpers
  phy_cpp2
  phy_utils
  snapshot_manager
  transceiver_cpp2
  alert_logger
  Folly::folly
  bidirectional_packet_stream
  fsdb_adapted_sub_manager
  fsdb_common_cpp2
  fsdb_model
  fsdb_stream_client
  fsdb_pub_sub
  fsdb_sub_mgr
  fsdb_flags
  ${IPROUTE2}
  ${NETLINK3}
  ${NETLINKROUTE3}
  thread_heartbeat
  platform_mapping_utils
  sw_switch_warmboot_helper
  hw_write_behavior
  hw_ctrl_cpp2
  loadbalancer_utils
  monolithic_switch_handler
  l2learn_event_observer
  agent_fsdb_sync_manager
  fboss_event_base
  phy_snapshot_manager
  build_info_wrapper
  ecmp_resource_manager
  thrift_method_rate_limit
  thrift_service_utils
  shel_manager
  pbr_acl_manager
  state_delta_logger
  switch_state_delta_logger
  dsfnode_utils
  hw_switch_thrift_client_table
  file_based_warmboot_utils
  validate_state_update
)

target_link_libraries(core ${core_libs})

add_library(thrifthandler_utils
  fboss/agent/ThriftHandlerUtils.cpp
)

target_link_libraries(thrifthandler_utils
  fboss_types
  state
)

add_library(handler
  fboss/agent/ThriftHandler.cpp
)

target_link_libraries(handler
  core
  pkt
  fb303::fb303
  ctrl_cpp2
  log_thrift_call
  Folly::folly
  thrifthandler_utils
  hw_switch_thrift_client_table
)

add_library(setup_thrift_prod
  fboss/agent/SetupThriftProd.cpp
)

target_link_libraries(setup_thrift_prod
   handler
   setup_thrift
   thrift_method_rate_limit
)

target_link_libraries(fboss_types
  switch_config_cpp2
  Folly::folly
  Boost::container
)

add_library(fboss_event_base
  fboss/agent/FbossEventBase.h
)

target_link_libraries(fboss_event_base
  agent_features
  Folly::folly
)

add_library(fboss_error
  fboss/agent/FbossError.h
  fboss/agent/FbossHwUpdateError.h
  fboss/agent/SysError.h
)

target_link_libraries(fboss_error
  fboss_cpp2
  Folly::folly
)

add_library(build_info_wrapper
  fboss/agent/BuildInfoWrapper.h
  fboss/agent/oss/BuildInfoWrapper.cpp
)

add_library(platform_base
  fboss/agent/Platform.cpp
  fboss/agent/PlatformPort.cpp
)

target_link_libraries(platform_base
  agent_config_cpp2
  agent_dir_util
  ctrl_cpp2
  fboss_error
  fboss_event_base
  fboss_types
  Folly::folly
  load_agent_config
  platform_mapping
  product_info
  switchid_scope_resolver
  switchinfo_utils
  utils
)

add_library(hw_switch
  fboss/agent/HwSwitch.cpp
  fboss/agent/HwSwitchRouteUpdateWrapper.cpp
)

target_link_libraries(hw_switch
  agent_features
  fboss_types
  ctrl_cpp2
  fboss_cpp2
  Folly::folly
  platform_base
  route_update_wrapper
  hw_switch_fb303_stats
  hw_write_behavior
  hw_switch_warmboot_helper
  multiswitch_ctrl_cpp2
)

add_library(async_logger_base
  fboss/agent/AsyncLoggerBase.cpp
)

target_link_libraries(async_logger_base
  fboss_error
  fb303::fb303
  Folly::folly
)

add_library(async_logger
  fboss/agent/AsyncLogger.cpp
)

target_link_libraries(async_logger
  async_logger_base
)

add_library(state_delta_logger
  fboss/agent/StateDeltaLogger.cpp
)

target_link_libraries(state_delta_logger
  agent_features
  async_logger_base
  state
  fsdb_oper_cpp2
  Folly::folly
)

add_library(switch_state_delta_logger
  fboss/agent/SwitchStateDeltaLogger.cpp
)

target_link_libraries(switch_state_delta_logger
  agent_features
  async_logger_base
  state
  state_delta_log_cpp2
  fsdb_oper_cpp2
  thrift_cow_serializer
  fb303::fb303
  FBThrift::thriftprotocol
  Folly::folly
)

add_library(sflow_shim_utils
  fboss/agent/SflowShimUtils.cpp
)

target_link_libraries(sflow_shim_utils
  Folly::folly
)


add_library(fsdb_helper
  fboss/agent/FsdbHelper.cpp
)

target_link_libraries(fsdb_helper
  fsdb_model
  fsdb_oper_cpp2
  fsdb_utils
  state
)

add_library(hwswitch_matcher
  fboss/agent/HwSwitchMatcher.cpp
)

target_link_libraries(hwswitch_matcher
  fboss_error
  fboss_types
)

add_library(switchid_scope_resolver
  fboss/agent/SwitchIdScopeResolver.cpp
)

target_link_libraries(switchid_scope_resolver
  fboss_types
  fboss_error
  hwswitch_matcher
  state
  switchinfo_utils
  Folly::folly
)

add_library(hwagent
  fboss/agent/HwAgent.cpp
)

target_link_libraries(hwagent
  fboss_init
  hw_switch
  platform_base
  switch_asics
)

add_library(hwagent-main
  fboss/agent/HwAgentMain.cpp
  fboss/agent/oss/Main.cpp
)

target_link_libraries(hwagent-main
  agent_features
  fboss_common_init
  platform_base
  fboss_common_cpp2
  restart_time_tracker
  setup_thrift
  split_agent_thrift_syncer
  Folly::folly
  agent_hw_test_thrift_handler
  utils
  test_utils
)

add_library(multiswitch_service
  fboss/agent/MultiSwitchThriftHandler.cpp
)

target_link_libraries(multiswitch_service
  core
  multiswitch_ctrl_cpp2
  state
  packet
)

add_library(route_update_wrapper
  fboss/agent/RouteUpdateWrapper.cpp
)

target_link_libraries(route_update_wrapper
  address_utils
  fboss_types
  switch_config_cpp2
  switchid_scope_resolver
  ctrl_cpp2
  fib_updater
  standalone_rib
  state
  Folly::folly
)

add_library(split_agent_thrift_syncer
  fboss/agent/mnpu/FdbEventSyncer.cpp
  fboss/agent/mnpu/HwSwitchStatsSinkClient.cpp
  fboss/agent/mnpu/LinkChangeEventSyncer.cpp
  fboss/agent/mnpu/SwitchReachabilityChangeEventSyncer.cpp
  fboss/agent/mnpu/OperDeltaSyncer.cpp
  fboss/agent/mnpu/RxPktEventSyncer.cpp
  fboss/agent/mnpu/SplitAgentThriftSyncer.cpp
  fboss/agent/mnpu/SplitAgentThriftSyncerClient.cpp
  fboss/agent/mnpu/TxPktEventSyncer.cpp
  fboss/agent/mnpu/IpcHealthMonitor.cpp
)

target_link_libraries(split_agent_thrift_syncer
  multiswitch_service
  Folly::folly
  hw_switch
)

add_library(load_agent_config
    fboss/agent/AgentConfig.cpp
)

target_link_libraries(load_agent_config
  agent_config_cpp2
  fboss_error
  switch_config_cpp2
  Folly::folly
)


add_library(fboss_common_init
  fboss/agent/CommonInit.cpp
  fboss/agent/oss/CommonInit.cpp
)

target_link_libraries(fboss_common_init
  fboss_init
  load_agent_config
  platform_base
  Folly::folly
  fb303::fb303
)

add_library(monolithic_agent_initializer
  fboss/agent/single/MonolithicAgentInitializer.cpp
)

target_link_libraries(monolithic_agent_initializer
  core
  fboss_common_init
  handler
  hw_switch
  hwagent
  load_agent_config
  monolithic_switch_handler
  setup_thrift
  sw_agent_initializer
  switch_asics
  utils
  Folly::folly
)

add_library(multi_switch_hw_switch_handler
  fboss/agent/mnpu/MultiSwitchHwSwitchHandler.cpp
)

target_link_libraries(multi_switch_hw_switch_handler
  core
  packet
  stats
  agent_config_cpp2
  multiswitch_ctrl_cpp2
  hw_switch_handler
)

add_library(split_agent_initializer
  fboss/agent/mnpu/SplitSwAgentInitializer.cpp
)

target_link_libraries(split_agent_initializer
  Folly::folly
  sw_agent_initializer
  multiswitch_service
  multi_switch_hw_switch_handler
)

add_library(agent_dir_util
  fboss/agent/AgentDirectoryUtil.cpp
)

target_link_libraries(agent_dir_util
  Folly::folly
)

add_executable(fboss_sw_agent
  fboss/agent/FbossSwAgent.cpp
)

target_link_libraries(fboss_sw_agent
  main
  split_agent_initializer
  handler
  -Wl,--whole-archive
  setup_thrift_prod
  thrift_service_client
  -Wl,--no-whole-archive
)

add_library(thrift_based_warmboot_utils
  fboss/agent/ThriftBasedWarmbootUtils.cpp
)

target_link_libraries(thrift_based_warmboot_utils
  hw_asic_table
  hw_switch_thrift_client_table
  switch_state_cpp2
)

add_library(file_based_warmboot_utils
  fboss/agent/FileBasedWarmbootUtils.cpp
)

target_link_libraries(file_based_warmboot_utils
  agent_dir_util
  agent_features
  hw_asic_table
  switch_state_cpp2
  standalone_rib
  state
  common_file_utils
  Folly::folly
)

add_library(sw_switch_warmboot_helper
  fboss/agent/SwSwitchWarmBootHelper.cpp
)

target_link_libraries(sw_switch_warmboot_helper
  agent_features
  async_logger
  fboss_error
  hw_asic_table
  state
  utils
  Folly::folly
  warm_boot_file_utils
  file_based_warmboot_utils
  thrift_based_warmboot_utils
)

add_library(sw_agent_initializer
  fboss/agent/SwAgentInitializer.cpp
)

target_link_libraries(sw_agent_initializer
  agent_features
  core
  Folly::folly
  FBThrift::thriftcpp2
  handler
  setup_thrift
  utils
)

add_library(agent_netwhoami
  fboss/agent/AgentNetWhoAmI.h
  fboss/agent/oss/AgentNetWhoAmI.cpp
)

target_link_libraries(agent_netwhoami
  common_file_utils
)

add_library(agent_features
  fboss/agent/AgentFeatures.cpp
)

target_link_libraries(agent_features)

add_library(loadbalancer_utils
  fboss/agent/LoadBalancerUtils.cpp
)

target_link_libraries(loadbalancer_utils
  switch_config_cpp2
  Folly::folly
)

add_library(hw_asic_table
  fboss/agent/HwAsicTable.cpp
)

target_link_libraries(hw_asic_table
  fboss_error
  fboss_types
  platform_mapping_utils
  product_info
  switch_asics
  utils
)

add_library(test_utils
  fboss/agent/TestUtils.cpp
)

target_link_libraries(test_utils
  agent_dir_util
  load_agent_config
  common_file_utils
  Folly::folly
)


add_library(validate_state_update
  fboss/agent/ValidateInterfaceDelta.cpp
  fboss/agent/ValidateStateUpdate.cpp
  fboss/agent/ResourceAccountant.cpp
)

target_link_libraries(validate_state_update
  state
  fboss_error
  switchid_scope_resolver
  hw_asic_table
  hw_switch_handler
  agent_features
  fib_helpers
  stats
  ${GTEST}
)
