# CMake to build libraries and binaries in fboss/lib/platforms/

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(platform_descriptor
  fboss/lib/platforms/PlatformDescriptor.cpp
)

target_link_libraries(platform_descriptor
  fboss_common_cpp2
  fboss_error
  platform_config_cpp2
  platform_descriptor_cpp2
  split_platform_mapping_utils
  switch_config_cpp2
  FBThrift::thriftcpp2
  Folly::folly
  gflags
)

add_library(split_platform_mapping_utils
  fboss/lib/platforms/PlatformMappingUtils.cpp
)

target_link_libraries(split_platform_mapping_utils
  fboss_error
  platform_config_cpp2
  Folly::folly
  FBThrift::thriftcpp2
)

add_library(product_info
  fboss/lib/platforms/PlatformProductInfo.cpp
  fboss/lib/platforms/oss/PlatformProductInfo.cpp
)

target_link_libraries(product_info
  fboss_common_cpp2
  platform_descriptor
  product_info_cpp2
  Folly::folly
  fboss_error
)

add_library(platform_mode
  fboss/lib/platforms/PlatformMode.h
)

target_link_libraries(platform_mode
  fboss_common_cpp2
)
