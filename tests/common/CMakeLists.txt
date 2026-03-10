add_executable(common_test status_test.cpp)
target_link_libraries(common_test PRIVATE fluxcache_common GTest::gtest_main)
add_test(NAME common_test COMMAND common_test)
