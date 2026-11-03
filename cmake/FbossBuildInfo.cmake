include_guard(GLOBAL)

function(fboss_register_build_info)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
  endif()

  set(FBOSS_SOURCE_REVISION "" CACHE STRING "Producer source revision for OSS binaries")
  set(FBOSS_SOURCE_DIRTY "" CACHE STRING "Producer dirty state: true, false, or unknown")
  set(FBOSS_BUILD_VERSION "" CACHE STRING "Optional producer build version for OSS binaries")
  find_program(FBOSS_BUILD_INFO_OBJCOPY NAMES objcopy)
  if(NOT FBOSS_BUILD_INFO_OBJCOPY)
    message(FATAL_ERROR "objcopy is required to capture OSS binary build identity")
  endif()
  set(script "${CMAKE_CURRENT_SOURCE_DIR}/fboss/oss/scripts/component_version.py")
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/fboss_build_info")
  set(arguments generate --source-dir "${CMAKE_CURRENT_SOURCE_DIR}" --output-dir "${output_dir}")
  if(FBOSS_SOURCE_REVISION)
    list(APPEND arguments --source-revision "${FBOSS_SOURCE_REVISION}")
  endif()
  if(NOT "${FBOSS_SOURCE_DIRTY}" STREQUAL "")
    list(APPEND arguments --source-dirty "${FBOSS_SOURCE_DIRTY}")
  endif()
  if(NOT "${FBOSS_BUILD_VERSION}" STREQUAL "")
    list(APPEND arguments --build-version "${FBOSS_BUILD_VERSION}")
  endif()

  get_property(targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  set(sources)
  set(executables)
  foreach(target ${targets})
    get_target_property(type ${target} TYPE)
    if(NOT type STREQUAL "EXECUTABLE")
      continue()
    endif()
    get_target_property(name ${target} OUTPUT_NAME)
    if(NOT name)
      set(name "${target}")
    endif()
    set(source "${output_dir}/${name}.cpp")
    list(APPEND sources "${source}")
    list(APPEND executables "${target}")
    list(APPEND arguments --target "${name}")
    set_source_files_properties("${source}" PROPERTIES GENERATED TRUE)
    target_sources(${target} PRIVATE "${source}")
    set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--undefined=fboss_build_info")
    add_custom_command(
      TARGET ${target} POST_BUILD
      COMMAND "${_py3_exe}" "${script}" record
        --binary "$<TARGET_FILE:${target}>"
        --objcopy "${FBOSS_BUILD_INFO_OBJCOPY}"
      VERBATIM
    )
  endforeach()
  if(executables)
    add_custom_target(fboss_generate_build_info
      COMMAND "${_py3_exe}" "${script}" ${arguments}
      BYPRODUCTS ${sources}
      VERBATIM
    )
    foreach(target ${executables})
      add_dependencies(${target} fboss_generate_build_info)
    endforeach()
  endif()
endfunction()
