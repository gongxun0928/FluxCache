add_executable(common_test status_test.cpp)
target_link_libraries(common_test PRIVATE fluxcache_common GTest::gtest_main)
add_test(NAME common_test COMMAND common_test)

add_executable(types_test types_test.cpp)
target_link_libraries(types_test PRIVATE fluxcache_common GTest::gtest_main)
add_test(NAME types_test COMMAND types_test)
