add_library(fluxcache_common STATIC
  status.cpp
  config/config.cpp
)
target_include_directories(fluxcache_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(fluxcache_common PUBLIC yaml-cpp::yaml-cpp)
