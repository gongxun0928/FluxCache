add_executable(client_page_cache_test client_page_cache_test.cpp)
target_include_directories(client_page_cache_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(client_page_cache_test PRIVATE fluxcache_client
                                         fluxcache_common GTest::gtest_main)
add_test(NAME client_page_cache_test COMMAND client_page_cache_test)

add_executable(client_test client_test.cpp)
target_link_libraries(client_test PRIVATE fluxcache_client fluxcache_common
                                         fluxcache_proto GTest::gtest_main)
add_test(NAME client_test COMMAND client_test)

add_executable(master_client_raft_test master_client_raft_test.cpp)
target_include_directories(master_client_raft_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(master_client_raft_test PRIVATE fluxcache_client
                                                fluxcache_common
                                                fluxcache_proto
                                                fluxcache_master
                                                fluxcache_ufs
                                                GTest::gtest_main)
add_test(NAME master_client_raft_test COMMAND master_client_raft_test)

add_executable(read_test read_test.cpp)
target_include_directories(read_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(read_test PRIVATE fluxcache_client fluxcache_common
                                     fluxcache_proto fluxcache_master
                                     fluxcache_worker fluxcache_ufs
                                     GTest::gtest_main)
add_test(NAME read_test COMMAND read_test)

add_executable(write_test write_test.cpp)
target_include_directories(write_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(write_test PRIVATE fluxcache_client fluxcache_common
                                     fluxcache_proto fluxcache_master
                                     fluxcache_worker fluxcache_ufs
                                     GTest::gtest_main)
add_test(NAME write_test COMMAND write_test)

add_executable(sdk_test sdk_test.cpp)
target_include_directories(sdk_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sdk_test PRIVATE fluxcache_sdk fluxcache_client
                                     fluxcache_common fluxcache_proto
                                     fluxcache_master fluxcache_worker
                                     fluxcache_ufs GTest::gtest_main)
add_test(NAME sdk_test COMMAND sdk_test)
