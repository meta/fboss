# CMake to build tests in fboss/lib/test

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_executable(rest_client_test
  fboss/lib/test/RestClientTest.cpp
)

target_link_libraries(rest_client_test
  rest_client
  fboss_error
  ${GTEST}
  ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(rest_client_test)
