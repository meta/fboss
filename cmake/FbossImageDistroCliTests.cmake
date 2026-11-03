# CMake to build and test fboss-image/distro_cli

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

# Prevent multiple inclusions
include_guard(GLOBAL)

# distro_cli requires Python 3.10+ with widespread use of union type syntax
# Save and temporarily override Python3_EXECUTABLE
if(DEFINED Python3_EXECUTABLE)
  set(SAVED_Python3_EXECUTABLE "${Python3_EXECUTABLE}")
endif()

# Find the highest available Python version >= 3.10
find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)
message(STATUS "Using Python ${Python3_VERSION} (${Python3_EXECUTABLE}) for distro_cli tests")

include(FBPythonBinary)
add_fb_python_library(
  fboss_component_version
  SOURCES
    fboss/oss/scripts/component_version.py
)

file(GLOB DISTRO_CLI_TEST_SOURCES
  "fboss-image/distro_cli/tests/*_test.py"
)

# Exclude image_builder_test.py - requires source tree access for templates
# which isn't available in CMake build directory
list(FILTER DISTRO_CLI_TEST_SOURCES EXCLUDE REGEX "image_builder_test\\.py$")

# Exclude: manual e2e tests
list(FILTER DISTRO_CLI_TEST_SOURCES EXCLUDE REGEX "kernel_build_test\\.py$")
list(FILTER DISTRO_CLI_TEST_SOURCES EXCLUDE REGEX "sai_build_test\\.py$")

# Exclude: Docker not available in standard builds
list(FILTER DISTRO_CLI_TEST_SOURCES EXCLUDE REGEX "build_entrypoint_test\\.py$")
list(FILTER DISTRO_CLI_TEST_SOURCES EXCLUDE REGEX "build_test\\.py$")
list(FILTER DISTRO_CLI_TEST_SOURCES EXCLUDE REGEX "docker_test\\.py$")

# Separate integration tests (require Docker) from unit tests
file(GLOB DISTRO_CLI_INTEGRATION_TEST_SOURCES
  "fboss-image/distro_cli/tests/device_integration_test.py"
)

file(GLOB DISTRO_CLI_TEST_HELPERS
  "fboss-image/distro_cli/tests/test_helpers.py"
)

file(GLOB_RECURSE DISTRO_CLI_LIB_SOURCES
  "fboss-image/distro_cli/builder/*.py"
  "fboss-image/distro_cli/cmds/*.py"
  "fboss-image/distro_cli/lib/*.py"
  "fboss-image/distro_cli/tools/*.py"
)

# Create Python unittest executable for unit tests
# Use TYPE "dir" to create a directory-based executable instead of zipapp.
# This allows tests to access data files via Path(__file__).parent, which
# doesn't work inside zip archives.
#
# Note: Only include .py files in SOURCES. Non-Python data files would be
# treated as Python modules during test discovery, causing import errors.
# Data files are copied via add_custom_command below after the build
# generates the directory structure.
add_fb_python_unittest(
  distro_cli_tests
  BASE_DIR "fboss-image"
  TYPE "dir"
  SOURCES
    ${DISTRO_CLI_TEST_SOURCES}
    ${DISTRO_CLI_TEST_HELPERS}
    ${DISTRO_CLI_LIB_SOURCES}
  DEPENDS
    fboss_component_version
  ENV
    "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/fboss-image"
  PROPERTIES
    LABELS "unit"
)

# Copy test data files AFTER the build generates the directory structure
# Tests access these via Path(__file__).parent / "data" / "filename"
# With TYPE "dir", the executable is created at distro_cli_tests/ so data
# files need to be inside that directory structure.
#
# We use add_custom_command to copy AFTER the .GEN_PY_EXE target runs,
# because that target creates/recreates the distro_cli_tests/ directory.
set(DATA_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/distro_cli_tests/distro_cli/tests")
set(DATA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fboss-image/distro_cli/tests/data")

add_custom_command(
  TARGET distro_cli_tests.GEN_PY_EXE
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${DATA_SOURCE_DIR}"
          "${DATA_DEST_DIR}/data"
  COMMENT "Copying test data files for distro_cli_tests"
)

# Copy scripts directory used in unit tests
set(SCRIPTS_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/distro_cli_tests/distro_cli/scripts")
set(SCRIPTS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fboss-image/distro_cli/scripts")

add_custom_command(
  TARGET distro_cli_tests.GEN_PY_EXE
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${SCRIPTS_SOURCE_DIR}"
          "${SCRIPTS_DEST_DIR}"
  COMMENT "Copying scripts for distro_cli_tests"
)

install_fb_python_executable(distro_cli_tests)

# Create Python unittest executable for integration tests (require Docker)
# These tests are labeled "integration" and "docker" so they can be run separately
# Run with: ctest -L integration
if(DISTRO_CLI_INTEGRATION_TEST_SOURCES)
  add_fb_python_unittest(
    distro_cli_integration_tests
    BASE_DIR "fboss-image"
    TYPE "dir"
    SOURCES
      ${DISTRO_CLI_INTEGRATION_TEST_SOURCES}
      ${DISTRO_CLI_TEST_HELPERS}
      ${DISTRO_CLI_LIB_SOURCES}
    ENV
      "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/fboss-image"
    PROPERTIES
      LABELS "integration;docker"
  )

  # Copy test data files for integration tests
  set(INTEGRATION_DATA_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/distro_cli_integration_tests/distro_cli/tests")
  add_custom_command(
    TARGET distro_cli_integration_tests.GEN_PY_EXE
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DATA_SOURCE_DIR}"
            "${INTEGRATION_DATA_DEST_DIR}/data"
    COMMENT "Copying test data files for distro_cli_integration_tests"
  )

  # Copy scripts directory for integration tests
  set(INTEGRATION_SCRIPTS_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/distro_cli_integration_tests/distro_cli/scripts")
  add_custom_command(
    TARGET distro_cli_integration_tests.GEN_PY_EXE
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SCRIPTS_SOURCE_DIR}"
            "${INTEGRATION_SCRIPTS_DEST_DIR}"
    COMMENT "Copying scripts for distro_cli_integration_tests"
  )

  # Copy proxy_device directory for integration tests
  set(PROXY_DEVICE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fboss-image/distro_cli/tests/proxy_device")
  set(PROXY_DEVICE_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/distro_cli_integration_tests/distro_cli/tests/proxy_device")
  add_custom_command(
    TARGET distro_cli_integration_tests.GEN_PY_EXE
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROXY_DEVICE_SOURCE_DIR}"
            "${PROXY_DEVICE_DEST_DIR}"
    COMMENT "Copying proxy_device for distro_cli_integration_tests"
  )

  # Copy test_topology directory for integration tests
  set(TEST_TOPOLOGY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fboss-image/distro_cli/tests/test_topology")
  set(TEST_TOPOLOGY_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/distro_cli_integration_tests/distro_cli/tests/test_topology")
  add_custom_command(
    TARGET distro_cli_integration_tests.GEN_PY_EXE
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TEST_TOPOLOGY_SOURCE_DIR}"
            "${TEST_TOPOLOGY_DEST_DIR}"
    COMMENT "Copying test_topology for distro_cli_integration_tests"
  )

  install_fb_python_executable(distro_cli_integration_tests)
endif()

# Restore the original Python3_EXECUTABLE if it was set
if(DEFINED SAVED_Python3_EXECUTABLE)
  set(Python3_EXECUTABLE "${SAVED_Python3_EXECUTABLE}")
  unset(SAVED_Python3_EXECUTABLE)
endif()
