add_library(fluxcache_master STATIC
  stub.cpp
)
target_include_directories(fluxcache_master PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_master PUBLIC fluxcache_common fluxcache_proto)
