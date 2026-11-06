# CMake to build libraries and binaries in fboss/platform/platform_checks

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_fbthrift_cpp_library(
  check_types_cpp2
  fboss/platform/platform_checks/check_types.thrift
  OPTIONS
    json
    reflection
)

add_library(platform_check
  fboss/platform/platform_checks/CommandLog.cpp
  fboss/platform/platform_checks/PlatformCheck.cpp
)

target_link_libraries(platform_check
  check_types_cpp2
  fmt::fmt
  platform_checks_host
  platform_config_lib
  platform_manager_config_cpp2
)

add_library(platform_checks_host
  fboss/platform/platform_checks/Host.cpp
  fboss/platform/platform_checks/LocalHost.cpp
  fboss/platform/platform_checks/RemoteHost.cpp
  fboss/platform/platform_checks/SshAskPass.cpp
)

target_link_libraries(platform_checks_host
  expect_session
  Folly::folly
  fmt::fmt
)

add_library(platform_checks_platform_name
  fboss/platform/platform_checks/PlatformName.cpp
)

target_link_libraries(platform_checks_platform_name
  platform_checks_host
  platform_name_lib
  Folly::folly
)

add_library(platform_checks
  fboss/platform/platform_checks/HostEeprom.cpp
  fboss/platform/platform_checks/checks/BmcChecks.cpp
  fboss/platform/platform_checks/checks/CommandCheck.cpp
  fboss/platform/platform_checks/checks/HwTestChecks.cpp
  fboss/platform/platform_checks/checks/MacAddressCheck.cpp
  fboss/platform/platform_checks/checks/ManagementPlaneChecks.cpp
  fboss/platform/platform_checks/checks/PciDeviceCheck.cpp
  fboss/platform/platform_checks/checks/PowerCycleChecks.cpp
  fboss/platform/platform_checks/checks/PowerResetCheck.cpp
  fboss/platform/platform_checks/checks/SolConsoleCheck.cpp
  # Not including KernelVersionCheck since it relies on internal tools
  fboss/platform/platform_checks/checks/i801SmbusTimeoutCheck.cpp
)

target_link_libraries(platform_checks
  platform_check
  platform_manager_config_cpp2
  weutil_fboss_eeprom_interface
  weutil_config_utils
  Folly::folly
  ${RE2}
)

add_executable(mac_address_check_test
  fboss/platform/platform_checks/tests/MacAddressCheckTest.cpp
)

target_link_libraries(mac_address_check_test
  platform_checks
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(mac_address_check_test)

add_executable(pci_device_check_test
  fboss/platform/platform_checks/tests/PciDeviceCheckTest.cpp
)

target_link_libraries(pci_device_check_test
  platform_checks
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(pci_device_check_test)

add_executable(platform_checks_local_host_test
  fboss/platform/platform_checks/tests/LocalHostTest.cpp
)

target_link_libraries(platform_checks_local_host_test
  platform_checks_host
  Folly::folly
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_checks_local_host_test)

add_executable(platform_checks_remote_host_test
  fboss/platform/platform_checks/tests/RemoteHostTest.cpp
)

target_link_libraries(platform_checks_remote_host_test
  platform_checks_host
  Folly::folly
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_checks_remote_host_test)

add_executable(platform_checks_platform_name_test
  fboss/platform/platform_checks/tests/PlatformNameTest.cpp
)

target_link_libraries(platform_checks_platform_name_test
  platform_checks_platform_name
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_checks_platform_name_test)

add_executable(platform_checks_bmc_reachable_check_test
  fboss/platform/platform_checks/tests/BmcReachableCheckTest.cpp
)

target_link_libraries(platform_checks_bmc_reachable_check_test
  platform_checks
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_checks_bmc_reachable_check_test)

add_executable(platform_checks_management_plane_checks_test
  fboss/platform/platform_checks/tests/ManagementPlaneChecksTest.cpp
)

target_link_libraries(platform_checks_management_plane_checks_test
  platform_checks
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_checks_management_plane_checks_test)

add_executable(platform_checks_bmc_checks_test
  fboss/platform/platform_checks/tests/BmcChecksTest.cpp
)

target_link_libraries(platform_checks_bmc_checks_test
  platform_checks
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_checks_bmc_checks_test)

add_executable(platform_checks_sol_console_check_test
  fboss/platform/platform_checks/tests/SolConsoleCheckTest.cpp
)

target_link_libraries(platform_checks_sol_console_check_test
  platform_checks
  Folly::folly
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_checks_sol_console_check_test)

add_executable(platform_checks_power_cycle_checks_test
  fboss/platform/platform_checks/tests/PowerCycleChecksTest.cpp
)

target_link_libraries(platform_checks_power_cycle_checks_test
  platform_checks
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_checks_power_cycle_checks_test)

add_executable(platform_checks_hw_test_checks_test
  fboss/platform/platform_checks/tests/HwTestChecksTest.cpp
)

target_link_libraries(platform_checks_hw_test_checks_test
  platform_checks
  Folly::folly
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_checks_hw_test_checks_test)
