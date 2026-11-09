# CMake to build libraries and binaries for agent link testing

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_executable(multi_switch_invariant_agent_test
  fboss/agent/test/prod_invariant_tests/SaiProdInvariantTests.cpp
)

add_sai_sdk_dependencies(multi_switch_invariant_agent_test)

target_link_libraries(multi_switch_invariant_agent_test
  -Wl,--whole-archive
  invariant_agent_tests
  multi_switch_agent_ensemble
  agent_hw_test_thrift_handler
  setup_thrift_prod
  sai_acl_utils
  sai_copp_utils
  sai_platform
  -Wl,--no-whole-archive
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

install(TARGETS multi_switch_invariant_agent_test)

function(BUILD_SAI_INVARIANT_AGENT_TEST SAI_IMPL_NAME SAI_IMPL_ARG)

  message(STATUS "Building SAI_IMPL_NAME: ${SAI_IMPL_NAME} SAI_IMPL_ARG: ${SAI_IMPL_ARG}")
  add_executable(sai_invariant_agent_test-${SAI_IMPL_NAME}
    fboss/agent/test/prod_invariant_tests/SaiProdInvariantTests.cpp
  )

  add_sai_sdk_dependencies(
    sai_invariant_agent_test-${SAI_IMPL_NAME} "${SAI_IMPL_ARG}")

  target_link_libraries(sai_invariant_agent_test-${SAI_IMPL_NAME}
    -Wl,--whole-archive
    ${SAI_IMPL_ARG}
    invariant_agent_tests
    mono_agent_ensemble
    agent_hw_test_thrift_handler
    setup_thrift_prod
    sai_acl_utils
    sai_copp_utils
    sai_platform
    -Wl,--no-whole-archive
    ${GTEST}
    ${LIBGMOCK_LIBRARIES}
  )

  set_target_properties(sai_invariant_agent_test-${SAI_IMPL_NAME}
    PROPERTIES COMPILE_FLAGS
    "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
    -DSAI_VER_MINOR=${SAI_VER_MINOR} \
    -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
  )
endfunction()

if(BUILD_SAI_FAKE)
BUILD_SAI_INVARIANT_AGENT_TEST("fake" fake_sai)
install(
  TARGETS
  sai_invariant_agent_test-fake)
endif()

# If libsai_impl is provided, build sai tests linking with it
find_library(SAI_IMPL sai_impl)
message(STATUS "SAI_IMPL: ${SAI_IMPL}")

if(SAI_IMPL)
  BUILD_SAI_INVARIANT_AGENT_TEST("sai_impl" ${SAI_IMPL})
  install(
    TARGETS
    sai_invariant_agent_test-sai_impl)
endif()
