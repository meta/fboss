# CMake to build LedServiceHwTest in fboss/led_service/hw_test

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_executable(led_service_hw_test
  fboss/led_service/hw_test/LedEnsemble.cpp
  fboss/led_service/hw_test/LedServiceTest.cpp
)

target_link_libraries(led_service_hw_test
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  fboss_common_init
  fboss_error
  fboss_types
  led_manager_lib
  common_file_utils
  platform_utils
  Folly::folly
  thrift::rpc
)

install(TARGETS led_service_hw_test)
