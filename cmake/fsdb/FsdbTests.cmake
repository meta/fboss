# CMake to build libraries and binaries in fboss/agent/capture

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

thrift_add_library(
  thriftpath_test_cpp2
  fboss/fsdb/tests/thriftpath_test.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    cfgr_fboss_common_cpp2
    common_cpp2
)

add_library(fsdb_test_server
  fboss/fsdb/tests/utils/FsdbTestServer.h
  fboss/fsdb/tests/utils/FsdbTestServer.cpp
  fboss/fsdb/tests/utils/oss/FsdbTestServer.cpp
)

target_link_libraries(fsdb_test_server
  fsdb_handler
  fsdb_oper_cpp2
  Folly::folly
  thrift::rpc
  thrift_service_utils
)

add_library(fsdb_test_subscriber
  fboss/fsdb/tests/utils/FsdbTestSubscriber.h
)

target_link_libraries(fsdb_test_subscriber
  fsdb_pub_sub
  fsdb_model
  Folly::folly
)

add_executable(fsdb_utils_benchmark
  fboss/fsdb/tests/utils/FsdbUtilsBenchmark.cpp
)

target_link_libraries(fsdb_utils_benchmark
  fsdb_utils
  Folly::folly
  Folly::follybenchmark
  thrift::rpc
  ${GTEST}
  ${LIBGMOCK}
  ${GFLAGS}
)

# Register this executable for fsdb_all_services target
set(FSDB_EXECUTABLES ${FSDB_EXECUTABLES} fsdb_utils_benchmark CACHE INTERNAL "List of all FSDB executables")

add_library(fsdb_test_clients
  fboss/fsdb/tests/client/FsdbTestClients.h
  fboss/fsdb/tests/client/FsdbTestClients.cpp
)

target_link_libraries(fsdb_test_clients
  fsdb_pub_sub
  fsdb_stream_client
  fsdb_model
  common_utils
  Folly::folly
  thrift::rpc
  thrift::protocol
  ${GTEST}
)

add_executable(fsdb_pub_sub_tests
  fboss/fsdb/tests/client/FsdbAddPatchSubscriptionPathsTest.cpp
  fboss/fsdb/tests/client/FsdbPubSubManagerTest.cpp
  fboss/util/oss/TestMain.cpp
)

target_link_libraries(fsdb_pub_sub_tests
  log_thrift_call
  fsdb_test_clients
  fsdb_test_server
  fsdb_pub_sub
  fsdb_cpp2
  fsdb_model_cpp2
  fsdb_oper_cpp2
  patch_cpp2
  extended_path_builder
  common_utils
  thrift_service_client
  Folly::folly
  thrift::rpc
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(fsdb_pub_sub_tests)

# Register this executable for fsdb_all_services target
set(FSDB_EXECUTABLES ${FSDB_EXECUTABLES} fsdb_pub_sub_tests CACHE INTERNAL "List of all FSDB executables")
