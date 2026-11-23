# Make to build libraries and binaries in fboss/platform/weutils

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake


add_executable(platform_data_corral_sw_test
  fboss/platform/data_corral_service/tests/ConfigValidatorTest.cpp
  fboss/platform/data_corral_service/tests/DataCorralServiceThriftHandlerTest.cpp
  fboss/platform/data_corral_service/tests/FruPresenceExplorerTests.cpp
  fboss/platform/data_corral_service/tests/LedManagerTests.cpp
)

target_link_libraries(platform_data_corral_sw_test
  common_file_utils
  data_corral_service_lib
  Folly::folly
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_data_corral_sw_test)
