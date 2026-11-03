# Make to build libraries and binaries in fboss/platform/reboot_cause_finder

add_fbthrift_cpp_library(
  reboot_cause_config_cpp2
  fboss/platform/reboot_cause_finder/if/reboot_cause_config.thrift
  OPTIONS
    json
    reflection
)

add_library(reboot_cause_finder_config_validator
  fboss/platform/reboot_cause_finder/ConfigValidator.cpp
)

target_link_libraries(reboot_cause_finder_config_validator
  fmt::fmt
  reboot_cause_config_cpp2
  Folly::folly
)

add_library(reboot_history_lib
  fboss/platform/reboot_cause_finder/RebootHistory.cpp
)

target_link_libraries(reboot_history_lib
  fmt::fmt
  reboot_cause_config_cpp2
  Folly::folly
  FBThrift::thriftcpp2
)

add_library(reboot_cause_finder_lib
  fboss/platform/reboot_cause_finder/RebootCauseFinderImpl.cpp
)

target_link_libraries(reboot_cause_finder_lib
  ${RE2}
  platform_config_lib
  platform_name_lib
  platform_utils
  reboot_cause_config_cpp2
  reboot_cause_finder_config_validator
  reboot_history_lib
  rest_client
  Folly::folly
  FBThrift::thriftcpp2
)

add_executable(reboot_cause_finder
  fboss/platform/reboot_cause_finder/Main.cpp
)

target_link_libraries(reboot_cause_finder
  reboot_cause_finder_lib
  structured_logger
  fb303::fb303
  fmt::fmt
)

install(TARGETS reboot_cause_finder)
