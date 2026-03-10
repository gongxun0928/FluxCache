add_library(fluxcache_master STATIC
  master_server.cpp
  master_service_impl.cpp
)
target_include_directories(fluxcache_master PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_master PUBLIC fluxcache_common fluxcache_proto)

add_executable(fluxcache-master main.cpp)
target_link_libraries(fluxcache-master PRIVATE fluxcache_master)
