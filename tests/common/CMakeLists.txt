add_executable(common_test status_test.cpp)
target_link_libraries(common_test PRIVATE fluxcache_common GTest::gtest_main)
add_test(NAME common_test COMMAND common_test)

add_executable(config_test config_test.cpp)
target_link_libraries(config_test PRIVATE fluxcache_common GTest::gtest_main)
add_test(NAME config_test COMMAND config_test)

add_executable(types_test types_test.cpp)
target_link_libraries(types_test PRIVATE fluxcache_common GTest::gtest_main)
add_test(NAME types_test COMMAND types_test)

add_executable(channel_pool_test channel_pool_test.cpp)
target_link_libraries(channel_pool_test PRIVATE fluxcache_common fluxcache_proto
                                                      GTest::gtest_main)
add_test(NAME channel_pool_test COMMAND channel_pool_test)

add_executable(channel_pool_retry_test channel_pool_retry_test.cpp)
target_link_libraries(channel_pool_retry_test PRIVATE fluxcache_common
                                                      GTest::gtest_main)
add_test(NAME channel_pool_retry_test COMMAND channel_pool_retry_test)

add_executable(metrics_test metrics_test.cpp)
target_link_libraries(metrics_test PRIVATE fluxcache_common GTest::gtest_main)
add_test(NAME metrics_test COMMAND metrics_test)

add_executable(slow_request_tracker_test slow_request_tracker_test.cpp)
target_link_libraries(slow_request_tracker_test PRIVATE fluxcache_common
                                                   GTest::gtest_main)
add_test(NAME slow_request_tracker_test COMMAND slow_request_tracker_test)

add_executable(resilience_config_test resilience_config_test.cpp)
target_link_libraries(resilience_config_test PRIVATE fluxcache_common
                                                     GTest::gtest_main)
add_test(NAME resilience_config_test COMMAND resilience_config_test)

add_executable(circuit_breaker_test circuit_breaker_test.cpp)
target_link_libraries(circuit_breaker_test PRIVATE fluxcache_common
                                                   GTest::gtest_main)
add_test(NAME circuit_breaker_test COMMAND circuit_breaker_test)
