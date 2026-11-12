# CMake to build libraries and binaries in fboss/agent/capture

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

thrift_add_library(
  thrift_cow_test_cpp2
  fboss/thrift_cow/nodes/tests/test.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  DEPENDS
    cfgr_fboss_common_cpp2
    switch_config_cpp2
)

add_executable(thrift_node_tests
  fboss/thrift_cow/nodes/tests/ThriftStructNodeTests.cpp
)

target_compile_options(thrift_node_tests PUBLIC "-DENABLE_DYNAMIC_APIS")

target_link_libraries(thrift_node_tests
    thrift_cow_test_cpp2
    switch_config_cpp2
    thrift_cow_nodes
    thrift_cow_serializer
    thrift_cow_visitor_test_utils
    fsdb_oper_cpp2
    fsdb_model_cpp2
    state
    thrift::rpc
    ${GTEST}
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(thrift_node_tests)
