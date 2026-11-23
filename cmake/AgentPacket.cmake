# CMake to build libraries and binaries in fboss/agent/packet

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(packet
  fboss/agent/Packet.cpp
  fboss/agent/TxPacket.cpp
  fboss/agent/packet/ArpHdr.cpp
  fboss/agent/packet/DHCPv4Packet.cpp
  fboss/agent/packet/DHCPv6Packet.cpp
  fboss/agent/packet/EthHdr.cpp
  fboss/agent/packet/ICMPExtHdr.cpp
  fboss/agent/packet/ICMPHdr.cpp
  fboss/agent/packet/IPv4Hdr.cpp
  fboss/agent/packet/IPv6Hdr.cpp
  fboss/agent/packet/LlcHdr.cpp
  fboss/agent/packet/MPLSHdr.cpp
  fboss/agent/packet/NDP.cpp
  fboss/agent/packet/NDPRouterAdvertisement.cpp
  fboss/agent/packet/PktUtil.cpp
  fboss/agent/packet/PTPHeader.cpp
  fboss/agent/packet/TCPHeader.cpp
  fboss/agent/packet/UDPHeader.cpp
)

target_link_libraries(packet
  pktutil
  stats
  utils
  Folly::folly
)

add_library(sflow_structs
  fboss/agent/packet/SflowStructs.cpp
)

target_link_libraries(sflow_structs
  Folly::folly
)

add_library(ipfix_header
  fboss/agent/packet/IpfixHeader.cpp
)

target_link_libraries(ipfix_header
  Folly::folly
)

add_library(xgs_psamp_mod
  fboss/agent/packet/bcm/XgsPsampMod.cpp
)

target_link_libraries(xgs_psamp_mod
  fmt::fmt
  ipfix_header
  switch_config_cpp2
  Folly::folly
)

add_library(tajo_punt_header
  fboss/agent/packet/tajo/TajoPuntHeader.cpp
)

target_link_libraries(tajo_punt_header
  Folly::folly
)

add_library(chenab_mod_header
  fboss/agent/packet/chenab/oss/ChenabModHeader.cpp
)

target_link_libraries(chenab_mod_header
  Folly::folly
)

add_library(pktutil
  fboss/agent/packet/PktUtil.cpp
)

target_link_libraries(pktutil
  fboss_error
  Folly::folly
)

add_library(packet_factory
  fboss/agent/packet/EthFrame.cpp
  fboss/agent/packet/IPPacket.cpp
  fboss/agent/packet/MPLSPacket.cpp
  fboss/agent/packet/PktFactory.cpp
  fboss/agent/packet/TCPPacket.cpp
  fboss/agent/packet/UDPDatagram.cpp
)

target_link_libraries(packet_factory
  hw_switch
  packet
  ctrl_cpp2
  switch_config_cpp2
  Folly::folly
  sflow_structs
  multiswitch_ctrl_cpp2
)

add_executable(xgs_psamp_mod_test
  fboss/util/oss/TestMain.cpp
  fboss/agent/packet/bcm/test/XgsPsampModTest.cpp
)

target_link_libraries(xgs_psamp_mod_test
  fmt::fmt
  ipfix_header
  xgs_psamp_mod
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(xgs_psamp_mod_test)

add_executable(tajo_punt_header_test
  fboss/util/oss/TestMain.cpp
  fboss/agent/packet/tajo/test/TajoPuntHeaderTest.cpp
)

target_link_libraries(tajo_punt_header_test
  tajo_punt_header
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(tajo_punt_header_test)

