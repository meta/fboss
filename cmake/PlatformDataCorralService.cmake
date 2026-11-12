# Make to build libraries and binaries in fboss/platform/weutils

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

thrift_add_library(
  data_corral_service_cpp2
  fboss/platform/data_corral_service/if/data_corral_service.thrift
  LANGUAGE cpp2
  SERVICES
    DataCorralServiceThrift
  OPTIONS
    json
    reflection
  DEPENDS
    fb303_cpp2
    fboss_cpp2
)

add_library(led_manager_config_types_cpp2)
thrift_generate(
  led_manager_config_types_cpp2
  fboss/platform/data_corral_service/if/led_manager_config.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  INCLUDE_DIRECTORIES
    ${PROJECT_SOURCE_DIR}
    ${THRIFT_INCLUDE_DIR}
)
target_include_directories(led_manager_config_types_cpp2
  PUBLIC ${PROJECT_BINARY_DIR}
)

add_library(data_corral_service_config_validator
  fboss/platform/data_corral_service/ConfigValidator.cpp
)

target_link_libraries(data_corral_service_config_validator
  fmt::fmt
  led_manager_config_types_cpp2
  Folly::folly
)

add_library(data_corral_service_lib
  fboss/platform/data_corral_service/DataCorralServiceThriftHandler.cpp
  fboss/platform/data_corral_service/FruPresenceExplorer.cpp
  fboss/platform/data_corral_service/LedManager.cpp
)

target_link_libraries(data_corral_service_lib
  log_thrift_call
  product_info
  common_file_utils
  weutil_lib
  platform_utils
  platform_name_lib
  structured_logger
  data_corral_service_cpp2
  led_manager_config_types_cpp2
  Folly::folly
  fb303::fb303
  thrift::rpc
  platform_manager_utils
  data_corral_service_config_validator
)

add_executable(data_corral_service
  fboss/platform/data_corral_service/Main.cpp
)

target_link_libraries(data_corral_service
  data_corral_service_lib
)

add_executable(data_corral_service_hw_test
  fboss/platform/data_corral_service/hw_test/DataCorralServiceHwTest.cpp
)

target_link_libraries(data_corral_service_hw_test
  data_corral_service_lib
  thrift_service_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)
