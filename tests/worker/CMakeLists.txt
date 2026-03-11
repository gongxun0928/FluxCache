add_executable(worker_test worker_test.cpp)
target_include_directories(worker_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(worker_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME worker_test COMMAND worker_test)

add_executable(memory_tier_test memory_tier_test.cpp)
target_include_directories(memory_tier_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(memory_tier_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME memory_tier_test COMMAND memory_tier_test)

add_executable(page_store_test page_store_test.cpp)
target_include_directories(page_store_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(page_store_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME page_store_test COMMAND page_store_test)

add_executable(read_pages_test read_pages_test.cpp)
target_include_directories(read_pages_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(read_pages_test PRIVATE fluxcache_worker fluxcache_ufs
                                               fluxcache_proto GTest::gtest_main)
add_test(NAME read_pages_test COMMAND read_pages_test)

add_executable(write_pages_test write_pages_test.cpp)
target_include_directories(write_pages_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(write_pages_test PRIVATE fluxcache_worker fluxcache_ufs
                                                fluxcache_proto GTest::gtest_main)
add_test(NAME write_pages_test COMMAND write_pages_test)
