# CMake to build libraries and binaries in fboss/agent/platforms/common/leh800bcls

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(leh800bcls_platform_mapping
  fboss/agent/platforms/common/leh800bcls/Leh800bclsPlatformMapping.cpp
)

target_link_libraries(leh800bcls_platform_mapping
  platform_mapping
  product_info
)
