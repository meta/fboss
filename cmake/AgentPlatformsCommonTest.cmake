# CMake to build tests in fboss/agent/platforms/common/tests

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_executable(platform_mapping_vendor_override_test
  fboss/agent/platforms/common/tests/PlatformMappingVendorOverrideTest.cpp
)

target_link_libraries(platform_mapping_vendor_override_test
  platform_mapping
  Folly::folly
  Folly::folly_test_util
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(platform_mapping_vendor_override_test)
