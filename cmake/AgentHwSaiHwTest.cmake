# CMake to build libraries and binaries in fboss/agent/hw/sai/hw_test

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(thrift_test_handler
  fboss/agent/hw/sai/hw_test/SaiTestHandler.cpp
)

target_link_libraries(thrift_test_handler
  diag_shell
  sai_test_ctrl_cpp2
)

set_target_properties(thrift_test_handler PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_switch_ensemble
  fboss/agent/hw/sai/hw_test/HwSwitchEnsembleFactory.cpp
  fboss/agent/hw/sai/hw_test/SaiSwitchEnsemble.cpp
)

target_link_libraries(sai_switch_ensemble
  core
  setup_thrift
  sai_switch
  thrift_test_handler
  hw_switch_ensemble
  hw_link_state_toggler
  sai_platform
  sai_test_ctrl_cpp2
  sai_traced_api
)

add_library(sai_phy_capabilities
  fboss/agent/hw/sai/hw_test/PhyCapabilities.cpp
)

target_link_libraries(sai_phy_capabilities
  sai_switch
)

set_target_properties(sai_switch_ensemble PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

set_target_properties(sai_phy_capabilities PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_aqm_utils
  fboss/agent/hw/sai/hw_test/HwTestAqmUtils.cpp
)

target_link_libraries(sai_aqm_utils
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
)

set_target_properties(sai_aqm_utils PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_ecmp_utils
  fboss/agent/hw/sai/hw_test/HwTestEcmpUtils.cpp
)

target_link_libraries(sai_ecmp_utils
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
)

set_target_properties(sai_ecmp_utils PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_ptp_tc_utils
  fboss/agent/hw/sai/hw_test/HwTestPtpTcUtils.cpp
)

target_link_libraries(sai_ptp_tc_utils
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
)

set_target_properties(sai_ptp_tc_utils PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_switching_mode_utils
  fboss/agent/hw/sai/hw_test/HwTestSwitchingModeUtils.cpp
)

target_link_libraries(sai_switching_mode_utils
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
)

set_target_properties(sai_switching_mode_utils PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_udf_utils
  fboss/agent/hw/sai/hw_test/HwTestUdfUtils.cpp
)

target_link_libraries(sai_udf_utils
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
)

set_target_properties(sai_udf_utils PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_port_utils
  fboss/agent/hw/sai/hw_test/HwTestPortUtils.cpp
  fboss/agent/hw/sai/hw_test/SaiPortUtils.cpp
)

target_link_libraries(sai_port_utils
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
  sai_platform
  config_factory
)

set_target_properties(sai_port_utils PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_copp_utils
  fboss/agent/hw/sai/hw_test/HwTestCoppUtils.cpp
)

target_link_libraries(sai_copp_utils
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
  hw_copp_utils
)

set_target_properties(sai_copp_utils PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_acl_utils
  fboss/agent/hw/sai/hw_test/HwTestAclUtils.cpp
)

target_link_libraries(sai_acl_utils
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
  hw_acl_utils
)

set_target_properties(sai_acl_utils PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_pfc_utils
  fboss/agent/hw/sai/hw_test/HwTestPfcUtils.cpp
  fboss/agent/hw/test/HwTestPfcUtils.cpp
)

target_link_libraries(sai_pfc_utils
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
  ${GTEST}
)

set_target_properties(sai_pfc_utils PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(sai_packet_trap_helper
  fboss/agent/hw/sai/hw_test/HwTestPacketTrapEntry.cpp
)

target_link_libraries(sai_packet_trap_helper
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
)

set_target_properties(sai_packet_trap_helper PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

add_library(agent_hw_test_thrift_handler
  fboss/agent/hw/test/HwTestThriftHandler.h
  fboss/agent/hw/test/HwTestLogCaptureThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestAclUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestAqmUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestMirrorUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestNeighborUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestPfcUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestEcmpUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestPortUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestVoqSwitchUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestRouteUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestTamUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestCommonUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestFlowletUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestPtpTcUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestSwitchingModeUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestUdfUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestTeFlowUtilsThriftHandler.cpp
  fboss/agent/hw/sai/hw_test/HwTestArsFlowletThriftHandler.cpp
)

target_link_libraries(agent_hw_test_thrift_handler
  hw_switch
  sai_switch # //fboss/agent/hw/sai/switch:sai_switch
  acl_test_utils
  agent_hw_test_ctrl_cpp2
  sai_aqm_utils
  sai_ecmp_utils
  sai_pfc_utils
  sai_port_utils
  sai_phy_capabilities
  diag_shell
  wedge_led_utils
)



function(BUILD_SAI_TEST SAI_IMPL_NAME SAI_IMPL_ARG)

  target_link_libraries(agent_hw_test_thrift_handler
    ${SAI_IMPL_ARG}
  )

  message(STATUS "Building SAI_IMPL_NAME: ${SAI_IMPL_NAME} SAI_IMPL_ARG: ${SAI_IMPL_ARG}")

  add_executable(sai_test-${SAI_IMPL_NAME}
    fboss/agent/hw/sai/hw_test/HwTestTamUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestAclUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestPfcUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestAqmUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestCoppUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestEcmpUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestFabricUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestFlowletSwitchingUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestNeighborUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestMirrorUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestMplsUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestPacketTrapEntry.cpp
    fboss/agent/hw/sai/hw_test/HwTestPtpTcUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestSwitchingModeUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestTeFlowUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestTrunkUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestRouteUtils.cpp
    fboss/agent/hw/sai/hw_test/HwTestUdfUtils.cpp
    fboss/agent/hw/sai/hw_test/HwVlanUtils.cpp
  )

  add_sai_sdk_dependencies(
    sai_test-${SAI_IMPL_NAME} "${SAI_IMPL_ARG}")

  target_link_libraries(sai_test-${SAI_IMPL_NAME}
    # --whole-archive is needed for gtest to find these tests
    -Wl,--whole-archive
    ${SAI_IMPL_ARG}
    sai_switch_ensemble
    sai_phy_capabilities
    sai_port_utils
    hw_switch_test
    hw_test_main
    -Wl,--no-whole-archive
    ref_map
    ${GTEST}
    ${LIBGMOCK_LIBRARIES}
  )

  if (SAI_BRCM_IMPL)
    target_link_libraries(sai_test-${SAI_IMPL_NAME}
      ${YAML}
    )
  endif()

  set_target_properties(sai_test-${SAI_IMPL_NAME}
      PROPERTIES COMPILE_FLAGS
      "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
      -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
      -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
    )

endfunction()

if(BUILD_SAI_FAKE)
BUILD_SAI_TEST("fake" fake_sai)
install(
  TARGETS
  sai_test-fake)
endif()

# If libsai_impl is provided, build sai tests linking with it
find_library(SAI_IMPL sai_impl)
message(STATUS "SAI_IMPL: ${SAI_IMPL}")

if(SAI_IMPL)
  BUILD_SAI_TEST("sai_impl" ${SAI_IMPL})
  install(
    TARGETS
    sai_test-sai_impl)
endif()
