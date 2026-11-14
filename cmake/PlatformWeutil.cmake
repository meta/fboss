# Make to build libraries and binaries in fboss/platform/weutils

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(weutil_config_cpp2)
thrift_generate(
  weutil_config_cpp2
  fboss/platform/weutil/if/weutil_config.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
  INCLUDE_DIRECTORIES
    ${PROJECT_SOURCE_DIR}
    ${THRIFT_INCLUDE_DIR}
)
target_include_directories(weutil_config_cpp2 PUBLIC ${PROJECT_BINARY_DIR})

thrift_add_library(
  weutil_eeprom_contents_cpp2
  fboss/platform/weutil/if/eeprom_contents.thrift
  LANGUAGE cpp2
  OPTIONS
    json
    reflection
)

add_library(weutil_crc16_ccitt_aug
  fboss/platform/weutil/Crc16CcittAug.cpp
)

add_library(weutil_fboss_eeprom_interface
  fboss/platform/weutil/ParserUtils.cpp
  fboss/platform/weutil/FbossEepromInterface.cpp
)

target_link_libraries(weutil_fboss_eeprom_interface
  Folly::folly
  weutil_crc16_ccitt_aug
  weutil_eeprom_contents_cpp2
)

add_library(weutil_lib
  fboss/platform/weutil/WeutilDarwin.cpp
  fboss/platform/weutil/WeutilImpl.cpp
  fboss/platform/weutil/prefdl/Prefdl.cpp
  fboss/platform/weutil/Weutil.cpp
)

target_link_libraries(weutil_lib
  weutil_config_utils
  platform_utils
  Folly::folly
  eeprom_content_validator
  weutil_config_cpp2
  weutil_fboss_eeprom_interface
  platform_manager_config_utils
  platform_name_lib
  ioctl_smbus_eeprom_reader
)

add_library(weutil_config_utils
  fboss/platform/weutil/ConfigUtils.cpp
)

target_link_libraries(weutil_config_utils
  platform_config_lib
  platform_manager_config_cpp2
  Folly::folly
  thrift::rpc
  ${RE2}
)

add_library(eeprom_content_validator
  fboss/platform/weutil/ContentValidator.cpp
)

target_link_libraries(eeprom_content_validator
  fmt::fmt
)

add_library(ioctl_smbus_eeprom_reader
  fboss/platform/weutil/IoctlSmbusEepromReader.cpp
)

target_link_libraries(ioctl_smbus_eeprom_reader
  fmt::fmt
)

add_executable(weutil
  fboss/platform/weutil/main.cpp
)

target_link_libraries(weutil
  weutil_lib
)

add_library(weutil_config_validator
  fboss/platform/weutil/ConfigValidator.cpp
)

target_link_libraries(weutil_config_validator
  weutil_config_cpp2
)

add_executable(weutil_hw_test
  fboss/platform/weutil/hw_test/WeutilTest.cpp
)

target_link_libraries(weutil_hw_test
  weutil_lib
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

install(TARGETS weutil)
