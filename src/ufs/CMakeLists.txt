add_library(fluxcache_ufs STATIC
  stub.cpp
)
target_include_directories(fluxcache_ufs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_ufs PUBLIC fluxcache_common)
