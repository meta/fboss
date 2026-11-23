# CMake to build libraries and binaries in fboss/agent/capture

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake


add_library(
  thrift_cow_serializer
  fboss/thrift_cow/nodes/Serializer.h
)

set_target_properties(thrift_cow_serializer PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(thrift_cow_serializer
  fsdb_oper_cpp2
  Folly::folly
  thrift::rpc
)

add_library(
  thrift_cow_nodes
  fboss/thrift_cow/nodes/ThriftListNode-inl.h
  fboss/thrift_cow/nodes/ThriftMapNode-inl.h
  fboss/thrift_cow/nodes/ThriftPrimitiveNode-inl.h
  fboss/thrift_cow/nodes/ThriftSetNode-inl.h
  fboss/thrift_cow/nodes/ThriftStructNode-inl.h
  fboss/thrift_cow/nodes/ThriftUnionNode-inl.h
  fboss/thrift_cow/nodes/Traits.h
  fboss/thrift_cow/nodes/Types.h
  fboss/thrift_cow/nodes/NodeUtils.h
)

set_target_properties(thrift_cow_nodes PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(thrift_cow_nodes
  cfgr_fboss_common_cpp2
  thrift_cow_serializer
  nodebase
  thrift_cow_visitors
  fsdb_oper_cpp2
  Folly::folly
  thrift::rpc
)
