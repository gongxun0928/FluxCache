add_library(fluxcache_worker STATIC
  worker_server.cpp
  worker_service_impl.cpp
  storage/memory_tier.cpp
)
target_include_directories(fluxcache_worker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_worker PUBLIC fluxcache_common fluxcache_proto)

add_executable(fluxcache-worker main.cpp)
target_link_libraries(fluxcache-worker PRIVATE fluxcache_worker fluxcache_proto)
