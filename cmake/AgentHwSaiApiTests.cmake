# CMake to build libraries and binaries in fboss/agent/hw/sai/api/tests

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

if(BUILD_SAI_FAKE)
add_executable(api_test
    fboss/util/oss/TestMain.cpp
    fboss/agent/hw/sai/api/tests/AclApiTest.cpp
    fboss/agent/hw/sai/api/tests/AdapterKeySerializerTest.cpp
    fboss/agent/hw/sai/api/tests/AddressUtilTest.cpp
    fboss/agent/hw/sai/api/tests/AttributeTest.cpp
    fboss/agent/hw/sai/api/tests/AttributeDataTypesTest.cpp
    fboss/agent/hw/sai/api/tests/BridgeApiTest.cpp
    fboss/agent/hw/sai/api/tests/BufferApiTest.cpp
    fboss/agent/hw/sai/api/tests/CounterApiTest.cpp
    fboss/agent/hw/sai/api/tests/DebugCounterApiTest.cpp
    fboss/agent/hw/sai/api/tests/FdbApiTest.cpp
    fboss/agent/hw/sai/api/tests/HashApiTest.cpp
    fboss/agent/hw/sai/api/tests/HostifApiTest.cpp
    fboss/agent/hw/sai/api/tests/IsolationGroupApiTest.cpp
    fboss/agent/hw/sai/api/tests/LagApiTest.cpp
    fboss/agent/hw/sai/api/tests/LoggingUtilTest.cpp
    fboss/agent/hw/sai/api/tests/MacsecApiTest.cpp
    fboss/agent/hw/sai/api/tests/MirrorApiTest.cpp
    fboss/agent/hw/sai/api/tests/MplsApiTest.cpp
    fboss/agent/hw/sai/api/tests/NeighborApiTest.cpp
    fboss/agent/hw/sai/api/tests/NextHopApiTest.cpp
    fboss/agent/hw/sai/api/tests/NextHopGroupApiTest.cpp
    fboss/agent/hw/sai/api/tests/PortApiTest.cpp
    fboss/agent/hw/sai/api/tests/QosMapApiTest.cpp
    fboss/agent/hw/sai/api/tests/QueueApiTest.cpp
    fboss/agent/hw/sai/api/tests/RouteApiTest.cpp
    fboss/agent/hw/sai/api/tests/RouterInterfaceApiTest.cpp
    fboss/agent/hw/sai/api/tests/MyMacApiTest.cpp
    fboss/agent/hw/sai/api/tests/SamplePacketApiTest.cpp
    fboss/agent/hw/sai/api/tests/SchedulerApiTest.cpp
    fboss/agent/hw/sai/api/tests/SwitchApiTest.cpp
    fboss/agent/hw/sai/api/tests/SystemPortApiTest.cpp
    fboss/agent/hw/sai/api/tests/TamApiTest.cpp
    fboss/agent/hw/sai/api/tests/Srv6ApiTest.cpp
    fboss/agent/hw/sai/api/tests/TunnelApiTest.cpp
    fboss/agent/hw/sai/api/tests/TunnelMapApiTest.cpp
    fboss/agent/hw/sai/api/tests/UdfApiTest.cpp
    fboss/agent/hw/sai/api/tests/VirtualChannelApiTest.cpp
    fboss/agent/hw/sai/api/tests/VirtualRouterApiTest.cpp
    fboss/agent/hw/sai/api/tests/VlanApiTest.cpp
    fboss/agent/hw/sai/api/tests/WredApiTest.cpp
)

target_link_libraries(api_test
    fake_sai
    sai_api
    ${GTEST}
    ${LIBGMOCK_LIBRARIES}
)

set_target_properties(api_test PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

gtest_discover_tests(api_test)
endif()
