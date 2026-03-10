add_library(fluxcache_worker STATIC
  stub.cpp
)
target_include_directories(fluxcache_worker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_worker PUBLIC fluxcache_common fluxcache_proto)
