cmake_minimum_required(VERSION 3.20)

# S3 UFS (minio-cpp) is required. Use vcpkg: cmake -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake ..
project(FluxCache LANGUAGES CXX)

# For clangd / C++ IntelliSense: generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FLUXCACHE_ENABLE_FUSE "Build FUSE client" OFF)
option(FLUXCACHE_ENABLE_TSAN "Build with ThreadSanitizer for concurrency checks" OFF)

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

# FUSE3 for FUSE mount (P2-06). Skip if not found.
set(FLUXCACHE_HAS_FUSE OFF)
if(FLUXCACHE_ENABLE_FUSE)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(FUSE3 QUIET fuse3)
    if(FUSE3_FOUND)
      set(FLUXCACHE_HAS_FUSE ON)
    endif()
  endif()
  if(NOT FLUXCACHE_HAS_FUSE)
    message(STATUS "FUSE: fuse3 not found, skipping fluxcache-fuse target")
  endif()
endif()

if(FLUXCACHE_ENABLE_TSAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
  message(STATUS "ThreadSanitizer enabled")
endif()

# S3 UFS (P2-07A): required. vcpkg exposes minio-cpp as miniocpp::miniocpp
find_package(miniocpp CONFIG REQUIRED)
message(STATUS "S3 UFS: minio-cpp (miniocpp) found")

add_subdirectory(src)
add_subdirectory(examples)
add_subdirectory(tests)
