add_library(fluxcache_client STATIC
  cache/client_page_cache.cpp
  cached_hash_ring.cpp
  fluxcache_client.cpp
  master_client.cpp
  worker_client.cpp
)
target_include_directories(fluxcache_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_client PUBLIC fluxcache_common fluxcache_proto)

add_library(fluxcache_sdk STATIC
  sdk/fluxcache_sdk.cpp
  sdk/file_handle.cpp
)
target_include_directories(fluxcache_sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_sdk PUBLIC fluxcache_client)

add_executable(fluxcache-cli cli/main.cpp)
target_include_directories(fluxcache-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache-cli PRIVATE fluxcache_client)

if(FLUXCACHE_HAS_FUSE)
  add_executable(fluxcache-fuse
    fuse/fuse_main.cpp
    fuse/fuse_ops.cpp
  )
  target_include_directories(fluxcache-fuse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
                                                   ${FUSE3_INCLUDE_DIRS})
  target_link_libraries(fluxcache-fuse PRIVATE fluxcache_sdk ${FUSE3_LIBRARIES})
  target_compile_definitions(fluxcache-fuse PRIVATE FUSE_USE_VERSION=31
                                               _FILE_OFFSET_BITS=64)
endif()
