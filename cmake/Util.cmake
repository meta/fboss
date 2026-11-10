# CMake to build libraries and binaries in fboss/util

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

set(WEDGE_QSFP_UTIL_SRCS
  fboss/util/wedge_qsfp_util.cpp
  fboss/util/qsfp_util_main.cpp
  fboss/util/qsfp/QsfpServiceDetector.cpp
  fboss/util/qsfp/QsfpUtilContainer.cpp
  fboss/util/qsfp/QsfpUtilTx.cpp
  fboss/util/oss/wedge_qsfp_util.cpp
)

set(WEDGE_QSFP_UTIL_DEPS
  firmware_upgrader
  qsfp_config
  qsfp_lib
  qsfp_module
  ${YAML-CPP}
  phy_management_base
  transceiver_manager
  port_manager
  qsfp_platforms_wedge
  fboss_common_cpp2
  wedge_i2c
)

if(SAI_BRCM_PAI_IMPL)
  BUILD_AND_INSTALL_WITH_XPHY_SDK_LIBS(
    "wedge_qsfp_util" WEDGE_QSFP_UTIL_SRCS WEDGE_QSFP_UTIL_DEPS "brcm_pai" XPHY_SDK_LIBS
  )
else()
  BUILD_AND_INSTALL_WITH_XPHY_SDK_LIBS(
    "wedge_qsfp_util" WEDGE_QSFP_UTIL_SRCS WEDGE_QSFP_UTIL_DEPS "" ""
  )
endif()


add_executable(thrift_state_updater
  fboss/util/ThriftStateUpdater.cpp
)

target_link_libraries(thrift_state_updater
  ctrl_cpp2
  thrift::rpc
)
