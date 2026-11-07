# CMake to build libraries and binaries in fboss/qsfp_service

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(qsfp_stats
  fboss/qsfp_service/StatsPublisher.h
  fboss/qsfp_service/StatsPublisher.cpp
  fboss/qsfp_service/StatsPublisherHelper.h
  fboss/qsfp_service/StatsPublisherHelper.cpp
)

target_link_libraries(qsfp_stats
  fboss_types
  transceiver_cpp2
  transceiver_manager
  phy_management_base
  port_manager
  Folly::folly
  fb303::fb303
)

add_library(qsfp_lib
  fboss/qsfp_service/fsdb/QsfpFsdbSubscriber.cpp
  fboss/qsfp_service/fsdb/QsfpFsdbSyncManager.cpp
  fboss/qsfp_service/lib/QsfpCache.cpp
)

target_link_libraries(qsfp_lib
    qsfp_cpp2
    ctrl_cpp2
    pim_state_cpp2
    i2c_controller_stats_cpp2
    transceiver_cpp2
    alert_logger
    Folly::folly
    fb303::fb303
    FBThrift::thriftcpp2
    qsfp_service_client
    fsdb_stream_client
    fsdb_pub_sub
    fsdb_cow_state_sub_mgr
    fsdb_flags
    fsdb_syncer
    fsdb_model
    qsfp_bsp_core
    thrift_cow_serializer
    wedge_transceiver
)

add_library(qsfp_config
  fboss/qsfp_service/QsfpConfig.cpp
)

target_link_libraries(qsfp_config
  fboss_error
  qsfp_config_cpp2
  Folly::folly
  FBThrift::thriftcpp2
)

add_library(build_from_xcvr_lib
  fboss/lib/bsp/BuildFromXcvrLib.cpp
)

target_link_libraries(build_from_xcvr_lib
  bsp_platform_mapping_cpp2
  led_mapping_cpp2
  xcvr_lib
  Folly::folly
  FBThrift::thriftcpp2
)

add_executable(build_from_xcvr_lib_test
  fboss/lib/bsp/tests/BuildFromXcvrLibTest.cpp
)

target_link_libraries(build_from_xcvr_lib_test
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  bsp_platform_mapping_cpp2
  build_from_xcvr_lib
  led_mapping_cpp2
  platform_config_lib
  xcvr_lib
  Folly::folly
  FBThrift::thriftcpp2
)

add_library(bsp_platform_mapping
  fboss/lib/bsp/BspPlatformMapping.cpp
)

target_link_libraries(bsp_platform_mapping
  bsp_platform_mapping_cpp2
  build_from_xcvr_lib
  FBThrift::thriftcpp2
)

add_library(meru800bia_bsp
  fboss/lib/bsp/meru800bia/Meru800biaBspPlatformMapping.cpp
)

target_link_libraries(meru800bia_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(meru800bfa_bsp
  fboss/lib/bsp/meru800bfa/Meru800bfaBspPlatformMapping.cpp
)

target_link_libraries(meru800bfa_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(montblanc_bsp
  fboss/lib/bsp/montblanc/MontblancBspPlatformMapping.cpp
)

target_link_libraries(montblanc_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(m4061clsc_bsp
  fboss/lib/bsp/m4061clsc/M4061clscBspPlatformMapping.cpp
)

target_link_libraries(m4061clsc_bsp
  bsp_platform_mapping
  bsp_platform_mapping_cpp2
  FBThrift::thriftcpp2
)

add_library(icecube800bc_bsp
  fboss/lib/bsp/icecube800bc/Icecube800bcBspPlatformMapping.cpp
)

target_link_libraries(icecube800bc_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(icetea800bc_bsp
  fboss/lib/bsp/icetea800bc/Icetea800bcBspPlatformMapping.cpp
)

target_link_libraries(icetea800bc_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(minipack3bta_bsp
  fboss/lib/bsp/minipack3bta/Minipack3BTABspPlatformMapping.cpp
)

target_link_libraries(minipack3bta_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(minipack3n_bsp
  fboss/lib/bsp/minipack3n/Minipack3NBspPlatformMapping.cpp
)

target_link_libraries(minipack3n_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(morgan800cc_bsp
  fboss/lib/bsp/morgan800cc/Morgan800ccBspPlatformMapping.cpp
)

target_link_libraries(morgan800cc_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(janga800bic_bsp
  fboss/lib/bsp/janga800bic/Janga800bicBspPlatformMapping.cpp
)

target_link_libraries(janga800bic_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(tahan800bc_bsp
  fboss/lib/bsp/tahan800bc/Tahan800bcBspPlatformMapping.cpp
)

target_link_libraries(tahan800bc_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(tahansb800bc_bsp
  fboss/lib/bsp/tahansb800bc/Tahansb800bcBspPlatformMapping.cpp
)

target_link_libraries(tahansb800bc_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(wedge800bact_bsp
  fboss/lib/bsp/wedge800bact/Wedge800BACTBspPlatformMapping.cpp
)

target_link_libraries(wedge800bact_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(wedge800cact_bsp
  fboss/lib/bsp/wedge800cact/Wedge800CACTBspPlatformMapping.cpp
)

target_link_libraries(wedge800cact_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(ladakh800bcls_bsp
  fboss/lib/bsp/ladakh800bcls/Ladakh800bclsBspPlatformMapping.cpp
)

target_link_libraries(ladakh800bcls_bsp
  bsp_platform_mapping
  bsp_platform_mapping_cpp2
  FBThrift::thriftcpp2
)

add_library(leh800bcls_bsp
  fboss/lib/bsp/leh800bcls/Leh800bclsBspPlatformMapping.cpp
)

target_link_libraries(leh800bcls_bsp
  bsp_platform_mapping
  bsp_platform_mapping_cpp2
  FBThrift::thriftcpp2
)

add_library(m4061nhp_bsp
  fboss/lib/bsp/m4061nhp/M4061nhpBspPlatformMapping.cpp
)

target_link_libraries(m4061nhp_bsp
  bsp_platform_mapping
  bsp_platform_mapping_cpp2
  FBThrift::thriftcpp2
)

add_library(m4062nhp_bsp
  fboss/lib/bsp/m4062nhp/M4062nhpBspPlatformMapping.cpp
)

target_link_libraries(m4062nhp_bsp
  bsp_platform_mapping
  bsp_platform_mapping_cpp2
  FBThrift::thriftcpp2
)

add_library(m4340nhp_bsp
  fboss/lib/bsp/m4340nhp/M4340nhpBspPlatformMapping.cpp
)

target_link_libraries(m4340nhp_bsp
  bsp_platform_mapping
  bsp_platform_mapping_cpp2
  FBThrift::thriftcpp2
)

add_library(m4052actm_bsp
	fboss/lib/bsp/m4052actm/M4052ACTMBspPlatformMapping.cpp
)

target_link_libraries(m4052actm_bsp
  bsp_platform_mapping
  FBThrift::thriftcpp2
)

add_library(m5120csc_bsp
  fboss/lib/bsp/m5120csc/M5120cscBspPlatformMapping.cpp
)

target_link_libraries(m5120csc_bsp
  bsp_platform_mapping
  bsp_platform_mapping_cpp2
  FBThrift::thriftcpp2
)

add_library(qsfp_bsp_core
  fboss/lib/bsp/BspGenericSystemContainer.cpp
  fboss/lib/bsp/BspIOBus.cpp
  fboss/lib/bsp/BspPimContainer.cpp
  fboss/lib/bsp/BspSystemContainer.cpp
  fboss/lib/bsp/BspPhyContainer.cpp
  fboss/lib/bsp/BspPhyIO.cpp
  fboss/lib/bsp/BspResetUtils.cpp
  fboss/lib/bsp/BspTransceiverAccess.cpp
  fboss/lib/bsp/BspTransceiverAccessImpl.cpp
  fboss/lib/bsp/BspTransceiverCpldAccess.cpp
  fboss/lib/bsp/BspTransceiverGpioAccess.cpp
  fboss/lib/bsp/BspTransceiverApi.cpp
  fboss/lib/bsp/BspTransceiverContainer.cpp
  fboss/lib/bsp/BspTransceiverIO.cpp
  fboss/lib/bsp/BspLedContainer.cpp
)

target_link_libraries(qsfp_bsp_core
  bsp_platform_mapping_cpp2
  bsp_platform_mapping
  common_file_utils
  gpiod_line
  i2_api
  i2c_controller_stats_cpp2
  Folly::folly
  meru800bia_bsp
  meru800bfa_bsp
  montblanc_bsp
  m4061clsc_bsp
  icecube800bc_bsp
  icetea800bc_bsp
  minipack3bta_bsp
  minipack3n_bsp
  morgan800cc_bsp
  janga800bic_bsp
  tahan800bc_bsp
  tahansb800bc_bsp
  wedge800bact_bsp
  wedge800cact_bsp
  ladakh800bcls_bsp
  leh800bcls_bsp
  m4052actm_bsp
  m4061nhp_bsp
  m4062nhp_bsp
  m4340nhp_bsp
  m5120csc_bsp
  device_mdio
  fpga_device
  phy_management_base
  i2c_ctrl
  fpga_multi_pim_container
  ledIO
  led_mapping_cpp2
  build_from_xcvr_lib
  ${LIBGPIOD}
)

add_library(transceiver_validator
  fboss/qsfp_service/TransceiverValidator.cpp
)

target_link_libraries(transceiver_validator
  alert_logger
  transceiver_validation_cpp2
  Folly::folly
)

add_library(transceiver_manager STATIC
    fboss/qsfp_service/TransceiverManager.cpp
    fboss/qsfp_service/TransceiverStateMachine.cpp
    fboss/qsfp_service/TransceiverStateMachineController.cpp
    fboss/qsfp_service/SlotThreadHelper.cpp
    fboss/qsfp_service/QsfpServiceThreads.cpp
)

target_link_libraries(transceiver_manager
  qsfp_lib
  qsfp_module
  transceiver_properties_manager
  ledIO
  qsfp_bsp_core
  platform_base
  io_stats_recorder
  platform_mapping_utils
  fboss_error
  fboss_types
  qsfp_config
  phy_management_base
  thrift_service_client
  common_file_utils
  thread_heartbeat
  utils
  product_info
  fsdb_flags
  firmware_upgrader
  transceiver_properties_manager
  transceiver_validator
  ${RE2}
  restart_time_tracker
)

add_library(port_manager STATIC
    fboss/qsfp_service/PortManager.cpp
    fboss/qsfp_service/PortStateMachine.cpp
    fboss/qsfp_service/PortStateMachineController.cpp
    fboss/qsfp_service/SlotThreadHelper.cpp
)

target_link_libraries(port_manager
  qsfp_lib
  fboss_error
  fboss_types
  utils
  transceiver_manager
  phy_management_base
  thrift_service_client
  thread_heartbeat
  utils
  product_info
  fsdb_flags
  restart_time_tracker
)

add_library(qsfp_handler
  fboss/qsfp_service/QsfpServiceHandler.cpp
)

target_link_libraries(qsfp_handler
  Folly::folly
  transceiver_manager
  port_manager
  log_thrift_call
  fsdb_stream_client
  fsdb_pub_sub
  fsdb_flags
  pai_diag_shell
)

# PAI Diag Shell library — same lib name in both cases (so qsfp_handler's
# link line is the same), but the sources + deps differ based on whether
# SAI_BRCM_PAI_IMPL is set. Mirrors the existing pattern used by
# `sai_phy_management` (LibPhy.cmake) and `qsfp_service` itself.
#
#   SAI_BRCM_PAI_IMPL=ON  -> real impl from PaiDiagShell.cpp, links against
#                            sai_repl + sai_phy_management + sai_phy. Gives
#                            users an interactive PAI shell over thrift.
#   SAI_BRCM_PAI_IMPL=OFF -> throw-on-call stub from PaiDiagShellStub.cpp.
#                            Calls return "PAI diag shell not supported on
#                            this platform" via FbossError.
if(SAI_BRCM_PAI_IMPL)
  add_library(pai_diag_shell
    fboss/qsfp_service/diag/PaiDiagShell.cpp
  )
  target_link_libraries(pai_diag_shell
    Folly::folly
    fboss_error
    sai_repl
    sai_api
    sai_phy_management
    ctrl_cpp2
  )
else()
  add_library(pai_diag_shell
    fboss/qsfp_service/diag/PaiDiagShellStub.cpp
  )
  target_link_libraries(pai_diag_shell
    Folly::folly
    fboss_error
    ctrl_cpp2
  )
endif()

add_library(qsfp_core
  fboss/qsfp_service/QsfpServer.cpp
  fboss/qsfp_service/QsfpServiceSignalHandler.cpp
  fboss/qsfp_service/oss/QsfpServer.cpp
)

target_link_libraries(qsfp_core
  qsfp_handler
  qsfp_config
  thrift_method_rate_limit_setup
  thrift_service_utils
)

set(QSFP_SERVICE_SRCS
  fboss/qsfp_service/Main.cpp
)

set(QSFP_SERVICE_DEPS
  qsfp_module
  qsfp_config
  phy_management_base
  transceiver_manager
  port_manager
  qsfp_platforms_wedge
  log_thrift_call
  qsfp_core
  qsfp_handler
)

if(SAI_BRCM_PAI_IMPL)
  BUILD_AND_INSTALL_WITH_XPHY_SDK_LIBS(
    "qsfp_service" QSFP_SERVICE_SRCS QSFP_SERVICE_DEPS "brcm_pai" XPHY_SDK_LIBS
  )
else()
  BUILD_AND_INSTALL_WITH_XPHY_SDK_LIBS(
    "qsfp_service" QSFP_SERVICE_SRCS QSFP_SERVICE_DEPS "" ""
  )
endif()

# Interactive REPL CLI client for the PAI diag shell — connects to a running
# qsfp_service over thrift's startPaiDiagShell + producePaiDiagShellInput
# streaming endpoints and gives the user a bcmsh-style prompt.
#
# Mirrors the agent's diag_shell_client (fboss/agent/hw/sai/diag/) which is
# the production-vetted pattern for the BCM NPU diag shell. The OSS variant
# uses a raw AsyncSocket + RocketClientChannel; the FB-internal variant
# (built via BUCK with facebook/) uses ServiceRouter.
add_executable(fboss_pai_diag_shell_client
  fboss/qsfp_service/diag/PaiDiagShellClient.cpp
  fboss/qsfp_service/diag/oss/PaiDiagShellClient.cpp
)

target_link_libraries(fboss_pai_diag_shell_client
  qsfp_cpp2
  ctrl_cpp2
  Folly::folly
  FBThrift::thriftcpp2
)

install(TARGETS fboss_pai_diag_shell_client)
