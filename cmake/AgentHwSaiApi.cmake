# CMake to build libraries and binaries in fboss/agent/hw/sai/api

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(address_util
  fboss/agent/hw/sai/api/AddressUtil.cpp
)

target_link_libraries(address_util
  Folly::folly
)

add_library(logging_util
  fboss/agent/hw/sai/api/LoggingUtil.cpp
)

target_link_libraries(logging_util
  fboss_cpp2
  fboss_error
  switch_config_cpp2
  Folly::folly
)

add_library(sai_version
  fboss/agent/hw/sai/api/SaiVersion.h
)

set_target_properties(sai_version PROPERTIES LINKER_LANGUAGE CXX)

add_library(sai_fake_extensions
  fboss/agent/hw/sai/api/fake/saifakeextensions.h
)

set_target_properties(sai_fake_extensions PROPERTIES LINKER_LANGUAGE C)

set(SAI_API_SRC
  fboss/agent/hw/sai/api/FdbApi.cpp
  fboss/agent/hw/sai/api/HashApi.cpp
  fboss/agent/hw/sai/api/MplsApi.cpp
  fboss/agent/hw/sai/api/NeighborApi.cpp
  fboss/agent/hw/sai/api/NextHopGroupApi.cpp
  fboss/agent/hw/sai/api/QosMapApi.cpp
  fboss/agent/hw/sai/api/PortApi.cpp
  fboss/agent/hw/sai/api/RouteApi.cpp
  fboss/agent/hw/sai/api/SaiApiLock.cpp
  fboss/agent/hw/sai/api/SaiAttribute.cpp
  fboss/agent/hw/sai/api/SaiApiTable.cpp
  fboss/agent/hw/sai/api/SwitchApi.cpp
  fboss/agent/hw/sai/api/DebugCounterApi.cpp
  fboss/agent/hw/sai/api/Types.cpp
  fboss/agent/hw/sai/api/AclApi.h
  fboss/agent/hw/sai/api/BridgeApi.h
  fboss/agent/hw/sai/api/FdbApi.h
  fboss/agent/hw/sai/api/FirmwareApi.h
  fboss/agent/hw/sai/api/HashApi.h
  fboss/agent/hw/sai/api/HostifApi.h
  fboss/agent/hw/sai/api/IsolationGroupApi.h
  fboss/agent/hw/sai/api/LagApi.h
  fboss/agent/hw/sai/api/MirrorApi.h
  fboss/agent/hw/sai/api/MplsApi.h
  fboss/agent/hw/sai/api/MyMacApi.h
  fboss/agent/hw/sai/api/NextHopApi.h
  fboss/agent/hw/sai/api/NextHopGroupApi.h
  fboss/agent/hw/sai/api/PortApi.h
  fboss/agent/hw/sai/api/QueueApi.h
  fboss/agent/hw/sai/api/RouteApi.h
  fboss/agent/hw/sai/api/RouterInterfaceApi.h
  fboss/agent/hw/sai/api/SaiApi.h
  fboss/agent/hw/sai/api/SaiApiError.h
  fboss/agent/hw/sai/api/SaiAttribute.h
  fboss/agent/hw/sai/api/SaiAttributeDataTypes.h
  fboss/agent/hw/sai/api/SaiObjectApi.h
  fboss/agent/hw/sai/api/SaiVersion.h
  fboss/agent/hw/sai/api/SamplePacketApi.h
  fboss/agent/hw/sai/api/SchedulerApi.h
  fboss/agent/hw/sai/api/SwitchApi.h
  fboss/agent/hw/sai/api/SystemPortApi.h
  fboss/agent/hw/sai/api/SystemPortApi.cpp
  fboss/agent/hw/sai/api/TamApi.h
  fboss/agent/hw/sai/api/TamEventAgingGroupApi.h
  fboss/agent/hw/sai/api/Traits.h
  fboss/agent/hw/sai/api/Srv6Api.cpp
  fboss/agent/hw/sai/api/Srv6Api.h
  fboss/agent/hw/sai/api/TunnelApi.h
  fboss/agent/hw/sai/api/TunnelMapApi.h
  fboss/agent/hw/sai/api/Types.h
  fboss/agent/hw/sai/api/UdfApi.h
  fboss/agent/hw/sai/api/VendorSwitchApi.h
  fboss/agent/hw/sai/api/SwitchPipelineApi.h
  fboss/agent/hw/sai/api/VirtualRouterApi.h
  fboss/agent/hw/sai/api/VlanApi.h
  fboss/agent/hw/sai/api/WredApi.h
  fboss/agent/hw/sai/api/DebugCounterApi.h
)

if (SAI_TAJO_IMPL)
  list(APPEND SAI_API_SRC
    fboss/agent/hw/sai/api/tajo/LagApi.cpp
    fboss/agent/hw/sai/api/tajo/ArsApi.cpp
    fboss/agent/hw/sai/api/tajo/ArsProfileApi.cpp
    fboss/agent/hw/sai/api/tajo/PortApi.cpp
    fboss/agent/hw/sai/api/tajo/TamApi.cpp
    fboss/agent/hw/sai/api/tajo/SwitchApi.cpp
    fboss/agent/hw/sai/api/tajo/CounterApi.cpp
    fboss/agent/hw/sai/api/tajo/DebugCounterApi.cpp
    fboss/agent/hw/sai/api/tajo/BufferApi.cpp
    fboss/agent/hw/sai/api/tajo/QueueApi.cpp
    fboss/agent/hw/sai/api/tajo/MirrorApi.cpp
    fboss/agent/hw/sai/api/tajo/SystemPortApi.cpp
    fboss/agent/hw/sai/api/tajo/NextHopGroupApi.cpp
    fboss/agent/hw/sai/api/tajo/AclApi.cpp
    fboss/agent/hw/sai/api/oss/HostifApi.cpp
  )

  find_path(SAI_IMPL_DIR NAMES lib/libsai_impl.a lib/libsai_impl.so)
  include_directories(${SAI_IMPL_DIR})
  message(STATUS "Found SAI_IMPL_DIR: ${SAI_IMPL_DIR}")
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR})
  message(STATUS "Using SAI Experimental dir: ${SAI_EXPERIMENTAL_INCLUDE_DIR}")
elseif (SAI_BRCM_IMPL)
  list(APPEND SAI_API_SRC
    fboss/agent/hw/sai/api/bcm/LagApi.cpp
    fboss/agent/hw/sai/api/bcm/ArsApi.cpp
    fboss/agent/hw/sai/api/bcm/ArsProfileApi.cpp
    fboss/agent/hw/sai/api/bcm/PortApi.cpp
    fboss/agent/hw/sai/api/bcm/TamApi.cpp
    fboss/agent/hw/sai/api/bcm/SwitchApi.cpp
    fboss/agent/hw/sai/api/bcm/CounterApi.cpp
    fboss/agent/hw/sai/api/bcm/DebugCounterApi.cpp
    fboss/agent/hw/sai/api/bcm/BufferApi.cpp
    fboss/agent/hw/sai/api/bcm/QueueApi.cpp
    fboss/agent/hw/sai/api/bcm/MirrorApi.cpp
    fboss/agent/hw/sai/api/bcm/SystemPortApi.cpp
    fboss/agent/hw/sai/api/bcm/NextHopGroupApi.cpp
    fboss/agent/hw/sai/api/bcm/AclApi.cpp
    fboss/agent/hw/sai/api/bcm/HostifApi.cpp
  )

  find_path(SAI_IMPL_DIR NAMES lib/libsai_impl.a lib/libsai_impl.so)
  include_directories(${SAI_IMPL_DIR})
  message(STATUS "Found SAI_IMPL_DIR: ${SAI_IMPL_DIR}")
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR})
  message(STATUS "Using SAI Experimental dir: ${SAI_EXPERIMENTAL_INCLUDE_DIR}")
elseif (CHENAB_SAI_SDK)
  list(APPEND SAI_API_SRC
    fboss/agent/hw/sai/api/chenab/LagApi.cpp
    fboss/agent/hw/sai/api/oss/ArsApi.cpp
    fboss/agent/hw/sai/api/oss/ArsProfileApi.cpp
    fboss/agent/hw/sai/api/chenab/PortApi.cpp
    fboss/agent/hw/sai/api/oss/TamApi.cpp
    fboss/agent/hw/sai/api/chenab/SwitchApi.cpp
    fboss/agent/hw/sai/api/oss/CounterApi.cpp
    fboss/agent/hw/sai/api/oss/DebugCounterApi.cpp
    fboss/agent/hw/sai/api/chenab/BufferApi.cpp
    fboss/agent/hw/sai/api/oss/QueueApi.cpp
    fboss/agent/hw/sai/api/oss/MirrorApi.cpp
    fboss/agent/hw/sai/api/oss/SystemPortApi.cpp
    fboss/agent/hw/sai/api/oss/NextHopGroupApi.cpp
    fboss/agent/hw/sai/api/oss/AclApi.cpp
    fboss/agent/hw/sai/api/oss/HostifApi.cpp
  )

  find_path(SAI_IMPL_DIR NAMES lib/libsai_impl.a lib/libsai_impl.so)
  include_directories(${SAI_IMPL_DIR})
  message(STATUS "Found SAI_IMPL_DIR: ${SAI_IMPL_DIR}")
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR})
  message(STATUS "Using SAI Experimental dir: ${SAI_EXPERIMENTAL_INCLUDE_DIR}")
elseif (SAI_BRCM_PAI_IMPL)
  list(APPEND SAI_API_SRC
    fboss/agent/hw/sai/api/oss/LagApi.cpp
    fboss/agent/hw/sai/api/oss/AclApi.cpp
    fboss/agent/hw/sai/api/oss/ArsApi.cpp
    fboss/agent/hw/sai/api/oss/ArsProfileApi.cpp
    fboss/agent/hw/sai/api/oss/CounterApi.cpp
    fboss/agent/hw/sai/api/oss/DebugCounterApi.cpp
    fboss/agent/hw/sai/api/oss/NextHopGroupApi.cpp
    fboss/agent/hw/sai/api/brcm_pai/PortApi.cpp
    fboss/agent/hw/sai/api/oss/SwitchApi.cpp
    fboss/agent/hw/sai/api/oss/TamApi.cpp
    fboss/agent/hw/sai/api/oss/BufferApi.cpp
    fboss/agent/hw/sai/api/oss/QueueApi.cpp
    fboss/agent/hw/sai/api/oss/MirrorApi.cpp
    fboss/agent/hw/sai/api/oss/SystemPortApi.cpp
    fboss/agent/hw/sai/api/oss/HostifApi.cpp
  )
else()
  list(APPEND SAI_API_SRC
    fboss/agent/hw/sai/api/fake/FakeSaiExtensions.cpp
  )
endif()

set(SAI_API_DEPS
  address_util
  logging_util
  fboss_error
  fboss_types
  function_call_time_reporter
  hw_write_behavior
  switch_config_cpp2
  Folly::folly
)

add_library(sai_api
  "${SAI_API_SRC}"
)

target_link_libraries(sai_api
  "${SAI_API_DEPS}"
)

set_target_properties(sai_api PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_traced_api
  "${SAI_API_SRC}"
)

target_link_libraries(sai_traced_api
  sai_tracer
  "${SAI_API_DEPS}"
)

set_target_properties(sai_traced_api PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

set_target_properties(logging_util PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)
