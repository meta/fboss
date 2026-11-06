# CMake to build libraries and binaries in fboss/agent/capture

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_fbthrift_cpp_library(
  patch_cpp2
  fboss/thrift_cow/patch.thrift
  OPTIONS
    json
    reflection
)

add_fbthrift_cpp_library(
  cow_visitor_results_cpp2
  fboss/thrift_cow/visitors/results.thrift
  OPTIONS
    json
    reflection
)

add_library(
  thrift_cow_visitors_common
  fboss/thrift_cow/visitors/Common.cpp
  fboss/thrift_cow/visitors/Common.h
)

target_link_libraries(thrift_cow_visitors_common
  Folly::folly
)

add_library(
  thrift_cow_visitors
  fboss/thrift_cow/visitors/DeltaVisitor.h
  fboss/thrift_cow/visitors/ExtendedPathVisitor.h
  fboss/thrift_cow/visitors/PathVisitor.h
  fboss/thrift_cow/visitors/RecurseVisitor.h
  fboss/thrift_cow/visitors/VisitorUtils.h
  fboss/thrift_cow/visitors/PatchBuilder.h
  fboss/thrift_cow/visitors/PatchBuilder.cpp
  fboss/thrift_cow/visitors/PatchApplier.h
  fboss/thrift_cow/visitors/PatchApplier.cpp
  fboss/thrift_cow/visitors/PatchHelpers.h
  fboss/thrift_cow/visitors/PatchHelpers.cpp
  fboss/thrift_cow/visitors/TraverseHelper.h
)

set_target_properties(thrift_cow_visitors PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(thrift_cow_visitors
  thrift_cow_visitors_common
  thrift_cow_serializer
  cow_visitor_results_cpp2
  fsdb_oper_cpp2
  patch_cpp2
  Folly::folly
  FBThrift::thriftcpp2
  ${RE2}
)
