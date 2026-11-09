# CMake to build libraries and binaries in fboss/qsfp_service/platforms/wedge

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

set(QSFP_PLATFORMS_WEDGE_SRC
  fboss/qsfp_service/platforms/wedge/GalaxyManager.cpp
  fboss/qsfp_service/platforms/wedge/QsfpRestClient.cpp
  fboss/qsfp_service/platforms/wedge/Wedge100Manager.cpp
  fboss/qsfp_service/platforms/wedge/Wedge400Manager.cpp
  fboss/qsfp_service/platforms/wedge/Wedge400CManager.cpp
  fboss/qsfp_service/platforms/wedge/WedgeManager.cpp
  fboss/qsfp_service/platforms/wedge/WedgeManagerInit.cpp
  fboss/qsfp_service/platforms/wedge/BspWedgeManager.cpp
  fboss/qsfp_service/platforms/wedge/oss/WedgeManagerInit.cpp
)

if(SAI_BRCM_PAI_IMPL)
  list(APPEND QSFP_PLATFORMS_WEDGE_SRC
    fboss/qsfp_service/platforms/wedge/brcm_pai/WedgeManagerInit.cpp
  )
else()
  list(APPEND QSFP_PLATFORMS_WEDGE_SRC
    fboss/qsfp_service/platforms/wedge/non_xphy/WedgeManagerInit.cpp
  )
endif()

add_library(qsfp_platforms_wedge
  "${QSFP_PLATFORMS_WEDGE_SRC}"
)

target_link_libraries(qsfp_platforms_wedge
  qsfp_module
  fpga_multi_pim_container
  ledIO
  qsfp_bsp_core
  galaxy_platform_mapping
  wedge100_platform_mapping
  wedge400c_platform_mapping
  meru800bia_platform_mapping
  meru800bfa_platform_mapping
  montblanc_platform_mapping
  minipack3bta_platform_mapping
  minipack3n_platform_mapping
  morgan_platform_mapping
  janga800bic_platform_mapping
  tahan800bc_platform_mapping
  icecube800bc_platform_mapping
  icetea800bc_platform_mapping
  tahansb800bc_platform_mapping
  ladakh800bcls_platform_mapping
  leh800bcls_platform_mapping
  platform_base
  qsfp_config
  wedge400_i2c
  io_stats_recorder
  platform_mapping_utils
  port_manager
  wedge_transceiver
  wedge_i2c
)

if(SAI_BRCM_PAI_IMPL)
  target_link_libraries(qsfp_platforms_wedge
    sai_phy_management
  )
endif()

add_library(wedge_transceiver
  fboss/qsfp_service/platforms/wedge/WedgeI2CBusLock.cpp
  fboss/qsfp_service/platforms/wedge/WedgeQsfp.cpp
)

target_link_libraries(wedge_transceiver
  io_stats_recorder
  base_i2c_dependencies
  i2_api
  usb_api
  qsfp_stats
  transceiver_manager
  firmware_upgrader
  i2c_log_buffer
  qsfp_module
  cmis_cpp2
  Folly::folly
)
