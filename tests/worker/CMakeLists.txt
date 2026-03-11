add_executable(worker_test worker_test.cpp)
target_include_directories(worker_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(worker_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME worker_test COMMAND worker_test)

add_executable(memory_tier_test memory_tier_test.cpp)
target_include_directories(memory_tier_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(memory_tier_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME memory_tier_test COMMAND memory_tier_test)

add_executable(ssd_tier_test ssd_tier_test.cpp)
target_include_directories(ssd_tier_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ssd_tier_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME ssd_tier_test COMMAND ssd_tier_test)

add_executable(hdd_tier_test hdd_tier_test.cpp)
target_include_directories(hdd_tier_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(hdd_tier_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME hdd_tier_test COMMAND hdd_tier_test)

add_executable(tier_manager_test tier_manager_test.cpp)
target_include_directories(tier_manager_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tier_manager_test PRIVATE fluxcache_worker fluxcache_proto GTest::gtest_main)
add_test(NAME tier_manager_test COMMAND tier_manager_test)

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

add_executable(lru_policy_test lru_policy_test.cpp)
target_include_directories(lru_policy_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lru_policy_test PRIVATE fluxcache_worker fluxcache_proto
                                               GTest::gtest_main)
add_test(NAME lru_policy_test COMMAND lru_policy_test)

add_executable(meta_store_test meta_store_test.cpp)
target_include_directories(meta_store_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(meta_store_test PRIVATE fluxcache_worker fluxcache_proto
                                               GTest::gtest_main)
add_test(NAME meta_store_test COMMAND meta_store_test)

add_executable(tier_promotion_test tier_promotion_test.cpp)
target_include_directories(tier_promotion_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tier_promotion_test PRIVATE fluxcache_worker fluxcache_proto
                                               GTest::gtest_main)
add_test(NAME tier_promotion_test COMMAND tier_promotion_test)
