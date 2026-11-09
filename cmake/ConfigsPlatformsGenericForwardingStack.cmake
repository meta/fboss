add_fbthrift_cpp_library(
  config_generation_cpp2
  fboss/configs/platforms/generic/forwarding_stack/config_generation.thrift
  OPTIONS
    json
)

add_fbthrift_cpp_library(
  feature_default_command_args_cpp2
  fboss/configs/platforms/generic/forwarding_stack/feature_default_command_args.thrift
  OPTIONS
    json
)

add_library(config_generation_utils
  fboss/configs/platforms/generic/forwarding_stack/utils/ConfigGenerationSpecUtils.h
  fboss/configs/platforms/generic/forwarding_stack/utils/ConfigGenerationSpecUtils.cpp
  fboss/configs/platforms/generic/forwarding_stack/utils/FeatureDefaultCommandArgsUtils.h
  fboss/configs/platforms/generic/forwarding_stack/utils/FeatureDefaultCommandArgsUtils.cpp
  fboss/configs/platforms/generic/forwarding_stack/utils/LinkTopologyUtils.h
  fboss/configs/platforms/generic/forwarding_stack/utils/LinkTopologyUtils.cpp
  fboss/configs/platforms/generic/forwarding_stack/utils/ThriftConfigUtils.h
  fboss/configs/platforms/generic/forwarding_stack/utils/ThriftConfigUtils.cpp
)

target_link_libraries(config_generation_utils
  agent_config_cpp2
  config_generation_cpp2
  feature_default_command_args_cpp2
  fboss_common_cpp2
  fboss_error
  FBThrift::thriftcpp2
  Folly::folly
)

add_executable(config_generation_spec_utils_test
  fboss/util/oss/TestMain.cpp
  fboss/configs/platforms/generic/forwarding_stack/tests/ConfigGenerationSpecUtilsTest.cpp
)

target_link_libraries(config_generation_spec_utils_test
  config_generation_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(config_generation_spec_utils_test)

add_executable(link_topology_utils_test
  fboss/util/oss/TestMain.cpp
  fboss/configs/platforms/generic/forwarding_stack/tests/LinkTopologyUtilsTest.cpp
)

target_link_libraries(link_topology_utils_test
  config_generation_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(link_topology_utils_test)

add_executable(feature_default_command_args_config_test
  fboss/util/oss/TestMain.cpp
  fboss/configs/platforms/generic/forwarding_stack/tests/FeatureDefaultCommandArgsConfigTest.cpp
)

target_link_libraries(feature_default_command_args_config_test
  config_generation_utils
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(feature_default_command_args_config_test)
