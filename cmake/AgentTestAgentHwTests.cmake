# CMake to build libraries and binaries in fboss/agent/test/agent_hw_tests

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

file(READ fboss/agent/test/agent_hw_tests/golden/asic/jericho3-11.csv JERICHO3-11)
file(READ fboss/agent/test/agent_hw_tests/golden/asic/jericho3-12.csv JERICHO3-12)
file(READ fboss/agent/test/agent_hw_tests/golden/asic/jericho3-default.csv JERICHO3-DEFAULT)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/fboss/agent/test/agent_hw_tests/oss/golden_data.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/fboss/agent/test/agent_hw_tests/golden_data.h
  @ONLY
)

# QoS test library - tests related to QoS scheduling, DSCP mapping, watermarks
add_library(agent_qos_test_src
  fboss/agent/test/agent_hw_tests/Agent2QueueToOlympicQoSTests.cpp
  fboss/agent/test/agent_hw_tests/AgentDeviceBufferGuaranteeTests.cpp
  fboss/agent/test/agent_hw_tests/AgentDscpQueueMappingTests.cpp
  fboss/agent/test/agent_hw_tests/AgentNetworkAIQosSchedulerTests.cpp
  fboss/agent/test/agent_hw_tests/AgentNetworkAIQosTests.cpp
  fboss/agent/test/agent_hw_tests/AgentEgressQueueScalingFactorTests.cpp
  fboss/agent/test/agent_hw_tests/AgentOlympicQosSchedulerTests.cpp
  fboss/agent/test/agent_hw_tests/AgentOlympicQosTests.cpp
  fboss/agent/test/agent_hw_tests/AgentQosSchedulerTestBase.cpp
  fboss/agent/test/agent_hw_tests/AgentQosTestBase.cpp
  fboss/agent/test/agent_hw_tests/AgentWatermarkTests.cpp
)

target_link_libraries(agent_qos_test_src
  agent_hw_test
  aqm_test_utils
  port_test_utils
  config_factory
  copp_test_utils
  ecmp_helper
  multi_port_traffic_test_utils
  network_ai_qos_utils
  olympic_qos_utils
  packet_factory
  pfc_test_utils
  pkt_test_utils
  qos_test_utils
  queue_test_utils
  resourcelibutil
  traffic_policy_utils
  Folly::folly
)

# VOQ/DSF test library - tests related to VOQ switches and distributed switch fabric
add_library(agent_voq_test_src
  fboss/agent/test/agent_hw_tests/AgentFabricSwitchFabricLinkMonitoringTests.cpp
  fboss/agent/test/agent_hw_tests/AgentFabricSwitchTests.cpp
  fboss/agent/test/agent_hw_tests/AgentVoqConditionalEntropyTests.cpp
  fboss/agent/test/agent_hw_tests/AgentVoqSwitchFabricLinkMonitoringTests.cpp
  fboss/agent/test/agent_hw_tests/AgentVoqSwitchFirmwareTests.cpp
  fboss/agent/test/agent_hw_tests/AgentVoqSwitchFullScaleDsfTests.cpp
  fboss/agent/test/agent_hw_tests/AgentVoqSwitchInterruptsTests.cpp
  fboss/agent/test/agent_hw_tests/AgentVoqSwitchLineRateTests.cpp
  fboss/agent/test/agent_hw_tests/AgentVoqSwitchTests.cpp
  fboss/agent/test/agent_hw_tests/AgentVoqSwitchWithFabricPortsTests.cpp
  fboss/agent/test/agent_hw_tests/AgentVoqSwitchWithMultipleDsfNodesTests.cpp
  fboss/agent/test/agent_hw_tests/AgentVoqSwitchWarmbootReconcileTests.cpp
)

target_link_libraries(agent_voq_test_src
  agent_hw_test
  config_factory
  copp_test_utils
  core
  dsf_config_utils
  ecmp_helper
  fabric_test_utils
  hw_resource_stats_publisher
  hw_switch_fb303_stats
  load_balancer_test_utils
  network_ai_qos_utils
  olympic_qos_utils
  packet_factory
  packet_snooper
  port_test_utils
  route_test_utils
  scale_test_utils
  switch_asics
  trap_packet_utils
  voq_test_utils
  Folly::folly
)

# ARS/Flowlet test library - tests related to adaptive routing and flowlet switching
add_library(agent_ars_test_src
  fboss/agent/test/agent_hw_tests/AgentArsBase.cpp
  fboss/agent/test/agent_hw_tests/AgentArsFlowletTest.cpp
  fboss/agent/test/agent_hw_tests/AgentEcmpSpilloverTests.cpp
  fboss/agent/test/agent_hw_tests/AgentFlowletSwitchingTests.cpp
)

target_link_libraries(agent_ars_test_src
  agent_hw_test
  acl_test_utils
  config_factory
  copp_test_utils
  ecmp_helper
  load_balancer_test_utils
  mirror_test_utils
  network_ai_qos_utils
  olympic_qos_utils
  packet_factory
  port_test_utils
  qos_test_utils
  route_test_utils
  scale_test_utils
  switch_asics
  udf_test_utils
  Folly::folly
)

add_library(agent_hw_test_src
  fboss/agent/test/agent_hw_tests/AgentAdjFrrRouteTests.cpp
  fboss/agent/test/agent_hw_tests/AgentCableLengthMeasurementTests.cpp
  fboss/agent/test/agent_hw_tests/AgentConfigSetupTest.cpp
  fboss/agent/test/agent_hw_tests/AgentConfigVerifyQosTests.cpp
  fboss/agent/test/agent_hw_tests/AgentCoppTests.cpp
  fboss/agent/test/agent_hw_tests/AgentDot1qMappingTest.cpp
  fboss/agent/test/agent_hw_tests/AgentDropBitmapTests.cpp
  fboss/agent/test/agent_hw_tests/AgentDropReasonTests.cpp
  fboss/agent/test/agent_hw_tests/AgentDropTestBase.cpp
  fboss/agent/test/agent_hw_tests/AgentDscpMarkingTests.cpp
  fboss/agent/test/agent_hw_tests/AgentDeepPacketInspectionTests.cpp
  fboss/agent/test/agent_hw_tests/AgentCsigTests.cpp
  fboss/agent/test/agent_hw_tests/AgentAsicDefaultProgrammingTests.cpp
  fboss/agent/test/agent_hw_tests/AgentDiagShellStressTests.cpp
  fboss/agent/test/agent_hw_tests/AgentEcmpTests.cpp
  fboss/agent/test/agent_hw_tests/AgentEcmpTrunkTests.cpp
  fboss/agent/test/agent_hw_tests/AgentEmptyTests.cpp
  fboss/agent/test/agent_hw_tests/AgentFlexPortTests.cpp
  fboss/agent/test/agent_hw_tests/AgentEgressForwardingDiscardCounterTests.cpp
  fboss/agent/test/agent_hw_tests/AgentRouteOverDifferentAddressFamilyNhopTests.cpp
  fboss/agent/test/agent_hw_tests/AgentAclInDiscardCounterTests.cpp
  fboss/agent/test/agent_hw_tests/AgentJumboFramesTests.cpp
  fboss/agent/test/agent_hw_tests/AgentIngressBufferTests.cpp
  fboss/agent/test/agent_hw_tests/AgentInNullRouteDiscardsTest.cpp
  fboss/agent/test/agent_hw_tests/AgentInPauseDiscardsTests.cpp
  fboss/agent/test/agent_hw_tests/AgentInTrapDiscardsTest.cpp
  fboss/agent/test/agent_hw_tests/AgentPacketSendTests.cpp
  fboss/agent/test/agent_hw_tests/AgentPbrTest.cpp
  fboss/agent/test/agent_hw_tests/AgentL3ForwardingTests.cpp
  fboss/agent/test/agent_hw_tests/AgentL4PortBlackholingTests.cpp
  fboss/agent/test/agent_hw_tests/AgentLinkLocalForwardingTests.cpp
  fboss/agent/test/agent_hw_tests/AgentLoadBalancerTests.cpp
  fboss/agent/test/agent_hw_tests/AgentHwLinkDebounceTests.cpp
  fboss/agent/test/agent_hw_tests/AgentHwCbfcTest.cpp
  fboss/agent/test/agent_hw_tests/AgentHwLlrTest.cpp
  fboss/agent/test/agent_hw_tests/AgentMacLearningTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMacLearningAndNeighborResolutionTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMirrorOnDropDnxTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMirrorOnDropSrv6Tests.cpp
  fboss/agent/test/agent_hw_tests/AgentMirrorOnDropStatefulTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMirrorOnDropStatelessTest.cpp
  fboss/agent/test/agent_hw_tests/AgentMirrorOnDropStatelessTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMirrorOnDropTajoImpl.cpp
  fboss/agent/test/agent_hw_tests/AgentMirrorOnDropTestBase.cpp
  fboss/agent/test/agent_hw_tests/AgentMirrorOnDropXgsImpl.cpp
  fboss/agent/test/agent_hw_tests/AgentHwMirrorTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMirroringTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMirroringScaleTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMyMacTest.cpp
  fboss/agent/test/agent_hw_tests/AgentMySidAdjFrrRouteTests.cpp
  fboss/agent/test/utils/AgentMPLSDataplaneTestUtils.cpp
  fboss/agent/test/agent_hw_tests/AgentMPLSHeadEndTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMPLSMidpointTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMPLSTtlTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMPLSTests.cpp
  fboss/agent/test/agent_hw_tests/AgentNSFScaleTests.cpp
  fboss/agent/test/agent_hw_tests/AgentNeighborTests.cpp
  fboss/agent/test/agent_hw_tests/AgentQueuePerHostL2Tests.cpp
  fboss/agent/test/agent_hw_tests/AgentQueuePerHostTests.cpp
  fboss/agent/test/agent_hw_tests/AgentQueuePerHostRouteTests.cpp
  fboss/agent/test/agent_hw_tests/AgentRouteStatTests.cpp
  fboss/agent/test/agent_hw_tests/AgentRouteTests.cpp
  fboss/agent/test/agent_hw_tests/AgentSaiPortAdminStateTests.cpp
  fboss/agent/test/agent_hw_tests/AgentPortBandWidthTests.cpp
  fboss/agent/test/agent_hw_tests/AgentPortLedTests.cpp
  fboss/agent/test/agent_hw_tests/AgentPortProfileTests.cpp
  fboss/agent/test/agent_hw_tests/AgentPortlessInterfaceScopeTests.cpp
  fboss/agent/hw/test/HwTestPortUtils.cpp
  fboss/agent/test/agent_hw_tests/AgentPrbsTests.cpp
  fboss/agent/test/agent_hw_tests/AgentAclCounterTests.cpp
  fboss/agent/test/agent_hw_tests/AgentAqmTests.cpp
  fboss/agent/test/agent_hw_tests/AgentOverflowTestBase.cpp
  fboss/agent/test/agent_hw_tests/AgentLoopBackTests.cpp
  fboss/agent/test/agent_hw_tests/AgentSendPacketToQueueTests.cpp
  fboss/agent/test/agent_hw_tests/AgentSwitchStatsTxCounterTests.cpp
  fboss/agent/test/agent_hw_tests/AgentMmuTuningTests.cpp
  fboss/agent/test/agent_hw_tests/AgentSflowMirrorTest.cpp
  fboss/agent/test/agent_hw_tests/AgentAclPriorityTests.cpp
  fboss/agent/test/agent_hw_tests/AgentTrafficPauseTests.cpp
  fboss/agent/test/agent_hw_tests/AgentTrunkLoadBalancerTests.cpp
  fboss/agent/test/agent_hw_tests/AgentTrunkTests.cpp
  fboss/agent/test/agent_hw_tests/AgentTunnelMgrTests.cpp
  fboss/agent/test/agent_hw_tests/AgentRxReasonTests.cpp
  fboss/agent/test/agent_hw_tests/AgentRouteScaleTests.cpp
  fboss/agent/test/agent_hw_tests/AgentHwAclMatchActionsTest.cpp
  fboss/agent/test/agent_hw_tests/AgentHwAclQualifierTest.cpp
  fboss/agent/test/agent_hw_tests/AgentHwAclStatTests.cpp
  fboss/agent/test/agent_hw_tests/AgentAclTableTests.cpp
  fboss/agent/test/agent_hw_tests/AgentAclTableGroupTests.cpp
  fboss/agent/test/agent_hw_tests/AgentAclTableGroupTrafficTests.cpp
  fboss/agent/test/agent_hw_tests/AgentHwResourceStatsTests.cpp
  fboss/agent/test/agent_hw_tests/AgentHwParityErrorTests.cpp
  fboss/agent/test/agent_hw_tests/AgentTrafficPfcTests.cpp
  fboss/agent/test/agent_hw_tests/AgentPfcConfigTest.cpp
  fboss/agent/test/agent_hw_tests/AgentPfcTests.cpp
  fboss/agent/test/agent_hw_tests/AgentIpInIpTunnelTests.cpp
  fboss/agent/test/agent_hw_tests/AgentSrv6BindingSidTests.cpp
  fboss/agent/test/agent_hw_tests/AgentSrv6EncapTests.cpp
  fboss/agent/test/agent_hw_tests/AgentSrv6DecapTests.cpp
  fboss/agent/test/agent_hw_tests/AgentSrv6MidpointTest.cpp
  fboss/agent/test/agent_hw_tests/AgentSrv6ResourceUsageTests.cpp
  fboss/agent/test/agent_hw_tests/AgentHwPtpTcTests.cpp
  fboss/agent/test/agent_hw_tests/AgentSwitchingModeTests.cpp
  fboss/agent/test/agent_hw_tests/AgentHwPortStressTests.cpp
  fboss/agent/test/agent_hw_tests/AgentHwUdfTests.cpp
  fboss/agent/test/agent_hw_tests/AgentRouterInterfaceCounterTest.cpp
  fboss/agent/test/agent_hw_tests/AgentHwPtpTcProvisionTests.cpp
  fboss/agent/test/agent_hw_tests/AgentRouteOverflowTests.cpp
  fboss/agent/test/agent_hw_tests/AgentRollbackTests.cpp
  fboss/agent/test/agent_hw_tests/AgentRouteRollbackTests.cpp
  fboss/agent/test/agent_hw_tests/AgentPacketStreamHandlerTests.cpp
)

target_link_libraries(agent_hw_test_src
  erspan_parser
  agent_ars_test_src
  agent_qos_test_src
  agent_voq_test_src
  acl_test_utils
  address_utils
  agent_test_utils
  aqm_test_utils
  copp_test_utils
  dscp_marking_utils
  pkt_test_utils
  port_stats_test_utils
  packet
  packet_snooper
  chenab_mod_header
  tajo_punt_header
  queue_per_host_test_utils
  multi_port_traffic_test_utils
  trap_packet_utils
  tunnel_mgr_test_utils
  core
  hw_asic_table
  hw_switch_fb303_stats
  hw_resource_stats_publisher
  config_factory
  agent_hw_test
  ecmp_helper
  ecmp_dataplane_test_util
  fabric_test_utils
  trunk_utils
  traffic_policy_utils
  olympic_qos_utils
  network_ai_qos_utils
  pfc_test_utils
  qos_test_utils
  queue_test_utils
  invariant_test_utils
  prod_config_factory
  port_test_utils
  udf_test_utils
  state
  stats
  route_scale_gen
  route_test_utils
  srv6_test_utils
  switch_asics
  trunk_test_utils
  resourcelibutil
  load_balancer_test_runner_h
  load_balancer_test_utils
  l2learn_observer_util
  mac_test_utils
  port_stats_test_utils
  utils
  sflow_shim_utils
  udf_test_utils
  pktutil
  xgs_psamp_mod
  mirror_test_utils
  dsf_config_utils
  voq_test_utils
  fib_updater
  ecmp_test_utils
  scale_test_utils
  neighbor_test_utils
  system_scale_test_utils
  hyper_port_test_utils
  sai_port_utils
  phy_utils
  platform_mapping
  ${RE2}
)

add_executable(multi_switch_agent_hw_test
  fboss/agent/test/agent_hw_tests/MultiSwitchAgentHwTest.cpp
)

add_sai_sdk_dependencies(multi_switch_agent_hw_test)

target_link_libraries(multi_switch_agent_hw_test
  -Wl,--whole-archive
  acl_test_utils
  agent_test_utils
  copp_test_utils
  pkt_test_utils
  agent_hw_test_src
  agent_ars_test_src
  agent_qos_test_src
  agent_voq_test_src
  agent_hw_test
  multi_switch_agent_ensemble
  olympic_qos_utils
  network_ai_qos_utils
  setup_thrift_prod
  trunk_utils
  traffic_policy_utils
  Folly::folly
  hw_packet_utils
  phy_utils
  -Wl,--no-whole-archive
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

add_library(agent_scale_test_src
  fboss/agent/test/agent_hw_tests/AgentAclScaleTests.cpp
  fboss/agent/test/agent_hw_tests/AgentEcmpScaleTests.cpp
)

add_sai_sdk_dependencies(agent_scale_test_src)

target_link_libraries(agent_scale_test_src
  config_factory
  packet_factory
  agent_hw_test_src
  ecmp_helper
  production_features_cpp2
  acl_test_utils
  asic_test_utils
  scale_test_utils
  Folly::folly
)

add_executable(multi_switch_agent_scale_test
  fboss/agent/test/agent_hw_tests/MultiSwitchAgentHwTest.cpp
)

add_sai_sdk_dependencies(multi_switch_agent_scale_test)

target_link_libraries(multi_switch_agent_scale_test
  -Wl,--whole-archive
  agent_scale_test_src
  agent_hw_test
  multi_switch_agent_ensemble
  setup_thrift_prod
  Folly::folly
  -Wl,--no-whole-archive
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

install(TARGETS multi_switch_agent_scale_test)

function(BUILD_SAI_AGENT_HW_TEST SAI_IMPL_NAME SAI_IMPL_ARG)

  message(STATUS "Building SAI_IMPL_NAME: ${SAI_IMPL_NAME} SAI_IMPL_ARG: ${SAI_IMPL_ARG}")
  add_executable(sai_agent_hw_test-${SAI_IMPL_NAME}
    fboss/agent/test/agent_hw_tests/SaiAgentHwTest.cpp
  )

  add_sai_sdk_dependencies(
    sai_agent_hw_test-${SAI_IMPL_NAME} "${SAI_IMPL_ARG}")

  target_link_libraries(sai_agent_hw_test-${SAI_IMPL_NAME}
    -Wl,--whole-archive
    agent_hw_test_src
    agent_ars_test_src
    agent_qos_test_src
    agent_voq_test_src
    ${SAI_IMPL_ARG}
    acl_test_utils
    copp_test_utils
    sai_acl_utils
    sai_copp_utils
    hw_packet_utils
    olympic_qos_utils
    network_ai_qos_utils
    traffic_policy_utils
    sai_traced_api
    setup_thrift_prod
    phy_utils
    -Wl,--no-whole-archive
  )

  set_target_properties(sai_agent_hw_test-${SAI_IMPL_NAME}
    PROPERTIES COMPILE_FLAGS
    "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
    -DSAI_VER_MINOR=${SAI_VER_MINOR} \
    -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
  )

  add_executable(sai_agent_scale_test-${SAI_IMPL_NAME}
    fboss/agent/test/agent_hw_tests/SaiAgentHwTest.cpp
  )

  add_sai_sdk_dependencies(
    sai_agent_scale_test-${SAI_IMPL_NAME} "${SAI_IMPL_ARG}")

  target_link_libraries(sai_agent_scale_test-${SAI_IMPL_NAME}
    -Wl,--whole-archive
    ${SAI_IMPL_ARG}
    agent_scale_test_src
    agent_hw_test
    sai_acl_utils
    mono_agent_ensemble
    agent_hw_test_thrift_handler
    sai_port_utils
    setup_thrift_prod
    -Wl,--no-whole-archive
    ${GTEST}
    ${LIBGMOCK_LIBRARIES}
  )
endfunction()

if(BUILD_SAI_FAKE)
BUILD_SAI_AGENT_HW_TEST("fake" fake_sai)
install(
  TARGETS
  sai_agent_hw_test-fake)
endif()

# If libsai_impl is provided, build sai tests linking with it
find_library(SAI_IMPL sai_impl)
message(STATUS "SAI_IMPL: ${SAI_IMPL}")

if(SAI_IMPL)
  BUILD_SAI_AGENT_HW_TEST("sai_impl" ${SAI_IMPL})
  install(
    TARGETS
    sai_agent_hw_test-sai_impl)
endif()
