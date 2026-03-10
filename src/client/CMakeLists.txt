add_library(fluxcache_client STATIC
  cached_hash_ring.cpp
  fluxcache_client.cpp
  master_client.cpp
  worker_client.cpp
)
target_include_directories(fluxcache_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_client PUBLIC fluxcache_common fluxcache_proto)
