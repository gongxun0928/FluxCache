add_executable(sdk_example sdk_example.cpp)
target_include_directories(sdk_example PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sdk_example PRIVATE fluxcache_sdk)
