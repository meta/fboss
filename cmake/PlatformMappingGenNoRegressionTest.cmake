# CMake to build libraries and binaries in fboss/lib/platform_mapping_v2/test

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

include(FBPythonBinary)

set(
    PLATFORM_MAPPING_PY_SRCS
    "fboss/lib/platform_mapping_v2/test/verify_generated_files.py"
    "fboss/lib/platform_mapping_v2/asic_vendor_config.py"
    "fboss/lib/platform_mapping_v2/gen.py"
    "fboss/lib/platform_mapping_v2/helpers.py"
    "fboss/lib/platform_mapping_v2/integrated_transceiver_mapping.py"
    "fboss/lib/platform_mapping_v2/platform_mapping_v2.py"
    "fboss/lib/platform_mapping_v2/port_profile_mapping.py"
    "fboss/lib/platform_mapping_v2/profile_settings.py"
    "fboss/lib/platform_mapping_v2/read_files_utils.py"
    "fboss/lib/platform_mapping_v2/si_settings.py"
    "fboss/lib/platform_mapping_v2/static_mapping.py"
)

file(COPY "fboss/configs/platforms" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/fboss/configs")
message(STATUS "Copying platform configs to: ${CMAKE_CURRENT_BINARY_DIR}/fboss/configs")

add_fb_thrift_python_executable(
    platform_mapping_gen_no_regression_test
    MAIN_MODULE fboss.lib.platform_mapping_v2.test.verify_generated_files:run_tests
    SOURCES ${PLATFORM_MAPPING_PY_SRCS}
    DEPENDS
        asic_config_v2_python
        platform_config_python
        switch_config_python
        transceiver_python
        phy_python
        platform_mapping_config_python
        fboss_common_python
)

install_fb_python_executable(platform_mapping_gen_no_regression_test)
