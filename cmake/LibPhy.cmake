# CMake to build libraries and binaries in fboss/lib/phy

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(external_phy
  fboss/lib/phy/ExternalPhy.cpp
  fboss/lib/phy/ExternalPhyPortStatsUtils.cpp
)

target_link_libraries(external_phy
  alert_logger
  ctrl_cpp2
  fboss_error
  fboss_types
  mdio
  platform_config_cpp2
  phy_cpp2
)

# Currently we only have opensource ready SAI XPHY implementation. If in the future, we have more
# types of SAI XPHY, we can consider a common subset of the sources and deps just like what we do
# for SAI_API_SRC in AgentHwSaiApi.cmake
if (SAI_BRCM_PAI_IMPL)
  set(SAI_XPHY_SRC)
  # SAI_BRCM_PAI_IMPL only
  list(APPEND SAI_XPHY_SRC
    fboss/lib/phy/SaiPhyRetimer.cpp
  )

  set(SAI_XPHY_DEPS
    sai_platform
    external_phy
    mdio
    Folly::folly
    thrift::rpc
  )
  # SAI_BRCM_PAI_IMPL only
  list(APPEND SAI_XPHY_DEPS
    fmt::fmt
    qsfp_bsp_core
  )

  add_library(sai_xphy
    "${SAI_XPHY_SRC}"
  )
  target_link_libraries(sai_xphy
    "${SAI_XPHY_DEPS}"
  )

  set(SAI_PHY_MANAGEMENT_SRC
    fboss/lib/phy/SaiPhyManager.cpp
  )
  # SAI_BRCM_PAI_IMPL only
  list(APPEND SAI_PHY_MANAGEMENT_SRC
    fboss/lib/phy/BspSaiPhyManager.cpp
  )

  set(SAI_PHY_MANAGEMENT_DEPS
    sai_xphy
  )
  # SAI_BRCM_PAI_IMPL only
  list(APPEND SAI_PHY_MANAGEMENT_DEPS
    bsp_platform_mapping
  )

  add_library(sai_phy_management
    "${SAI_PHY_MANAGEMENT_SRC}"
  )
  target_link_libraries(sai_phy_management
    "${SAI_PHY_MANAGEMENT_DEPS}"
  )
endif()

add_library(phy_management_base
  fboss/lib/phy/PhyManager.cpp
)

target_link_libraries(phy_management_base
  external_phy
  fboss_error
  platform_mapping
  fboss_config_utils
  Folly::folly
  io_stats_cpp2
  fb303::fb303
  phy_snapshot_manager
  fpga_multi_pim_container
)

add_library(phy_utils
  fboss/lib/phy/PhyUtils.cpp
)

target_link_libraries(phy_utils
  phy_cpp2
)
