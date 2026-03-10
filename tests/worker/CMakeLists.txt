add_executable(worker_test worker_test.cpp)
target_include_directories(worker_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(worker_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME worker_test COMMAND worker_test)

add_executable(memory_tier_test memory_tier_test.cpp)
target_include_directories(memory_tier_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(memory_tier_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME memory_tier_test COMMAND memory_tier_test)
