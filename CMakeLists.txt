cmake_minimum_required(VERSION 3.20)
project(FluxCache LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FLUXCACHE_ENABLE_FUSE "Build FUSE client" OFF)
option(FLUXCACHE_ENABLE_S3 "Build S3 UFS backend" OFF)

add_subdirectory(src)
add_subdirectory(tests)
