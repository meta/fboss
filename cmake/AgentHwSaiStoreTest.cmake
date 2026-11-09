# CMake to build libraries and binaries in fboss/agent/hw/sai/store/tests

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

if(BUILD_SAI_FAKE)
add_executable(store_test
    fboss/util/oss/TestMain.cpp
    fboss/agent/hw/sai/store/tests/AclTableGroupStoreTest.cpp
    fboss/agent/hw/sai/store/tests/AclTableStoreTest.cpp
    fboss/agent/hw/sai/store/tests/BridgeStoreTest.cpp
    fboss/agent/hw/sai/store/tests/BufferStoreTest.cpp
    fboss/agent/hw/sai/store/tests/CounterStoreTest.cpp
    fboss/agent/hw/sai/store/tests/DebugCounterStoreTest.cpp
    fboss/agent/hw/sai/store/tests/FdbStoreTest.cpp
    fboss/agent/hw/sai/store/tests/HashStoreTest.cpp
    fboss/agent/hw/sai/store/tests/HostifTrapStoreTest.cpp
    fboss/agent/hw/sai/store/tests/InSegStoreTest.cpp
    fboss/agent/hw/sai/store/tests/IsolationGroupStoreTest.cpp
    fboss/agent/hw/sai/store/tests/LagStoreTest.cpp
    fboss/agent/hw/sai/store/tests/MirrorStoreTest.cpp
    fboss/agent/hw/sai/store/tests/NeighborStoreTest.cpp
    fboss/agent/hw/sai/store/tests/NextHopStoreTest.cpp
    fboss/agent/hw/sai/store/tests/NextHopGroupStoreTest.cpp
    fboss/agent/hw/sai/store/tests/PortStoreTest.cpp
    fboss/agent/hw/sai/store/tests/QosMapStoreTest.cpp
    fboss/agent/hw/sai/store/tests/QueueStoreTest.cpp
    fboss/agent/hw/sai/store/tests/RouteStoreTest.cpp
    fboss/agent/hw/sai/store/tests/RouterInterfaceStoreTest.cpp
    fboss/agent/hw/sai/store/tests/SaiEmptyStoreTest.cpp
    fboss/agent/hw/sai/store/tests/MyMacStoreTest.cpp
    fboss/agent/hw/sai/store/tests/SamplePacketStoreTest.cpp
    fboss/agent/hw/sai/store/tests/SchedulerStoreTest.cpp
    fboss/agent/hw/sai/store/tests/TamStoreTest.cpp
    fboss/agent/hw/sai/store/tests/Srv6StoreTest.cpp
    fboss/agent/hw/sai/store/tests/TunnelStoreTest.cpp
    fboss/agent/hw/sai/store/tests/TunnelMapStoreTest.cpp
    fboss/agent/hw/sai/store/tests/UdfStoreTest.cpp
    fboss/agent/hw/sai/store/tests/VirtualChannelStoreTest.cpp
    fboss/agent/hw/sai/store/tests/VirtualRouterStoreTest.cpp
    fboss/agent/hw/sai/store/tests/VlanStoreTest.cpp
    fboss/agent/hw/sai/store/tests/WredStoreTest.cpp
)

target_link_libraries(store_test
    sai_store
    fake_sai
    ${GTEST}
    ${LIBGMOCK_LIBRARIES}
)

set_target_properties(store_test PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

gtest_discover_tests(store_test)
endif()
