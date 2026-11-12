# CMake to build libraries and binaries for agent link testing

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(agent_ensemble_link_tests
  fboss/agent/test/link_tests/AgentEnsembleLinkTest.cpp
  fboss/agent/test/link_tests/LinkTestUtils.cpp
  fboss/agent/test/link_tests/AgentEnsembleOpticsTest.cpp
  fboss/agent/test/link_tests/AgentEnsembleLacpSanityTests.cpp
  fboss/agent/test/link_tests/AgentEnsembleEmptyLinkTest.cpp
  fboss/agent/test/link_tests/AgentEnsembleMacLearningTests.cpp
  fboss/agent/test/link_tests/AgentEnsemblePrbsTest.cpp
  fboss/agent/test/link_tests/AgentEnsemblePtpTests.cpp
  fboss/agent/test/link_tests/AgentEnsembleOpenBmcUpgradeTests.cpp
  fboss/agent/test/link_tests/AgentEnsembleSpeedChangeTest.cpp
  fboss/agent/test/link_tests/AgentEnsembleDependencyTest.cpp
  fboss/agent/test/link_tests/AgentEnsembleLinkSanityTests.cpp
  fboss/agent/test/link_tests/AgentEnsemblePhyInfoTest.cpp
  fboss/agent/test/link_tests/AgentEnsembleFabricLinkTests.cpp
  fboss/agent/test/link_tests/AgentEnsembleQsfpFsdbTests.cpp
  fboss/agent/test/link_tests/AgentEnsembleLinkDebounceTests.cpp
  fboss/agent/test/link_tests/AgentEnsembleLoopbackTest.cpp
  fboss/agent/test/link_tests/oss/TransceiverQuirks.cpp
)

target_link_libraries(agent_ensemble_link_tests
  agent_ensemble_test_lib
  agent_test_utils
  ecmp_helper
  main
  config_factory
  fboss_config_utils
  load_balancer_test_utils
  load_balancer_utils
  olympic_qos_utils
  network_ai_qos_utils
  port_test_utils
  copp_test_utils
  packet_snooper
  pkt_test_utils
  trap_packet_utils
  packet
  packet_snooper
  trunk_utils
  hyper_port_test_utils
  link_test_production_features_cpp2
  link_qsfp_test_port_info_cpp2
  link_qsfp_test_port_info_utils
  fsdb_pub_sub
  fsdb_model
  thrift_service_client
  qsfp_config_cpp2
  qsfp_state_cpp2
  qsfp_stats_cpp2
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  Boost::filesystem
)
