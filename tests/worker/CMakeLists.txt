add_executable(worker_test worker_test.cpp)
target_include_directories(worker_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(worker_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME worker_test COMMAND worker_test)
