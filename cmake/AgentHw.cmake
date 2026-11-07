# CMake to build libraries and binaries in fboss/agent/hw

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(counter_utils
  fboss/agent/hw/CounterUtils.cpp
)

target_link_libraries(counter_utils
  fb303::fb303
  FBThrift::thriftcpp2
  hardware_stats_cpp2
  switch_config_cpp2
)

add_library(diag_cmd_filter
  fboss/agent/hw/DiagCmdFilter.cpp
)

target_link_libraries(diag_cmd_filter
  Folly::folly
)

add_library(hw_switch_fb303_stats
  fboss/agent/hw/HwSwitchFb303Stats.cpp
)

add_library(hw_fb303_stats
  fboss/agent/hw/HwFb303Stats.cpp
)

add_library(hw_port_fb303_stats
  fboss/agent/hw/HwBasePortFb303Stats.cpp
  fboss/agent/hw/HwPortFb303Stats.cpp
  fboss/agent/hw/HwSysPortFb303Stats.cpp
)

add_library(hw_rif_fb303_stats
  fboss/agent/hw/HwRouterInterfaceFb303Stats.cpp
)

add_library(hw_cpu_fb303_stats
  fboss/agent/hw/HwCpuFb303Stats.cpp
)

add_library(hw_trunk_counters
  fboss/agent/hw/HwTrunkCounters.cpp
)

add_library(hw_switch_warmboot_helper
  fboss/agent/hw/HwSwitchWarmBootHelper.cpp
)

add_library(buffer_stats
  fboss/agent/hw/BufferStatsLogger.cpp
)

add_library(hw_resource_stats_publisher
  fboss/agent/hw/HwResourceStatsPublisher.cpp
)

add_library(prbs_stats_entry
  fboss/agent/hw/common/PrbsStatsEntry.h
)

target_link_libraries(hw_switch_warmboot_helper
  agent_features
  async_logger
  utils
  common_file_utils
  Folly::folly
  sw_switch_warmboot_helper
  warm_boot_file_utils
  state
)

target_link_libraries(hw_switch_fb303_stats
  stats
  fb303::fb303
  Folly::folly
  common_utils
)

target_link_libraries(hw_fb303_stats
  counter_utils
  fboss_error
  fb303::fb303
  Folly::folly
  switch_config_cpp2
)

target_link_libraries(hw_port_fb303_stats
  counter_utils
  fboss_types
  FBThrift::thriftcpp2
  hardware_stats_cpp2
  hw_fb303_stats
  Folly::folly
)

target_link_libraries(hw_rif_fb303_stats
  hardware_stats_cpp2
  hw_fb303_stats
)

target_link_libraries(hw_cpu_fb303_stats
  counter_utils
  FBThrift::thriftcpp2
  hardware_stats_cpp2
  Folly::folly
  switch_config_cpp2
  hw_fb303_stats
)

target_link_libraries(hw_trunk_counters
  counter_utils
  FBThrift::thriftcpp2
  hardware_stats_cpp2
  Folly::folly
  fboss_types
)

target_link_libraries(buffer_stats
  Folly::folly
)

target_link_libraries(hw_resource_stats_publisher
  fb303::fb303
  hardware_stats_cpp2
)

target_link_libraries(prbs_stats_entry
  ctrl_cpp2
)

add_executable(hw_port_fb303_stats_test
  fboss/util/oss/TestMain.cpp
  fboss/agent/hw/test/HwPortFb303StatsTests.cpp
  fboss/agent/hw/test/HwSysPortFb303StatsTests.cpp
)

target_link_libraries(hw_port_fb303_stats_test
  hw_port_fb303_stats
  switch_config_cpp2
  fb303::fb303
  Folly::folly
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(hw_port_fb303_stats_test)
