# CMake to build libraries and binaries in fboss/qsfp_service/test

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(mock_transceiver_i2capi
  fboss/lib/usb/tests/MockTransceiverI2CApi.cpp
)

target_link_libraries(mock_transceiver_i2capi
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  i2_api
  transceiver_cpp2
  fake_transceiver_impl
  Folly::folly
)

add_library(mock_wedge_manager INTERFACE)

target_link_libraries(mock_wedge_manager INTERFACE
  fboss_error
  fake_test_platform_mapping
  mock_transceiver_i2capi
  qsfp_platforms_wedge
  Folly::folly
)

target_include_directories(mock_wedge_manager INTERFACE
  ${CMAKE_SOURCE_DIR}
)

add_library(transceiver_manager_test_helper
  fboss/qsfp_service/test/TransceiverManagerTestHelper.cpp
)

target_link_libraries(transceiver_manager_test_helper
  ${GTEST}
  mock_wedge_manager
  transceiver_validation_cpp2
  qsfp_config
  load_agent_config
  transceiver_manager
  fake_test_platform_mapping
  Folly::folly
)

add_executable(transceiver_manager_test
  fboss/util/oss/TestMain.cpp
  fboss/qsfp_service/test/TransceiverManagerTest.cpp
)

target_link_libraries(transceiver_manager_test
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  transceiver_manager_test_helper
  common_file_utils
  qsfp_module
)

gtest_discover_tests(transceiver_manager_test)

add_executable(transceiver_state_machine_test
  fboss/util/oss/TestMain.cpp
  fboss/qsfp_service/test/TransceiverStateMachineTest.cpp
)

target_link_libraries(transceiver_state_machine_test
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  transceiver_manager_test_helper
  transceiver_manager
  qsfp_module
  fake_transceiver_impl
  hw_transceiver_utils
  Folly::folly
)

gtest_discover_tests(transceiver_state_machine_test)

add_executable(cpo_transceiver_manager_test
  fboss/util/oss/TestMain.cpp
  fboss/qsfp_service/test/CpoTransceiverManagerTest.cpp
)

target_link_libraries(cpo_transceiver_manager_test
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  transceiver_manager_test_helper
  platform_mapping
  transceiver_manager
  qsfp_module
  fake_transceiver_impl
  Folly::folly
)

# CpoTransceiverManagerTest reads the checked-in example CPO platform mapping
# JSON via a repo-relative path (folly::readFile), so run the discovered tests
# from the source root where fboss/lib/.../*.json resolves.
gtest_discover_tests(cpo_transceiver_manager_test
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(sdk_dump_path_test
  fboss/util/oss/TestMain.cpp
  fboss/qsfp_service/test/SdkDumpPathTest.cpp
)

target_link_libraries(sdk_dump_path_test
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
  fboss_error
  Folly::folly
)

gtest_discover_tests(sdk_dump_path_test)
