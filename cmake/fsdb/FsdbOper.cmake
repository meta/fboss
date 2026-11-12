# CMake to build libraries and binaries in fboss/agent

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(subscription_manager
  fboss/fsdb/oper/DeltaValue.h
  fboss/fsdb/oper/CowDeletePathTraverseHelper.h
  fboss/fsdb/oper/CowInitialSyncTraverseHelper.h
  fboss/fsdb/oper/CowPublishAndAddTraverseHelper.h
  fboss/fsdb/oper/CowPublishAndAddTraverseHelper.cpp
  fboss/fsdb/oper/CowSubscriptionManager.h
  fboss/fsdb/oper/CowSubscriptionTraverseHelper.h
  fboss/fsdb/oper/DeltaValue.cpp
  fboss/fsdb/oper/Subscription.cpp
  fboss/fsdb/oper/Subscription.h
  fboss/fsdb/oper/SubscriptionManager.h
  fboss/fsdb/oper/SubscriptionManager.cpp
  fboss/fsdb/oper/SubscriptionMetadataServer.cpp
  fboss/fsdb/oper/SubscriptionMetadataServer.h
  fboss/fsdb/oper/SubscriptionPathStore.cpp
  fboss/fsdb/oper/SubscriptionPathStore.h
  fboss/fsdb/oper/SubscriptionStore.cpp
  fboss/fsdb/oper/SubscriptionStore.h
)

target_link_libraries(subscription_manager
  cow_storage
  Folly::folly
  fsdb_common_cpp2
  fsdb_cpp2
  fsdb_oper_cpp2
  fsdb_model
  fsdb_utils
  fsdb_oper_metadata_tracker
  patch_cpp2
  thrift_cow_visitors
)

add_library(extended_path_builder
  fboss/fsdb/oper/ExtendedPathBuilder.cpp
  fboss/fsdb/oper/ExtendedPathBuilder.h
)

target_link_libraries(extended_path_builder
  fsdb_oper_cpp2
  thrift::rpc
  Folly::folly
)

add_library(oper_path_helpers
  fboss/fsdb/oper/PathConverter.cpp
  fboss/fsdb/oper/PathConverter.h
  fboss/fsdb/oper/PathValidator.cpp
  fboss/fsdb/oper/PathValidator.h
)

target_link_libraries(oper_path_helpers
  fsdb_config_cpp2
  fsdb_common_cpp2
  fsdb_utils
  fsdb_oper_cpp2
  fsdb_model
  thrift_visitors
  ${RE2}
)

add_library(subscribable_storage
  fboss/fsdb/oper/NaivePeriodicSubscribableStorageBase.h
  fboss/fsdb/oper/NaivePeriodicSubscribableStorageBase.cpp
  fboss/fsdb/oper/NaivePeriodicSubscribableStorage.h
  fboss/fsdb/oper/SubscribableStorage.h
)

target_link_libraries(subscribable_storage
  cow_storage
  oper_path_helpers
  fsdb_config_cpp2
  fsdb_common_cpp2
  fsdb_oper_cpp2
  fsdb_model
  patch_cpp2
  subscription_manager
  thread_heartbeat
  thrift_visitors
  Folly::folly
  fb303::fb303
)
