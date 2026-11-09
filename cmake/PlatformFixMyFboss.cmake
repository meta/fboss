# CMake to build libraries and binaries in fboss/platform/fixmyfboss

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(result_printer
  fboss/platform/fixmyfboss/ResultPrinter.cpp
)

target_link_libraries(result_printer
  check_types_cpp2
)

add_library(fixmyfboss_lib
  fboss/platform/fixmyfboss/CheckEnvironment.cpp
  fboss/platform/fixmyfboss/CheckRegistry.cpp
  fboss/platform/fixmyfboss/CheckRunner.cpp
  fboss/platform/fixmyfboss/CliOptions.cpp
  fboss/platform/fixmyfboss/Extensions.cpp
  fboss/platform/fixmyfboss/PasswordPrompt.cpp
)

target_link_libraries(fixmyfboss_lib
  ${RE2}
  CLI11::CLI11
  check_types_cpp2
  platform_checks
  platform_checks_host
  platform_checks_platform_name
)

add_executable(fixmyfboss
  fboss/platform/fixmyfboss/main.cpp
)

target_link_libraries(fixmyfboss
  fixmyfboss_lib
  result_printer
  CLI11::CLI11
)

add_executable(fixmyfboss_result_printer_test
  fboss/platform/fixmyfboss/tests/ResultPrinterTest.cpp
)

target_link_libraries(fixmyfboss_result_printer_test
  result_printer
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(fixmyfboss_result_printer_test)

add_executable(fixmyfboss_check_runner_test
  fboss/platform/fixmyfboss/tests/CheckRunnerTest.cpp
)

target_link_libraries(fixmyfboss_check_runner_test
  fixmyfboss_lib
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(fixmyfboss_check_runner_test)

add_executable(fixmyfboss_check_environment_test
  fboss/platform/fixmyfboss/tests/CheckEnvironmentTest.cpp
)

target_link_libraries(fixmyfboss_check_environment_test
  fixmyfboss_lib
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(fixmyfboss_check_environment_test)

add_executable(fixmyfboss_extensions_test
  fboss/platform/fixmyfboss/tests/ExtensionsTest.cpp
)

target_link_libraries(fixmyfboss_extensions_test
  fixmyfboss_lib
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(fixmyfboss_extensions_test)

add_executable(fixmyfboss_cli_options_test
  fboss/platform/fixmyfboss/tests/CliOptionsTest.cpp
)

target_link_libraries(fixmyfboss_cli_options_test
  fixmyfboss_lib
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(fixmyfboss_cli_options_test)
