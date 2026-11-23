# CMake to build libraries and binaries in fboss/agent

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(fsdb_oper_metadata_tracker
  fboss/fsdb/server/FsdbOperTreeMetadataTracker.cpp
  fboss/fsdb/server/OperPathToPublisherRoot.cpp
)

target_link_libraries(fsdb_oper_metadata_tracker
  Folly::folly
  fsdb_common_cpp2
  fsdb_oper_cpp2
)

add_library(fsdb_handler
  fboss/fsdb/server/FsdbConfig.cpp
  fboss/fsdb/server/ServiceHandler.cpp
)

target_link_libraries(fsdb_handler
  fsdb_common_cpp2
  fsdb_config_cpp2
  fsdb_cpp2
  fsdb_model_cpp2
  fsdb_oper_metadata_tracker
  fsdb_naive_periodic_subscribable_storage
  fsdb_flags
  log_thrift_call
  oper_path_helpers
  Folly::folly
  thrift::rpc
  fb303::fb303
)

add_library(fsdb_server
  fboss/fsdb/server/Server.cpp
  fboss/fsdb/server/oss/Server.cpp
)

target_link_libraries(fsdb_server
  fsdb_handler
  fsdb_utils
  fsdb_flags
  restart_time_tracker
  Folly::folly
  thrift_service_utils
)

add_executable(fsdb
  fboss/fsdb/server/FsdbMain.cpp
)

target_link_libraries(fsdb
  fsdb_server
  fboss_init
  restart_time_tracker
)

# Register this executable for fsdb_all_services target
set(FSDB_EXECUTABLES ${FSDB_EXECUTABLES} fsdb CACHE INTERNAL "List of all FSDB executables")
