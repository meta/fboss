# CMake to build libraries and binaries in fboss/platform/xcvr_lib

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(xcvr_lib
  fboss/platform/xcvr_lib/XcvrLib.cpp
)

target_link_libraries(xcvr_lib
  fmt::fmt
  platform_config_lib
  platform_manager_config_cpp2
  platform_manager_system_interface
  Folly::folly
  thrift::rpc
)
