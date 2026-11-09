# CMake to build libraries and binaries in fboss/lib/config

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(fboss_config_utils
  fboss/lib/config/PlatformConfigUtils.cpp
)

target_link_libraries(fboss_config_utils
	fboss_error
	fboss_types
	platform_config_cpp2
	switch_config_cpp2
	external_phy
)

add_library(fboss_agent_config_utils
  fboss/lib/config/agent/AclConfigUtils.cpp
  fboss/lib/config/agent/CoppConfigUtils.cpp
  fboss/lib/config/agent/InterfaceConfigUtils.cpp
  fboss/lib/config/agent/PortConfigUtils.cpp
  fboss/lib/config/agent/VlanConfigUtils.cpp
)

target_link_libraries(fboss_agent_config_utils
	Folly::folly
	fboss_error
	fboss_types
	switch_config_cpp2
	platform_mapping
	switch_asics
)
