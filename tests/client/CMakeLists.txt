add_executable(client_test client_test.cpp)
target_link_libraries(client_test PRIVATE fluxcache_client fluxcache_common
                                         fluxcache_proto GTest::gtest_main)
add_test(NAME client_test COMMAND client_test)
