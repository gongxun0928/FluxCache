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
target_link_libraries(channel_pool_test PRIVATE fluxcache_proto GTest::gtest_main)
add_test(NAME channel_pool_test COMMAND channel_pool_test)
