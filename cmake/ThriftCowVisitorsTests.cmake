# CMake to build libraries and binaries in fboss/agent/capture

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(thrift_cow_visitor_test_utils
  fboss/thrift_cow/visitors/tests/VisitorTestUtils.cpp
)

target_link_libraries(thrift_cow_visitor_test_utils
  switch_config_cpp2
  thrift_cow_test_cpp2
  thrift_cow_visitors
  Folly::folly
  thrift::rpc
)

add_executable(thrift_cow_visitor_tests
  fboss/thrift_cow/visitors/tests/DeltaVisitorTests.cpp
  fboss/thrift_cow/visitors/tests/ExtendedPathMatcherTest.cpp
  fboss/thrift_cow/visitors/tests/PathVisitorTests.cpp
  fboss/thrift_cow/visitors/tests/RecurseVisitorTests.cpp
)

target_compile_options(thrift_cow_visitor_tests PUBLIC "-DENABLE_DYNAMIC_APIS")

target_link_libraries(thrift_cow_visitor_tests
    switch_config_cpp2
    thrift_cow_nodes
    thrift_cow_test_cpp2
    thrift_cow_visitors
    thrift_cow_visitor_test_utils
    Folly::folly
    thrift::rpc
    state
    ${GTEST}
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(thrift_node_tests)
