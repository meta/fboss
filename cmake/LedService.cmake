# Make to build libraries and binaries in fboss/led_service

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_fbthrift_cpp_library(
  led_structs_types_cpp2
  fboss/led_service/if/led_structs.thrift
  OPTIONS
    json
    reflection
)

add_fbthrift_cpp_library(
  led_service_types_cpp2
  fboss/led_service/if/led_service.thrift
  SERVICES
    LedService
  OPTIONS
    json
    reflection
  DEPENDS
    fboss_cpp2
    ctrl_cpp2
    led_structs_types_cpp2
)

add_library(led_utils
  fboss/led_service/LedUtils.cpp
)

target_link_libraries(led_utils
  led_config
  led_structs_types_cpp2
)

add_library(led_config
  fboss/led_service/LedConfig.cpp
)

target_link_libraries(led_config
  fboss_error
  led_config_cpp2
  Folly::folly
  FBThrift::thriftcpp2
)

add_library(led_manager_lib
  fboss/led_service/BspLedManager.cpp
  fboss/led_service/FsdbSwitchStateSubscriber.cpp
  fboss/led_service/oss/DarwinLedManager.cpp
  fboss/led_service/oss/ElbertLedManager.cpp
  fboss/led_service/oss/FsdbSwitchStateSubscriber.cpp
  fboss/led_service/oss/FujiLedManager.cpp
  fboss/led_service/oss/MinipackLedManager.cpp
  fboss/led_service/oss/Wedge400BaseLedManager.cpp
  fboss/led_service/oss/Wedge400CLedManager.cpp
  fboss/led_service/oss/Wedge400LedManager.cpp
  fboss/led_service/oss/YampLedManager.cpp
  fboss/led_service/LedManager.cpp
  fboss/led_service/LedManagerInit.cpp
  fboss/led_service/LedUtils.cpp
  fboss/led_service/MinipackBaseLedManager.cpp
  fboss/led_service/MontblancLedManager.cpp
  fboss/led_service/M4061clscLedManager.cpp
  fboss/led_service/Icecube800banwLedManager.cpp
  fboss/led_service/Icecube800bcLedManager.cpp
  fboss/led_service/M4052ACTMLedManager.cpp
  fboss/led_service/M4061nhpLedManager.cpp
  fboss/led_service/M4062nhpLedManager.cpp
  fboss/led_service/M4340nhpLedManager.cpp
  fboss/led_service/Icetea800bcLedManager.cpp
  fboss/led_service/Meru800biaLedManager.cpp
  fboss/led_service/Meru800bfaLedManager.cpp
  fboss/led_service/Morgan800ccLedManager.cpp
  fboss/led_service/Minipack3BTALedManager.cpp
  fboss/led_service/Minipack3NLedManager.cpp
  fboss/led_service/Janga800bicLedManager.cpp
  fboss/led_service/Tahan800bcLedManager.cpp
  fboss/led_service/Tahansb800bcLedManager.cpp
  fboss/led_service/Wedge800BACTLedManager.cpp
  fboss/led_service/Wedge800CACTLedManager.cpp
  fboss/led_service/Ladakh800bclsLedManager.cpp
  fboss/led_service/Leh800bclsLedManager.cpp
)

target_link_libraries(led_manager_lib
  bsp_platform_mapping_cpp2
  common_port_utils
  qsfp_bsp_core
  ledIO
  led_config
  led_structs_types_cpp2
  log_thrift_call
  led_utils
  montblanc_bsp
  m4061clsc_bsp
  icecube800bc_bsp
  m4061nhp_bsp
  m4062nhp_bsp
  m4340nhp_bsp
  icetea800bc_bsp
  meru800bia_bsp
  meru800bfa_bsp
  janga800bic_bsp
  tahan800bc_bsp
  morgan800cc_bsp
  tahansb800bc_bsp
  ladakh800bcls_bsp
  leh800bcls_bsp
  darwin_platform_mapping
  elbert_platform_mapping
  fuji_platform_mapping
  janga800bic_platform_mapping
  minipack_platform_mapping
  minipack3bta_platform_mapping
  minipack3n_platform_mapping
  montblanc_platform_mapping
  icecube800bc_platform_mapping
  icetea800bc_platform_mapping
  meru800bia_platform_mapping
  meru800bfa_platform_mapping
  morgan_platform_mapping
  tahan800bc_platform_mapping
  tahansb800bc_platform_mapping
  ladakh800bcls_platform_mapping
  leh800bcls_platform_mapping
  wedge400_fpga
  wedge400_platform_mapping
  yamp_platform_mapping
  platform_mapping_utils
  product_info
  Folly::folly
  FBThrift::thriftcpp2
  fsdb_stream_client
  fsdb_pub_sub
  fsdb_flags
  fsdb_model
)

add_library(led_core_lib
  fboss/led_service/LedService.cpp
  fboss/led_service/LedServiceHandler.cpp
)

target_link_libraries(led_core_lib
  led_manager_lib
  led_service_types_cpp2
)

add_executable(led_service
  fboss/led_service/Main.cpp
)

target_link_libraries(led_service
  led_core_lib
  platform_utils
  fb303::fb303
)

install(TARGETS led_service)
