add_library(fluxcache_ufs STATIC
  fake_ufs.cpp
  hdfs_ufs.cpp
  local_ufs.cpp
  s3_ufs.cpp
  ufs_factory.cpp
)
target_include_directories(fluxcache_ufs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_ufs PUBLIC fluxcache_common)
if(FLUXCACHE_HAS_S3)
  target_compile_definitions(fluxcache_ufs PUBLIC FLUXCACHE_HAS_S3=1)
  target_link_libraries(fluxcache_ufs PUBLIC minio-cpp)
endif()
