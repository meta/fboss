# CMake to build libraries and binaries in fboss/lib

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(radix_tree
  fboss/lib/RadixTree.h
  fboss/lib/RadixTree-inl.h
)

target_link_libraries(radix_tree
  Folly::folly
  glog::glog
)

add_library(log_thrift_call
  fboss/lib/LogThriftCall.cpp
  fboss/lib/oss/LogThriftCall.cpp
)

target_link_libraries(log_thrift_call
  Folly::folly
  FBThrift::thriftcpp2
)

add_library(alert_logger
  fboss/lib/AlertLogger.cpp
)

target_link_libraries(alert_logger
  Folly::folly
  fmt::fmt
)

add_library(ref_map
  fboss/lib/RefMap.h
)

set_target_properties(ref_map PROPERTIES LINKER_LANGUAGE CXX)

add_library(tuple_utils
  fboss/lib/TupleUtils.h
)

set_target_properties(tuple_utils PROPERTIES LINKER_LANGUAGE CXX)

add_library(exponential_back_off
  fboss/lib/ExponentialBackoff.cpp
)

target_link_libraries(exponential_back_off
  Folly::folly
  glog::glog
)

add_library(function_call_time_reporter
  fboss/lib/FunctionCallTimeReporter.cpp
)

target_link_libraries(function_call_time_reporter
  Folly::folly
)

add_library(common_file_utils
  fboss/lib/CommonFileUtils.cpp
)

target_link_libraries(common_file_utils
  fboss_error
  Boost::filesystem
)

add_library(common_port_utils
  fboss/lib/CommonPortUtils.cpp
)

target_link_libraries(common_port_utils
  fboss_error
)

add_library(common_utils
  fboss/lib/CommonUtils.cpp
)

target_link_libraries(common_utils
  common_file_utils
  fboss_error
  fb303::fb303
)

add_library(thread_heartbeat
  fboss/lib/ThreadHeartbeat.cpp
)

target_link_libraries(thread_heartbeat
  Folly::folly
)

add_library(thrift_method_rate_limit
  fboss/lib/ThriftMethodRateLimit.cpp
)

target_link_libraries(thrift_method_rate_limit
  Folly::folly
  FBThrift::thriftcpp2
)

add_library(thrift_method_rate_limit_setup
  fboss/lib/ThriftMethodRateLimitSetup.cpp
)

target_link_libraries(thrift_method_rate_limit_setup
  thrift_method_rate_limit
  fb303::fb303
  Folly::folly
  FBThrift::thriftcpp2
)

find_library(PCIACCESS pciaccess)
add_library(pci_device
  fboss/lib/PciDevice.cpp
  fboss/lib/PciSystem.cpp
)

target_link_libraries(pci_device
  Folly::folly
  ${PCIACCESS}
)

add_library(physical_memory
  fboss/lib/PhysicalMemory.cpp
)

target_link_libraries(physical_memory
  Folly::folly
)

add_library(pci_access
  fboss/lib/PciAccess.cpp
)


add_library(hw_write_behavior
  fboss/lib/HwWriteBehavior.cpp
)

target_link_libraries(hw_write_behavior
  common_cpp2
)

add_library(common_thrift_utils
  fboss/lib/CommonThriftUtils.cpp
)

target_link_libraries(common_thrift_utils
  Folly::folly
  fb303::fb303
  thrift_service_client
)

add_library(gpiod_line
  fboss/lib/GpiodLine.cpp
)

target_link_libraries(gpiod_line
  Folly::folly
  ${LIBGPIOD}
)

add_library(restart_time_tracker
  fboss/lib/restart_tracker/RestartTimeTracker.cpp
)

target_link_libraries(restart_time_tracker
  utils
  fb303::fb303
  Folly::folly
)

add_library(warm_boot_file_utils
  fboss/lib/WarmBootFileUtils.cpp
)

target_link_libraries(warm_boot_file_utils
  fboss_error
  switch_state_cpp2
  utils
  state
  Folly::folly
)

add_library(rest_client
  fboss/lib/RestClient.cpp
)

target_link_libraries(rest_client
  fboss_error
  Folly::folly
  ${CURL}
)

add_library(bmc_rest_client
  fboss/lib/BmcRestClient.cpp
)

target_link_libraries(bmc_rest_client
  rest_client
  Folly::folly
)

add_library(thrift_service_utils
  fboss/lib/oss/ThriftServiceUtils.cpp
)

target_link_libraries(thrift_service_utils
  Folly::folly
  FBThrift::thriftcpp2
)
