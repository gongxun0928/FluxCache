add_executable(ufs_test local_ufs_test.cpp)
target_link_libraries(ufs_test PRIVATE fluxcache_ufs GTest::gtest_main)
add_test(NAME ufs_test COMMAND ufs_test)
