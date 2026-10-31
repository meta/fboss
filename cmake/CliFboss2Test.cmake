# CMake to build test binaries in fboss/cli/fboss2/test

# framework_test - Framework tests from BUCK file
# Note: Some tests are excluded because they require Thrift bundled schema infrastructure
# which is not available in the CMake OSS build. Buck builds have full bundled schema
# support via automatic generation and linking of *_sinit.cpp files.
# TODO(joseph5wu) Need to come up with a solution to enable these tests for OSS
add_executable(fboss2_framework_test
  fboss/util/oss/TestMain.cpp
  fboss/cli/fboss2/oss/config/CmdListImpl.cpp
  fboss/cli/fboss2/test/AggregationParsingTest.cpp
  # fboss/cli/fboss2/test/AggregationTest.cpp - excluded (requires bundled schema)
  # fboss/cli/fboss2/test/AggregationValidationTest.cpp - excluded (requires bundled schema)
  fboss/cli/fboss2/test/CmdArgsTest.cpp
  # fboss/cli/fboss2/test/CmdHelpTest.cpp - excluded (requires bundled schema)
  fboss/cli/fboss2/test/CmdSubCommandsTest.cpp
  # fboss/cli/fboss2/test/FilterTest.cpp - excluded (requires bundled schema)
  # fboss/cli/fboss2/test/FilterValidationTest.cpp - excluded (requires bundled schema)
)

target_link_libraries(fboss2_framework_test
  fboss2_lib
  fboss2_config_lib
  thrift_service_utils
  CLI11::CLI11
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  Folly::folly
  Folly::follybenchmark
  Folly::folly_test_util
  FBThrift::thriftcpp2
)

gtest_discover_tests(fboss2_framework_test)

# cmd_test - Command tests from BUCK file
add_executable(fboss2_cmd_test
  fboss/util/oss/TestMain.cpp
  fboss/cli/fboss2/oss/config/CmdListImpl.cpp
  fboss/cli/fboss2/test/CmdConfigSessionClearTest.cpp
  fboss/cli/fboss2/test/CmdGetPcapTest.cpp
  fboss/cli/fboss2/test/CmdListConfigTest.cpp
  fboss/cli/fboss2/commands/config/gen/agent/tests/CmdConfigGenAgentTest.cpp
  fboss/cli/fboss2/test/CmdSetInterfaceAdminStateTest.cpp
  fboss/cli/fboss2/test/CmdSetPortStateTest.cpp
  fboss/cli/fboss2/test/CmdShowAclTest.cpp
  fboss/cli/fboss2/test/CmdShowAgentBootTypeTest.cpp
  fboss/cli/fboss2/test/CmdShowAgentFirmwareTest.cpp
  fboss/cli/fboss2/test/CmdShowAgentSslTest.cpp
  fboss/cli/fboss2/test/CmdShowArpTest.cpp
  fboss/cli/fboss2/test/CmdShowConfigRunningBgpTest.cpp
  fboss/cli/fboss2/test/CmdShowConfigRunningTest.cpp
  # fboss/cli/fboss2/test/CmdShowHardwareTest.cpp - excluded (hardware model not built in CMake)
  fboss/cli/fboss2/test/CmdShowInterfaceStatusTest.cpp
  fboss/cli/fboss2/test/CmdShowInterfaceTest.cpp
  fboss/cli/fboss2/test/CmdShowInterfaceTrafficTest.cpp
  fboss/cli/fboss2/test/CmdShowL2Test.cpp
  fboss/cli/fboss2/test/CmdShowLldpTest.cpp
  fboss/cli/fboss2/test/CmdShowNdpTest.cpp
  fboss/cli/fboss2/test/CmdShowAggregatePortTest.cpp
  fboss/cli/fboss2/test/CmdShowCpuPortTest.cpp
  fboss/cli/fboss2/test/CmdShowDsfNodesTest.cpp
  fboss/cli/fboss2/test/CmdShowDsfSubscriptionTest.cpp
  fboss/cli/fboss2/test/CmdShowExampleTest.cpp
  fboss/cli/fboss2/test/CmdShowFabricInputBalanceTest.cpp
  fboss/cli/fboss2/test/CmdShowFabricReachabilityTest.cpp
  fboss/cli/fboss2/test/CmdShowFabricTest.cpp
  fboss/cli/fboss2/test/CmdShowFabricTopologyTest.cpp
  fboss/cli/fboss2/test/CmdShowFlowletTest.cpp
  fboss/cli/fboss2/test/CmdShowFsdbTest.cpp
  fboss/cli/fboss2/test/CmdShowHostTest.cpp
  fboss/cli/fboss2/test/CmdShowHwAgentStatusTest.cpp
  fboss/cli/fboss2/test/CmdShowHwObjectTest.cpp
  fboss/cli/fboss2/test/CmdShowInterfaceCountersFecBerTest.cpp
  fboss/cli/fboss2/test/CmdShowInterfaceCountersFecHistogramTest.cpp
  fboss/cli/fboss2/test/CmdShowInterfaceCountersFecUncorrectableTest.cpp
  fboss/cli/fboss2/test/CmdShowInterfaceCountersTest.cpp
  fboss/cli/fboss2/test/CmdShowInterfaceErrorsTest.cpp
  # fboss/cli/fboss2/test/CmdShowInterfaceFlapsTest.cpp - excluded (depends on hardware model not built in CMake)
  fboss/cli/fboss2/test/CmdShowInterfacePhyTest.cpp
  fboss/cli/fboss2/test/CmdShowInterfacePhymapTest.cpp
  fboss/cli/fboss2/test/CmdShowInterfacePrbsTest.cpp
  fboss/cli/fboss2/test/CmdShowMacDetailsTest.cpp
  fboss/cli/fboss2/test/CmdShowMirrorTest.cpp
  fboss/cli/fboss2/test/CmdShowMyMacTest.cpp
  fboss/cli/fboss2/test/CmdShowMySidTest.cpp
  fboss/cli/fboss2/test/CmdShowNextHopGroupsTest.cpp
  fboss/cli/fboss2/test/CmdShowPortTest.cpp
  fboss/cli/fboss2/test/CmdShowProductDetailsTest.cpp
  fboss/cli/fboss2/test/CmdShowProductTest.cpp
  fboss/cli/fboss2/test/CmdShowRifTest.cpp
  fboss/cli/fboss2/test/config/CmdConfigTestBase.cpp
  fboss/cli/fboss2/test/config/CmdConfigIpRouteTest.cpp
  fboss/cli/fboss2/test/config/CmdDeleteConfigInterfaceTest.cpp
  fboss/cli/fboss2/test/CmdShowRouteCountersTest.cpp
  fboss/cli/fboss2/test/CmdShowRouteDetailsTest.cpp
  fboss/cli/fboss2/test/CmdShowRouteSummaryTest.cpp
  fboss/cli/fboss2/test/CmdShowRouteTest.cpp
  fboss/cli/fboss2/test/CmdShowSystemPortTest.cpp
  fboss/cli/fboss2/test/CmdShowTeFlowTest.cpp
  fboss/cli/fboss2/test/CmdShowInterfaceTransceiverEepromTest.cpp
  fboss/cli/fboss2/test/CmdShowTransceiverLoopbackTest.cpp
  # fboss/cli/fboss2/test/CmdShowTransceiverTest.cpp - excluded (depends on configerator bgp namespace)
  # fboss/cli/fboss2/test/CmdShowVersionAgentTest.cpp - excluded (facebook-only command; depends on configerator)
  fboss/cli/fboss2/test/CmdBgpTestUtils.cpp
  fboss/cli/fboss2/test/CanonicalRibResolverTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpChangelistTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpConfigTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpFibNexthopsTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpHealthTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpHoldTimersWikiDocsTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpInitializationEventsTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpNeighborsAdvertisedPostPolicyTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpNeighborsAdvertisedPrePolicyTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpNeighborsAdvertisedRejectedTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpNeighborsReceivedPostPolicyTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpNeighborsReceivedPrePolicyTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpNeighborsReceivedRejectedTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpNeighborsByNameTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpPolicyTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpPolicyQueryTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpProfilerTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpNeighborsTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpNexthopInfoWikiDocsTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpOriginatedRoutesTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpPolicyConfigTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpShadowRibTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpStatsAttrsTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpStatsEntriesTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpStatsPolicyTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpStreamSubscriberTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpStreamSummaryTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpSummaryEgressTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpSummaryTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpVersionTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpTableCommunityTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpTableDetailTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpTableMoreSpecificsTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpTablePrefixTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpTableSummaryTest.cpp
  fboss/cli/fboss2/test/CmdShowBgpTableTest.cpp
  fboss/cli/fboss2/test/CmdShowConfigTestUtils.cpp
  fboss/cli/fboss2/test/config/CmdConfigTestBase.cpp
  fboss/cli/fboss2/test/config/BgpConfigSessionTest.cpp
  fboss/cli/fboss2/test/CmdStartPcapTest.cpp
  fboss/cli/fboss2/test/CmdStartPortCableLengthMeasurementTest.cpp
  fboss/cli/fboss2/test/CmdStopPcapTest.cpp
  fboss/cli/fboss2/test/GitTest.cpp
  fboss/cli/fboss2/test/InterfaceListTest.cpp
  fboss/cli/fboss2/test/PortMapTest.cpp
)

target_link_libraries(fboss2_cmd_test
  fboss2_lib
  fboss2_config_lib
  thrift_service_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  Folly::folly
  Folly::follybenchmark
  Folly::folly_test_util
  FBThrift::thriftcpp2
  Boost::filesystem
)

gtest_discover_tests(fboss2_cmd_test)

# thrift_latency_test - Comprehensive backend comparison test
#
# This test demonstrates the performance difference between various EventBase
# backends for Thrift RPC calls. It runs all three backends (LibEvent, Epoll,
# IoUring) sequentially and reports average/max latency for each.
add_executable(thrift_latency_test
  fboss/cli/fboss2/test/thrift_latency_test.cpp
)

target_link_libraries(thrift_latency_test
  ctrl_cpp2
  thrift_service_utils
  Folly::folly
  FBThrift::thriftcpp2
)
