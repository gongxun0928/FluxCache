add_executable(read_path_e2e_test read_path_e2e_test.cpp)
target_include_directories(read_path_e2e_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(read_path_e2e_test PRIVATE fluxcache_client fluxcache_common
                                         fluxcache_proto fluxcache_master
                                         fluxcache_worker fluxcache_ufs
                                         GTest::gtest_main)
add_test(NAME read_path_e2e_test COMMAND read_path_e2e_test)

add_executable(write_path_e2e_test write_path_e2e_test.cpp)
target_include_directories(write_path_e2e_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(write_path_e2e_test PRIVATE fluxcache_client fluxcache_common
                                          fluxcache_proto fluxcache_master
                                          fluxcache_worker fluxcache_ufs
                                          GTest::gtest_main)
add_test(NAME write_path_e2e_test COMMAND write_path_e2e_test)

add_executable(cli_smoke_test cli_smoke_test.cpp)
target_include_directories(cli_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cli_smoke_test PRIVATE fluxcache_client fluxcache_common
                                     fluxcache_proto fluxcache_master
                                     fluxcache_worker fluxcache_ufs
                                     GTest::gtest_main)
add_test(NAME cli_smoke_test COMMAND cli_smoke_test)

add_executable(gc_reconciliation_test gc_reconciliation_test.cpp)
target_include_directories(gc_reconciliation_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(gc_reconciliation_test PRIVATE fluxcache_client
                                          fluxcache_common fluxcache_proto
                                          fluxcache_master fluxcache_worker
                                          fluxcache_ufs GTest::gtest_main)
add_test(NAME gc_reconciliation_test COMMAND gc_reconciliation_test)

add_executable(degradation_fault_injection_test degradation_fault_injection_test.cpp)
target_include_directories(degradation_fault_injection_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(degradation_fault_injection_test PRIVATE fluxcache_client
                                              fluxcache_common fluxcache_proto
                                              fluxcache_master fluxcache_worker
                                              fluxcache_ufs GTest::gtest_main)
add_test(NAME degradation_fault_injection_test COMMAND degradation_fault_injection_test)

add_executable(multi_worker_test multi_worker_test.cpp)
target_include_directories(multi_worker_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(multi_worker_test PRIVATE fluxcache_client fluxcache_common
                                        fluxcache_proto fluxcache_master
                                        fluxcache_worker fluxcache_ufs
                                        GTest::gtest_main)
add_test(NAME multi_worker_test COMMAND multi_worker_test)
