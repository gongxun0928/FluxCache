add_executable(master_test master_test.cpp)
target_link_libraries(master_test PRIVATE fluxcache_master fluxcache_proto GTest::gtest_main)
add_test(NAME master_test COMMAND master_test)
