add_library(fluxcache_proto STATIC
  stub.cpp
)
target_include_directories(fluxcache_proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_proto PUBLIC fluxcache_common)
