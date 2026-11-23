  # CMake to build config generation CLI in fboss/lib/bsp/bspmapping

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_executable(fboss-bspmapping-gen
    fboss/lib/bsp/bspmapping/Parser.h
    fboss/lib/bsp/bspmapping/Parser.cpp
    fboss/lib/bsp/bspmapping/Main.cpp
)

target_link_libraries(fboss-bspmapping-gen
  bsp_platform_mapping_cpp2
  fboss_common_cpp2
  platform_mode
  Folly::folly
  thrift::rpc
)

install(TARGETS fboss-bspmapping-gen)
