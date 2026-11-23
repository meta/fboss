# CMake to build FsdbCgoPubSubWrapper library for CGO (Go<->C++ interop)

include_directories(
    ${LIBGMOCK_INCLUDE_DIR}
    ${GTEST_INCLUDE_DIRS}
)

# FsdbCgoPubSubWrapper library
add_library(fsdb_cgo_pub_sub_wrapper STATIC
  fboss/fsdb/client/cgo/FsdbCgoPubSubWrapper.cpp
)

# Match BUCK; only FSDB_CGO_API entry points stay exported.
target_compile_options(fsdb_cgo_pub_sub_wrapper PRIVATE
  -fvisibility=hidden
  -fvisibility-inlines-hidden
)

set(fsdb_cgo_pub_sub_wrapper_libs
  fsdb_pub_sub
  fsdb_model
  thrift_service_client
  Folly::folly
  thrift::rpc
)

target_link_libraries(fsdb_cgo_pub_sub_wrapper ${fsdb_cgo_pub_sub_wrapper_libs})
