add_library(fluxcache_worker STATIC
  worker_server.cpp
  worker_service_impl.cpp
  meta/page_meta.cpp
  meta/meta_store.cpp
  storage/memory_tier.cpp
  storage/file_tier.cpp
  storage/ssd_tier.cpp
  storage/hdd_tier.cpp
  storage/tier_manager.cpp
  page/page_store.cpp
  cache/lru_policy.cpp
  cache/eviction_policy_factory.cpp
)
target_include_directories(fluxcache_worker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_worker PUBLIC fluxcache_common fluxcache_proto
                                            fluxcache_ufs ${FLUXCACHE_ROCKSDB_TARGET})

add_executable(fluxcache-worker main.cpp)
target_link_libraries(fluxcache-worker PRIVATE fluxcache_worker fluxcache_proto)
