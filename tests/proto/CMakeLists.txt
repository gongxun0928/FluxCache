add_executable(proto_test proto_test.cpp)
target_link_libraries(proto_test PRIVATE fluxcache_proto GTest::gtest_main)
add_test(NAME proto_test COMMAND proto_test)
