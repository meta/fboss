# CMake to build libraries and binaries in fboss/lib/asic_config_v3
# Data-driven ASIC config generation

include(FBPythonBinary)

set(
    ASIC_CONFIG_V3_PY_SRCS
    "fboss/lib/asic_config_v3/__init__.py"
    "fboss/lib/asic_config_v3/base_generator.py"
    "fboss/lib/asic_config_v3/gen.py"
    "fboss/lib/asic_config_v3/paths.py"
    "fboss/lib/asic_config_v3/generators/__init__.py"
    "fboss/lib/asic_config_v3/generators/broadcom_dnx_generator.py"
    "fboss/lib/asic_config_v3/generators/broadcom_xgs_generator.py"
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

add_fb_thrift_python_executable(
    fboss-asic-config-v3-gen
    MAIN_MODULE fboss.lib.asic_config_v3.gen:main
    SOURCES ${ASIC_CONFIG_V3_PY_SRCS}
    DEPENDS
        platform_config_python
        switch_config_python
        transceiver_python
        phy_python
        platform_mapping_config_python
        fboss_common_python
        python-pyyaml::python-pyyaml
)

install_fb_python_executable(fboss-asic-config-v3-gen)

add_fb_python_unittest(
    asic_config_v3_conditional_settings_test
    SOURCES
        "fboss/lib/asic_config_v3/__init__.py"
        "fboss/lib/asic_config_v3/base_generator.py"
        "fboss/lib/asic_config_v3/paths.py"
        "fboss/lib/asic_config_v3/test/__init__.py"
        "fboss/lib/asic_config_v3/test/test_conditional_settings.py"
    PROPERTIES
        LABELS "unit"
)
