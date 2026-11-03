# Make to build libraries and binaries in fboss/platform/weutils

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_fbthrift_cpp_library(
  fw_util_config-cpp2-types
  fboss/platform/fw_util/if/fw_util_config.thrift
  OPTIONS
    json
    reflection
)

add_library(fw_util_libs
  fboss/platform/fw_util/FwUtilImpl.cpp
  fboss/platform/fw_util/FwUtilVerify.cpp
  fboss/platform/fw_util/FwUtilRead.cpp
  fboss/platform/fw_util/FwUtilPreUpgrade.cpp
  fboss/platform/fw_util/FwUtilPostUpgrade.cpp
  fboss/platform/fw_util/FwUtilUpgrade.cpp
  fboss/platform/fw_util/FwUtilOperations.cpp
  fboss/platform/fw_util/FwUtilFlashrom.cpp
  fboss/platform/fw_util/fw_util_helpers.cpp
  fboss/platform/fw_util/FwUtilVersionHandler.cpp
  fboss/platform/fw_util/ConfigValidator.cpp
)

target_link_libraries(fw_util_libs
  Folly::folly
  CLI11::CLI11
  platform_config_lib
  platform_name_lib
  platform_utils
  FBThrift::thriftcpp2
  fw_util_config-cpp2-types
)

add_executable(fw_util
  fboss/platform/fw_util/fw_util.cpp
)

target_link_libraries(fw_util
  fw_util_libs
)

add_executable(fw_util_sw_test
  fboss/platform/fw_util/tests/ConfigValidatorTest.cpp
)

target_link_libraries(fw_util_sw_test
  fw_util_libs
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(fw_util_sw_test)

install(TARGETS fw_util_sw_test)

add_executable(fw_util_hw_test
  fboss/platform/fw_util/hw_test/FwUtilHwTest.cpp
)

target_link_libraries(fw_util_hw_test
  fw_util_libs
  ${LIBGMOCK_LIBRARIES}
)

install(TARGETS fw_util)
