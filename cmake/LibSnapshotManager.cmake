# CMake to build libraries and binaries from fboss/lib/link_snapshots/SnapshotManager

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(async_filewriter_factory
  fboss/lib/link_snapshots/AsyncFileWriterFactory.cpp
)

target_link_libraries(async_filewriter_factory
  Folly::folly
  Boost::filesystem
)


add_library(snapshot_manager
  fboss/lib/link_snapshots/SnapshotManager.cpp
)

target_link_libraries(snapshot_manager
  FBThrift::thriftcpp2
  fboss_cpp2
  phy_cpp2
  alert_logger
  async_filewriter_factory
)
