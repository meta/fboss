# Make to build libraries and binaries in fboss/platform/weutils

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_fbthrift_cpp_library(
  sensor_service_cpp2
  fboss/platform/sensor_service/if/sensor_service.thrift
  SERVICES
    SensorServiceThrift
  OPTIONS
    json
    reflection
  DEPENDS
    fboss_cpp2
    reboot_cause_config_cpp2
    sensor_config_cpp2
)

add_fbthrift_cpp_library(
  sensor_config_cpp2
  fboss/platform/sensor_service/if/sensor_config.thrift
  OPTIONS
    json
    reflection
)

add_library(power_config_utils INTERFACE)

target_include_directories(power_config_utils INTERFACE
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(power_config_utils INTERFACE
  sensor_config_cpp2
  ${RE2}
)

add_library(sensor_service_config_validator
  fboss/platform/sensor_service/ConfigValidator.cpp
)

target_link_libraries(sensor_service_config_validator
  fmt::fmt
  platform_manager_config_validator
  platform_manager_utils
  power_config_utils
  sensor_config_cpp2
  Folly::folly
)

add_library(sensor_service_utils
  fboss/platform/sensor_service/Utils.cpp
  fboss/platform/sensor_service/PmUnitInfoFetcher.cpp
  fboss/platform/sensor_service/oss/PmClientFactory.cpp
)

target_link_libraries(sensor_service_utils
  sensor_config_cpp2
  sensor_service_config_validator
  platform_config_lib
  platform_name_lib
  platform_manager_service_cpp2
  platform_manager_config_cpp2
  platform_manager_config_validator
  ${RE2}
)

add_library(sensor_service_lib
  fboss/platform/sensor_service/FsdbSyncer.cpp
  fboss/platform/sensor_service/Flags.cpp
  fboss/platform/sensor_service/SensorServiceImpl.cpp
  fboss/platform/sensor_service/SensorServiceThriftHandler.cpp
  fboss/platform/sensor_service/oss/FsdbSyncer.cpp
)

target_link_libraries(sensor_service_lib
  log_thrift_call
  platform_config_lib
  platform_manager_config_validator
  platform_name_lib
  platform_utils
  power_config_utils
  structured_logger
  sensor_service_utils
  sensor_service_cpp2
  sensor_service_stats_cpp2
  sensor_config_cpp2
  reboot_cause_config_cpp2
  reboot_history_lib
  Folly::folly
  FBThrift::thriftcpp2
  fsdb_stream_client
  fsdb_pub_sub
  fsdb_flags
)

add_executable(sensor_service
  fboss/platform/sensor_service/Main.cpp
)

target_link_libraries(sensor_service
  sensor_service_lib
  fb303::fb303
)

add_executable(sensor_service_client
  fboss/platform/sensor_service/SensorServicePlainTextClient.cpp
)

target_link_libraries(sensor_service_client
  sensor_service_cpp2
  platform_utils
  tabulate::tabulate
  Folly::folly
  FBThrift::thriftcpp2
  CLI11::CLI11
)

install(TARGETS sensor_service)

# TODO: paulcruz74 for the sake for consistency, this should technically live in `PlatformSensorServiceHwTest.cmake`
add_executable(sensor_service_hw_test
  fboss/platform/sensor_service/hw_test/SensorServiceHwTest.cpp
)

target_link_libraries(sensor_service_hw_test
  power_config_utils
  sensor_service_lib
  thrift_service_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

install(TARGETS sensor_service_hw_test)
