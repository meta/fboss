# CMake to build libraries and binaries in fboss/qsfp_service/module/properties

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(transceiver_properties_manager
    fboss/qsfp_service/module/properties/TransceiverPropertiesManager.cpp
)

target_link_libraries(transceiver_properties_manager
    fboss_error
    switch_config_cpp2
    transceiver_cpp2
    transceiver_properties_cpp2
    Folly::folly
    thrift::rpc
)

add_executable(transceiver_properties_manager_test
    fboss/qsfp_service/module/properties/test/TransceiverPropertiesManagerTest.cpp
)

target_link_libraries(transceiver_properties_manager_test
    fboss_error
    transceiver_properties_manager
    Folly::folly
    ${GTEST}
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(transceiver_properties_manager_test)
