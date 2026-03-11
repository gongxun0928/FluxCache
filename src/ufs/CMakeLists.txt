add_library(fluxcache_ufs STATIC
  fake_ufs.cpp
  hdfs_ufs.cpp
  local_ufs.cpp
  s3_ufs.cpp
  ufs_factory.cpp
)
target_include_directories(fluxcache_ufs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_ufs PUBLIC fluxcache_common)
