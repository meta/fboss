# CMake to build libraries and binaries in fboss/agent/hw/bcm

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(bidirectional_packet_stream
    fboss/agent/thrift_packet_stream/PacketStreamService.cpp
    fboss/agent/thrift_packet_stream/PacketStreamClient.cpp
    fboss/agent/thrift_packet_stream/AsyncThriftPacketTransport.cpp
    fboss/agent/thrift_packet_stream/BidirectionalPacketStream.cpp
)

target_link_libraries(bidirectional_packet_stream
    Folly::folly
    thrift::rpc
    async_packet_transport
    packet_stream_cpp2
    fboss_cpp2
)
