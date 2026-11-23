# CMake to build libraries and binaries in fboss/cli/fboss2

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

thrift_add_library(
  fsdb_common_cpp2
  fboss/fsdb/if/fsdb_common.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
)

thrift_add_library(
  fsdb_config_cpp2
  fboss/fsdb/if/fsdb_config.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    fsdb_oper_cpp2
)

thrift_add_library(
  fsdb_oper_cpp2
  fboss/fsdb/if/fsdb_oper.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    fsdb_common_cpp2
    patch_cpp2
)

thrift_add_library(
  fsdb_cpp2
  fboss/fsdb/if/fsdb.thrift
  LANGUAGE cpp2
  SERVICES
    FsdbService
  OPTIONS
    json
    reflection
  DEPENDS
    fsdb_common_cpp2
    fsdb_oper_cpp2
    fb303_cpp2
)

thrift_add_library(
  bgp_attr_cpp2
  configerator/structs/neteng/fboss/bgp/if/bgp_attr.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
)

thrift_add_library(
  nsf_policy_cpp2
  configerator/structs/neteng/bgp_policy/thrift/nsf_policy.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
)

thrift_add_library(
  routing_policy_cpp2
  configerator/structs/neteng/bgp_policy/thrift/routing_policy.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
)

thrift_add_library(
  bgp_policy_cpp2
  configerator/structs/neteng/bgp_policy/thrift/bgp_policy.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    nsf_policy_cpp2
    routing_policy_cpp2
)

thrift_add_library(
  rib_policy_cpp2
  configerator/structs/neteng/bgp_policy/thrift/rib_policy.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    bgp_attr_cpp2
    bgp_policy_cpp2
    routing_policy_cpp2
)

thrift_add_library(
  bgp_config_cpp2
  neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/bgp_config.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    bgp_attr_cpp2
    bgp_policy_cpp2
)

# Bridge: thriftpath header references bgp_config under the public_tld-prefixed
# path; BGP++ source code references it under the canonical configerator path.
# Adding the public_tld output dir as an include root makes canonical-style
# includes (configerator/structs/.../gen-cpp2/...) resolve against the
# public_tld-prefixed generated headers.
target_include_directories(bgp_config_cpp2
  SYSTEM PUBLIC ${CMAKE_BINARY_DIR}/neteng/fboss/bgp/public_tld
)

thrift_add_library(
  bgp_structs_cpp2
  neteng/fboss/bgp/if/BgpStructs.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    network_address_cpp2
)

thrift_add_library(
  bmp_structs_cpp2
  neteng/fboss/bgp/if/BmpStructs.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    bgp_structs_cpp2
    network_address_cpp2
)

add_library(bgp_cpp2)
thrift_generate(
  bgp_cpp2
  neteng/fboss/bgp/if/bgp.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  INCLUDE_DIRECTORIES
    ${PROJECT_SOURCE_DIR}
    ${THRIFT_INCLUDE_DIR}
)
target_include_directories(bgp_cpp2 PUBLIC ${PROJECT_BINARY_DIR})

thrift_add_library(
  policy_thrift_cpp2
  neteng/fboss/bgp/if/policy_thrift.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
)

thrift_add_library(
  bgp_route_types_cpp2
  neteng/fboss/bgp/if/bgp_route_types.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    bgp_attr_cpp2
    rib_policy_cpp2
)

thrift_add_library(
  bgp_thrift_cpp2
  neteng/fboss/bgp/if/bgp_thrift.thrift
  LANGUAGE cpp2
  SERVICES
    TBgpService
  OPTIONS
    json
    reflection
  DEPENDS
    bgp_attr_cpp2
    bgp_config_cpp2
    bgp_policy_cpp2
    bgp_route_types_cpp2
    bgp_structs_cpp2
    fb303_cpp2
    policy_thrift_cpp2
    rib_policy_cpp2
)

thrift_add_library(
  fsdb_model_cpp2
  fboss/fsdb/if/oss/fsdb_model.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    agent_config_cpp2
    agent_info_cpp2
    agent_stats_cpp2
    bgp_config_cpp2
    bgp_route_types_cpp2
    fsdb_common_cpp2
    rib_policy_cpp2
    switch_reachability_cpp2
    switch_state_cpp2
    qsfp_state_cpp2
    qsfp_stats_cpp2
    sensor_service_stats_cpp2
    te_srv6_agent_stats_cpp2
)

add_library(thriftpath_lib
  fboss/thriftpath_plugin/Path.cpp
)

target_link_libraries(thriftpath_lib
  fsdb_utils
  switch_config_cpp2
  fsdb_oper_cpp2
  thrift::rpc
  Folly::folly
  ${RE2}
)

add_library(fsdb_model_thriftpath_cpp2
  fboss/fsdb/if/oss/fsdb_model_thriftpath.h
)

target_link_libraries(fsdb_model_thriftpath_cpp2
  thriftpath_lib
  fsdb_model_cpp2
)

# use this library to include both fsdb model and thriftpath
add_library(fsdb_model
  fboss/fsdb/if/FsdbModel.h
)

target_link_libraries(fsdb_model
  fsdb_model_cpp2
  fsdb_model_thriftpath_cpp2
)
