add_executable(page_store_bench page_store_bench.cpp)
target_include_directories(page_store_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(page_store_bench PRIVATE fluxcache_worker fluxcache_common)
add_test(NAME page_store_bench COMMAND page_store_bench)

add_executable(sequential_read_bench sequential_read_bench.cpp)
target_include_directories(sequential_read_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sequential_read_bench PRIVATE fluxcache_client fluxcache_common
                                         fluxcache_proto fluxcache_master
                                         fluxcache_worker fluxcache_ufs)
add_test(NAME sequential_read_bench COMMAND sequential_read_bench)

# P3-04: Single-page read benchmark (zero-copy fast path)
add_executable(single_page_read_bench single_page_read_bench.cpp)
target_include_directories(single_page_read_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(single_page_read_bench PRIVATE fluxcache_client fluxcache_common
                                            fluxcache_proto fluxcache_master
                                            fluxcache_worker fluxcache_ufs)
add_test(NAME single_page_read_bench COMMAND single_page_read_bench)
