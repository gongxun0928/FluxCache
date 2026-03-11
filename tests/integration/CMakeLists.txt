add_executable(read_path_e2e_test read_path_e2e_test.cpp)
target_include_directories(read_path_e2e_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(read_path_e2e_test PRIVATE fluxcache_client fluxcache_common
                                         fluxcache_proto fluxcache_master
                                         fluxcache_worker fluxcache_ufs
                                         GTest::gtest_main)
add_test(NAME read_path_e2e_test COMMAND read_path_e2e_test)
