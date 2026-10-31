# CMake to build test binaries in fboss/cli/fboss2/test/integration_test

# fboss2_integration_test - CLI E2E test binary
#
# CLI tests are platform/SAI independent - they test the CLI binary which
# communicates with the agent via Thrift, without running the actual fboss2-dev
# binary.
add_executable(fboss2_integration_test
  fboss/cli/fboss2/oss/config/CmdListImpl.cpp
  fboss/cli/fboss2/test/integration_test/AgentMemoryReclamationTest.cpp
  fboss/cli/fboss2/test/integration_test/Fboss2IntegrationTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigAdminDistanceTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigAclRuleTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigAclTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigArpTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigSflowRateTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigSflowSampleDestTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigBgpGlobalTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigBgpNeighborTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigBgpPeerGroupTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigBgpPolicyAsPathListTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigBgpPolicyCommunityListTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigBgpPolicyPrefixListTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigBgpPolicyPrefixListEntryTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigBgpPolicyRoutingPolicyTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigBgpPolicyRoutingPolicyTermTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigBgpSessionTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigConcurrentSessionsTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigHostnameTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigIcmpV4UnavailableSrcAddrTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigCoppTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigDhcpSourceOverrideTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigInterfaceDescriptionTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigInterfaceIpv6NdpTest.cpp
  fboss/cli/fboss2/test/integration_test/DeleteCoppTest.cpp
  fboss/cli/fboss2/test/integration_test/DeleteInterfaceTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigInterfaceMtuTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigInterfaceProfileTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigL2LearningModeTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigLoadBalancingTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigMacTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigPfcTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigPortQueueConfigTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigPtpTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigInterfaceFlowControlTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigInterfaceLldpExpectedValueTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigInterfaceLookupClassTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigInterfaceLoopbackModeTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigInterfaceTypeTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigQosDefaultPolicyTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigQosDefaultQueueConfigTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigQosPolicyMapTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigSessionClearTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigTrafficCounterTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigVlanCreateTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigVlanDefaultTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigVlanPortTaggingModeTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigVlanStaticMacTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigVlanSwitchportAccessTest.cpp
  fboss/cli/fboss2/test/integration_test/DeleteArpTest.cpp
  fboss/cli/fboss2/test/integration_test/DeleteDhcpSourceOverrideTest.cpp
  fboss/cli/fboss2/test/integration_test/DeleteInterfaceIpv6NdpTest.cpp
  fboss/cli/fboss2/test/integration_test/DeleteLoadBalancingTest.cpp
  fboss/cli/fboss2/test/integration_test/DeleteQosPolicyTest.cpp
  fboss/cli/fboss2/test/integration_test/DeleteVlanTest.cpp
  fboss/cli/fboss2/test/integration_test/TunnelIpInIpTest.cpp
  fboss/cli/fboss2/utils/CmdInitUtils.cpp
  fboss/cli/fboss2/utils/oss/CmdInitUtils.cpp
  fboss/cli/fboss2/test/integration_test/ConfigIpRouteTest.cpp
  fboss/cli/fboss2/test/integration_test/ConfigCoppTrafficPolicyTest.cpp
  fboss/cli/fboss2/test/integration_test/ShowRouteFilterTest.cpp
)

target_link_libraries(fboss2_integration_test
  fboss2_lib
  fboss2_config_lib
  thrift_service_utils
  CLI11::CLI11
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  Folly::folly
  Folly::folly_test_util
  FBThrift::thriftcpp2
  gflags
  fmt::fmt
)

target_include_directories(fboss2_integration_test PUBLIC
  ${CMAKE_SOURCE_DIR}
)
