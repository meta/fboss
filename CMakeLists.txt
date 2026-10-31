cmake_minimum_required(VERSION 3.8)

include(functions.cmake)

option(WITH_ASAN "Turn ON address sanitizer build options" OFF)
if ($ENV{WITH_ASAN})
  message(STATUS "ENV WITH_ASAN is set")
  set (WITH_ASAN ON)
endif()

if (WITH_ASAN)
  message(STATUS "Building WITH_ASAN")
endif()

option(BENCHMARK_INSTALL "Installation of benchmark test binaries" OFF)
if ($ENV{BENCHMARK_INSTALL})
  message(STATUS "ENV BENCHMARK_INSTALL is set")
  set (BENCHMARK_INSTALL ON)
endif()

if (BENCHMARK_INSTALL)
  message(STATUS "Configured to install benchmark binaries")
endif()

option(BUILD_SAI_FAKE "Build sai binaries for fake_sai" OFF)
if ($ENV{BUILD_SAI_FAKE})
  message(STATUS "ENV BUILD_SAI_FAKE is set")
  set (BUILD_SAI_FAKE ON)
endif()

option(GITHUB_ACTIONS_BUILD
  "Enables custom targets specific to GitHub Actions automation"
  OFF)
if ($ENV{GITHUB_ACTIONS_BUILD})
  message(STATUS "ENV GITHUB_ACTIONS_BUILD is set")
  set (GITHUB_ACTIONS_BUILD ON)
endif()

option(BUILD_SAI_FAKE_BENCHMARKS "Build benchmarks binaries for fake_sai" OFF)
if ($ENV{BUILD_SAI_FAKE_BENCHMARKS})
  message(STATUS "ENV BUILD_SAI_FAKE_BENCHMARKS is set")
  set (BUILD_SAI_FAKE_BENCHMARKS ON)
endif()

if (BUILD_SAI_FAKE_BENCHMARKS)
  message(STATUS "Building benchmarks binaries for fake_sai")
endif()

option(SKIP_ALL_INSTALL "Installation of fboss binaries" OFF)
if ($ENV{SKIP_ALL_INSTALL})
  message(STATUS "ENV SKIP_ALL_INSTALL is set")
  set (SKIP_ALL_INSTALL ON)
endif()

set(FBOSS_BUILD_PROFILE "full" CACHE STRING
  "Build profile: full | fsdb_client | exported_libraries")
# Env wins over -D: the OSS CI rules pass the profile as a container env var
# while getdeps passes a cache define, and a rule must be able to override.
if (NOT "$ENV{FBOSS_BUILD_PROFILE}" STREQUAL "")
  set(FBOSS_BUILD_PROFILE "$ENV{FBOSS_BUILD_PROFILE}")
endif()
if (NOT FBOSS_BUILD_PROFILE MATCHES
    "^(full|fsdb_client|exported_libraries)$")
  message(FATAL_ERROR "Unknown FBOSS_BUILD_PROFILE '${FBOSS_BUILD_PROFILE}'")
endif()
set_property(
  CACHE FBOSS_BUILD_PROFILE
  PROPERTY STRINGS full fsdb_client exported_libraries
)
if (NOT FBOSS_BUILD_PROFILE STREQUAL "full")
  message(STATUS "FBOSS build profile: ${FBOSS_BUILD_PROFILE}")
endif()

# One macro with a runtime switch, never two: CMake's _name shadowing is a
# single level, so a second macro(install) makes _install resolve to the first.
if (SKIP_ALL_INSTALL OR NOT FBOSS_BUILD_PROFILE STREQUAL "full")
  set(FBOSS_INSTALL_ENABLED FALSE)
  install(FILES README.md DESTINATION .)
  macro (install)
    if (FBOSS_INSTALL_ENABLED)
      _install(${ARGV})
    elseif (SKIP_ALL_INSTALL)
      message(STATUS "Skipping installing ${ARGV} fboss artifact")
      _install(${ARGV} EXCLUDE_FROM_ALL)
    else()
      message(STATUS "Profile ${FBOSS_BUILD_PROFILE}: skip install ${ARGV}")
    endif()
  endmacro ()
endif()

option(SAI_TAJO_IMPL "Build SAI api with tajo extensions" OFF)
if ($ENV{SAI_TAJO_IMPL})
  message(STATUS "ENV SAI_TAJO_IMPL is set")
  set (SAI_TAJO_IMPL ON)
  add_definitions (-DTAJO_SDK)
endif()
if (SAI_TAJO_IMPL)
  # Code checks TAJO_SDK directly; SAI_SDK_VERSION only defines the selector.
  add_definitions(-DTAJO_SDK)
endif()

option(SAI_BRCM_IMPL "Build SAI api with BRCM-SAI extensions" OFF)
if ($ENV{SAI_BRCM_IMPL})
  message(STATUS "ENV SAI_BRCM_IMPL is set")
  set (SAI_BRCM_IMPL ON)
endif()

option(CHENAB_SAI_SDK "Build SAI api with CHENAB-SAI extensions" OFF)
if ($ENV{CHENAB_SAI_SDK})
  message(STATUS "ENV CHENAB_SAI_SDK is set")
  set (CHENAB_SAI_SDK ON)
  add_definitions (-DCHENAB_SAI_SDK)
endif()

option(SAI_BRCM_PAI_IMPL "Build SAI api with BRCOM_PAI-SAI extensions" OFF)
if ($ENV{SAI_BRCM_PAI_IMPL})
  message(STATUS "ENV SAI_BRCM_PAI_IMPL is set")
  set(SAI_BRCM_PAI_IMPL ON)
  add_definitions(-DBRCM_PAI_SDK)
  # NOTE: Unlike agent that we use sai_impl, we directly use pre-built SDK from
  # local directory
  # Header files for libpai.a, the whole inc directory from SDK
  set(SAI_BRCM_PAI_IMPL_INC_DIR "/var/FBOSS/pai_impl/include")
  # PAI vendor headers that sit at the root of the SDK's inc/, notably
  # brcm_pai_extensions.h. These are siblings of the sai/ and pai_macsec/
  # subdirs, not members of them, so the root needs its own entry.
  include_directories("${SAI_BRCM_PAI_IMPL_INC_DIR}")
  # macsec headers
  include_directories("${SAI_BRCM_PAI_IMPL_INC_DIR}/pai_macsec")
  # epdm headers
  include_directories("${SAI_BRCM_PAI_IMPL_INC_DIR}/epdm")

  # SDK artifacts directory which hosts libpai.a, EPDM SW artifact, PHY Driver
  # SW artifact
  set(SAI_BRCM_PAI_IMPL_LIB_DIR "/var/FBOSS/pai_impl/lib")
  set(CMAKE_PREFIX_PATH ${SAI_BRCM_PAI_IMPL_LIB_DIR} ${CMAKE_PREFIX_PATH})
  find_library(PAI_LIB pai)
  find_library(EPDM_LIB epdm)
  find_library(DRIVER_LIB phymodepil)
  message(
    STATUS
    "PAI_LIB: ${PAI_LIB}, EPDM_LIB: ${EPDM_LIB}, DRIVER_LIB: ${DRIVER_LIB}"
  )
  set(XPHY_SDK_LIBS
    ${DRIVER_LIB}
    ${EPDM_LIB}
    ${PAI_LIB}
  )
endif()

if (NOT $ENV{SAI_VERSION} EQUAL "")
  message(STATUS "ENV SAI_VERSION is set")
  set (SAI_VERSION $ENV{SAI_VERSION})
else()
  message(STATUS "ENV SAI_VERSION is not set, using default:1.19.0")
  set (SAI_VERSION "1.19.0")
endif()

option(BUILD_SAI_FAKE_LINK_TEST "Building of SAI Fake Link Test binaries" OFF)
if ($ENV{BUILD_SAI_FAKE_LINK_TEST})
  message(STATUS "ENV BUILD_SAI_FAKE_LINK_TEST is set")
  set (BUILD_SAI_FAKE_LINK_TEST ON)
endif()

if (BUILD_SAI_FAKE_LINK_TEST)
  message(STATUS "Configured to build SAI Fake Link Test binaries")
endif()

if (SAI_BRCM_IMPL OR SAI_TAJO_IMPL OR CHENAB_SAI_SDK)
  # Every NPU SDK needs its selector: SaiVersion.h derives the SDK feature
  # flags from it, and without it version-gated code compiles out silently.
  if ("$ENV{SAI_SDK_VERSION}" STREQUAL "")
    message(FATAL_ERROR
      "SAI_SDK_VERSION environment variable is required when building with an NPU SDK.\n"
      "Pass the SDK version with run-getdeps.py --npu-sai-sdk-version; "
      "run-getdeps.py -h lists the supported values.")
  endif()

  set(SAI_SDK_VERSION "$ENV{SAI_SDK_VERSION}")
  message(STATUS "Building for SAI_SDK_VERSION: ${SAI_SDK_VERSION}")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D${SAI_SDK_VERSION}")
  message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
endif()

# SAI version is set. Extract MAJOR, MINOR, RELEASE ver
strtok(${SAI_VERSION} "." tokens)
set(listLength 0)
list(LENGTH tokens listLength)
if(NOT ${listLength} EQUAL 3)
  message(FATAL_ERROR "Invalid SAI_VERSION in env variable $ENV{SAI_VERSION}")
else()
  list(GET tokens 0 SAI_VER_MAJOR)
  list(GET tokens 1 SAI_VER_MINOR)
  list(GET tokens 2 SAI_VER_RELEASE)
endif()
message(STATUS "Building for SAI Spec ${SAI_VER_MAJOR}.${SAI_VER_MINOR}.${SAI_VER_RELEASE}")

set(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
  ${CMAKE_MODULE_PATH})

set (CMAKE_MODULE_PATH
  "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if (POLICY CMP0054)
  # Enable CMP0054 to indicate that we want the new safer if() argument behavior
  cmake_policy(SET CMP0054 NEW)
endif()
project(FBOSS)

include(CMakeParseArguments)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Match Python's -march flag when building with -python variant dependencies.
# Cython extensions inherit -march from Python sysconfig (e.g. -march=x86-64-v2
# on CentOS Stream 9), but cmake does not.  All C++ code linking against
# folly-python's libfolly.so must use the same -march to avoid F14LinkCheck
# ABI mismatch.
if(Python3_EXECUTABLE)
  set(_py3_exe "${Python3_EXECUTABLE}")
else()
  set(_py3_exe "python3")
endif()
execute_process(
  COMMAND ${_py3_exe} -c "
import sysconfig
f = sysconfig.get_config_var('CFLAGS') or ''
m = next((x for x in f.split() if x.startswith('-march=')), '')
print(m)"
  OUTPUT_VARIABLE _python_march
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE _march_rc
)
if(_march_rc EQUAL 0 AND _python_march)
  message(STATUS "Adding ${_python_march} from Python sysconfig")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_python_march}")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-compare")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused-variable")
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-maybe-uninitialized")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
# Clang-specific warning suppressions and fixes
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-literal-operator")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-lambda-capture")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-vla-cxx-extension")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-const-variable")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-private-field")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-character-conversion")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-missing-template-arg-list-after-template-kw")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-tautological-constant-out-of-range-compare")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-inconsistent-missing-override")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-invalid-constexpr")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unneeded-internal-declaration")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nullability-completeness")
  # Use lld linker and disable PIE
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld -no-pie")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
endif()
if (WITH_ASAN)
  # -g1 (line tables only) keeps file:line in sanitizer reports. Full -g is
  # over 80% of each ASAN binary, too large for a full build to fit on disk.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g1 -static-libasan")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-sanitize-recover=all")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-common")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize-address-use-after-scope")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-sanitize=alignment")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-sanitize=null")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-sanitize=vptr")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libasan")
endif()

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BUILD_DIR})

find_package(GTest REQUIRED)
include_directories(
  ${GTEST_INCLUDE_DIRS}
)

find_package(GMock MODULE REQUIRED)
find_package(gflags REQUIRED)
include_directories(${GFLAGS_INCLUDE_DIR})
find_package(Glog REQUIRED)
include_directories(${GLOG_INCLUDE_DIRS})
find_package(folly CONFIG REQUIRED)
include_directories(${FOLLY_INCLUDE_DIR})
find_package(Boost REQUIRED COMPONENTS container filesystem regex)
include_directories(${Boost_INCLUDE_DIR})
find_package(fb303 CONFIG REQUIRED)
find_package(fizz CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(wangle CONFIG REQUIRED)
find_package(FBThrift CONFIG REQUIRED COMPONENTS)
find_package(range-v3 REQUIRED)
include_directories(${FB303_INCLUDE_DIR})


find_library(NLOHMANN_JSON nlohmann-json)
find_path(NLOHMANN_JSON_INCLUDE_DIRS NAMES nlohmann/json.hpp)
include_directories(${NLOHMANN_JSON_INCLUDE_DIRS})
find_package(Python3 COMPONENTS Interpreter Development)
include_directories(${Python3_INCLUDE_DIR})

# SAI_BRCM_PAI_IMPL SDK comes with SAI headers instead of using public SAI
if (SAI_BRCM_PAI_IMPL)
  set(SAI_INCLUDE_DIR "${SAI_BRCM_PAI_IMPL_INC_DIR}/sai/inc")
else()
  # An NPU SDK may also ship the SAI headers it was built against, as sai/inc/
  # beside its library, which run-getdeps.py stages as lib/. Prefer those.
  find_path(SAI_SDK_HEADERS_DIR NAMES lib/sai/inc/sai.h)
  if (SAI_SDK_HEADERS_DIR)
    set(SAI_INCLUDE_DIR "${SAI_SDK_HEADERS_DIR}/lib/sai/inc")
  else()
    find_path(SAI_INCLUDE_DIR NAMES sai.h)
  endif()
endif()
include_directories(${SAI_INCLUDE_DIR})
message(STATUS "Found SAI Include dir: ${SAI_INCLUDE_DIR}")

if (SAI_BRCM_IMPL)
  find_path(SAI_EXPERIMENTAL_INCLUDE_DIR NAMES brcm_sai_extensions.h)
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR})
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR}/experimental)
  message(STATUS "Found SAI Experimental dir: ${SAI_EXPERIMENTAL_INCLUDE_DIR}/experimental")
elseif(SAI_TAJO_IMPL)
  find_path(SAI_EXPERIMENTAL_INCLUDE_DIR NAMES experimental/sai_attr_ext.h)
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR})
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR}/experimental)
  message(STATUS "Found SAI Experimental dir: ${SAI_EXPERIMENTAL_INCLUDE_DIR}/experimental")
elseif(CHENAB_SAI_SDK)
  find_path(SAI_EXPERIMENTAL_INCLUDE_DIR NAMES experimental/saiportcustom.h)
  if (NOT SAI_EXPERIMENTAL_INCLUDE_DIR)
    message(FATAL_ERROR
      "The NPU SDK has no experimental/saiportcustom.h. Pass the SDK with "
      "run-getdeps.py --npu-libsai-impl-path and --npu-experiments-path, or "
      "--npu-libsai-impl-tarball.")
  endif()
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR})
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR}/experimental)
  message(STATUS "Found SAI Experimental dir: ${SAI_EXPERIMENTAL_INCLUDE_DIR}/experimental")
elseif(SAI_BRCM_PAI_IMPL)
  set(
    SAI_EXPERIMENTAL_INCLUDE_DIR
    "${SAI_BRCM_PAI_IMPL_INC_DIR}/sai/experimental"
  )
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR})
  message(STATUS "Found SAI Experimental dir: ${SAI_EXPERIMENTAL_INCLUDE_DIR}")
else()
  find_path(SAI_EXPERIMENTAL_INCLUDE_DIR
    NAMES
    experimental/saiswitchextensions.h)
  include_directories(${SAI_EXPERIMENTAL_INCLUDE_DIR}/experimental)
  message(
    STATUS
    "Found SAI Experimental dir: ${SAI_EXPERIMENTAL_INCLUDE_DIR}/experimental"
  )
endif()

find_library(LIBNL nl-3)
find_path(LIBNL_INCLUDE_DIR NAMES libnl3/netlink/socket.h)
include_directories(${LIBNL_INCLUDE_DIR})
include_directories(${LIBNL_INCLUDE_DIR}/libnl3)

find_library(IPROUTE2 netlink)
find_path(IPROUTE2_INCLUDE_DIR NAMES libnetlink.h)
include_directories(${IPROUTE2_INCLUDE_DIR})

find_library(YAML-CPP yaml-cpp)
find_path(YAML-CPP_INCLUDE_DIR NAMES yaml-cpp/yaml.h)
include_directories(${YAML-CPP_INCLUDE_DIR})
message(STATUS "Found YAML-CPP_INCLUDE_DIR: ${YAML-CPP_INCLUDE_DIR}")

find_library(YAML yaml)
find_path(YAML_INCLUDE_DIR NAMES yaml.h)
include_directories(${YAML_INCLUDE_DIR})
message(STATUS "Found YAML_INCLUDE_DIR: ${YAML_INCLUDE_DIR}")

find_package(python-pyyaml REQUIRED)

# FBOSS's packaged Python tools link the thrift-python runtime from this
# Python's site-packages, where the images that run them install the same
# fbthrift-python wheel.
file(TO_CMAKE_PATH "$ENV{CMAKE_PREFIX_PATH}" _fboss_prefixes)
foreach(_fboss_prefix IN LISTS CMAKE_PREFIX_PATH _fboss_prefixes)
  set(_fboss_wheel_dir "${_fboss_prefix}/share/thrift/wheels")
  file(GLOB _fboss_thrift_python_wheels "${_fboss_wheel_dir}/thrift-*.whl")
  if(_fboss_thrift_python_wheels)
    # Every build's wheel is version 0.0.1, so pip replaces an installed one
    # only when forced. pip also picks the wheel that matches this Python.
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" -m pip install --no-deps --no-index
        --force-reinstall --find-links "${_fboss_wheel_dir}" thrift
      RESULT_VARIABLE _fboss_pip_result
    )
    if(NOT _fboss_pip_result EQUAL 0)
      message(FATAL_ERROR
        "Can't install thrift-python from ${_fboss_wheel_dir} into "
        "${Python3_EXECUTABLE}")
    endif()
    break()
  endif()
endforeach()

# Generate thrift deps and libraries
include(FBThriftCppLibrary)
include(FBThriftPythonLibrary)

# Thrift libraries
add_fbthrift_cpp_library(
  fb303_cpp2
  common/fb303/if/fb303.thrift
  SERVICES
    FacebookService
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  fb303_python
  common/fb303/if/fb303.thrift
  SERVICES
    FacebookService
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  network_address_cpp2
  common/network/if/Address.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  network_address_python
  common/network/if/Address.thrift
  NAMESPACE "facebook.network"
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  mpls_cpp2
  fboss/agent/if/mpls.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  mpls_python
  fboss/agent/if/mpls.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  cfgr_fboss_common_cpp2
  configerator/structs/neteng/fboss/thrift/common.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  fboss_common_cpp2
  fboss/lib/if/fboss_common.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  fboss_common_python
  fboss/lib/if/fboss_common.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  switch_config_cpp2
  fboss/agent/switch_config.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    common_cpp2
    fboss_common_cpp2
    mpls_cpp2
)
add_fbthrift_python_library(
  switch_config_python
  fboss/agent/switch_config.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    common_python
    mpls_python
    fboss_common_python
)
add_fbthrift_cpp_library(
  agent_config_cpp2
  fboss/agent/agent_config.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    platform_config_cpp2
    switch_config_cpp2
)
add_fbthrift_cpp_library(
  agent_info_cpp2
  fboss/agent/agent_info.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  platform_config_cpp2
  fboss/agent/platform_config.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    bcm_config_cpp2
    asic_config_cpp2
    asic_config_v2_cpp2
    phy_cpp2
)
add_fbthrift_cpp_library(
  platform_descriptor_cpp2
  fboss/lib/platforms/platform_descriptor.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    fboss_common_cpp2
    switch_config_cpp2
)
add_fbthrift_python_library(
  platform_config_python
  fboss/agent/platform_config.thrift
  NAMESPACE "neteng.fboss.platform_config"
  OPTIONS
    json
  DEPENDS
    asic_config_v2_python
    asic_config_python
    bcm_config_python
    phy_python
    switch_config_python
    transceiver_python
)
add_fbthrift_cpp_library(
  switch_reachability_cpp2
  fboss/agent/switch_reachability.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  switch_state_cpp2
  fboss/agent/switch_state.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    cfgr_fboss_common_cpp2
    switch_config_cpp2
    phy_cpp2
    ctrl_cpp2
)
add_fbthrift_cpp_library(
  state_delta_log_cpp2
  fboss/agent/state_delta_log.thrift
  OPTIONS
    json
  DEPENDS
    fsdb_oper_cpp2
)
add_fbthrift_cpp_library(
  prbs_cpp2
  fboss/lib/phy/prbs.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  prbs_python
  fboss/lib/phy/prbs.thrift
  NAMESPACE "neteng.fboss.prbs"
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  link_cpp2
  fboss/lib/phy/link.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  link_python
  fboss/lib/phy/link.thrift
  NAMESPACE "neteng.fboss.link"
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  transceiver_cpp2
  fboss/qsfp_service/if/transceiver.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    link_cpp2
    prbs_cpp2
)
add_fbthrift_python_library(
  transceiver_python
  fboss/qsfp_service/if/transceiver.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    link_python
    prbs_python
)
add_fbthrift_cpp_library(
  transceiver_properties_cpp2
  fboss/qsfp_service/if/transceiver_properties.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    switch_config_cpp2
    transceiver_cpp2
)
add_fbthrift_cpp_library(
  pim_state_cpp2
  fboss/lib/if/pim_state.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  transceiver_validation_cpp2
  fboss/qsfp_service/if/transceiver_validation.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    transceiver_cpp2
    switch_config_cpp2
    fboss_common_cpp2
)
add_fbthrift_cpp_library(
  qsfp_config_cpp2
  fboss/qsfp_service/if/qsfp_service_config.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    transceiver_cpp2
    transceiver_validation_cpp2
    switch_config_cpp2
)
add_fbthrift_cpp_library(
  qsfp_state_cpp2
  fboss/qsfp_service/if/qsfp_state.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    qsfp_config_cpp2
    phy_cpp2
    transceiver_cpp2
    pim_state_cpp2
    port_state_cpp2
)
add_fbthrift_cpp_library(
  port_state_cpp2
  fboss/qsfp_service/if/port_state.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  qsfp_stats_cpp2
  fboss/qsfp_service/if/qsfp_stats.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    hardware_stats_cpp2
    phy_cpp2
    transceiver_cpp2
)
add_fbthrift_cpp_library(
  cmis_cpp2
  fboss/qsfp_service/module/cmis/cmis.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  sff_cpp2
  fboss/qsfp_service/module/sff/sff.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  sff8472_cpp2
  fboss/qsfp_service/module/sff/sff8472.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  sensor_service_stats_cpp2
  fboss/platform/sensor_service/sensor_service_stats.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    sensor_service_cpp2
)
add_fbthrift_cpp_library(
  te_srv6_agent_stats_cpp2
  fboss/fsdb/if/te_srv6_agent/te_srv6_agent_stats.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  phy_cpp2
  fboss/lib/phy/phy.thrift
  SERVICES
    FbossCommonPhyCtrl
  OPTIONS
    json
    reflection
  DEPENDS
    switch_config_cpp2
    transceiver_cpp2
    fboss_cpp2
    fb303_cpp2
    io_stats_cpp2
)
add_fbthrift_python_library(
  phy_python
  fboss/lib/phy/phy.thrift
  SERVICES
    FbossCommonPhyCtrl
  NAMESPACE "neteng.fboss.phy"
  OPTIONS
    json
  DEPENDS
    switch_config_python
    transceiver_python
    fb303_python
    fboss_python
    prbs_python
    io_stats_python
)
add_fbthrift_cpp_library(
  optic_cpp2
  fboss/agent/if/optic.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    transceiver_cpp2
)
add_fbthrift_cpp_library(
  fboss_cpp2
  fboss/agent/if/fboss.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  fboss_python
  fboss/agent/if/fboss.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  sflow_cpp2
  fboss/agent/if/sflow.thrift
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  common_cpp2
  fboss/agent/if/common.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    mpls_cpp2
    network_address_cpp2
)
add_fbthrift_python_library(
  common_python
  fboss/agent/if/common.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    mpls_python
    network_address_python
)
add_fbthrift_cpp_library(
  product_info_cpp2
  fboss/agent/if/product_info.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  hardware_stats_cpp2
  fboss/agent/hw/hardware_stats.thrift
  OPTIONS
    json
    visitation
    reflection
  DEPENDS
    mka_structs_cpp2
)
add_fbthrift_cpp_library(
  ctrl_cpp2
  fboss/agent/if/ctrl.thrift
  SERVICES
    FbossCtrl
    NeighborListenerClient
  OPTIONS
    json
    reflection
  DEPENDS
    agent_stats_cpp2
    cfgr_fboss_common_cpp2
    fboss_cpp2
    fb303_cpp2
    mpls_cpp2
    network_address_cpp2
    optic_cpp2
    platform_config_cpp2
    hardware_stats_cpp2
    product_info_cpp2
    transceiver_cpp2
    common_cpp2
    phy_cpp2
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  packet_stream_cpp2
  fboss/agent/if/packet_stream.thrift
  SERVICES
    PacketStream
  DEPENDS
    fb303_cpp2
)
add_fbthrift_cpp_library(
    bcm_config_cpp2
  fboss/agent/hw/bcm/bcm_config.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  bcm_config_python
  fboss/agent/hw/bcm/bcm_config.thrift
  NAMESPACE "neteng.fboss.bcm_config"
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  asic_config_cpp2
  fboss/agent/hw/sai/config/asic_config.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  asic_config_python
  fboss/agent/hw/sai/config/asic_config.thrift
  NAMESPACE "neteng.fboss.asic_config"
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  asic_config_v2_cpp2
  fboss/agent/hw/config/asic_config_v2.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  asic_config_v2_python
  fboss/agent/hw/config/asic_config_v2.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  mka_structs_cpp2
  fboss/mka_service/if/mka_structs.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  highfreq_cpp2
  fboss/agent/if/highfreq.thrift
  OPTIONS
    json
)
add_fbthrift_cpp_library(
  asic_temp_cpp2
  fboss/agent/if/asic_temp.thrift
  SERVICES
    AsicTempThrift
  OPTIONS
    json
    reflection
  DEPENDS
    fboss_cpp2
)
add_fbthrift_cpp_library(
  agent_stats_cpp2
  fboss/agent/agent_stats.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    asic_temp_cpp2
    hardware_stats_cpp2
    phy_cpp2
)
add_fbthrift_cpp_library(
  production_features_cpp2
  fboss/agent/test/production_features.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  qsfp_production_features_cpp2
  fboss/qsfp_service/test/hw_test/qsfp_production_features.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  link_test_production_features_cpp2
  fboss/agent/test/link_tests/link_test_production_features.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_cpp_library(
  link_qsfp_test_port_info_cpp2
  fboss/lib/if/link_qsfp_test_port_info.thrift
  OPTIONS
    json
  DEPENDS
    phy_cpp2
    transceiver_cpp2
)

add_library(link_qsfp_test_port_info_utils
  fboss/lib/if/LinkQsfpTestPortInfoUtils.cpp
  fboss/lib/if/oss/LinkQsfpTestMachineInfo.cpp
)

target_link_libraries(link_qsfp_test_port_info_utils
  link_qsfp_test_port_info_cpp2
  transceiver_cpp2
)
add_fbthrift_cpp_library(
  i2c_controller_stats_cpp2
  fboss/lib/i2c/i2c_controller_stats.thrift
)


add_fbthrift_cpp_library(
  bgp_summary_cpp2
  fboss/cli/fboss2/commands/show/bgp/summary/bgp_summary.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    bgp_thrift_cpp2
)

add_fbthrift_cpp_library(
  bgp_table_summary_cpp2
  fboss/cli/fboss2/commands/show/bgp/table/bgp_table_summary.thrift
  OPTIONS
    json
  DEPENDS
    bgp_thrift_cpp2
)

add_fbthrift_cpp_library(
  bgp_update_group_cpp2
  fboss/cli/fboss2/commands/show/bgp/updategroup/bgp_update_group.thrift
  OPTIONS
    json
  DEPENDS
    bgp_thrift_cpp2
)

add_fbthrift_cpp_library(
  io_stats_cpp2
  fboss/lib/if/io_stats.thrift
  OPTIONS
    json
    reflection
)
add_fbthrift_python_library(
  io_stats_python
  fboss/lib/if/io_stats.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
)

add_fbthrift_cpp_library(
  led_mapping_cpp2
  fboss/lib/led/led_mapping.thrift
  OPTIONS
    json
    reflection
)

add_fbthrift_cpp_library(
  bsp_platform_mapping_cpp2
  fboss/lib/bsp/bsp_platform_mapping.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    led_mapping_cpp2
)

add_fbthrift_cpp_library(
  led_config_cpp2
  fboss/led_service/if/led_service_config.thrift
  OPTIONS
    json
    reflection
)

add_fbthrift_cpp_library(
  qsfp_cpp2
  fboss/qsfp_service/if/qsfp.thrift
  SERVICES
    QsfpService
  OPTIONS
    json
  DEPENDS
    fb303_cpp2
    ctrl_cpp2
    fboss_cpp2
    transceiver_cpp2
    switch_config_cpp2
    mpls_cpp2
    mka_structs_cpp2
    hardware_stats_cpp2
)

add_fbthrift_cpp_library(
  hw_ctrl_cpp2
  fboss/agent/if/hw_ctrl.thrift
  SERVICES
    FbossHwCtrl
  DEPENDS
    ctrl_cpp2
    highfreq_cpp2
    switch_state_cpp2
)

add_fbthrift_cpp_library(
  sai_ctrl_cpp2
  fboss/agent/hw/sai/switch/sai_ctrl.thrift
  SERVICES
    SaiCtrl
  DEPENDS
    ctrl_cpp2
    hw_ctrl_cpp2
)

add_fbthrift_cpp_library(
  hw_test_ctrl_cpp2
  fboss/agent/hw/hw_test_ctrl.thrift
  SERVICES
    HwTestCtrl
  DEPENDS
    ctrl_cpp2
)

add_fbthrift_cpp_library(
  sai_test_ctrl_cpp2
  fboss/agent/hw/sai/hw_test/sai_test_ctrl.thrift
  SERVICES
    SaiTestCtrl
  DEPENDS
    ctrl_cpp2
    hw_test_ctrl_cpp2
)

add_fbthrift_cpp_library(
  multiswitch_ctrl_cpp2
  fboss/agent/if/multiswitch_ctrl.thrift
  SERVICES
    MultiSwitchCtrl
  DEPENDS
    common_cpp2
    ctrl_cpp2
    fsdb_oper_cpp2
    hardware_stats_cpp2
    phy_cpp2
)

add_fbthrift_cpp_library(
  agent_hw_test_ctrl_cpp2
  fboss/agent/if/agent_hw_test_ctrl.thrift
  SERVICES
    AgentHwTestCtrl
  OPTIONS
    json
    reflection
  DEPENDS
    common_cpp2
    ctrl_cpp2
    fboss_cpp2
    switch_state_cpp2
    network_address_cpp2
    mpls_cpp2
    phy_cpp2
    prbs_cpp2
  OPTIONS
    json
)

add_fbthrift_python_library(
  platform_mapping_config_python
  fboss/lib/platform_mapping_v2/platform_mapping_config.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
)

add_fbthrift_python_library(
  mka_structs_python
  fboss/mka_service/if/mka_structs.thrift
  NAMESPACE "neteng.fboss.mka"
  OPTIONS
    json
)
add_fbthrift_python_library(
  hardware_stats_python
  fboss/agent/hw/hardware_stats.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    mka_structs_python
)
add_fbthrift_python_library(
  asic_temp_python
  fboss/agent/if/asic_temp.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    fboss_python
)
add_fbthrift_python_library(
  agent_stats_python
  fboss/agent/agent_stats.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    hardware_stats_python
    switch_config_python
    phy_python
    asic_temp_python
)
add_fbthrift_python_library(
  product_info_python
  fboss/agent/if/product_info.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
)
add_fbthrift_python_library(
  cfgr_fboss_common_python
  configerator/structs/neteng/fboss/thrift/common.thrift
  NAMESPACE "neteng.fboss.configerator"
  OPTIONS
    json
)
add_fbthrift_python_library(
  ctrl_python
  fboss/agent/if/ctrl.thrift
  SERVICES
    FbossCtrl
    NeighborListenerClient
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    fboss_python
    fb303_python
    network_address_python
    agent_stats_python
    mpls_python
    cfgr_fboss_common_python
    common_python
    product_info_python
    transceiver_python
    switch_config_python
    platform_config_python
    phy_python
    prbs_python
    hardware_stats_python
)
add_fbthrift_python_library(
  highfreq_python
  fboss/agent/if/highfreq.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
)
add_fbthrift_python_library(
  switch_state_python
  fboss/agent/switch_state.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    switch_config_python
    phy_python
    cfgr_fboss_common_python
    common_python
    transceiver_python
    network_address_python
    ctrl_python
    mka_structs_python
)
add_fbthrift_python_library(
  hw_ctrl_python
  fboss/agent/if/hw_ctrl.thrift
  SERVICES
    FbossHwCtrl
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    hardware_stats_python
    common_python
    ctrl_python
    fboss_python
    highfreq_python
    switch_state_python
    phy_python
    prbs_python
)
add_fbthrift_python_library(
  qsfp_python
  fboss/qsfp_service/if/qsfp.thrift
  SERVICES
    QsfpService
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    ctrl_python
    common_python
    fboss_python
    transceiver_python
    switch_config_python
    mka_structs_python
    hardware_stats_python
    phy_python
    prbs_python
)
add_fbthrift_python_library(
  fsdb_common_python
  fboss/fsdb/if/fsdb_common.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
)
add_fbthrift_python_library(
  patch_python
  fboss/thrift_cow/patch.thrift
  NAMESPACE "neteng.fboss.thrift_cow"
  OPTIONS
    json
)
add_fbthrift_python_library(
  fsdb_oper_python
  fboss/fsdb/if/fsdb_oper.thrift
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    fsdb_common_python
    patch_python
)
add_fbthrift_python_library(
  fsdb_python
  fboss/fsdb/if/fsdb.thrift
  SERVICES
    FsdbService
  NAMESPACE "neteng.fboss"
  OPTIONS
    json
  DEPENDS
    fb303_python
    fsdb_common_python
    fsdb_oper_python
)

add_fbthrift_cpp_library(
  hal_test_config_cpp2
  fboss/qsfp_service/test/hal_test/hal_test_config.thrift
  OPTIONS
    json
  DEPENDS
    qsfp_config_cpp2
    transceiver_cpp2
)

find_library(USB usb-1.0)
find_path(USB_INCLUDE_DIR NAMES libusb-1.0/libusb.h)
include_directories(${USB_INCLUDE_DIR})

find_library(NETLINK3 nl-3)
find_library(NETLINKROUTE3 nl-route-3 libnl-route-3.a nl-route-3)

find_library(CURL curl)
find_path(CURL_INCLUDE_DIR NAMES curl/curl.h)
include_directories(${CURL_INCLUDE_DIR})

find_library(MNL mnl)
find_library(SODIUM sodium)

find_library(RE2 re2)
find_path(RE2_INCLUDE_DIR NAMES re2/re2.h)
include_directories(${RE2_INCLUDE_DIR})

find_library(EXPRTK exprtk)
find_path(EXPRTK_INCLUDE_DIR NAMES exprtk.hpp)
include_directories(${EXPRTK_INCLUDE_DIR})

find_library(LIBGPIOD gpiod)
find_path(LIBGPIOD_INCLUDE_DIR NAMES gpiod.h)
include_directories(${LIBGPIOD_INCLUDE_DIR})

find_library(SYSTEMD systemd)
find_path(SYSTEMD_INCLUDE_DIR NAMES systemd/sd-daemon.h)
include_directories(${SYSTEMD_INCLUDE_DIR})

find_package(tabulate CONFIG REQUIRED)

# Unit Testing
add_definitions (-DIS_OSS=true)

# Strip build directory prefixes from __FILE__ so folly's LoggerDB resolves
# log categories like "fboss=DBG2" correctly in OSS builds. Without this,
# __FILE__ is an absolute path and the "fboss" category is never matched.
set_property(
  DIRECTORY
  ${CMAKE_CURRENT_SOURCE_DIR}
  APPEND
  PROPERTY
  COMPILE_DEFINITIONS
  "FOLLY_XLOG_STRIP_PREFIXES=\"${CMAKE_SOURCE_DIR}:${CMAKE_BINARY_DIR}\""
)
find_package(Threads REQUIRED)
enable_testing()

# Shipit maps fbcode/neteng/fboss/bgp/public_tld/configerator/structs/neteng/
# to configerator/structs/neteng/ in the OSS repo, but thrift includes and
# CMake source paths reference the original internal path under public_tld.
# Create a symlink to bridge the two so both paths resolve to the same files.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/neteng/fboss/bgp/public_tld/configerator/structs/neteng")
  file(MAKE_DIRECTORY
    "${CMAKE_SOURCE_DIR}/neteng/fboss/bgp/public_tld/configerator/structs")
  file(CREATE_LINK
    "${CMAKE_SOURCE_DIR}/configerator/structs/neteng"
    "${CMAKE_SOURCE_DIR}/neteng/fboss/bgp/public_tld/configerator/structs/neteng"
    SYMBOLIC)
endif()

# Deferred to end-of-directory: targets are still created after this file's last
# line (FbossImageDistroCliTests.cmake, and the SAI groups under
# BUILD_SAI_FAKE), and anything created later would escape the sweep.
function(fboss_prune_to_profile_targets)
  # Only the exported targets need to be in `all`; ninja pulls their transitive
  # dependencies in anyway, because EXCLUDE_FROM_ALL does not stop a target
  # being built when something still in `all` depends on it. That covers every
  # dependency edge but not `add_custom_target(... ALL ...)`, whose only tie to
  # `all` is the keyword this sweep overrides.
  get_property(_all DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
               PROPERTY BUILDSYSTEM_TARGETS)
  set(_dropped 0)
  set(_found 0)
  foreach(_t IN LISTS _all)
    get_target_property(_type ${_t} TYPE)
    if(_t IN_LIST FBOSS_PROFILE_TARGETS)
      math(EXPR _found "${_found} + 1")
    elseif(NOT _type STREQUAL "INTERFACE_LIBRARY")
      set_target_properties(${_t} PROPERTIES EXCLUDE_FROM_ALL TRUE)
      math(EXPR _dropped "${_dropped} + 1")
    endif()
  endforeach()
  message(STATUS "FBOSS profile ${FBOSS_BUILD_PROFILE}: kept ${_found} "
    "exported targets, excluded ${_dropped}")
endfunction()

if (NOT FBOSS_BUILD_PROFILE STREQUAL "full")
  # The pruned path needs cmake_language(DEFER) (3.19); the file's declared
  # 3.8 minimum only covers the full build.
  if (CMAKE_VERSION VERSION_LESS 3.19)
    message(FATAL_ERROR
      "FBOSS_BUILD_PROFILE ${FBOSS_BUILD_PROFILE} needs CMake >= 3.19, "
      "found ${CMAKE_VERSION}; build the default 'full' profile instead")
  endif()
  cmake_language(DEFER CALL fboss_prune_to_profile_targets)
endif()

# Minimal set of existing FBOSS libraries consumed by standalone projects.
# Include this list in the full exported set so the two sets cannot drift.
set(FBOSS_MINIMAL_LIBRARY_TARGETS
  nodebase
  radix_tree
  exponential_back_off
  log_thrift_call
  alert_logger
)

set(FBOSS_EXPORTED_TARGETS
  ${FBOSS_MINIMAL_LIBRARY_TARGETS}
  address_utils
  switch_config_cpp2 ctrl_cpp2 common_cpp2 fboss_cpp2
  fboss_common_cpp2 fboss_error fboss_types
  network_address_cpp2 mpls_cpp2
  agent_stats_cpp2 hardware_stats_cpp2 mka_structs_cpp2
  platform_config_cpp2 phy_cpp2 transceiver_cpp2
  product_info_cpp2 optic_cpp2 prbs_cpp2 link_cpp2
  cfgr_fboss_common_cpp2 asic_temp_cpp2
  bcm_config_cpp2 asic_config_cpp2 asic_config_v2_cpp2
  io_stats_cpp2
  # FSDB client libraries for downstream consumers (BGP++ FsdbSyncer,
  # NeighborWatcher). Dependents before dependencies for one-pass linkers.
  fsdb_syncer fsdb_pub_sub fsdb_sub_mgr fsdb_stream_client
  fsdb_flags fsdb_utils
  fsdb_model_cpp2 thriftpath_lib
  fsdb_common_cpp2 fsdb_oper_cpp2 fsdb_cpp2
  cow_storage_mgr oper_path_helpers
  common_thrift_utils thrift_service_client
  # COW subscription instantiations (FsdbCowStateSubManager)
  fsdb_cow_state_sub_mgr fsdb_cow_storage fsdb_cow_root
  # FSDB model transitive thrift deps (agent state/config/info types)
  switch_state_cpp2 agent_config_cpp2 agent_info_cpp2
  switch_reachability_cpp2
  qsfp_state_cpp2 qsfp_stats_cpp2 sensor_service_stats_cpp2
  te_srv6_agent_stats_cpp2
  # Leaf thrift deps of the FsdbOperStateRoot COW model, transitively
  # required by downstream consumers that link it (BGP++ NeighborWatcher
  # FsdbCowStateSubManager). Headers ship via the include dir, but the
  # static archives must be installed for one-pass linkers to resolve.
  qsfp_config_cpp2 port_state_cpp2 pim_state_cpp2 sensor_service_cpp2
  # thrift_cow C++ libraries (CowStorage / visitors / patch appliers) that
  # back the COW storage instantiations above.
  patch_cpp2 cow_visitor_results_cpp2
  storage cow_storage
  thrift_cow_serializer thrift_cow_nodes
  thrift_cow_visitors_common thrift_cow_visitors
  # FsdbOperStateRoot extern-template instantiation libs. fsdb_cow_storage
  # references these for the PathVisitor (visitNode), PatchApplier, and
  # ThriftStructNode instantiations over the oper-state tree; without them a
  # one-pass link leaves thousands of undefined visitNode/PatchApplier refs.
  fsdb_cow_root_path_visitor
  fsdb_path_visitor_oper_state_instantiations
  fsdb_patch_applier_oper_state_instantiations
  fsdb_thrift_struct_oper_instantiations
  # Additional leaf thrift deps reached through the qsfp_state / sensor /
  # service-client portions of the model: transceiver_validation
  # (FirmwarePair/TransceiverAttributes/VendorConfig), sensor_config
  # (Thresholds), reboot_cause_config (RebootCauseRecord), and qsfp
  # (QsfpService client vtable via thrift_service_client).
  transceiver_validation_cpp2
  sensor_config_cpp2
  reboot_cause_config_cpp2
  qsfp_cpp2
)

if(FBOSS_BUILD_PROFILE STREQUAL "exported_libraries")
  set(FBOSS_PROFILE_TARGETS ${FBOSS_MINIMAL_LIBRARY_TARGETS})
else()
  set(FBOSS_PROFILE_TARGETS ${FBOSS_EXPORTED_TARGETS})
endif()

# Build all cmake files under cmake/*cmake
file(GLOB_RECURSE cmakeFiles ${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake)
foreach(cmakeFile ${cmakeFiles})
  message(status " INCLUDE ${cmakeFile}")
  INCLUDE(${cmakeFile})
endforeach(cmakeFile)

add_custom_target(fboss_exported_libraries)
add_dependencies(fboss_exported_libraries
  ${FBOSS_MINIMAL_LIBRARY_TARGETS}
)

if (NOT SKIP_ALL_INSTALL AND NOT FBOSS_BUILD_PROFILE STREQUAL "full")
  set(FBOSS_INSTALL_ENABLED TRUE)
endif()

# Install headers and libraries for downstream consumers (e.g., BGP++).
# Both blocks live here, after the install() override and after
# FBOSS_INSTALL_ENABLED is flipped, so SKIP_ALL_INSTALL and a pruned profile
# both apply to them consistently.
#
# Header sources are installed per-subdirectory rather than via
# install(DIRECTORY ${CMAKE_SOURCE_DIR}/fboss/ ...) because
# ${CMAKE_SOURCE_DIR}/fboss is a symlink (github/fboss -> ..) and CMake
# materialises that symlink at the destination, causing the subsequent
# install of ${CMAKE_BINARY_DIR}/fboss/ to fail with "File exists".
if(FBOSS_BUILD_PROFILE STREQUAL "exported_libraries")
  install(
    FILES ${CMAKE_SOURCE_DIR}/fboss/agent/StrongTypes.h
    DESTINATION include/fboss/agent
  )
  install(
    FILES
      ${CMAKE_SOURCE_DIR}/fboss/agent/state/NodeBase.h
      ${CMAKE_SOURCE_DIR}/fboss/agent/state/NodeBase-defs.h
    DESTINATION include/fboss/agent/state
  )
  install(
    FILES
      ${CMAKE_SOURCE_DIR}/fboss/lib/AlertLogger.h
      ${CMAKE_SOURCE_DIR}/fboss/lib/ExponentialBackoff.h
      ${CMAKE_SOURCE_DIR}/fboss/lib/LogThriftCall.h
      ${CMAKE_SOURCE_DIR}/fboss/lib/RadixTree.h
      ${CMAKE_SOURCE_DIR}/fboss/lib/RadixTree-inl.h
      ${CMAKE_SOURCE_DIR}/fboss/lib/ThriftCallDurationLogger.h
    DESTINATION include/fboss/lib
  )
else()
  foreach(_fboss_header_subdir
    agent cli common fsdb led_service lib mdio mka_service platform
    qsfp_service thrift_cow thrift_visitors thriftpath_plugin)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/fboss/${_fboss_header_subdir}
      DESTINATION include/fboss
      FILES_MATCHING PATTERN "*.h" PATTERN "*.tcc")
  endforeach()
  install(DIRECTORY ${CMAKE_BINARY_DIR}/fboss/
    DESTINATION include/fboss
    FILES_MATCHING PATTERN "*.h" PATTERN "*.tcc")
  install(DIRECTORY ${CMAKE_BINARY_DIR}/configerator/
    DESTINATION include/configerator
    FILES_MATCHING PATTERN "*.h" PATTERN "*.tcc")
endif()

install(TARGETS ${FBOSS_PROFILE_TARGETS}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)

add_custom_target(fboss_agent_benchmark_libs)
add_dependencies(fboss_agent_benchmark_libs
  hw_flowlet_stats_collection_speed
  hw_rx_slow_path_arp_rate
  hw_bgp_rx_slow_path_rate
  hw_init_and_exit_benchmark_helper
  hw_system_scale_memory_benchmark
)
add_custom_target(fboss_platform_services)
add_dependencies(fboss_platform_services
  rma-showtech
  # Services
  sensor_service
  fan_service
  rackmon
  platform_manager
  fw_util
  weutil
  data_corral_service
  reboot_cause_finder
  watchdog_util
  # Utilities
  sensor_service_client
  fixmyfboss
  # Unit Tests
  platform_config_lib_config_lib_test
  reboot_cause_finder_impl_test
  platform_manager_device_path_resolver_test
  platform_manager_platform_explorer_test
  platform_data_corral_sw_test
  platform_manager_utils_test
  platform_manager_pm_unit_identity_test
  platform_helpers_platform_name_lib_test
  platform_helpers_platform_fs_utils_test
  platform_helpers_platform_utils_test
  platform_checks_local_host_test
  platform_checks_remote_host_test
  platform_checks_platform_name_test
  platform_helpers_expect_session_test
  platform_checks_management_plane_checks_test
  platform_checks_bmc_checks_test
  platform_checks_sol_console_check_test
  platform_checks_power_cycle_checks_test
  platform_checks_hw_test_checks_test
  platform_manager_data_store_test
  platform_manager_config_validator_test
  platform_manager_presence_checker_test
  platform_manager_i2c_explorer_test
  platform_manager_handler_test
  weutil_crc16_ccitt_test
  weutil_fboss_eeprom_interface_test
  weutil_parser_utils_test
  sensor_service_sw_test
  sensor_service_utils_test
  rackmon_test
  fan_service_sw_test
  data_corral_service_hw_test
  fan_service_hw_test
  fan_service_integration_test
  sensor_service_hw_test
  weutil_hw_test
  platform_hw_test
  platform_checks_bmc_reachable_check_test
  platform_manager_hw_test
  cross_config_validator_test
  fw_util_sw_test
  fw_util_hw_test
  fixmyfboss_result_printer_test
  fixmyfboss_check_runner_test
  fixmyfboss_check_environment_test
  fixmyfboss_cli_options_test
)

add_custom_target(qsfp_targets)
add_dependencies(qsfp_targets
  qsfp_service
  qsfp_hw_test
  qsfp_hal_test
  wedge_qsfp_util
  cmis_test
  transceiver_manager_test
  transceiver_state_machine_test
)

add_custom_target(led_targets)
add_dependencies(led_targets
  led_service
  led_service_hw_test
)

add_custom_target(fboss2_targets)
add_dependencies(fboss2_targets
  config_generation_spec_utils_test
  fboss2
  fboss2_cmd_test
  fboss2_cmd_config_test
  fboss2_framework_test
  fboss2-dev
  fboss2_integration_test
  feature_default_command_args_config_test
)

if (SAI_IMPL OR BUILD_SAI_FAKE)
  add_custom_target(fboss_forwarding_stack)
  # Common forwarding services
  add_dependencies(fboss_forwarding_stack
    diag_shell_client
    fboss2
    fboss2-dev
    fsdb
    qsfp_service
    wedge_qsfp_util
  )

  if (BUILD_SAI_FAKE)
    add_dependencies(fboss_forwarding_stack
      fboss_fake_agent_targets
    )
  else()
    add_dependencies(fboss_forwarding_stack
      fboss_hw_agent-sai_impl
      fboss_sw_agent
      wedge_agent-sai_impl
    )
  endif()
endif()

add_custom_target(fboss_other_services)
add_dependencies(fboss_other_services
  qsfp_targets
  led_targets
  fboss2_targets
  fsdb_all_services
)

add_custom_target(fboss_platform_mapping_gen)
add_dependencies(fboss_platform_mapping_gen
  fboss-platform-mapping-gen.GEN_PY_EXE
  platform_mapping_gen_unit_test.GEN_PY_EXE
  platform_mapping_gen_no_regression_test.GEN_PY_EXE
)

if (BUILD_SAI_FAKE)
add_custom_target(fboss_agent_thrift_libs)
add_dependencies(fboss_agent_thrift_libs
  mpls_cpp2
  switch_config_cpp2
  agent_config_cpp2
  platform_config_cpp2
  switch_reachability_cpp2
  switch_state_cpp2
  optic_cpp2
  fboss_cpp2
  sflow_cpp2
  common_cpp2
  product_info_cpp2
  hardware_stats_cpp2
  ctrl_cpp2
  packet_stream_cpp2
  bcm_config_cpp2
  asic_config_cpp2
  asic_config_v2_cpp2
  agent_stats_cpp2
  production_features_cpp2
  link_test_production_features_cpp2
  hw_ctrl_cpp2
  sai_ctrl_cpp2
  hw_test_ctrl_cpp2
  sai_test_ctrl_cpp2
  multiswitch_ctrl_cpp2
  agent_hw_test_ctrl_cpp2
  lldp_structs_cpp2
  validated_shell_commands_cpp2
)

add_custom_target(fboss_fake_agent_targets)
add_dependencies(fboss_fake_agent_targets
  fboss_agent_thrift_libs
  fboss_sw_agent
  sai_replayer-fake
  wedge_agent-sai_impl
  fboss_hw_agent-sai_impl
)

add_custom_target(fboss_fake_agent_test_targets)
add_dependencies(fboss_fake_agent_test_targets
  fboss_agent_thrift_libs
  sai_test-fake
  sai_agent_hw_test-fake
  multi_switch_agent_hw_test
  sai_agent_scale_test-fake
  sai_invariant_agent_test-fake
  switch_test
)

if(BUILD_SAI_FAKE_BENCHMARKS)
add_custom_target(fboss_fake_agent_benchmarks)

# SAI_BENCHMARKS defined in cmake/AgentHwSaiBenchmarks.cmake
add_dependencies(fboss_fake_agent_benchmarks
  sai_all_benchmarks-fake
  sai_multi_switch_all_benchmarks-fake
)
endif()

if (BUILD_SAI_FAKE_LINK_TEST)
add_dependencies(fboss_fake_agent_test_targets
  sai_mono_link_test-fake
  sai_multi_link_test-fake
)
endif()

endif()

if (GITHUB_ACTIONS_BUILD)
  add_custom_target(github_actions_fboss_other_services)
  add_dependencies(github_actions_fboss_other_services
    # Services
    fboss2
    fsdb_all_services
  )
endif()

# FBOSS Image Builder distro_cli tests
include(cmake/FbossImageDistroCliTests.cmake)

# Match fbcode's default allocator for non-sanitized binaries. Apply this after
# all target definitions so only final executables, rather than every shared
# library, acquire the allocator dependency. The generated object defines
# jemalloc startup defaults supported by distro builds; MALLOC_CONF can still
# override individual options at process launch.
if (NOT WITH_ASAN)
  find_library(FBOSS_JEMALLOC_LIBRARY NAMES jemalloc)
  if (NOT FBOSS_JEMALLOC_LIBRARY)
    message(FATAL_ERROR "jemalloc is required for non-ASAN FBOSS builds")
  endif()

  add_library(fboss_jemalloc_config OBJECT
    fboss/oss/JemallocConfig.cpp
  )

  get_property(FBOSS_BUILD_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(FBOSS_BUILD_TARGET ${FBOSS_BUILD_TARGETS})
    get_target_property(FBOSS_BUILD_TARGET_TYPE ${FBOSS_BUILD_TARGET} TYPE)
    if (FBOSS_BUILD_TARGET_TYPE STREQUAL "EXECUTABLE")
      target_sources(
        ${FBOSS_BUILD_TARGET}
        PRIVATE $<TARGET_OBJECTS:fboss_jemalloc_config>
      )
      target_link_libraries(
        ${FBOSS_BUILD_TARGET}
        ${FBOSS_JEMALLOC_LIBRARY}
      )
    endif()
  endforeach()
endif()
