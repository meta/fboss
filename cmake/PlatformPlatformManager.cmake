# Make to build libraries and binaries in fboss/platform/platform_manager

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(platform_manager_fbiob_ioctl_h INTERFACE)

target_sources(platform_manager_fbiob_ioctl_h
  INTERFACE
    fboss/platform/platform_manager/uapi/fbiob-ioctl.h
)

add_library(platform_manager_fbcpld_ioctl_h INTERFACE)

target_sources(platform_manager_fbcpld_ioctl_h
  INTERFACE
    fboss/platform/platform_manager/uapi/fbcpld-ioctl.h
)

add_fbthrift_cpp_library(
  platform_manager_snapshot_cpp2
  fboss/platform/platform_manager/platform_manager_snapshot.thrift
  OPTIONS
    json
    reflection
)

add_fbthrift_cpp_library(
  platform_manager_presence_cpp2
  fboss/platform/platform_manager/platform_manager_presence.thrift
  OPTIONS
    json
    reflection
)

add_fbthrift_cpp_library(
  platform_manager_validators_cpp2
  fboss/platform/platform_manager/platform_manager_validators.thrift
  OPTIONS
    json
    reflection
)

add_fbthrift_cpp_library(
  platform_manager_config_cpp2
  fboss/platform/platform_manager/platform_manager_config.thrift
  OPTIONS
    json
    reflection
  DEPENDS
    platform_manager_presence_cpp2
)

add_fbthrift_cpp_library(
  platform_manager_service_cpp2
  fboss/platform/platform_manager/platform_manager_service.thrift
  SERVICES
    PlatformManagerService
  OPTIONS
    json
    reflection
  DEPENDS
   platform_manager_snapshot_cpp2
   platform_manager_config_cpp2
   weutil_eeprom_contents_cpp2
)

add_library(platform_manager_i2c_explorer
  fboss/platform/platform_manager/I2cExplorer.cpp
)

target_link_libraries(platform_manager_i2c_explorer
  fmt::fmt
  platform_fs_utils
  platform_manager_config_cpp2
  platform_manager_utils
  i2c_ctrl
  platform_utils
  Folly::folly
  ${RE2}
)

add_library(platform_manager_pm_unit_identity
  fboss/platform/platform_manager/PmUnitIdentity.cpp
)

target_link_libraries(platform_manager_pm_unit_identity
  fmt::fmt
  platform_manager_config_cpp2
  weutil_eeprom_contents_cpp2
  Folly::folly
)

add_library(platform_manager_data_store
  fboss/platform/platform_manager/DataStore.cpp
)

target_link_libraries(platform_manager_data_store
  fmt::fmt
  platform_manager_config_cpp2
  platform_manager_utils
  weutil_eeprom_contents_cpp2
  weutil_fboss_eeprom_interface
  Folly::folly
)

add_library(platform_manager_utils
  fboss/platform/platform_manager/Utils.cpp
)

target_link_libraries(platform_manager_utils
  gpiod_line
  platform_manager_config_cpp2
  ${RE2}
  Folly::folly
)

add_library(platform_manager_config_utils
  fboss/platform/platform_manager/ConfigUtils.cpp
)

target_link_libraries(platform_manager_config_utils
  platform_manager_config_validator
  platform_manager_config_cpp2
  platform_config_lib
  platform_name_lib
  Folly::folly
)

add_library(platform_manager_presence_checker
  fboss/platform/platform_manager/PresenceChecker.cpp
)

target_link_libraries(platform_manager_presence_checker
  platform_fs_utils
  platform_manager_device_path_resolver
  platform_manager_utils
  weutil_eeprom_contents_cpp2
)

add_library(platform_manager_pci_explorer
  fboss/platform/platform_manager/PciExplorer.cpp
)

target_link_libraries(platform_manager_pci_explorer
  platform_manager_i2c_explorer
  platform_manager_config_cpp2
  platform_manager_utils
  Folly::folly
  fb303::fb303
)

add_library(platform_manager_cpld_manager
  fboss/platform/platform_manager/CpldManager.cpp
)

target_link_libraries(platform_manager_cpld_manager
  fmt::fmt
  platform_manager_config_cpp2
  platform_manager_utils
  Folly::folly
)

add_library(platform_manager_fbfancpld_ioctl_h INTERFACE)

target_sources(platform_manager_fbfancpld_ioctl_h
  INTERFACE
    fboss/platform/platform_manager/uapi/fbfancpld-ioctl.h
)

add_library(platform_manager_fan_cpld_manager
  fboss/platform/platform_manager/FanCpldManager.cpp
)

target_link_libraries(platform_manager_fan_cpld_manager
  fmt::fmt
  platform_manager_config_cpp2
  platform_manager_utils
  Folly::folly
)

add_library(platform_manager_device_path_resolver
  fboss/platform/platform_manager/DevicePathResolver.cpp
)

target_link_libraries(platform_manager_device_path_resolver
  platform_manager_data_store
  platform_manager_i2c_explorer
  platform_manager_pci_explorer
  platform_manager_config_cpp2
  platform_manager_utils
  weutil_eeprom_contents_cpp2
)

add_library(platform_manager_platform_explorer
  fboss/platform/platform_manager/PlatformExplorer.cpp
  fboss/platform/platform_manager/ExplorationSummary.cpp
  fboss/platform/platform_manager/ExplorationErrors.cpp
)

target_link_libraries(platform_manager_platform_explorer
  platform_manager_cpld_manager
  platform_manager_fan_cpld_manager
  platform_manager_data_store
  platform_manager_device_path_resolver
  platform_manager_i2c_explorer
  platform_manager_pci_explorer
  platform_manager_config_cpp2
  platform_manager_service_cpp2
  platform_manager_presence_checker
  platform_manager_utils
  platform_fs_utils
  fb303::fb303
  weutil_fboss_eeprom_interface
  ioctl_smbus_eeprom_reader
  Folly::folly
  structured_logger
)

add_library(platform_manager_config_validator
  fboss/platform/platform_manager/ConfigValidator.cpp
)

target_link_libraries(platform_manager_config_validator
  platform_manager_cpld_manager
  platform_manager_i2c_explorer
  platform_manager_config_cpp2
  platform_manager_validators_cpp2
  Folly::folly
  range-v3
)

add_library(platform_manager_system_interface
  fboss/platform/platform_manager/SystemInterface.cpp
)

target_link_libraries(platform_manager_system_interface
  fmt::fmt
  range-v3
  platform_utils
  Folly::folly
)

add_library(platform_manager_pkg_manager
  fboss/platform/platform_manager/PkgManager.cpp
)

target_link_libraries(platform_manager_pkg_manager
  range-v3
  platform_manager_config_cpp2
  platform_manager_system_interface
  platform_fs_utils
  platform_utils
  Folly::folly
  FBThrift::thriftcpp2
)

add_library(platform_manager_handler
  fboss/platform/platform_manager/PlatformManagerHandler.cpp
)

target_link_libraries(platform_manager_handler
  platform_manager_pkg_manager
  platform_manager_platform_explorer
  platform_manager_service_cpp2
  platform_manager_snapshot_builder
)

add_executable(platform_manager
  fboss/platform/platform_manager/Main.cpp
)

add_library(platform_manager_snapshot_builder
  fboss/platform/platform_manager/PlatformSnapshotBuilder.cpp
)

target_link_libraries(platform_manager_snapshot_builder
  fmt::fmt
  platform_manager_config_cpp2
  platform_manager_data_store
)

target_link_libraries(platform_manager
  fb303::fb303
  platform_config_lib
  platform_fs_utils
  platform_name_lib
  platform_utils
  platform_manager_config_utils
  platform_manager_config_cpp2
  platform_manager_config_validator
  platform_manager_handler
  platform_manager_pkg_manager
  platform_manager_platform_explorer
  platform_manager_presence_cpp2
  platform_manager_service_cpp2
  platform_manager_snapshot_cpp2
  platform_manager_utils
  weutil_fboss_eeprom_interface
  ioctl_smbus_eeprom_reader
  i2c_ctrl
  ${LIBGPIOD}
  ${SYSTEMD}
  gpiod_line
  range-v3
  structured_logger
)

install(TARGETS platform_manager)
