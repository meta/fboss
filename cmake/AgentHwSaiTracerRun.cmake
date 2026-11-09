# CMake to build libraries and binaries in fboss/agent/hw/sai/tracer/run

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

function(BUILD_SAI_REPLAYER SAI_IMPL_NAME SAI_IMPL_ARG)

  message(STATUS "Building Sai Replayer SAI_IMPL_NAME: ${SAI_IMPL_NAME} SAI_IMPL_ARG: ${SAI_IMPL_ARG}")

  add_executable(sai_replayer-${SAI_IMPL_NAME}
    fboss/agent/hw/sai/tracer/run/Main.cpp
    fboss/agent/hw/sai/tracer/run/SaiLog.cpp
  )

  register_npu_sdk_metadata_post_build(
    sai_replayer-${SAI_IMPL_NAME} "${SAI_IMPL_ARG}")

  target_link_libraries(sai_replayer-${SAI_IMPL_NAME}
    # This is needed for 'dlsym', 'dlopen' etc.
    -Wl,--no-as-needed -ldl
    -lz
    ${SAI_IMPL_ARG}
    ${CMAKE_THREAD_LIBS_INIT}
  )

  set_target_properties(sai_replayer-${SAI_IMPL_NAME}
      PROPERTIES COMPILE_FLAGS
      "-DSAI_VER_MAJOR=${SAI_VER_MAJOR} \
      -DSAI_VER_MINOR=${SAI_VER_MINOR}  \
      -DSAI_VER_RELEASE=${SAI_VER_RELEASE}"
    )

endfunction()

if(BUILD_SAI_FAKE)
BUILD_SAI_REPLAYER("fake" fake_sai)
install(
  TARGETS
  sai_replayer-fake)
endif()

# If libsai_impl is provided, build sai replayer linking with it
find_library(SAI_IMPL sai_impl)
message(STATUS "SAI_IMPL: ${SAI_IMPL}")

if(SAI_IMPL)
  BUILD_SAI_REPLAYER("sai_impl" ${SAI_IMPL})
  install(
    TARGETS
    sai_replayer-sai_impl)
endif()
