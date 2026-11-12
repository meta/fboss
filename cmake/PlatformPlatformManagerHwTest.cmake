# Make to build libraries and binaries in fboss/platform/platform_manager/hw_test

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_executable(platform_manager_hw_test
  fboss/platform/platform_manager/hw_test/PlatformManagerHwTest.cpp
)

target_link_libraries(platform_manager_hw_test
  fmt::fmt
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  platform_utils
  platform_manager_config_utils
  platform_manager_pkg_manager
  platform_manager_handler
  thrift_service_utils
  xcvr_lib
  thrift::rpc
)
