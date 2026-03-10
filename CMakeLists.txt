cmake_minimum_required(VERSION 3.20)
project(FluxCache LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FLUXCACHE_ENABLE_FUSE "Build FUSE client" OFF)
option(FLUXCACHE_ENABLE_S3 "Build S3 UFS backend" OFF)

# yaml-cpp for config loading
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
  )
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  FetchContent_MakeAvailable(yaml-cpp)
  unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

# protobuf and gRPC for RPC contract (P1-03)
# Requires: brew install grpc (macOS) or apt install libgrpc++-dev libprotobuf-dev (Linux)
set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# RocksDB for InodeStore persistence (P1-05B)
# Prefer system-installed (e.g. brew install rocksdb)
set(FLUXCACHE_ROCKSDB_TARGET "rocksdb")
find_package(RocksDB CONFIG QUIET)
if(RocksDB_FOUND)
  set(FLUXCACHE_ROCKSDB_TARGET "RocksDB::rocksdb")
else()
  include(FetchContent)
  FetchContent_Declare(
    rocksdb
    GIT_REPOSITORY https://github.com/facebook/rocksdb.git
    GIT_TAG v9.0.0
    GIT_SHALLOW TRUE
  )
  set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
  set(WITH_SNAPPY OFF CACHE BOOL "" FORCE)
  set(WITH_LZ4 OFF CACHE BOOL "" FORCE)
  set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
  set(WITH_ZSTD OFF CACHE BOOL "" FORCE)
  set(WITH_JEMALLOC OFF CACHE BOOL "" FORCE)
  set(ROCKSDB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(rocksdb)
endif()

add_subdirectory(src)
add_subdirectory(tests)
