# CMake to build libraries and binaries in fboss/agent

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(fsdb_cow_root
  fboss/fsdb/oper/instantiations/FsdbHybridStateCowRoot.cpp
  fboss/fsdb/oper/instantiations/FsdbStatCowRoot.cpp
  fboss/fsdb/oper/instantiations/FsdbStateCowRoot.cpp
)

target_link_libraries(fsdb_cow_root
  fsdb_model
  thrift_cow_nodes
)

add_library(fsdb_cow_root_path_visitor
  fboss/fsdb/oper/instantiations/FsdbCowHybridStateRootPathVisitor.cpp
  fboss/fsdb/oper/instantiations/FsdbCowStateRootPathVisitor.cpp
  fboss/fsdb/oper/instantiations/FsdbCowStatsRootPathVisitor.cpp
)

target_link_libraries(fsdb_cow_root_path_visitor
  fsdb_cow_root
  fsdb_model
  thrift_cow_visitors
)

add_library(fsdb_patch_applier_oper_state_instantiations
  fboss/fsdb/oper/instantiations/templates/FsdbPatchApplierOperStateInstantiations.cpp
)

target_link_libraries(fsdb_patch_applier_oper_state_instantiations
  fsdb_model
  thrift_cow_nodes
  thrift_cow_visitors
)

add_library(fsdb_thrift_struct_oper_instantiations
  fboss/fsdb/oper/instantiations/templates/FsdbThriftStructAgentDataInstantiations.cpp
  fboss/fsdb/oper/instantiations/templates/FsdbThriftStructOperStateInstantiations.cpp
)

target_link_libraries(fsdb_thrift_struct_oper_instantiations
  fsdb_model
  thrift_cow_nodes
)

add_library(fsdb_path_visitor_oper_state_instantiations
  fboss/fsdb/oper/instantiations/templates/FsdbPathVisitorOperStateInstantiations.cpp
)

target_link_libraries(fsdb_path_visitor_oper_state_instantiations
  fsdb_model
  thrift_cow_visitors
)

add_library(fsdb_cow_storage
  fboss/fsdb/oper/instantiations/FsdbCowStateStorage.cpp
  fboss/fsdb/oper/instantiations/FsdbCowStateStoragePatch.cpp
  fboss/fsdb/oper/instantiations/FsdbCowStatsStorage.cpp
  fboss/fsdb/oper/instantiations/FsdbCowHybridStateStorage.cpp
  fboss/fsdb/oper/instantiations/FsdbCowHybridStateStoragePatch.cpp
)

target_link_libraries(fsdb_cow_storage
  fsdb_model
  cow_storage
  fsdb_cow_root
  fsdb_cow_root_path_visitor
  fsdb_patch_applier_oper_state_instantiations
  fsdb_thrift_struct_oper_instantiations
  fsdb_path_visitor_oper_state_instantiations
)

add_library(fsdb_cow_subscription_manager
  fboss/fsdb/oper/instantiations/FsdbCowHybridStateSubscriptionManagerPrune.cpp
  fboss/fsdb/oper/instantiations/FsdbCowHybridStateSubscriptionManagerServe.cpp
  fboss/fsdb/oper/instantiations/FsdbCowHybridStateSubscriptionManagerSync.cpp
  fboss/fsdb/oper/instantiations/FsdbCowStateSubscriptionManagerPrune.cpp
  fboss/fsdb/oper/instantiations/FsdbCowStateSubscriptionManagerServe.cpp
  fboss/fsdb/oper/instantiations/FsdbCowStateSubscriptionManagerSync.cpp
  fboss/fsdb/oper/instantiations/FsdbCowStatsSubscriptionManagerPrune.cpp
  fboss/fsdb/oper/instantiations/FsdbCowStatsSubscriptionManagerServe.cpp
  fboss/fsdb/oper/instantiations/FsdbCowStatsSubscriptionManagerSync.cpp
)

target_link_libraries(fsdb_cow_subscription_manager
  fsdb_model
  cow_storage
  fsdb_cow_storage
  subscription_manager
  thrift_cow_visitors
)

add_library(fsdb_path_converter
  fboss/fsdb/oper/instantiations/FsdbStatePathConverter.cpp
  fboss/fsdb/oper/instantiations/FsdbStatsPathConverter.cpp
)

target_link_libraries(fsdb_path_converter
  fsdb_model
  oper_path_helpers
)

add_library(fsdb_thrift_struct_std_functions_instantiations
  fboss/fsdb/oper/instantiations/templates/FsdbThriftStructStdFunctionsInstantiations.cpp
)

target_link_libraries(fsdb_thrift_struct_std_functions_instantiations
  fsdb_model
  thrift_cow_nodes
)

add_library(fsdb_state_sub_mgr_instantiations
  fboss/fsdb/oper/instantiations/templates/FsdbStateSubscriptionManagerInstantiations.cpp
)

target_link_libraries(fsdb_state_sub_mgr_instantiations
  subscription_manager
  fsdb_cow_root
  fsdb_cow_subscription_manager
  thrift_cow_nodes
)

add_library(fsdb_naive_periodic_subscribable_storage
  fboss/fsdb/oper/instantiations/FsdbHybridNaivePeriodicSubscribableStateStorage.cpp
  fboss/fsdb/oper/instantiations/FsdbNaivePeriodicSubscribableStateStorage.cpp
  fboss/fsdb/oper/instantiations/FsdbNaivePeriodicSubscribableStatsStorage.cpp
  fboss/fsdb/oper/instantiations/FsdbStateStorage.cpp
)

target_link_libraries(fsdb_naive_periodic_subscribable_storage
  fsdb_model
  fsdb_cow_storage
  fsdb_cow_subscription_manager
  fsdb_path_converter
  subscribable_storage
  fsdb_thrift_struct_std_functions_instantiations
  fsdb_state_sub_mgr_instantiations
)

add_library(fsdb_cow_state_sub_mgr
  fboss/fsdb/client/instantiations/FsdbCowStateSubManager.cpp
)

target_link_libraries(fsdb_cow_state_sub_mgr
  fsdb_model
  fsdb_pub_sub
  fsdb_cow_storage
  fsdb_sub_mgr
)

add_library(fsdb_cow_stats_sub_mgr
  fboss/fsdb/client/instantiations/FsdbCowStatsSubManager.cpp
)

target_link_libraries(fsdb_cow_stats_sub_mgr
  fsdb_cow_state_sub_mgr
  fsdb_model
  fsdb_pub_sub
  fsdb_cow_storage
  fsdb_sub_mgr
)
