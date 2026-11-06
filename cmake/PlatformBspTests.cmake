
add_fbthrift_cpp_library(
  bsp_tests_config_cpp2
  fboss/platform/bsp_tests/bsp_tests_config.thrift
  OPTIONS
    json
    reflection
)

add_fbthrift_cpp_library(
  fbiob_device_config_cpp2
  fboss/platform/bsp_tests/fbiob_device_config.thrift
  OPTIONS
    json
    reflection
)

add_fbthrift_cpp_library(
  bsp_tests_runtime_config_cpp2
  fboss/platform/bsp_tests/bsp_tests_runtime_config.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    fbiob_device_config_cpp2
    platform_manager_config_cpp2
    bsp_tests_config_cpp2
)


add_library(bsp_test_utils
  fboss/platform/bsp_tests/utils/CdevUtils.cpp
  fboss/platform/bsp_tests/utils/GpioUtils.cpp
  fboss/platform/bsp_tests/utils/HwmonUtils.cpp
  fboss/platform/bsp_tests/utils/KmodUtils.cpp
  fboss/platform/bsp_tests/utils/I2CUtils.cpp
  fboss/platform/bsp_tests/utils/IdpromUtils.cpp
  fboss/platform/bsp_tests/utils/WatchdogUtils.cpp
)

target_link_libraries(bsp_test_utils
  fmt::fmt
  ${GTEST}
  bsp_tests_config_cpp2
  bsp_tests_runtime_config_cpp2
  fbiob_device_config_cpp2
  platform_utils
  platform_manager_i2c_explorer
  platform_manager_pm_unit_identity
  weutil_fboss_eeprom_interface
  platform_manager_system_interface
  platform_manager_config_cpp2
  platform_manager_fan_cpld_manager
  platform_manager_pci_explorer
  gpiod_line
  ${LIBGPIOD}
  Folly::folly
  ${RE2}
)

add_library(bsp_test_environment
  fboss/platform/bsp_tests/BspTestEnvironment.cpp
  fboss/platform/bsp_tests/RuntimeConfigBuilder.cpp
)

target_link_libraries(bsp_test_environment
  ${GTEST}
  bsp_tests_config_cpp2
  bsp_tests_runtime_config_cpp2
  platform_config_lib
  platform_name_lib
  platform_manager_pkg_manager
  platform_manager_config_cpp2
  platform_manager_utils
  Folly::folly
  FBThrift::thriftcpp2
  fmt::fmt
)

add_executable(bsp_tests
  fboss/platform/bsp_tests/BspTest.cpp
  fboss/platform/bsp_tests/BspTestRunner.cpp
  fboss/platform/bsp_tests/CdevTests.cpp
  fboss/platform/bsp_tests/GpioTests.cpp
  fboss/platform/bsp_tests/KmodTests.cpp
  fboss/platform/bsp_tests/I2CTests.cpp
  fboss/platform/bsp_tests/LedTests.cpp
  fboss/platform/bsp_tests/WatchdogTests.cpp
  fboss/platform/bsp_tests/XcvrTests.cpp
  fboss/platform/bsp_tests/HwmonTests.cpp
  fboss/platform/bsp_tests/MdioTests.cpp
)

target_link_libraries(bsp_tests
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  bsp_test_environment
  bsp_test_utils
  platform_manager_i2c_explorer
  platform_manager_config_cpp2
  device_mdio
  Folly::folly
  ${GFLAGS}
)

add_executable(runtime_config_builder_test
  fboss/platform/bsp_tests/RuntimeConfigBuilderTest.cpp
)

target_link_libraries(runtime_config_builder_test
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  bsp_test_environment
  platform_manager_utils
  Folly::folly
  FBThrift::thriftcpp2
)
