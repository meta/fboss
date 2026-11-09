# CMake to build libraries and binaries in fboss/qsfp_service/test/hw_test

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(hw_qsfp_ensemble
  fboss/qsfp_service/test/hw_test/HwQsfpEnsemble.cpp
)

target_link_libraries(hw_qsfp_ensemble
  platform_mapping
  resourcelibutil
  phy_management_base
  transceiver_manager
  port_manager
  qsfp_handler
  qsfp_core
  qsfp_platforms_wedge
)

add_library(hw_transceiver_utils
  fboss/qsfp_service/test/hw_test/HwTransceiverUtils.cpp
)

target_link_libraries(hw_transceiver_utils
  Folly::folly
  fboss_error
  platform_mapping
  port_test_utils
  switch_config_cpp2
  transceiver_cpp2
  transceiver_manager
  transceiver_properties_manager
)

set(QSFP_HW_TEST_SRCS
  fboss/qsfp_service/test/hw_test/EmptyHwTest.cpp
  fboss/qsfp_service/test/hw_test/HwXphyFirmwareTest.cpp
  fboss/qsfp_service/test/hw_test/HwPimTest.cpp
  fboss/qsfp_service/test/hw_test/HwPortProfileTest.cpp
  fboss/qsfp_service/test/hw_test/HwPortUtils.cpp
  fboss/qsfp_service/test/hw_test/HwTransceiverConfigTest.cpp
  fboss/qsfp_service/test/hw_test/HwStatsCollectionTest.cpp
  fboss/qsfp_service/test/hw_test/HwTest.cpp
  fboss/qsfp_service/test/hw_test/HwTransceiverResetTest.cpp
  fboss/qsfp_service/test/hw_test/HwExternalPhyPortTest.cpp
  fboss/qsfp_service/test/hw_test/HwPortPrbsTest.cpp
  fboss/qsfp_service/test/hw_test/HwI2CStressTest.cpp
  fboss/qsfp_service/test/hw_test/HwI2cSelectTest.cpp
  fboss/qsfp_service/test/hw_test/HwStateMachineTest.cpp
  fboss/qsfp_service/test/hw_test/HwTransceiverTest.cpp
  fboss/qsfp_service/test/hw_test/OpticsFwUpgradeTest.cpp
  fboss/util/oss/TestMain.cpp
)

set(QSFP_HW_TEST_DEPS
  hw_qsfp_ensemble
  qsfp_module
  qsfp_lib
  hw_transceiver_utils
  port_test_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  FBThrift::thriftcpp2
  fb303::fb303
  fboss_init
  qsfp_production_features_cpp2
  link_qsfp_test_port_info_cpp2
  link_qsfp_test_port_info_utils
)

if(SAI_BRCM_PAI_IMPL)
  BUILD_AND_INSTALL_WITH_XPHY_SDK_LIBS(
    "qsfp_hw_test" QSFP_HW_TEST_SRCS QSFP_HW_TEST_DEPS "brcm_pai" XPHY_SDK_LIBS
  )
else()
  BUILD_AND_INSTALL_WITH_XPHY_SDK_LIBS(
    "qsfp_hw_test" QSFP_HW_TEST_SRCS QSFP_HW_TEST_DEPS "" ""
  )
endif()
