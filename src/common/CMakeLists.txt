add_library(fluxcache_common STATIC
  status.cpp
)
target_include_directories(fluxcache_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
