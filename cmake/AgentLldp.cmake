# CMake to build libraries and binaries in fboss/agent/hw/switch_asics

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake


add_library(lldp_structs_cpp2)
thrift_generate(
  lldp_structs_cpp2
  fboss/agent/lldp/lldp.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  INCLUDE_DIRECTORIES
    ${PROJECT_SOURCE_DIR}
    ${THRIFT_INCLUDE_DIR}
)
target_include_directories(lldp_structs_cpp2 PUBLIC ${PROJECT_BINARY_DIR})

add_library(lldp
  fboss/agent/lldp/LinkNeighbor.cpp
  fboss/agent/lldp/LinkNeighborDB.cpp
)

target_link_libraries(lldp
  agent_features
  cow_storage
  fboss_types
  lldp_structs_cpp2
  Folly::folly
  thrift_cow_nodes
)
