# provide list of libraries  that FBOSS binaries must link with
# because SDK library (libsai_impl.a) uses it and has undefined
# symbols, leading to linker errors while linking binaries.
# specify this list in sdk_dependencies.txt with each library on
# different line (without leading or trailing spaces)
#
#
# $ cat sdk_dependencies.txt
# foo
# bar
# foobar
# foobarfoo
#
#
# FBOSS  binaries link with  above libraries which libsai_impl.a
# needs
function(register_npu_sdk_metadata_post_build name sai_impl_arg)
  get_target_property(TARGET_TYPE ${name} TYPE)
  if(NOT TARGET_TYPE STREQUAL "EXECUTABLE")
    return()
  endif()

  set(NPU_SDK_METADATA_PATH "${CMAKE_BINARY_DIR}/npu_sdk_metadata.json")
  set(NPU_SDK_METADATA_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/fboss/oss/scripts/npu_sdk_utils.py")

  if("${sai_impl_arg}" STREQUAL "fake_sai")
    add_custom_command(
      TARGET ${name}
      POST_BUILD
      COMMAND "${_py3_exe}" "${NPU_SDK_METADATA_SCRIPT}" remove
        --metadata-path "${NPU_SDK_METADATA_PATH}"
        --binary-name "$<TARGET_FILE_NAME:${name}>"
      VERBATIM
    )
    return()
  endif()

  if(SAI_BRCM_IMPL)
    set(NPU_SAI_IMPL "SAI_BRCM_IMPL")
  elseif(SAI_TAJO_IMPL)
    set(NPU_SAI_IMPL "SAI_TAJO_IMPL")
  elseif(CHENAB_SAI_SDK)
    set(NPU_SAI_IMPL "CHENAB_SAI_SDK")
  else()
    return()
  endif()

  if("$ENV{SAI_SDK_VERSION}" STREQUAL "" OR
     "$ENV{NPU_ASIC_SDK_VERSION}" STREQUAL "" OR
     "$ENV{NPU_SAI_SDK_VERSION}" STREQUAL "")
    message(WARNING
      "Skipping NPU SDK metadata for ${name}: detailed SDK environment is incomplete")
    return()
  endif()

  add_custom_command(
    TARGET ${name}
    POST_BUILD
    COMMAND "${_py3_exe}" "${NPU_SDK_METADATA_SCRIPT}" record
      --metadata-path "${NPU_SDK_METADATA_PATH}"
      --binary-name "$<TARGET_FILE_NAME:${name}>"
      --npu-sai-impl "${NPU_SAI_IMPL}"
      --npu-sai-sdk-selector "$ENV{SAI_SDK_VERSION}"
      --asic-sdk-version "$ENV{NPU_ASIC_SDK_VERSION}"
      --sai-sdk-version "$ENV{NPU_SAI_SDK_VERSION}"
    VERBATIM
  )
endfunction()

function (add_sai_sdk_dependencies name)
  file(READ sdk_dependencies.txt DEPENDENCIES_TEXT)
  string(REPLACE "\n" ";" DEPENDENCIES "${DEPENDENCIES_TEXT}")
  foreach(DEPENDENCY ${DEPENDENCIES})
    target_link_libraries(${name} ${DEPENDENCY})
  endforeach ()

  # Also load sai_dependencies.txt if it exists beside libsai_impl.a.
  # SAI vendors may ship a list of libraries their libsai_impl.a needs;
  # link those too so binaries can resolve the SDK's undefined symbols.
  # Entries may be bare filenames (libfoo.so) that resolve against the
  # SDK's lib dir, -lfoo flags, or absolute paths.
  # cmake/*.cmake are included alphabetically, and some callers sort before
  # AgentHwSaiApi.cmake, which otherwise finds SAI_IMPL_DIR first.
  if (NOT SAI_IMPL_DIR)
    find_path(SAI_IMPL_DIR NAMES lib/libsai_impl.a lib/libsai_impl.so)
  endif ()
  if (SAI_IMPL_DIR AND EXISTS "${SAI_IMPL_DIR}/lib/sai_dependencies.txt")
    file(READ "${SAI_IMPL_DIR}/lib/sai_dependencies.txt" SAI_DEPENDENCIES_TEXT)
    string(REPLACE "\n" ";" SAI_DEPENDENCIES "${SAI_DEPENDENCIES_TEXT}")
    target_link_directories(${name} PRIVATE "${SAI_IMPL_DIR}/lib")
    foreach(DEPENDENCY ${SAI_DEPENDENCIES})
      if (NOT "${DEPENDENCY}" STREQUAL "")
        target_link_libraries(${name} ${DEPENDENCY})
      endif ()
    endforeach ()
  endif ()

  if(ARGC GREATER 1)
    register_npu_sdk_metadata_post_build(${name} "${ARGV1}")
  endif()
endfunction ()

function (strtok str delim out_list)
  set(result_list "")
  string(REPLACE "${delim}" ";" str_list ${str})
  foreach(item IN LISTS str_list)
    list(APPEND result_list "${item}")
  endforeach()
  set(${out_list} "${result_list}" PARENT_SCOPE)
endfunction()

function(BUILD_AND_INSTALL_WITH_XPHY_SDK_LIBS NAME SRCS_VAR DEPS_VAR XPHY_SDK_NAME XPHY_SDK_LIBS_VAR)
  if("${XPHY_SDK_NAME}" STREQUAL "")
    message(STATUS "Building ${NAME} without XPHY SDK")
  else()
    message(STATUS "Building ${NAME} with XPHY SDK=${XPHY_SDK_NAME}")
  endif()

  add_executable(${NAME} ${${SRCS_VAR}})

  target_link_libraries(${NAME} ${${DEPS_VAR}})

  if(NOT "${XPHY_SDK_NAME}" STREQUAL "" AND DEFINED ${XPHY_SDK_LIBS_VAR})
    # Only add XPHY_SDK_LIBS if it's defined and not empty
    target_link_libraries(${NAME}
      -Wl,--whole-archive
      ${${XPHY_SDK_LIBS_VAR}}
      -Wl,--no-whole-archive
    )
  endif()

  install(TARGETS ${NAME})
endfunction()
