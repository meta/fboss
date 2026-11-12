# CMake to build libraries and binaries in fboss/agent/capture

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(thrift_visitors_results_cpp2)
thrift_generate(
  thrift_visitors_results_cpp2
  fboss/thrift_visitors/results.thrift
  LANGUAGE cpp2
  OPTIONS
    json
  INCLUDE_DIRECTORIES
    ${PROJECT_SOURCE_DIR}
    ${THRIFT_INCLUDE_DIR}
)
target_include_directories(thrift_visitors_results_cpp2
  PUBLIC ${PROJECT_BINARY_DIR}
)

add_library(
  thrift_visitors
  fboss/thrift_visitors/NameToPathVisitor.h
  fboss/thrift_visitors/ThriftDeltaVisitor.h
  fboss/thrift_visitors/ThriftLeafVisitor.h
  fboss/thrift_visitors/ThriftPathVisitor.h
)

target_compile_options(thrift_visitors PUBLIC "-ftemplate-backtrace-limit=0")

target_link_libraries(thrift_visitors
  fsdb_oper_cpp2
  Folly::folly
  thrift::rpc
  thrift_visitors_results_cpp2
)
