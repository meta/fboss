# CMake to build libraries and binaries in fboss/agent

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

include_directories(
    ${LIBGMOCK_INCLUDE_DIR}
    ${GTEST_INCLUDE_DIRS}
)

add_library(fsdb_stream_client
  fboss/fsdb/client/FsdbStreamClient.cpp
)

set(fsdb_stream_client_libs
  fsdb_utils
  Folly::folly
  thrift::rpc
  common_thrift_utils
  fsdb_oper_cpp2
  fsdb_cpp2
  thrift_service_client
)

target_link_libraries(fsdb_stream_client ${fsdb_stream_client_libs})

set(fsdb_pub_sub_files
  fboss/fsdb/oper/DeltaValue.h
  fboss/fsdb/client/FsdbSubscriber.cpp
  fboss/fsdb/client/FsdbPublisher.cpp
  fboss/fsdb/client/FsdbPubSubManager.cpp
  fboss/fsdb/client/FsdbDeltaPublisher.cpp
  fboss/fsdb/client/FsdbDeltaSubscriber.cpp
  fboss/fsdb/client/FsdbStatePublisher.cpp
  fboss/fsdb/client/FsdbStateSubscriber.cpp
  fboss/fsdb/client/FsdbPatchPublisher.cpp
  fboss/fsdb/client/FsdbPatchSubscriber.cpp
  fboss/fsdb/oper/DeltaValue.cpp
)

add_library(fsdb_pub_sub ${fsdb_pub_sub_files})

set(fsdb_pub_sub_libs
  fsdb_common_cpp2
  fsdb_flags
  fsdb_oper_cpp2
  patch_cpp2
  fsdb_stream_client
  Folly::folly
  thrift::rpc
  fb303::fb303
  fsdb_cpp2
  fsdb_utils
)

target_link_libraries(fsdb_pub_sub ${fsdb_pub_sub_libs})

add_library(fsdb_sub_mgr
  fboss/fsdb/client/FsdbSubManagerBase.cpp
)

target_link_libraries(fsdb_sub_mgr
  fsdb_flags
  fsdb_pub_sub
  fsdb_model
  oper_path_helpers
  thrift_service_client
)

add_library(fsdb_syncer
  fboss/fsdb/client/FsdbSyncManager.h
  fboss/fsdb/client/FsdbSyncManager.cpp
)

target_link_libraries(fsdb_syncer
  cow_storage_mgr
  fsdb_common_cpp2
  fsdb_flags
  fsdb_oper_cpp2
  fsdb_stream_client
  fsdb_pub_sub
  Folly::folly
  thrift::rpc
  fb303::fb303
)
