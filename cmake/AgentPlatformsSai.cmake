# CMake to build libraries and binaries in fboss/agent/platforms/sai

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

set(SAI_PLATFORM_COMMON_SRC
  fboss/agent/platforms/sai/SaiPlatform.cpp
  fboss/agent/platforms/sai/SaiPlatformPort.cpp
  fboss/agent/platforms/sai/SaiPlatformInit.cpp
)

set(SAI_PLATFORM_BCM_SRC
  fboss/agent/platforms/sai/GenericSaiBcmPlatform.cpp
  fboss/agent/platforms/sai/SaiBcmPlatform.cpp
  fboss/agent/platforms/sai/SaiBcmPlatformPort.cpp
  fboss/agent/platforms/sai/SaiBcmWedge100Platform.cpp
  fboss/agent/platforms/sai/SaiBcmWedge400Platform.cpp
  fboss/agent/platforms/sai/SaiBcmWedge400PlatformPort.cpp
  fboss/agent/platforms/sai/SaiBcmDarwinPlatform.cpp
  fboss/agent/platforms/sai/SaiBcmElbertPlatform.cpp
  fboss/agent/platforms/sai/SaiBcmMinipackPlatform.cpp
  fboss/agent/platforms/sai/SaiBcmYampPlatform.cpp
  fboss/agent/platforms/sai/SaiBcmFujiPlatform.cpp
  fboss/agent/platforms/sai/SaiElbert8DDPhyPlatformPort.cpp
  fboss/agent/platforms/sai/SaiPlatformInitBcm.cpp
  fboss/agent/platforms/sai/oss/SaiBcmMinipackPlatform.cpp
  fboss/agent/platforms/sai/oss/SaiBcmPlatform.cpp
  fboss/agent/platforms/sai/oss/SaiBcmMinipackPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmFujiPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmWedge100PlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmWedge400PlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmDarwinPlatform.cpp
  fboss/agent/platforms/sai/oss/SaiBcmDarwinPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmYampPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmElbertPlatformPort.cpp
)

set(SAI_PLATFORM_FAKE_SRC
  fboss/agent/platforms/sai/SaiBcmMinipackPlatform.cpp
  fboss/agent/platforms/sai/SaiBcmPlatform.cpp
  fboss/agent/platforms/sai/SaiBcmPlatformPort.cpp
  fboss/agent/platforms/sai/SaiFakePlatform.cpp
  fboss/agent/platforms/sai/SaiFakePlatformPort.cpp
  fboss/agent/platforms/sai/SaiPlatformInitFake.cpp
  fboss/agent/platforms/sai/oss/SaiBcmMinipackPlatform.cpp
  fboss/agent/platforms/sai/oss/SaiBcmPlatform.cpp
  fboss/agent/platforms/sai/oss/SaiBcmMinipackPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmFujiPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmWedge100PlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmWedge400PlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmDarwinPlatform.cpp
  fboss/agent/platforms/sai/oss/SaiBcmDarwinPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmYampPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiBcmElbertPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiWedge400CPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiTajoPlatform.cpp
)

set(SAI_PLATFORM_TAJO_SRC
  fboss/agent/platforms/sai/GenericSaiTajoPlatform.cpp
  fboss/agent/platforms/sai/SaiPlatformInitTajo.cpp
  fboss/agent/platforms/sai/SaiWedge400CPlatform.cpp
  fboss/agent/platforms/sai/SaiWedge400CPlatformPort.cpp
  fboss/agent/platforms/sai/SaiTajoPlatform.cpp
  fboss/agent/platforms/sai/SaiTajoPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiWedge400CPlatformPort.cpp
  fboss/agent/platforms/sai/oss/SaiTajoPlatform.cpp
)

set(SAI_PLATFORM_CHENAB_SRC
  fboss/agent/platforms/sai/GenericSaiYangraPlatform.cpp
  fboss/agent/platforms/sai/SaiMinipack3NPlatform.cpp
  fboss/agent/platforms/sai/SaiChenabPlatformPort.cpp
  fboss/agent/platforms/sai/SaiPlatformInitYangra.cpp
)

set(SAI_PLATFORM_SRC
  ${SAI_PLATFORM_COMMON_SRC}
)

if (SAI_TAJO_IMPL)
  list(APPEND SAI_PLATFORM_SRC
    ${SAI_PLATFORM_TAJO_SRC}
  )
elseif (SAI_BRCM_IMPL)
  list(APPEND SAI_PLATFORM_SRC
    ${SAI_PLATFORM_BCM_SRC}
  )
elseif (CHENAB_SAI_SDK)
  list(APPEND SAI_PLATFORM_SRC
    ${SAI_PLATFORM_CHENAB_SRC}
  )
elseif (SAI_BRCM_PAI_IMPL)
  # PAI (retimer / XPHY) build: same platform-port sources as the fake build,
  # but with a PAI-specific createSaiPlatformPort that builds real ports for the
  # Agera3 retimer boxes (LEH800BCLS/LADAKH800BCLS) instead of the fake-only
  # factory. Mirrors brcm_pai_srcs in platform.bzl.
  set(SAI_PLATFORM_PAI_SRC ${SAI_PLATFORM_FAKE_SRC})
  list(REMOVE_ITEM SAI_PLATFORM_PAI_SRC
    fboss/agent/platforms/sai/SaiPlatformInitFake.cpp)
  list(APPEND SAI_PLATFORM_SRC
    fboss/agent/platforms/sai/SaiPhyPlatform.cpp
    fboss/agent/platforms/sai/SaiPlatformInitPai.cpp
    ${SAI_PLATFORM_PAI_SRC}
  )
else()
  list(APPEND SAI_PLATFORM_SRC
    ${SAI_PLATFORM_FAKE_SRC}
  )
endif()

add_library(sai_platform ${SAI_PLATFORM_SRC})

target_link_libraries(sai_platform
  handler
  agent_features
  dsfnode_utils
  Folly::folly
  product_info
  sai_switch
  thrift_handler
  switch_asics
  hw_switch_warmboot_helper
  fake_test_platform_mapping
  minipack_platform_mapping
  elbert_platform_mapping
  yamp_platform_mapping
  fuji_platform_mapping
  galaxy_platform_mapping
  wedge100_platform_mapping
  wedge400_platform_utils
  wedge400c_platform_utils
  darwin_platform_mapping
  wedge400_platform_mapping
  wedge400c_platform_mapping
  morgan_platform_mapping
  wedge_led_utils
  bcm_yaml_config
  meru800bia_platform_mapping
  meru800bfa_platform_mapping
  yangra_platform_mapping
  minipack3bta_platform_mapping
  minipack3n_platform_mapping
  montblanc_platform_mapping
  janga800bic_platform_mapping
  tahan800bc_platform_mapping
  tahansb800bc_platform_mapping
  blackwolf800banw_platform_mapping
  ladakh800bcls_platform_mapping
  leh800bcls_platform_mapping
  icecube800bc_platform_mapping
  icetea800bc_platform_mapping
  led_structs_types_cpp2
  led_mapping_cpp2
  yangra2_platform_mapping
)

set_target_properties(sai_platform PROPERTIES COMPILE_FLAGS
  "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
  -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
  -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
)

function(BUILD_SAI_WEDGE_AGENT SAI_IMPL_NAME SAI_IMPL_ARG)

  message(STATUS "Building Sai WedgeAgent SAI_IMPL_NAME: ${SAI_IMPL_NAME} SAI_IMPL_ARG: ${SAI_IMPL_ARG}")

  add_executable(wedge_agent-${SAI_IMPL_NAME}
    fboss/agent/platforms/sai/wedge_agent.cpp
  )

  add_sai_sdk_dependencies(
    wedge_agent-${SAI_IMPL_NAME} "${SAI_IMPL_ARG}")

  target_link_libraries(wedge_agent-${SAI_IMPL_NAME}
    -Wl,--whole-archive
    main
    monolithic_agent_initializer
    sai_platform
    sai_traced_api
    setup_thrift_prod
    thrift_service_client
    ${SAI_IMPL_ARG}
    -Wl,--no-whole-archive
    ${CMAKE_THREAD_LIBS_INIT}
  )

  if (SAI_BRCM_IMPL)
    target_link_libraries(wedge_agent-${SAI_IMPL_NAME}
      ${YAML}
    )
  endif()

  set_target_properties(wedge_agent-${SAI_IMPL_NAME}
      PROPERTIES COMPILE_FLAGS
      "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
      -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
      -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
    )

  add_executable(fboss_hw_agent-${SAI_IMPL_NAME}
    fboss/agent/platforms/sai/WedgeHwAgent.cpp
    fboss/agent/platforms/sai/oss/WedgeHwAgent.cpp
  )

  add_sai_sdk_dependencies(
    fboss_hw_agent-${SAI_IMPL_NAME} "${SAI_IMPL_ARG}")

  target_link_libraries(fboss_hw_agent-${SAI_IMPL_NAME}
    -Wl,--whole-archive
    hwagent-main
    fboss_common_init
    load_agent_config
    sai_platform
    hwagent
    thrift_service_client
    ${SAI_IMPL_ARG}
    -Wl,--no-whole-archive
  )

endfunction()

# If libsai_impl is provided, build wedge_agent linking with it
find_library(SAI_IMPL sai_impl)
message(STATUS "SAI_IMPL: ${SAI_IMPL}")

if(BUILD_SAI_FAKE)
  # Name fake-SAI agent binaries with the -sai_impl suffix so they sit at
  # the same /opt/fboss/bin/ paths the systemd unit files reference and
  # package.py FORWARDING_BINARIES matches without modification.
  BUILD_SAI_WEDGE_AGENT("sai_impl" fake_sai)
elseif(SAI_IMPL)
  BUILD_SAI_WEDGE_AGENT("sai_impl" ${SAI_IMPL})
  install(
    TARGETS
    wedge_agent-sai_impl)
  install(
    TARGETS
    fboss_hw_agent-sai_impl)
endif()
