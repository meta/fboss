# Make to build libraries and binaries in fboss/platform/watchdog_util

add_library(watchdog_util_core
  fboss/platform/watchdog_util/ConfigResolver.cpp
  fboss/platform/watchdog_util/DevmemReader.cpp
  fboss/platform/watchdog_util/I2cReader.cpp
  fboss/platform/watchdog_util/Readers/AristaFanCpldWdReader.cpp
  fboss/platform/watchdog_util/Readers/AristaScdWdReader.cpp
  fboss/platform/watchdog_util/Readers/CiscoFpgaWdReader.cpp
  fboss/platform/watchdog_util/Readers/FbFanCpldWdReader.cpp
  fboss/platform/watchdog_util/WatchdogUtil.cpp
)

target_link_libraries(watchdog_util_core
  platform_config_lib
  platform_manager_config_cpp2
  Folly::folly
  thrift::rpc
  ${RE2}
)

add_executable(watchdog_util
  fboss/platform/watchdog_util/main.cpp
)

target_link_libraries(watchdog_util
  watchdog_util_core
  platform_utils
)

install(TARGETS watchdog_util)
