# CMake to build libraries and binaries in fboss/agent/hw/sai/switch

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

set(SAI_SWITCH_SRC
  fboss/agent/hw/sai/switch/ConcurrentIndices.cpp
  fboss/agent/hw/sai/switch/DropReasonUtils.cpp
  fboss/agent/hw/sai/switch/SaiAclTableGroupManager.cpp
  fboss/agent/hw/sai/switch/SaiAclTableManager.cpp
  fboss/agent/hw/sai/switch/SaiArsManager.cpp
  fboss/agent/hw/sai/switch/SaiArsProfileManager.cpp
  fboss/agent/hw/sai/switch/SaiBridgeManager.cpp
  fboss/agent/hw/sai/switch/SaiBufferManager.cpp
  fboss/agent/hw/sai/switch/SaiCounterManager.cpp
  fboss/agent/hw/sai/switch/SaiDebugCounterManager.cpp
  fboss/agent/hw/sai/switch/SaiFdbManager.cpp
  fboss/agent/hw/sai/switch/SaiHashManager.cpp
  fboss/agent/hw/sai/switch/SaiHostifManager.cpp
  fboss/agent/hw/sai/switch/SaiInSegEntryManager.cpp
  fboss/agent/hw/sai/switch/SaiLagManager.cpp
  fboss/agent/hw/sai/switch/SaiManagerTable.cpp
  fboss/agent/hw/sai/switch/SaiMacsecManager.cpp
  fboss/agent/hw/sai/switch/SaiMirrorManager.cpp
  fboss/agent/hw/sai/switch/SaiNeighborManager.cpp
  fboss/agent/hw/sai/switch/SaiNextHopManager.cpp
  fboss/agent/hw/sai/switch/SaiNextHopGroupManager.cpp
  fboss/agent/hw/sai/switch/SaiPortManager.cpp
  fboss/agent/hw/sai/switch/SaiPortUtils.cpp
  fboss/agent/hw/sai/switch/SaiQosMapManager.cpp
  fboss/agent/hw/sai/switch/SaiQueueManager.cpp
  fboss/agent/hw/sai/switch/SaiRouteManager.cpp
  fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.cpp
  fboss/agent/hw/sai/switch/SaiRxPacket.cpp
  fboss/agent/hw/sai/switch/SaiMyMacManager.cpp
  fboss/agent/hw/sai/switch/SaiSamplePacketManager.cpp
  fboss/agent/hw/sai/switch/SaiSchedulerManager.cpp
  fboss/agent/hw/sai/switch/SaiSwitch.cpp
  fboss/agent/hw/sai/switch/SaiSwitchManager.cpp
  fboss/agent/hw/sai/switch/SaiSystemPortManager.cpp
  fboss/agent/hw/sai/switch/SaiTunnelManager.cpp
  fboss/agent/hw/sai/switch/SaiSrv6MySidManager.cpp
  fboss/agent/hw/sai/switch/SaiSrv6SidListManager.cpp
  fboss/agent/hw/sai/switch/SaiSrv6TunnelManager.cpp
  fboss/agent/hw/sai/switch/SaiTunnelUtils.cpp
  fboss/agent/hw/sai/switch/SaiVxlanTunnelManager.cpp
  fboss/agent/hw/sai/switch/SaiUdfManager.cpp
  fboss/agent/hw/sai/switch/SaiVlanManager.cpp
  fboss/agent/hw/sai/switch/SaiVirtualRouterManager.cpp
  fboss/agent/hw/sai/switch/SaiVirtualChannelManager.cpp
  fboss/agent/hw/sai/switch/SaiWredManager.cpp
)

# TODO Support SAI_PHY_IMPL(T86789543)
if (SAI_TAJO_IMPL)
  list(APPEND SAI_SWITCH_SRC
    fboss/agent/hw/sai/switch/npu/SaiAclTableManager.cpp
    fboss/agent/hw/sai/switch/npu/SaiPortManager.cpp
    fboss/agent/hw/sai/switch/npu/SaiSwitch.cpp
    fboss/agent/hw/sai/switch/npu/tajo/SaiSwitch.cpp
    fboss/agent/hw/sai/switch/npu/tajo/SaiTamManager.cpp
    fboss/agent/hw/sai/switch/npu/tajo/SaiPortManager.cpp
    fboss/agent/hw/sai/switch/npu/tajo/SaiAclTableManager.cpp
    fboss/agent/hw/sai/switch/npu/tajo/SaiBufferManager.cpp
    fboss/agent/hw/sai/switch/npu/tajo/SaiSwitchManager.cpp
    fboss/agent/hw/sai/switch/npu/tajo/oss/SaiSwitchManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiArsProfileManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiArsManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiQueueManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiVendorSwitchManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiFirmwareManager.cpp
  )
elseif (SAI_BRCM_IMPL)
  list(APPEND SAI_SWITCH_SRC
    fboss/agent/hw/sai/switch/npu/SaiAclTableManager.cpp
    fboss/agent/hw/sai/switch/npu/SaiPortManager.cpp
    fboss/agent/hw/sai/switch/npu/SaiSwitch.cpp
    fboss/agent/hw/sai/switch/npu/bcm/SaiArsManager.cpp
    fboss/agent/hw/sai/switch/npu/bcm/SaiSwitch.cpp
    fboss/agent/hw/sai/switch/npu/bcm/SaiTamManager.cpp
    fboss/agent/hw/sai/switch/npu/bcm/SaiPortManager.cpp
    fboss/agent/hw/sai/switch/npu/bcm/SaiQueueManager.cpp
    fboss/agent/hw/sai/switch/npu/bcm/SaiSwitchManager.cpp
    fboss/agent/hw/sai/switch/npu/bcm/SaiBufferManager.cpp
    fboss/agent/hw/sai/switch/npu/bcm/oss/SaiSwitchManager.cpp
    fboss/agent/hw/sai/switch/npu/bcm/SaiFirmwareManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiAclTableManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiArsProfileManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiVendorSwitchManager.cpp
  )
elseif (CHENAB_SAI_SDK)
  list(APPEND SAI_SWITCH_SRC
    fboss/agent/hw/sai/switch/npu/SaiAclTableManager.cpp
    fboss/agent/hw/sai/switch/npu/SaiPortManager.cpp
    fboss/agent/hw/sai/switch/npu/SaiSwitch.cpp
    fboss/agent/hw/sai/switch/npu/chenab/SaiArsProfileManager.cpp
    fboss/agent/hw/sai/switch/npu/chenab/SaiArsManager.cpp
    fboss/agent/hw/sai/switch/npu/chenab/SaiPortManager.cpp
    fboss/agent/hw/sai/switch/npu/chenab/SaiBufferManager.cpp
    fboss/agent/hw/sai/switch/npu/chenab/SaiSwitchManager.cpp
    fboss/agent/hw/sai/switch/npu/chenab/oss/SaiSwitchManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiSwitch.cpp
    fboss/agent/hw/sai/switch/npu/chenab/SaiTamManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiAclTableManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiQueueManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiVendorSwitchManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiFirmwareManager.cpp
  )
elseif (SAI_BRCM_PAI_IMPL)
  list(APPEND SAI_SWITCH_SRC
    fboss/agent/hw/sai/switch/phy/SaiAclTableManager.cpp
    fboss/agent/hw/sai/switch/phy/SaiPortManager.cpp
    fboss/agent/hw/sai/switch/phy/SaiSwitch.cpp
    fboss/agent/hw/sai/switch/oss/SaiArsProfileManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiArsManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiSwitch.cpp
    fboss/agent/hw/sai/switch/oss/SaiTamManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiBufferManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiPortManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiAclTableManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiSwitchManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiQueueManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiVendorSwitchManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiFirmwareManager.cpp
  )
else()
  list(APPEND SAI_SWITCH_SRC
    fboss/agent/hw/sai/switch/npu/SaiAclTableManager.cpp
    fboss/agent/hw/sai/switch/npu/SaiPortManager.cpp
    fboss/agent/hw/sai/switch/npu/SaiSwitch.cpp
    fboss/agent/hw/sai/switch/oss/SaiArsProfileManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiArsManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiSwitch.cpp
    fboss/agent/hw/sai/switch/oss/SaiTamManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiPortManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiAclTableManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiSwitchManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiQueueManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiVendorSwitchManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiFirmwareManager.cpp
    fboss/agent/hw/sai/switch/oss/SaiBufferManager.cpp
  )
endif()

add_library(sai_switch
  "${SAI_SWITCH_SRC}"
)

target_link_libraries(sai_switch
  # implementation for sai_api would be provided by lib that links later. Thus,
  # allow unresolved-symbols here.
  -Wl,--unresolved-symbols=ignore-all
  core
  hw_switch_fb303_stats
  hw_trunk_counters
  hw_fb303_stats
  hw_rif_fb303_stats
  hw_cpu_fb303_stats
  hw_port_fb303_stats
  hw_resource_stats_publisher
  hw_switch_warmboot_helper
  prbs_stats_entry
  mka_structs_cpp2
  sai_api
  sai_platform
  sai_store
  ref_map
  validate_state_update
  Folly::folly
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  -Wl,--unresolved-symbols=report-all
)

set_target_properties(sai_switch PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(thrift_handler
  fboss/agent/hw/sai/switch/SaiHandler.cpp
)

target_link_libraries(thrift_handler
  diag_shell
  handler
  sai_ctrl_cpp2
)

set_target_properties(thrift_handler PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)
