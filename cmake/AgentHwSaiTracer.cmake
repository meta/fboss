# CMake to build libraries and binaries in fboss/agent/hw/sai/tracer

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(sai_tracer
  fboss/agent/hw/sai/tracer/AclApiTracer.cpp
  fboss/agent/hw/sai/tracer/ArsApiTracer.cpp
  fboss/agent/hw/sai/tracer/ArsProfileApiTracer.cpp
  fboss/agent/hw/sai/tracer/BridgeApiTracer.cpp
  fboss/agent/hw/sai/tracer/BufferApiTracer.cpp
  fboss/agent/hw/sai/tracer/CounterApiTracer.cpp
  fboss/agent/hw/sai/tracer/DebugCounterApiTracer.cpp
  fboss/agent/hw/sai/tracer/FdbApiTracer.cpp
  fboss/agent/hw/sai/tracer/FirmwareApiTracer.cpp
  fboss/agent/hw/sai/tracer/HashApiTracer.cpp
  fboss/agent/hw/sai/tracer/HostifApiTracer.cpp
  fboss/agent/hw/sai/tracer/IsolationGroupApiTracer.cpp
  fboss/agent/hw/sai/tracer/LagApiTracer.cpp
  fboss/agent/hw/sai/tracer/MacsecApiTracer.cpp
  fboss/agent/hw/sai/tracer/MirrorApiTracer.cpp
  fboss/agent/hw/sai/tracer/MplsApiTracer.cpp
  fboss/agent/hw/sai/tracer/MyMacApiTracer.cpp
  fboss/agent/hw/sai/tracer/NeighborApiTracer.cpp
  fboss/agent/hw/sai/tracer/NextHopApiTracer.cpp
  fboss/agent/hw/sai/tracer/NextHopGroupApiTracer.cpp
  fboss/agent/hw/sai/tracer/PortApiTracer.cpp
  fboss/agent/hw/sai/tracer/QosMapApiTracer.cpp
  fboss/agent/hw/sai/tracer/QueueApiTracer.cpp
  fboss/agent/hw/sai/tracer/RouteApiTracer.cpp
  fboss/agent/hw/sai/tracer/RouterInterfaceApiTracer.cpp
  fboss/agent/hw/sai/tracer/SaiTracer.cpp
  fboss/agent/hw/sai/tracer/SamplePacketApiTracer.cpp
  fboss/agent/hw/sai/tracer/SchedulerApiTracer.cpp
  fboss/agent/hw/sai/tracer/SwitchApiTracer.cpp
  fboss/agent/hw/sai/tracer/SystemPortApiTracer.cpp
  fboss/agent/hw/sai/tracer/TamApiTracer.cpp
  fboss/agent/hw/sai/tracer/TamEventAgingGroupApiTracer.cpp
  fboss/agent/hw/sai/tracer/Srv6ApiTracer.cpp
  fboss/agent/hw/sai/tracer/TunnelApiTracer.cpp
  fboss/agent/hw/sai/tracer/UdfApiTracer.cpp
  fboss/agent/hw/sai/tracer/Utils.cpp
  fboss/agent/hw/sai/tracer/VendorSwitchApiTracer.cpp
  fboss/agent/hw/sai/tracer/SwitchPipelineApiTracer.cpp
  fboss/agent/hw/sai/tracer/VirtualChannelApiTracer.cpp
  fboss/agent/hw/sai/tracer/VirtualRouterApiTracer.cpp
  fboss/agent/hw/sai/tracer/VlanApiTracer.cpp
  fboss/agent/hw/sai/tracer/WredApiTracer.cpp
)

target_link_libraries(sai_tracer
  fboss_error
  fboss_types
  async_logger
  sai_version
  sai_api
  function_call_time_reporter
  tuple_utils
  switch_config_cpp2
  Folly::folly
)

set_target_properties(sai_tracer PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

target_link_options(sai_tracer
  PUBLIC
  "LINKER:-wrap,sai_api_query"
  "LINKER:-wrap,sai_api_initialize"
  "LINKER:-wrap,sai_api_uninitialize"
  "LINKER:-wrap,sai_get_object_key"
)
