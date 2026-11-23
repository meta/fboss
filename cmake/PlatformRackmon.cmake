add_custom_command(
  OUTPUT  fboss/platform/rackmon/GeneratedRackmonInterfaceConfig.cpp fboss/platform/rackmon/GeneratedRackmonPlsConfig.cpp fboss/platform/rackmon/GeneratedRackmonRegisterMapConfig.cpp
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/fboss/platform/rackmon/configs/interface/rackmon.conf fboss/platform/rackmon/configs/interface/rackmon.conf
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/fboss/platform/rackmon/configs/interface/rackmon_pls.conf fboss/platform/rackmon/configs/interface/rackmon_pls.conf
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/fboss/platform/rackmon/configs/register_map/orv2_psu.json fboss/platform/rackmon/configs/register_map/orv2_psu.json
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/fboss/platform/rackmon/configs/register_map/orv3_psu.json fboss/platform/rackmon/configs/register_map/orv3_psu.json
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/fboss/platform/rackmon/configs/register_map/orv3_bbu.json fboss/platform/rackmon/configs/register_map/orv3_bbu.json
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/fboss/platform/rackmon/generate_rackmon_config.sh fboss/platform/rackmon/generate_rackmon_config.sh
  COMMAND fboss/platform/rackmon/generate_rackmon_config.sh --install_dir=fboss/platform/rackmon/
  DEPENDS fboss/platform/rackmon/configs/interface/rackmon.conf fboss/platform/rackmon/configs/interface/rackmon_pls.conf fboss/platform/rackmon/configs/register_map/orv2_psu.json fboss/platform/rackmon/configs/register_map/orv3_psu.json fboss/platform/rackmon/configs/register_map/orv3_bbu.json
)

thrift_add_library(
  rackmon_cpp2
  fboss/platform/rackmon/if/rackmonsvc.thrift
  LANGUAGE cpp2
  SERVICES
  RackmonCtrl
  OPTIONS
    json
    reflection
  DEPENDS
    fb303_cpp2
    fboss_cpp2
)


add_library(rackmon_lib
  fboss/platform/rackmon/RackmonThriftHandler.cpp
  fboss/platform/rackmon/Device.cpp
  fboss/platform/rackmon/Modbus.cpp
  fboss/platform/rackmon/ModbusCmds.cpp
  fboss/platform/rackmon/ModbusDevice.cpp
  fboss/platform/rackmon/Msg.cpp
  fboss/platform/rackmon/Rackmon.cpp
  fboss/platform/rackmon/RackmonPlsManager.cpp
  fboss/platform/rackmon/Register.cpp
  fboss/platform/rackmon/UARTDevice.cpp
  fboss/platform/rackmon/GeneratedRackmonRegisterMapConfig.cpp
  fboss/platform/rackmon/GeneratedRackmonInterfaceConfig.cpp
  fboss/platform/rackmon/GeneratedRackmonPlsConfig.cpp
)

target_link_libraries(rackmon_lib
  rackmon_cpp2
  Folly::folly
  ${NLOHMANN_JSON_LIBRARIES}
  ${LIBGPIOD}
)
target_include_directories(rackmon_lib PRIVATE
  fboss/platform/rackmon
)

add_executable(rackmon
  fboss/platform/rackmon/Main.cpp
)

target_link_libraries(rackmon
  platform_utils
  rackmon_lib
  fb303::fb303
)

install(TARGETS rackmon)

add_executable(rackmon_test
  fboss/platform/rackmon/tests/DeviceTest.cpp
  fboss/platform/rackmon/tests/TempDir.h
  fboss/platform/rackmon/tests/ModbusCmdsTest.cpp
  fboss/platform/rackmon/tests/ModbusDeviceTest.cpp
  fboss/platform/rackmon/tests/ModbusTest.cpp
  fboss/platform/rackmon/tests/MsgTest.cpp
  fboss/platform/rackmon/tests/PlsConfigTest.cpp
  fboss/platform/rackmon/tests/PlsManagerTest.cpp
  fboss/platform/rackmon/tests/PollThreadTest.cpp
  fboss/platform/rackmon/tests/RackmonConfigTest.cpp
  fboss/platform/rackmon/tests/RackmonTest.cpp
  fboss/platform/rackmon/tests/RegisterDescriptorTest.cpp
  fboss/platform/rackmon/tests/RegisterMapTest.cpp
  fboss/platform/rackmon/tests/RegisterTest.cpp
  fboss/platform/rackmon/tests/RegisterValueTest.cpp
  fboss/platform/rackmon/tests/ThriftHandlerTest.cpp
  fboss/platform/rackmon/tests/UARTDeviceTest.cpp
)

target_link_libraries(rackmon_test
  platform_utils
  rackmon_lib
  fb303::fb303
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

target_include_directories(rackmon_test PRIVATE
  fboss/platform/rackmon
)

target_compile_definitions(rackmon_test PRIVATE __TEST__=1)

install(TARGETS rackmon_test)
gtest_discover_tests(rackmon_test)

# Firmware Update Library and Tools
add_library(updater_lib
  fboss/platform/rackmon/rack_firmware_update/UpdaterExceptions.cpp
  fboss/platform/rackmon/rack_firmware_update/UpdaterUtils.cpp
  fboss/platform/rackmon/rack_firmware_update/RackmonClient.cpp
  fboss/platform/rackmon/rack_firmware_update/HexFileParser.cpp
  fboss/platform/rackmon/rack_firmware_update/FirmwareUpdater.cpp
  fboss/platform/rackmon/rack_firmware_update/MEIFirmwareUpdater.cpp
  fboss/platform/rackmon/rack_firmware_update/MailboxFirmwareUpdater.cpp
)

target_link_libraries(updater_lib
  rackmon_cpp2
  Folly::folly
  gflags
)

target_include_directories(updater_lib PRIVATE
  fboss/platform/rackmon/rack_firmware_update
)

# PSU Update Tool (Delta ORv3)
add_executable(psu_update_delta_orv3
  fboss/platform/rackmon/rack_firmware_update/psu_update_delta_orv3.cpp
)

target_link_libraries(psu_update_delta_orv3
  updater_lib
  rackmon_cpp2
  Folly::folly
  gflags
)

install(TARGETS psu_update_delta_orv3)

# Device Update Tool (Mailbox Protocol for BBU/PSU variants)
add_executable(device_update_mailbox
  fboss/platform/rackmon/rack_firmware_update/device_update_mailbox.cpp
)

target_link_libraries(device_update_mailbox
  updater_lib
  rackmon_cpp2
  Folly::folly
  gflags
)

install(TARGETS device_update_mailbox)
