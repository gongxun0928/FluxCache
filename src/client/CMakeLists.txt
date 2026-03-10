add_library(fluxcache_client STATIC
  stub.cpp
)
target_include_directories(fluxcache_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_client PUBLIC fluxcache_common fluxcache_proto)
