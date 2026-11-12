# CMake to build libraries and binaries in fboss/lib/firmware_storage

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(firmware_storage
  fboss/lib/firmware_storage/FbossFirmware.cpp
  fboss/lib/firmware_storage/FbossFwStorage.cpp
)

target_link_libraries(firmware_storage
  ${YAML-CPP}
  Folly::folly
  Boost::filesystem
)
