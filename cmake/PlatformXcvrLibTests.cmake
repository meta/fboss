# Make to build libraries and binaries in fboss/platform/xcvr_lib

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_executable(xcvr_lib_test
  fboss/platform/xcvr_lib/tests/XcvrLibTest.cpp
)

target_link_libraries(xcvr_lib_test
  xcvr_lib
  platform_config_lib
  platform_manager_config_cpp2
  thrift::rpc
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(xcvr_lib_test)
