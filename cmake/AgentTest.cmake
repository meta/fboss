# CMake to build libraries and binaries in fboss/agent/test

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(label_forwarding_utils
  fboss/agent/test/LabelForwardingUtils.cpp
)

target_link_libraries(label_forwarding_utils
  state
)

add_library(route_distribution_gen
  fboss/agent/test/RouteDistributionGenerator.cpp
)

add_library(resourcelibutil
  fboss/agent/test/ResourceLibUtil.cpp
)

target_link_libraries(resourcelibutil
  state
)

target_link_libraries(route_distribution_gen
  ecmp_helper
  resourcelibutil
  state
)

add_library(route_scale_gen
  fboss/agent/test/RouteScaleGenerators.cpp
)

target_link_libraries(route_scale_gen
  ecmp_helper
  route_distribution_gen
  state
)

add_library(agent_test_utils
  fboss/agent/test/CounterCache.cpp
  fboss/agent/test/MockTunManager.cpp
  fboss/agent/test/TestUtils.cpp
)

target_link_libraries(agent_test_utils
  agent_features
  core
  label_forwarding_utils
  hw_mock
  monolithic_switch_handler
  multi_switch_hw_switch_handler
  route_distribution_gen
  state_utils
)

add_library(ecmp_helper
  fboss/agent/test/EcmpSetupHelper.cpp
)

target_link_libraries(ecmp_helper
  switch_config_cpp2
  state
  core
  neighbor_test_utils
)

add_library(trunk_utils
  fboss/agent/test/TrunkUtils.cpp
)

target_link_libraries(trunk_utils
  switch_config_cpp2
  state
)

add_executable(async_logger_test
  fboss/util/oss/TestMain.cpp
  fboss/agent/test/AsyncLoggerTest.cpp
)

target_link_libraries(async_logger_test
  async_logger
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(async_logger_test)

add_executable(mpls_handler_test
  fboss/util/oss/TestMain.cpp
  fboss/agent/test/MPLSHandlerTest.cpp
)

target_link_libraries(mpls_handler_test
  agent_test_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(mpls_handler_test)

add_executable(vrf_config_test
  fboss/util/oss/TestMain.cpp
  fboss/agent/test/VrfConfigTests.cpp
)

target_link_libraries(vrf_config_test
  agent_test_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(vrf_config_test)

add_library(agent_test_lib
  fboss/agent/test/AgentTest.cpp
)

target_link_libraries(agent_test_lib
  agent_test_utils
  main
  qsfp_cpp2
  qsfp_service_client
  fboss_config_utils
  monolithic_agent_initializer
  qos_test_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

add_library(agent_ensemble_test_lib
  fboss/agent/test/AgentEnsembleTest.cpp
)

target_link_libraries(agent_ensemble_test_lib
  main
  qsfp_cpp2
  qsfp_service_client
  fboss_config_utils
  agent_ensemble
  agent_features
  qos_test_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

add_library(agent_ensemble_integration_test_base
  fboss/agent/test/AgentEnsembleIntegrationTestBase.cpp
)

target_link_libraries(agent_ensemble_integration_test_base
  agent_ensemble_test_lib
  main
  config_factory
  fboss_config_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

add_library(agent_integration_test_base
  fboss/agent/test/AgentIntegrationTestBase.cpp
)

target_link_libraries(agent_integration_test_base
  agent_test_lib
  main
  config_factory
  fboss_config_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

add_library(agent_hw_test
  fboss/agent/test/AgentHwTest.cpp
)

target_link_libraries(agent_hw_test
  agent_hw_test_constants
  mono_agent_ensemble
  production_features_cpp2
  core
  switch_asics
  hw_copp_utils
  stats_test_utils
  hardware_stats_cpp2
  multiswitch_ctrl_cpp2
  ${GTEST}
)

add_library(multinode_tests
  fboss/agent/test/MultiNodeTest.cpp
  fboss/agent/test/MultiNodeLacpTests.cpp
  fboss/agent/test/MultiNodeLoadBalancerTests.cpp
)

target_link_libraries(multinode_tests
  agent_test_lib
  agent_test_utils
  hw_copp_utils
  hw_packet_utils
  load_balancer_utils
  main
  config_factory
  trunk_utils
  fboss_config_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

add_library(test_ensemble_if
  fboss/agent/test/TestEnsembleIf.cpp
)

target_link_libraries(test_ensemble_if
  state
  hw_switch
)

add_library(agent_ensemble
  fboss/agent/test/AgentEnsemble.cpp
)

target_link_libraries(agent_ensemble
  agent_test_utils
  handler
  hw_link_state_toggler
  route_distribution_gen
  main
  config_factory
  fboss_config_utils
  sw_agent_initializer
  test_ensemble_if
  pkt_test_utils
  agent_hw_test_ctrl_cpp2
  hw_switch_thrift_client_table
  FBThrift::thriftcpp2
  ${GTEST}
)

add_library(mono_agent_ensemble
  fboss/agent/test/MonoAgentEnsemble.cpp
)

target_link_libraries(mono_agent_ensemble
  agent_ensemble
  monolithic_agent_initializer
  agent_hw_test_thrift_handler
  ${GTEST}
)

add_library(multi_switch_agent_ensemble
  fboss/agent/test/MultiSwitchAgentEnsemble.cpp
)

target_link_libraries(multi_switch_agent_ensemble
  agent_ensemble
  split_agent_initializer
  ${GTEST}
)

add_library(linkstate_toggler
  fboss/agent/test/LinkStateToggler.cpp
)

target_link_libraries(linkstate_toggler
  state
  core
  test_ensemble_if
)

add_library(system_scale_test_utils
  fboss/agent/test/utils/SystemScaleTestUtils.cpp
)

target_link_libraries(system_scale_test_utils
  agent_ensemble
  config_factory
  packet_factory
  ecmp_helper
  acl_test_utils
  acl_scale_test_utils
  asic_test_utils
  function_call_time_reporter
  copp_test_utils
  scale_test_utils
  route_scale_gen
  qos_test_utils
  trap_packet_utils
  port_flap_helper
  mac_learning_flood_helper
  Folly::folly
  Folly::follybenchmark
)

add_library(acl_scale_test_utils
  fboss/agent/test/utils/AclScaleTestUtils.cpp
)

target_link_libraries(acl_scale_test_utils
  acl_test_utils
  asic_test_utils
)
