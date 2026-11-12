# CMake to build libraries and binaries in fboss/agent

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(fsdb_flags
  fboss/fsdb/common/Flags.cpp
)

target_link_libraries(fsdb_flags
  fsdb_common_cpp2
)

add_library(fsdb_utils
  fboss/fsdb/common/PathHelpers.cpp
  fboss/fsdb/common/Utils.cpp
)

target_link_libraries(fsdb_utils
  fsdb_common_cpp2
  fsdb_oper_cpp2
  cow_storage
  thrift_cow_visitors
  Folly::folly
  thrift::rpc
)
