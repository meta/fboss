# CMake to build libraries and binaries in fboss/agent/hw/bcm

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(thrift_packet_stream_test
    fboss/agent/thrift_packet_stream/tests/PacketStreamTest.cpp
    fboss/agent/thrift_packet_stream/tests/BidirectionalPacketStreamTest.cpp
)

target_link_libraries(thrift_packet_stream_test
    Folly::folly
    thrift::rpc
    async_packet_transport
    bidirectional_packet_stream
    packet_stream_cpp2
    ${GTEST}
    ${LIBGMOCK_LIBRARIES}
)
