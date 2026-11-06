# Make to build libraries and binaries in fboss/platform/helpers

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_executable(platform_helpers_platform_name_lib_test
  fboss/platform/helpers/tests/PlatformNameLibTest.cpp
)

target_link_libraries(platform_helpers_platform_name_lib_test
  platform_utils
  platform_name_lib
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_helpers_platform_name_lib_test)

add_executable(platform_helpers_platform_utils_test
  fboss/platform/helpers/tests/PlatformUtilsTest.cpp
)

target_link_libraries(platform_helpers_platform_utils_test
  platform_utils
  Folly::folly
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_helpers_platform_utils_test)

add_executable(platform_helpers_platform_fs_utils_test
  fboss/platform/helpers/tests/PlatformFsUtilsTest.cpp
)

target_link_libraries(platform_helpers_platform_fs_utils_test
  platform_fs_utils
  Folly::folly
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_helpers_platform_fs_utils_test)

add_executable(platform_helpers_expect_session_test
  fboss/platform/helpers/tests/ExpectSessionTest.cpp
)

target_link_libraries(platform_helpers_expect_session_test
  expect_session
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_helpers_expect_session_test)
